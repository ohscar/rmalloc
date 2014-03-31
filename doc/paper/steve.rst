.. vim:tw=120

Steve
=====
... is a benchmark tool for memory access profiling without modifying apps, lets users simulate different allocators by
adding a small wrapper.
* Plot histogram of object lifetime
* Plot efficiency, speed
* Compare allocators
* Produce animation of malloc ops

- choices throughout the entire code
- why not, in the end (large per-block structures -- too big overhead)

- purpose
- why valgrind
  + modifications to memtest
  + memtrace-to-ops-mapping
- locking heuristics
  - full vs simple locking
- what animation shows
- what benchmark(s) show(s)
- sample outputs
  + allocators
  + test programs w/ inputs
- results
- conclusion?
- future work
- how to run tools

