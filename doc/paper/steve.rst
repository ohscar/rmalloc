.. vim:tw=120

Steve
======
... is a benchmark tool for collecting runtime memory access and allocation patterns in arbitrary binary applications that use
the system malloc, without access to source code or recompilation.

Steve consists of mostly Python code and Cython for tight inner loops such as the memtrace to ops calculation, plus
some Bash scripts for glueing it all together.

Measuring Jeff requires a rewrite of the application needing to be tested, to use the new malloc interface. The simple
solution to do so is to emulate a regular malloc, i.e. directly lock after malloc. But that would make the compact
operation no-op since no blocks can be moved. On the other hand, adapting existing code to benefit from Jeff's interface
is error-prone, it is not obvious which application would make good candidates, and finally, source code to the applications
is required, which is not always possible.

Retrieving memory access data
------------------------------
Simply getting malloc/free calls is trivially done by writing a malloc wrapper and make use of Linux' ``LD_PRELOAD`` to
make the applications use our own allocator that can do logging. Unfortunately that is not enough. To get statistics on
*memory access patterns* one needs to essentially simulate the system the application runs in.  Options considered were
TEMU from the BitBlazer project, but because it would not build on my Linux system, I looked at Valgrind before trying
again. Turns out Valgrind has good instrumentation support in their tools. Valgrind was originally a tool for
detecting memory leaks in applications on the x86 platform via emulation and has since evolved to support more hardware
platforms and providing tools for doing other instrumentation tasks. Provided with Valgrind an example tool *Lackey*
does parts of what I was looking for but missing other parts. I instead ended up patching the *memcheck* tool <REF: PATCH:
https://github.com/mikaelj/rmalloc/commit/a64ab55d9277492a936d7d7acfb0a3416c098e81> to capture load/store/access
operations and logging them to file, if they were in the boundaries of *lowest address allocated* to *highest address
allocated*. This will still give false positives when there are holes (lowest is only decreased and highest is only
increased) but reduces the logging output somewhat. Memory access can then be analyzed offline.

memtrace-run.sh and translate-memtrace-to-ops.py
-----------------------------------------------------
The basis of all further data analysis is a *memtrace*, a file with the output produced my the patched memcheck tool in
the following format::

    >>> op address size

where op is one of N, F, L, S, M for New, Free, Load, Store and Modify, respectively and size is the how many bytes are
affected by the operation (always 0 for F).  The operation New has an address and size associated, and it's therefore
possible to map memory access (L, S, M) to a specific pointer. This is done by creating a unique integer and mapping all
keys from *address* to *address+size* to that identifier. On free, conversely, all mappings in that address range are
removed. At each access a list of (id, access type, address, size) is recorded. 

Simulating locking behaviour based on heuristics
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
As noted above, it is not a practical solution to rewrite applications. If there is a possible of creating an automated
method for finding approximate locking behaivour of applications, it should be investigated. To begin with, let's define
the terms used in the coming algorithm:

* Op: Any memory operation: new, free, load, store, modify. Generally, load, store and modify is generalized to Access
  (A).
* Lifetime: The number of total operations, thus indirectly the time, between a New and a Free op for a specific block.
* Block: A chunk of allocated memory.

A block with a lifetime close to the total number of operations has a long lifetime and therefore created in the
beginning of the application's lifetime.  The *macro* lifetime of a block is the relation between all ops within its
lifetime through the total ops count of the application.  A block with a small macro lifetime therefore is an object
that has a short life span, whereas a block with a large macro lifetime is an object with a large life span. Typically
a large value for macro lifetime means it's a global object and can be modelled thereafter.

Depnding on the relation between ops accessing the block in question and ops accessing other objects the access pattern
of the object can be modeled.  For example, if an object has 100 ops within its lifetime and 10 of them are its own
and 90 are others', the object would probably be locked at each access, whereas if it was the other way around, it is
more likely that the object is locked throughout its entire lifetime.

translate-ops-to-histogram.py
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
To visualize and experiment with different ways of calculating lifetime I have a small application that takes as input
an ops file (created by ``translate-memtrace-to-ops.py``), in particular to look at macro lifetimes in different
intervals. It turns out that for some (larger) applications, lifetimes are highly clustered for the short-lived objects:

