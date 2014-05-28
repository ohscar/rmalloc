#!/bin/bash

for alloc in jemalloc dlmalloc tcmalloc  rmalloc  rmalloc_compacting rmalloc_compacting_maxmem; do echo "===== START WITH $alloc"; date; LD_LIBRARY_PATH=drivers/jemalloc:drivers/tcmalloc CORES=2 KILLPERCENT=1 ALLOCATOR=drivers/plot_${alloc} nice -n20 time ./run_allocator_stats.sh ../memtrace-runs/ls/ls-lockopsfull; echo "================ DONE WITH $alloc"; date ; done
