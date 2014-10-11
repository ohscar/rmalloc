.. raw:: latex

    \chapter{Simulating Application Runtime
        \label{chapter-simulating-application-runtime}}

As described in the introduction, it is not a practical solution to rewrite applications to use the new API. If there is
a way to automatically approximate locking behaviour of applications, it should be investigated.  One way of doing that
is to simulate an application, which requires at the minimum the application's memory access patterns.

.. raw:: comment-done

    X X X
    expand on why this is done! what is its relevance? connect to earlier discussion on how error-prone it is to rewrite
    application. ("as discussed in chapter :ref:`jeff....`, ...")

Gathering Memory Access Data
==================================

.. raw:: comment-done

    X X X (gres/DONE)
    ~~~~~~~~~~~~~~~~~~
    * DONE: en bild skulle göra mekanismen med LD_PRELOAD betydligt tydligare, jag tror att det är hyfsat esoteriskt unixanvändande
    * DONE: (ang. "Turns out Valgrind..." ) bisats? "I evaluated ... , and concluded that the support for ... is good / sufficient / ..." 
    * (ang. Valgrind) man får bonuspoäng om amn gör en tabell som visar skillnaderna mellan verktygen

Simply getting malloc/free calls is trivially done by writing a malloc wrapper and make use of Linux' *LD_PRELOAD*
technique for preloading a shared library, to make the applications use our own allocator that can do logging, instead
of the system allocator.  By pointing a special environment variable (``LD_PRELOAD``) to the location of a shared
library prior to executing the application, any symbols missing from the main application (which in the normal case is,
among others, *malloc* and *free*) are searched for in that library, and only after the system libraries are searched.
This is called *dynamic* linking, where symbols in the application are linked together at runtime, as opposed to
*static* linking, where all symbols must exist in the application binary. This requires the application to use the
system *malloc* and *free* to work, since calls to a custom allocator within the application cannot be captured.

