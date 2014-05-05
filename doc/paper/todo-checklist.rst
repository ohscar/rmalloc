MISC JEFF TODO
===============
- detailed breakdown of

  + rminit
  + rmmalloc -> newblock -> find free header -> find free block -> ...
  + rmfree -> add to free list
  + rmcompact -> find blocks
  + rmdestroy

- based on buddy allocator
- requires modifications of application

  + indirect memory access through handles
  + benchmark w/ modified apps? time-consuming
  + enter Steve for automating testing
  + locked/unlocked objects (based on heuristics, Steve)

- unknown since first time writing allocator, iterations w/ problems

  + first iteration build a plain buddy allocator to get a feel for problems, proved devil is in the details
  + gtest in beginning to find regressions
  + naive malloc/compact cycle doesn't work w/ locked block at the end

    - need proper free list and splitting, describe free list
    - not considered in original design

  + double indirection creates memory overhead <STEVE>

- header list: design choices (describe layout of internal house-keeping structures)
- original idea of simple malloc, simple free not possible due to locked-blocks-at-end.
- compacting based on lisp-2(?) naive greedy allocator 
- sorting (possible future optimization)
- benchmark (see Steve)
- discarded ideas

  + notification on low memory for user compact (spent much time trying to work out algorithm before there was working
    code, premature optimization) <FUTURE-WORK>

- possible optimizations (future work)

  - speed is good enough
  - memory usage: make it more specific to save memory per-handle
  - weak locking

* existing work
* fragmentation issue
* how it works

  + alloc
  + free
  + compacting

* compare w/ others (results)
* conclusion
* future work
* design choices during implementation, including discarded code (e.g. fragmentation formula in sketch book)



MISC STEVE TODO
=================
Unused
~~~~~~~~~~~
* // TODO: UNUSED - bool user_has_heap_layout_changed()
* // TODO: UNUSED - uint32_t user_get_used_block_count()
* // TODO: UNUSED - void user_get_used_blocks(ptr_t \*blocks) // caller allocates!
* // TODO: UNUSED - void user_reset(); // basically destroy + init

Checklist
~~~~~~~~~~
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

  + full vs simple locking
  + access lock heuristics at http://rmalloc.blogspot.se/2013/09/memory-block-acces-locking-heuristics.html
  + histogram for lifetime at http://rmalloc.blogspot.se/2013/09/making-sense-of-histograms.html and http://rmalloc.blogspot.se/2012/08/determining-global-variables.html

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

