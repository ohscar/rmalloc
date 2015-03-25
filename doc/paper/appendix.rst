.. raw:: latex

    \chapter{Appendix
        \label{chapter-appendix}}


The report and the source code in its entirety can be found on GitHub, http://github.com/mikaelj/rmalloc

Tools
======
memtrace-run.sh and translate-memtrace-to-ops.py
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Generates memtrace data from an application run, and translates memtrace data to ops file, respectively, as described in
section :ref:`translating-memory-access-data-to-ops`.

translate-ops-to-histogram.py
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
To visualize and experiment with different ways of calculating lifetime I have a small application that takes as input
an ops file (created by ``translate-memtrace-to-ops.py``), in particular to look at macro lifetimes in different
intervals. This is described in section :ref:`lifetime-visualization`.

translate-ops-to-locking-lifetime.py
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``translate-memtrace-to-ops.py`` produces coarse locking that is quick to calculate, since it simply looks at the macro
lifetime of an object and keeps it locked during its entire lifetime.  It's an either-or situation, instead of locking
and unlocking throughout the object lifetime. 

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

The tool calls the *memplot* mode described above and calls *ffmpeg* to generate an animation of the heap image sequence
produced by the alloc drver for the given ops file.


run_allocator_stats.sh
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Run::

    CORES=2 ALLOCATOR=./drivers/plot_dlmalloc \
        ./run_allocator_stats.sh result.soffice-ops

Generates::

    result.soffice-ops.allocstats

Heap size in allocstats mode is set to this value, increased by 5% until there is no OOM on the last
operation, to make sure that the entire program can be run in full at least once.


.. raw:: comment-na

    run_allocator_stats_payload.sh
    -------------------------------
    T O D O: document me!


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

All functions to be implemented by the driver have a ``user_`` prefix and the driver code is linked together with
``plot.cpp`` to form the binary.  An alternative would be to create a library and register callbacks instead.

user_init(heap_size, heap, name)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``bool user_init(uint32_t heap_size, void *heap, char *name)``

Initialize the allocator with the given parameters.  Since the heap is passed onto the driver, any *mmap* functionality
must beO disabled and only *sbrk*-style allocation is possible. The driver must fill ``name`` with a name that can be
used as a part of a filename, e.g. an alphanumeric string like "dlmalloc".

A driver would store *heap_size*, initialize its own sbrk-equivalent with *heap* and initialize the allocator itself if
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

Perform a memory allocation and return it or ``NULL`` on error. ``op_time`` is the same as above.
``handle`` is an identifier for this allocation request as translated from the memtrace, unique for this block for the
lifetime of the application being benchmarked. It can be used as an index to a map in case the driver wants to store
information associated with this particular block. Finally, ``*memaddress`` can be used to store the memory address at
the time of the allocation, in case the allocation function is using indirect accessing via a handle (e.g. Jeff). In
that case, the handle is returned by *user_malloc()* and the memory address stored in ``*memaddress``. 
If *memaddress* is ``NULL`` no data should be written to it, but if it is not ``NULL``, either the address or ``NULL`` should be
stored in ``*memaddress``.

user_free(ptr, handle, op_time)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``void user_free(void *, uint32_t handle, uint32_t *op_time)``

Like ``user_malloc``.

user_lock(ptr)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``void *user_lock(void *)``

This locks a block of memory, i.e. maps a handle to a pointer in memory, and marks it as in use. It can no longer be
moved since the client code now has a reference to the memory referred to by this handle, until ``user_unlock()`` or
``user_free()`` is called on the handle. Its input value is the return value of ``user_malloc()``. 

user_unlock(ptr)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``void user_unlock(void *)``

This unlocks a block of memory, i.e. marking the block of memory as no longer being in use. Any memory operation is free
to move this block around in memory. Its input value is the return value of ``user_malloc()``. 

user_highest_address(fullcalc)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``void *user_highest_address(bool full_calculation)``
What is the highest address allocated at this time? ``NULL`` if not available.
If ``full_calculation`` is false a less exact calculation is acceptable if it's quicker.