Unfortunately that is not enough. To get statistics on memory *access* patterns one needs to essentially simulate the
system the application runs in.  Options considered were TEMU [#]_ from the BitBlaze [#]_ project, but because it would
not build on my Linux system, I evaluated Valgrind [#]_ and concluded that the support for instrumentation in Valgrind
was satisfactory to my my requirements.

Valgrind was originally a tool for detecting memory leaks in applications on the x86 platform
via emulation and has since evolved to support more hardware platforms and providing tools for doing other
instrumentation tasks. Provided with Valgrind an example tool *Lackey* does parts of what I was looking for but missing
other parts. I instead ended up patching the *memcheck* tool [#]_ to capture load/store/access operations and logging
them to file, if they were in the boundaries of lowest address allocated to highest address allocated. This will still
give false positives when there are holes (lowest is only decreased and highest is only increased, i.e. only keeping a
range of *lowest..highest* of used memor) but reduces complexity of tracking memory. It does not affect the end result,
except for taking longer time to filter out false positives. Memory access is then analyzed
offline. Note that it will only work for applications that use the system-malloc/free. Any applications using custom
allocators must be modified to use the system allocator, which generally means changing a setting in the source code and
recompiling.

.. [#] http://bitblaze.cs.berkeley.edu/temu.html
.. [#] http://bitblaze.cs.berkeley.edu/ 
.. [#] http://valgrind.org
.. [#] https://github.com/mikaelj/rmalloc/commit/a64ab55d9277492a936d7d7acfb0a3416c098e81 (2014-02-09: "valgrind-3.9.0: memcheck patches")

This is how the modified Valgrind, memtrace translation and locking calculation fits together, example given running
StarOffice (``soffice /tmp/path/to/document.ods``)::

    #!/bin/bash

    theapp=$1

    if [[ ! -d "$theapp" ]]; then
        mkdir $theapp
    fi
    shift
    echo "$*" >> ${theapp}/${theapp}-commandline
    ../../valgrind/vg-in-place --tool=memcheck $* 2>&1 > \
        /dev/null | grep '^>>>' > ${theapp}/${theapp}

    python -u ../steve/memtrace-to-ops/translate-memtrace-to-ops.py \
        ${theapp}/${theapp}

    python -u \
        ../steve/memtrace-to-ops/translate-ops-to-locking-lifetime.py \
        ${theapp}/${theapp}

Beware that running larger applications through the memory access-logging Valgrind takes very long time, about 30
minutes on an Intel Core i3-based system to load http://www.google.com in the web browser Opera [#]_.

.. [#] http://www.opera.com

Translating Memory Access Data to Ops
======================================
The basis of all further data analysis is a *memtrace*, a file with the output produced my the patched memcheck tool in
the following format::

    >>> op address size

where op is one of N, F, L, S, M for New, Free, Load, Store and Modify, respectively and size is how many bytes are
affected by the operation (always 0 for F).  The operation New has an address and size associated, and it's therefore
possible to map memory access <L, S, M> to a specific pointer. This is done by creating a unique integer and mapping all
keys from *address* to *address+size* to that identifier. On free, conversely, all mappings in that address range are
removed. At each access a list of tuples <id, access type, address, size> is recorded. 

The output file (*opsfile*) has the following format::

    <handle> <op> <address> <size>

This is done by the tools ``memtrace-run.sh`` and ``translate-memtrace-to-ops.py``.  It took some effort to figure out
the best way to perform the translation, however. I'll discuss the effort below.

Linear Scan
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
My initial attempt was to scan through the entire list each time for each operation.  The problem is that Python is very
slow and uses too much memory, which my laptop with 4 GB of RAM and an intel Core i3 CPU can't handle - this only works
for small-ish outputs. This because the list of handles is checked for each memory access, i.e. a :math:`\sim` 2000
entries list for each memory access (:math:`\sim` 500 MB), quickly becomes unusable.   I tried various approaches, such as moving
out the code to Cython (formerly known as Pyrex), which translates the Python code into C and builds it as a Python
extension module (a regular shared library), but only doing that did not markedly speed things up.

Save CPU at the Expense of Memory
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
I eventually tried a mapping on the start and end addresses, where each access address would be decremented towards
start and incremented towards end. Each address was checked against against a mapping from address to handle. If the
value (i.e. the memory handle) of the mapping are the same, I know that memory access belonged to a specific handle.
That was even slower than iterating through 2000 elements, because the hash has to be checked on average one lookup per
allocated byte in the memory area, even though the time complexity is similar: *O(n*m + c)* - the constant makes it
slower, assuming hash lookup is *O(1)* i.e. *c*.

Finally, I came up with a brute-force solution: hash all addresses within the requested memory area - from start to end,
mapping each address to the corresponding memory handle.  The complexity was *O(m)*, but blew up with a MemoryError at
about 2 GB data read (out of 12 GB in total) My server with 8 GB RAM has swap enabled, but by default Ubuntu 10.04 LTS
doesn't over-commit memory. Setting ``/proc/sys/vm/overcommit_memory`` to 1 effectively enables swap for application memory
allocation.  So, what I realized is that the problem is, of course, that using a 32-bit system to allocate data larger
than 4GB doesn't work very well.  Installed a 64-bit Ubuntu LiveCD on a USB stick and did post-processing from that end.
Now I could successfully translate a memory trace run to a ops file, given a computer with a large amount of RAM.

.. raw:: foobar

    However, it's not good enough. Calculating the handle mappings can be done in one pass, but also including all ops
    (mapped to handles, instead of pointers) will not fit in memory. Therefore, my nice and handy post-processing script
    that does everything in one pass does not cut the mustard.   Splitting it up into more parts, where each one does one
    specific thing:

    - map addresses to handles and write out ops (mapped to handle) to a file
    - read ops file, pruning duplicate ops (e.g. two or more successive L/M/S to the same handle) and write out malloc C source
    - read ops file, calculate handle lifetime for histogram

    That's what it does for now.  

More on Lifetime
~~~~~~~~~~~~~~~~~~~~~
The lifetime calculation could be more elaborate, for now the calculation is fairly naive in that it only checks for really
long-lived areas, but it could also be setup to scan for "sub-lifetimes", i.e. module-global.  My guess is that it would
look like the histogram data below in section :ref:`lifetime-visualization` (spikes), but located in the middle.
Calculating that would mean that start and end points for calculating lifetime would be sliding, such that end is fixed
and start moves towards end, or the other way around, where start is fixed and end moves towards start.  Storing each
value takes up lots of memory and analyzing the end-result by hand takes a very long time since one'd have to look at
each histogram.  I've implemented a simpler version of this, described below in section :ref:`lifetime-calculation`.

.. raw:: comment

    Current histograms is plotted for lifetime which is already calculated. A plot showing ops per handle over time (3D
    graph: ops, handle, time) could possibly give useful information about the clustering of ops and handles, in turn being
    used for calculating new lifetimes.  If time allows for it, otherwise left in future work, since I'm not quite sure on
    what to plot to give the most useful information, and how much it would affect real-life lock/unlock patterns.

Performance Optimization of Lifetime Calculation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Recall from definitions, section :ref:`definitions`, lifetime is defined as number of ops on own handle divided by ops
for all other handles, the given handle's lifetime.  Each handle is mapped to a tuple <own, others>, and for each
operation either own or others is incremented, until the handle is freed, at which point it's moved to the set of
inactive handles. This means going through all handles for each operation, which for smaller datasets is OK.
Even removing duplicates (two successive ops on the same handle) this quadratic *O(m\*n)* (m = ops, n = live handles)
takes too long time.

.. Again, we don't have that luck, and for the Opera data set it's about 8GB data. Even removing duplicates (two successive ops on the same handle) this quadratic *O(m\*n)* (m = ops, n = live handles) takes too long time.

Instead, keep a counter of ops so far (ops_counter) and for each handle, store the tuple <own, value of ops_counter at
New>, and only increase the *own* value for ops mapping to a handle. Then, at death (free), calculate the "others"
value: *others_ops = ops_counter - own - cached_ops_counter*. Example, with ops counter, set of alive, set of dead::

    20 | {(a 5 0) (b 2 5) (c 10 7) (d 3 17)} | {}, (death b) =>
    20 | {(a 5 0) (c 10 7) (d 3 17)} | {(b 2 20-5-2=13)}, (death a) =>
    20 | {(c 10 7) (d 3 17)} | {(b 2 13) {a 5 20-5-0=15}, (death d) =>
    20 | {(c 10 7) (d 3 17)} | {(b 2 13) (a 5 15) (d 3 20-17-3=0)},
         (new e) =>
    25 | {(c 10 7) (d 3 17) (e 5 20)} | {(b 2 13) (a 5 15) (d 3 0)},
         (new f) =>
    28 | {(c 10 7) (d 3 17) (e 5 20) (f 3 25)} |
         {(b 2 13) (a 5 15) (d 3 0)}, (death e) =>
    28 | {(c 10 7) (d 3 17) (e 5 20) (f 3 25)} |
         {(b 2 13) (a 5 15) (d 3 0) (e 5 28-20-5=3}

At end, any remaining live handles (due to missing frees) are moved to the dead set.

This algorithm is *O(m) + O(n)*. 

.. XXX - is it O(m) + O(n)?

Lifetime Visualization
========================
A block with a lifetime close to the total number of operations is considered to be a long lifetime and therefore
created in the beginning of the application's lifetime.  The *macro* lifetime of a block is the relation between all ops
within its lifetime through the total ops count of the application.  A block with a small macro lifetime therefore is an
object that has a short life span, whereas a block with a large macro lifetime is an object with a large life span.
Typically a large value for macro lifetime means it's a global object and can be modelled as such.

A coarse locking lifetime based on the macro lifetime, with a threshold of 50%, is calculated at memtrace-to-ops
translation time, as described in section :ref:`translating-memory-access-data-to-ops` above. The threshold value 50% is
chosen from the assumption that any object that has more than half of all memory accesses in one iteration of a loop is
the primary object on which the loop operates.

Depnding on the relation between ops accessing the block in question and ops accessing other objects the access pattern
of the object can be modeled.  For example, if an object has 100 ops within its lifetime and 10 of them are its own
and 90 are others', the object would probably be locked at each access, whereas if it was the other way around, it is
more likely that the object is locked throughout its entire lifetime. Calculating lifetime requires a full opsfile,
including all access ops.

It turns out that for some (larger) applications, lifetimes are highly clustered for the short-lived objects,
as seen in Figure :ref:`appendixhistogram01000`. This is calculated by the tool ``translate-ops-to-histogram.py`` as
described in section :ref:`lifetime-calculation` below and visualised here.

.. figure:: graphics/result-soffice-macro-histogram-0-1000.png
   :scale: 50%

   :label:`appendixhistogram01000` This shows the number of objects within a specific lifetime. Short-lived objects dominates.

By removing the short-lived objects, we can get a better understanding of the distribution of the other objects in
Figure :ref:`appendixhistogram10100`.

.. figure:: graphics/result-soffice-macro-histogram-10-1000.png
   :scale: 50%

   :label:`appendixhistogram10100` Limited to blocks with a lifetime between 1% and 100%

And conversely, if we want to see the distribution of the short-lived objects only, as in :ref:`appendixhistogram020`.

.. figure:: graphics/result-soffice-macro-histogram-0-20.png
   :scale: 50%

   :label:`appendixhistogram020` Limited to blocks with a lifetime between 0% and 2%

Lifetime Calculation
=================================
Coarsely grained lifetime calculation is done when the raw memtrace is translated into ops, as described above in
section :ref:`translating-memory-access-data-to-ops`.  The method I'll describe in the following section is more refined
but takes more time to calculate.

.. The script takes an ops file, i.e. a list of (block handle, operation type, address, size) tuples.

When a block is initially created, a threshold value, life, is set to zero and will either increase or decrease depending
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
inside the loop. The time under the graph where lifetime is non-zero is one iteration of the loop.

When all ops have been processed they are written out to a new file that in addition to the regular ops also contains
detailed locking information. Since the number of objects is large and the calculation is independent of other objects,
the process can be broken down into smaller tasks. This is done using the Python ``multiprocessing`` module, and by
recording start and stop indices (based on the New or Free ops, respectively) into the input list, the list of start
indices can be broken down into smaller parts to maximize usage of multi-core systems making processing the entire input
file faster on the order of the number of available cores.  The tools automatically picks the number of cores plus two
for the number of worker threads to saturate the CPU.

In the case of no corresponding Free operation for the block, no lifetime calculation is done, i.e. it is assumed to
be unlocked. This is a limitation of the calculation based on the observation of applications that has a large
amount of objects that are never explicitly freed, and assuming a lifetime of the entire application would be
incorrect.  An implicit free could be inserted at the point of the last memory access, however it is not
implemented.

.. raw:: comment-todo

    X X X: As explained above, g_stops[handle] should be set to the last access belonging to that handle.

The fine grained calculation of this method is slower (*O(m\*n)*, where *m* is the number of handles and *n* is the
total number of operations), but intersperses lock/unlock instructions throughout the lifetime of an object, instead of forcing
the object to be locked its entire lifetime. The more fine-grained locking/unlocking, specifically unlocking, the more
efficient compacting can be performed.

.. raw:: comment

    X X X (DONE)
    ~~~~~~~~~~~~
    .. + memtrace-to-ops-mapping at http://rmalloc.blogspot.se/2012/08/large-scale-data-processing.html

    This is described in the Appendix, in the first sections on the tools.  Move the theory to this section!

    More on the specifics of lifetime calculation:

    - why valgrind

      + modifications to memtest

    - locking heuristics

      + full vs simple locking
      + access lock heuristics at http://rmalloc.blogspot.se/2013/09/memory-block-acces-locking-heuristics.html
      + histogram for lifetime at http://rmalloc.blogspot.se/2013/09/making-sense-of-histograms.html and http://rmalloc.blogspot.se/2012/08/determining-global-variables.html



.. This will be expanded upon in Chapter :ref:`chapter-steve`.  X X X: make sure to expand on it!
