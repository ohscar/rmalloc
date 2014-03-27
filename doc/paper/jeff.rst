.. vim:tw=120

Jeff
====
... is a compacting allocator.

- based on buddy allocator
- quick malloc, quick free, slow compact
- locked/unlocked objects
- compacting: based on lisp-2(?) naive greedy allocator 

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



