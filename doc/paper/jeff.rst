.. vim:tw=120

Jeff
====
... is a compacting allocator.

- different signature
- quick malloc, quick free, slow compact
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
  + double indirection creates memory overhead
- header list: design choices (describe layout of internal house-keeping structures)
- compacting based on lisp-2(?) naive greedy allocator 
  - sorting (possible future optimization)
- benchmark (see Steve)
- discarded ideas
  + notification on low memory for user compact (spent much time trying to work out algorithm before there was working
    code, premature optimization)
- possible optimizations (future work)
  - speed is good enough
  - memory usage: make it more specific to save memory per-handle

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

