Tools
======
memtrace-run.sh and translate-memtrace-to-ops.py
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The basis of all further data analysis is a *memtrace*, a file with the output produced my the patched memcheck tool in
the following format::

    >>> op address size

where op is one of N, F, L, S, M for New, Free, Load, Store and Modify, respectively and size is the how many bytes are
affected by the operation (always 0 for F).  The operation New has an address and size associated, and it's therefore
possible to map memory access (L, S, M) to a specific pointer. This is done by creating a unique integer and mapping all
keys from *address* to *address+size* to that identifier. On free, conversely, all mappings in that address range are
removed. At each access a list of (id, access type, address, size) is recorded. 

translate-ops-to-histogram.py
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
To visualize and experiment with different ways of calculating lifetime I have a small application that takes as input
an ops file (created by ``translate-memtrace-to-ops.py``), in particular to look at macro lifetimes in different
intervals. It turns out that for some (larger) applications, lifetimes are highly clustered for the short-lived objects,
as seen in Figure :ref:`appendixhistogram01000`.

.. figure:: graphics/result-soffice-macro-histogram-0-1000.png
   :scale: 50%

   :label:`appendixhistogram01000` This shows the number of objects within a specific lifetime. Short-lived objects dominates.

By removing the short-lived objects, we can get a better understanding of the distribution of the other objects in
Figure :ref:`appendixhistogram10100`.

.. figure:: graphics/result-soffice-macro-histogram-10-100.png
   :scale: 50%

   :label:`appendixhistogram10100` Limited to blocks with a lifetime between 1% and 100%

And conversely, if we want to see the distribution of the short-lived objects only, as in :ref:`appendixhistogram020`.

.. figure:: graphics/result-soffice-macro-histogram-0-20.png
   :scale: 50%

   :label:`appendixhistogram020` Limited to blocks with a lifetime between 0% and 2%

translate-ops-to-locking-lifetime.py
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
translate-memtrace-to-ops.py produces coarse locking that is quick to calculate, since it simply looks at the macro
lifetime of an object and keeps it locked during its entire lifetime.  It's an either-or situation.

This method more refined but takes more time to calculate. The script takes an ops file, i.e. a list of (block handle,
operation type, address, size) tuples.

When a block is initially created a threshold value, life, is set to zero and will either increase or decrease depending
on the operations that come between the new operation and the free operation. A memory access op for the current block
increases life by 1, and conversely, another block's operation (regardless of type) decreases life by 0.5. Life is not
capped in the upper range but has a lower limit of 0. When life is higher than 0, the current operation's lock status is
set, otherwise reset. 

The value was chosen by testing different input parameters against random data, and the graphs that looked best was verified
against the smaller application memtraces. This is the algorithm used, with different values for percent, float speed
and sink speed::

    let life = 0
    let lifetime = empty array
    let number of points = 1000
    for i from 0 to number of points:
        let operation belongs to current handle = random() < percent
        if operation belongs to current handle:
            life = life + float_speed
        else:
            if life >= sink_speed:
                life = life - sink_speed

        lifetime.append(life)

The results are shown in Figure :ref:`appendixlockinglifetime`.

.. figure:: graphics/locking-lifetime-explanation
   :scale: 40%

   :label:`appendixlockinglifetime` Simulated lifetime calculations by varying the values of input parameters.

Clockwise from upper left corner, we see that lock status (i.e. lifetime > 0) varies if the current handle is less than
30% of the ops, and if it's less than 50%, it'll diverge towards always being locked -- which is sound, since any object
that is accessed so often is likely to be locked during its lifetime.  With sink equal to or larger than float, a very
jagged graph is produced where the current object is locked/unlocked continously. A real-world application would want to
lock the object once per tight loop and keep it locked until done, instead of continuously locking/unlocking the handle
inside the loop. A loop is the time under the graph where lifetime is non-zero.

When all ops have been processed they are written out to a new file that in addition to the regular ops also contained
detailed locking information. Since the number of objects is large and the calculation is independent of other objects,
the process can be broken down into smaller tasks. This is done using the Python ``multiprocessing`` module, and by
recording start and stop indices (based on the New or Free ops, respectively) into the input list, the list of start
indices can be broken down into smaller parts to maximize usage of multi-core systems making processing the entire input
file faster by the order of the number of available cores.  In the case of no corresponding Free operation for the
block, no lifetime calculation is done.

The fine grained calculation of this method is slower (*O(m\*n)*, where *m* is the number of handles and *n* is the
total number of operations), but intersperses lock/unlock instructions throughout the lifetime of an object, instead of forcing
the object to be locked its entire lifetime.

run_memory_frag_animation.sh
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Syntax::

    ALLOCATOR=path/to/alloc_driver \
        ./run_memory_frag_animation.sh opsfile

