.. vim:tw=120

Steve
======
... is a benchmark tool for collecting runtime memory access and allocation patterns in arbitrary binary applications that use
the system malloc, without access to source code or recompilation.

Measuring Jeff requires a rewrite of the application needing to be tested, to use the new malloc interface. The simple
solution to do so is to emulate a regular malloc, i.e. directly lock after malloc. But that would make the compact
operation no-op since no blocks can be moved. On the other hand, adapting existing code to benefit from Jeff's interface
is error-prone, it is not obvious which application would make good candidates, and finally, source code to the applications
is required, which is not always possible.

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

Steve consists of mostly Python code and Cython for tight inner loops such as the memtrace to ops calculation, plus
some Bash scripts for glueing it all together.

memtrace-run.sh and translate-memtrace-to-ops.py
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The basis of all further data analysis is a *memtrace*, a file with the output produced my the patched memcheck tool in
the following format::

    >>> op address size

where op is one of N, F, L, S, M for New, Free, Load, Store and Modify, respectively and size is the how many bytes are
affected by the operation (always 0 for F).  The operation New has an address and size associated, and it's therefore
possible to map memory access (L, S, M) to a specific pointer. This is done by creating a unique integer and mapping all
keys from *address* to *address+size* to that identifier. On free, conversely, all mappings in that address range are
removed. At each access a list of (id, access type, address, size) is recorded. 

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

bool user_init(uint32_t heap_size, void *heap, void *colormap, char *name)
-------------------------------------------------------------------------------

void user_destroy()
-------------------------------------------------------------------------------

void user_reset(); // basically destroy + init
-------------------------------------------------------------------------------

bool user_handle_oom(int size, uint32_t *op_time)
-------------------------------------------------------------------------------
// number of bytes tried to be allocated, return true if <size> bytes could be compacted.

void *user_malloc(int size, uint32_t handle, uint32_t *op_time, void **memaddress)
------------------------------------------------------------------------------------

void user_free(void *, uint32_t handle, uint32_t *op_time)
------------------------------------------------------------------------------------

void *user_lock(void *)
------------------------------------------------------------------------------------
// takes whatever's returned from user_malloc()

void user_unlock(void *)
------------------------------------------------------------------------------------
// takes whatever's returned from user_malloc() 

void *user_highest_address(bool full_calculation)
------------------------------------------------------------------------------------
// what is the highest address allocated? NULL if not accessible.

bool user_has_heap_layout_changed()
------------------------------------------------------------------------------------

uint32_t user_get_used_block_count()
------------------------------------------------------------------------------------
void user_get_used_blocks(ptr_t *blocks) // caller allocates!
------------------------------------------------------------------------------------

I'll go through each one in turn.

translate-ops-to-histogram.py
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

translate-ops-to-locking-lifetime.py
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


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

