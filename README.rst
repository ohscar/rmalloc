=========================================
rmalloc: Relocatable memory allocator
=========================================

Gives you defragmentation within a custom allocator::

   handle_t rmalloc(int size);
   void rmfree(handle_t *);
   void *rmlock(handle_t *);
   void rmunlock(handle_t *);

Lock a handle to get the memory address, unlock it when done. Unlocked memory blobs can be moved around at defragmentation time.

Requires modifications to code using a normal malloc(), but can potentially be quicker and more memory efficient.

Running test apps
===================
e.g.::

    ALLOCATOR=./plot_rmalloc_compacting ./run_maxmem.sh /code/rmalloc/memtrace-runs/soffice2/soffice.memstat-lockops-complete

Resources on the Internet and elsewhere
============================================================
* Different allocators: http://publib.boulder.ibm.com/infocenter/pseries/v5r3/index.jsp?topic=/com.ibm.aix.genprogc/doc/genprogc/sys_mem_alloc.htm
* About garbage collectors (good text!): http://www.gamedev.net/page/resources/_/technical/general-programming/modern-garbage-collectors-under-the-hood-r2995

src/valgrind
=========================
Building::

    cd rmalloc/valgrind
    ./autogen.sh
    ./configure
    make

Run::

    ./vg-in-place --tool=memcheck theapp 2>&1 >/dev/null | grep '^>>>' > theapp.memtrace

Format::

    >>> N 69586984 352
    ...
    >>> F 69589088 0
    >>> L 69587700 4
    >>> L 69589376 4
    >>> F 69589360 0
    >>> L 69587708 4
    ...

* <N>ew <resulting address> <size>
* <F>ree <address> 0
* <L>oad <address> <size>
* <S>tore <address> <size>
* <M>odify <address> <size>

src/valgrind-memory-log-to-handle-mapper/
==========================================================================
Scripts to process and visualize the data from a Valgrind test run.  Converts memory address accesses to handles, plotting histograms of macro lifetime.

translate.py
~~~~~~~~~~~~~~~~~~~~
Usage::

    translate-memtrace-to-ops.py soffice.memtrace

Writes result.soffice-ops in the following format::

    <int: handle> <char: operation> <int: address> <int: size>

``operation`` is one of N (new), F (free), S (store), L (load), M (modify), e.g.::

    

result.soffice in the format::

    >>> N 69586984 352
    ...
    >>> F 69589088 0
    >>> L 69587700 4
    >>> L 69589376 4
    >>> F 69589360 0
    >>> L 69587708 4
    ...

Each line is mapped to its corresponding handle by building a lookup table mapping a range of memory locations to a
number. The handles are then plugged into an allocator.  Since Lackey (the Valgrind tool) only stores the lowest and
highest addresses allocated, there will be false positives that are not mapped to antyhing. This is OK.

Mapping requires (highest_addr - lowest_addr) * M amount of RAM, where M is size of Python integer storage class plus
any other overhead required in the heap mapping.  This is done in blocks/blocks.pyx, compiled with Pyrex into C then
compiled into a native Python extension.

translate-2.py
~~~~~~~~~~~~~~~~~~~~~~~~~
Usage::

    translate-2.py result.soffice

Reads files of the same format as translate.py. Writes C allocation data, result.soffice-allocations.c

translate-3-2.py
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Usage:

    translate.py result.soffice

Output: result.soffice-ops-lock, result.soffice-histogram-xxx-yyy.pdf, result.soffice-statistics

Reads from result.soffice-ops (not result.soffice).

Writes result.soffice-ops-lock containing operation and lock/unlock commands directly after New/Free. Not used,
though.  Main purpose is to produce an histogram, result.soffice-histogram.pdf of different lifetime spans:
[(0, 1), (10, 15), (75, 100), (0, 100)]. Lifetime is defined from this::

            skipped += 1
            continue
        else:
        #if True:
            op = (lh, lo, la, ls)
            if op[1] == 'N':
                # own count, current total ops
                lifetime_ops[op[0]] = [0, ops_counter]
            elif op[1] == 'F':
                dead_ops[lh] = lifetime_ops[op[0]]
                # ops_counter - own count - ops counter at creation = correct number of others ops
                dead_ops[lh][1] = ops_counter - dead_ops[lh][0] - dead_ops[lh][1]
                del lifetime_ops[lh]
            #elif op[1] in ['L', 'S', 'M']:
            else:
                lifetime_ops[lh][0] += 1


Macro lifetime (used for plotting)
-----------------------------------------
Own ops plus the others ops (within own handle's lifetime), compared to the total number of ops by all handles, is a
measure of lifetime throughout the entire program::

    macro_lifetime = float(other+own)/float(ops_counter)

It can be used to answer the question if it's a global variable or not, by being close to 1, or 0 if it's very
short-lived.  Then one could perhaps define different cut-off points where it can be said to live within modules.  Most
globals have been measured to be about 99% lifetime, then it's a big gap until the next.  

Micro lifetime
-----------------------
Essentially, how many other handles' ops compared to own handle's ops have been executed within a handles lifespan, e.g.
own/(own+other).  That gives a number of how large part of a handle's lifetime any operations are
performed on it (the value is currently not calculated), ranging from 0..1.  1 means there are only e.g. own ops within
its lifetime.  0.5 means half are its own, half are others. The distribution of values (scaled by 1000 or so)
in a histogram can tell us if there is a cut-off point where we can say with certainty that a a handle should, or should
not be, locked during its entire lifetime or on a when-used basis. It could also (XXX future work) be possible to
analyse when a handle should be autolocked or not, by running the application on a wide range of inputs to get a good
understanding of the behaviour of memory access.



src/locktest/plot
==========================
* plot.cpp - driver program
* plot.h - includes
* plot_<application> - application, specific.

plot_optimal.cpp shows how an optimal allocation would look like.  To be extended to other allocators, for comparison.

grapher.py
~~~~~~~~~~~~~~~~~~~~~~~~~~
Usage::

    python grapher.py lifetime optimal.alloc-stats dlmalloc.alloc-stats

The first argument (lifetime) is currently unused.  Stores PDF in plot-memory-usage.pdf