.. figure:: result.soffice-macro-histogram-0-1000.png

   This shows the number of objects within a specific lifetime. Short-lived objects dominates.

By removing the short-lived objects, we can get a better understanding of the distribution of the other objects.

.. figure:: result.soffice-macro-histogram-10-100.png

   Limited to blocks with a lifetime between 1% and 100%

And conversely, if we want to see the distribution of the short-lived objects only:

.. figure:: result.soffice-macro-histogram-0-20.png

   Limited to blocks with a lifetime between 0% and 2%

translate-ops-to-locking-lifetime.py
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
translate-memtrace-to-ops.py produces coarse locking that is quick to calculate, since it simply looks at the macro
lifetime of an object and keeps it locked during its entire lifetime.  It's an either-or situation.

This method more refined but takes more time to calculate. The script takes an ops file, i.e. a list of (block handle,
operation type, address, size) tuples.

When a block is initially created a threshold value, life, is set to zero and will either increase or decrease depending
on the operations that come between the new operation and the free operation. A memory access op for the current block
increases life by 1, and conversely, another block's operation (regardless of type) decreases life by 0.5. Life is not
capped in the upper range but has a lower limit of 0. When life is higher than 0, the current operation's lock status is
set, otherwise reset. 

XXX: pretty picture.

When all ops have been processed they are written out to a new file that in addition to the regular ops also contained
detailed locking information. Since the number of objects is large and the calculation is independent of other objects,
the process can be broken down into smaller tasks. This is done using the Python ``multiprocessing`` module, and by
recording start and stop indices (based on the New or Free ops, respectively) into the input list, the list of start
indices can be broken down into smaller parts to maximize usage of multi-core systems making processing the entire input
file faster by the order of the number of available cores. 

The fine grained calculation of this method is slower, but gives lock/unlock instructions throughout the lifetime of an
object, instead of forcing the object to be locked its entire lifetime.

Allocator driver usage
~~~~~~~~~~~~~~~~~~~~~~~
The scripts below:

../../src/steve/run_allocator_stats_payload.sh
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
../../src/steve/run_allocator_stats.sh
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
../../src/steve/run_memory_frag_animation.sh
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Allocator driver API
~~~~~~~~~~~~~~~~~~~~~~~~~~
This gives the essentials of a program's memory usage -- allocation, access and free -- and can be processed by other
tools.

Testing an allocator is done with a driver application by implementing an interface that calls the appropriate functions
of the allocator and linking to a library. The functions to implement are::

    extern bool user_init(uint32_t heap_size, void *heap, void *colormap, char *name);
    extern void user_destroy();
    extern void user_reset(); // basically destroy + init
    extern bool user_handle_oom(int size, uint32_t *op_time); // number of bytes tried to be allocated, return true if <size> bytes could be compacted.
    extern void *user_malloc(int size, uint32_t handle, uint32_t *op_time, void **memaddress);
    extern void user_free(void *, uint32_t handle, uint32_t *op_time);
    extern void *user_lock(void *); // takes whatever's returned from user_malloc()
    extern void user_unlock(void *); // takes whatever's returned from user_malloc() 
    extern void *user_highest_address(bool full_calculation); // what is the highest address allocated? NULL if not accessible.
    extern bool user_has_heap_layout_changed();

    // currently in-use memory blocks. useful after compact() has happened.
    extern uint32_t user_get_used_block_count();
    extern void user_get_used_blocks(ptr_t *blocks); // caller allocates!

All functions to be implemented by the driver has a ``user_`` prefix and the driver code is linked together with
``plot.cpp`` to form the binary.  An alternative would be to create a library and register callbacks instead.

bool user_init(uint32_t heap_size, void *heap, /*void *colormap, */char *name)
-------------------------------------------------------------------------------
XXX: remove colormap from API (plot.h, plot.cpp, drivers)

Initialize the allocator with the given parameters.  Since the heap is passed onto the driver, any *mmap* functionality
must beO disabled and only *sbrk*-style allocation is possible. The driver must fill ``name`` with a name that can be
used as a part of a filename, e.g. an alphanumeric string like "dlmalloc".

A driver would store *heap_size*,initialize its own sbrk-equivalent with *heap* and initialize the allocator itself if
needed. As large amount as possible of the allocator's runtime data structures should be stored in this heap space.