Example::

    ALLOCATOR=./drivers/plot_dlmalloc \
        ./run_memory_frag_animation.sh result.soffice-ops

Output::

    result.soffice-ops-animation.avi

The toolcalls the *memplot* mode described above and calls *ffmpeg* to generate an animation of the heap image sequence
produced by the alloc drver for the given ops file.


run_allocator_stats.sh
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Run::

    CORES=2 ALLOCATOR=./drivers/plot_dlmalloc \
        ./run_allocator_stats.sh result.soffice-ops

Generates::

    result.soffice-ops.allocstats

Heap size in allocstats mode is set te this value, increased by 5% until there is no OOM on the last
operation, to make sure that the entire program can be run in full at least once.


run_allocator_stats_payload.sh
-------------------------------
TODO: document me!


run_graphs_from_allocstats.py
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
From data created by run_allocator_stats.

Single
-----------
Run::

    python run_graphs_from_allocstats.py result.soffice-ops

Generates::

    plot-<driver>-<opsfile>.png

Multiple
-------------
Run::

    python run_graphs_from_allocstats.py soffice \
        result.soffice-ops-dlmalloc \
        result.soffice-ops-rmmalloc [...]

Generates:

    soffice.png

Allocator driver API
============================
This gives the essentials of a program's memory usage -- allocation, access and free -- and can be processed by other
tools.

Testing an allocator is done with a driver application by implementing an interface that calls the appropriate functions
of the allocator and linking to a library. The functions to implement are::

    bool user_init(uint32_t heap_size,
                   void *heap,
                   char *name);
    void user_destroy();
    bool user_handle_oom(int size,
                         uint32_t *op_time);
    void *user_malloc(int size,
                      uint32_t handle_id,
                      uint32_t *op_time,
                      void **memaddress);
    void user_free(void *handle,
                   uint32_t handle_id,
                   uint32_t *op_time);
    void *user_lock(void *handle);
    void user_unlock(void *handle);
    void *user_highest_address(bool full_calculation);

All functions to be implemented by the driver has a ``user_`` prefix and the driver code is linked together with
``plot.cpp`` to form the binary.  An alternative would be to create a library and register callbacks instead.

user_init(heap_size, heap, name)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``bool user_init(uint32_t heap_size, void *heap, char *name)``

TODO: remove colormap from API (plot.h, plot.cpp, drivers)

Initialize the allocator with the given parameters.  Since the heap is passed onto the driver, any *mmap* functionality
must beO disabled and only *sbrk*-style allocation is possible. The driver must fill ``name`` with a name that can be
used as a part of a filename, e.g. an alphanumeric string like "dlmalloc".

A driver would store *heap_size*,initialize its own sbrk-equivalent with *heap* and initialize the allocator itself if
needed. As large amount as possible of the allocator's runtime data structures should be stored in this heap space.

user_destroy()
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``void user_destroy()``

Clean up internal structures. The heap given to ``user_init`` is owned by the framework and does not have to be freed.

user_handle_oom(size, op_time)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``bool user_handle_oom(int size, uint32_t *op_time)``

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

user_malloc(size, handle, op_time, memaddress)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``void *user_malloc(int size, uint32_t handle, uint32_t *op_time, void **memaddress)``

Perform a memory allocation and return it or NULL on error. ``op_time`` is the same as above.
``handle`` is an identifier for this allocation request as translated from the memtrace, unique for this block for the
lifetime of the application being benchmarked. It can be used as an index to a map in case the driver wants to store
information associated with this particular block. Finally, ``*memaddress`` can be used to store the memory address at
the time of the allocation, in case the allocation function is using indirect accessing via a handle (e.g. Jeff). In
that case, the handle is returned by *user_malloc()* and the memory address stored in ``*memaddress``. 
If *memaddress* is NULL no data should be written to it, but if it is not NULL, either the address or NULL should be
stored in ``*memaddress``.

user_free(ptr, handle, op_time)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``void user_free(void *, uint32_t handle, uint32_t *op_time)``

Like ``user_malloc``.

user_lock(ptr)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``void *user_lock(void *)``

This locks a block of memory, i.e. maps a handle to a pointer in memory, and marking it as in use. It can no longer be
moved since the client code now has a reference to the memory referred to by this handle, until ``user_unlock()`` or
``user_free()`` is called on the handle. Its input value is the return value of ``user_malloc()``. 

user_unlock(ptr)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``void user_unlock(void *)``

This unlocks a block of memory, i.e. marking the block of memory as no longer being in use. Any memory operation is free
to move this block around in memory.. Its input value is the return value of ``user_malloc()``. 

user_highest_address(fullcalc)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``void *user_highest_address(bool full_calculation)``
What is the highest address allocated at this time? NULL if not available.
If ``full_calculation`` is false a less exakt calculation is acceptable if it's quicker.