void user_destroy()
-------------------------------------------------------------------------------
Clean up internal structures. The heap given to ``user_init`` is owned by the framework and does not have to be freed.

// XXX: UNUSED - void user_reset(); // basically destroy + init
-------------------------------------------------------------------------------

bool user_handle_oom(int size, uint32_t *op_time)
-------------------------------------------------------------------------------
// number of bytes tried to be allocated, return true if <size> bytes could be compacted.

Handle an out-of-memory situation. ``size`` is the number of bytes requested at the time of OOM.
``op_time`` is an out variable storing the time of the actual OOM-handling code (such as a compact operation), not
considering the code before or after. For convenience, Steve pre-defines macros for time measuring.  A typical
implementation where OOM is actually handled looks like this::
    
    bool user_handle_oom(int size, uint32_t *op_time)
    {
        TIMER_DECL;

        TIMER_START;
        bool ok = full_compact();
        TIMER_END;
        if (op_time)
            *op_time = TIMER_ELAPSED;

        return ok;
    }

``op_time`` can also be ``NULL``, as shown in the example, in which case time must not be stored. Return value is *true*
if the OOM was handled, *false* otherwise.

void *user_malloc(int size, uint32_t handle, uint32_t *op_time, void **memaddress)
------------------------------------------------------------------------------------
Perform a memory allocation and return it or NULL on error. ``op_time`` is the same as above.
``handle`` is an identifier for this allocation request as translated from the memtrace, unique for this block for the
lifetime of the application being benchmarked. It can be used as an index to a map in case the driver wants to store
information associated with this particular block. Finally, ``*memaddress`` can be used to store the memory address at
the time of the allocation, in case the allocation function is using indirect accessing via a handle (e.g. Jeff). In
that case, the handle is returned by *user_malloc()* and the memory address stored in ``*memaddress``. 
If *memaddress* is NULL no data should be written to it, but if it is not NULL, either the address or NULL should be
stored in ``*memaddress``.

void user_free(void *, uint32_t handle, uint32_t *op_time)
------------------------------------------------------------------------------------
Like ``user_malloc``.

void *user_lock(void *)
------------------------------------------------------------------------------------
This locks a block of memory, i.e. maps a handle to a pointer in memory, and marking it as in use. It can no longer be
moved since the client code now has a reference to the memory referred to by this handle, until ``user_unlock()`` or
``user_free()`` is called on the handle. Its input value is the return value of ``user_malloc()``. 

void user_unlock(void *)
------------------------------------------------------------------------------------
This unlocks a block of memory, i.e. marking the block of memory as no longer being in use. Any memory operation is free
to move this block around in memory.. Its input value is the return value of ``user_malloc()``. 

void *user_highest_address(bool full_calculation)
------------------------------------------------------------------------------------
What is the highest address allocated at this time? NULL if not available.
If ``full_calculation`` is false a less exakt calculation is acceptable if it's quicker.

UNUSED
-----------
* // XXX: UNUSED - bool user_has_heap_layout_changed()
* // XXX: UNUSED - uint32_t user_get_used_block_count()
* // XXX: UNUSED - void user_get_used_blocks(ptr_t \*blocks) // caller allocates!


... is a benchmark tool for memory access profiling without modifying apps, lets users simulate different allocators by
adding a small wrapper.
* Plot histogram of object lifetime
* Plot efficiency, speed
* Compare allocators
* Produce animation of malloc ops

- choices throughout the entire code
- why not, in the end (large per-block structures -- too big overhead)

- purpose
- investigated
  + valgrind
  + bitblazer / temu
  + clang / llvm
- why valgrind
  + modifications to memtest
  + memtrace-to-ops-mapping at http://rmalloc.blogspot.se/2012/08/large-scale-data-processing.html
- locking heuristics
  - full vs simple locking
  - access lock heuristics at http://rmalloc.blogspot.se/2013/09/memory-block-acces-locking-heuristics.html
  - histogram for lifetime at http://rmalloc.blogspot.se/2013/09/making-sense-of-histograms.html and
    http://rmalloc.blogspot.se/2012/08/determining-global-variables.html
- colormap (0xdeadbeef, 0xbeefbabe, 0xdeadbabe)
- what animation shows
- what benchmark(s) show(s)
- sample outputs
  + allocators
  + test programs w/ inputs
- results
- conclusion?
- future work
- how to run tools

