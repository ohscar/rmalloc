.. vim:tw=120


... is a benchmark tool for collecting and visualizing runtime memory access and allocation patterns in arbitrary binary
applications that use the system malloc, without access to source code or recompilation.

Overview
=========
Steve consists of mostly Python code and Cython for tight inner loops such as the memtrace to ops calculation, plus
some Bash scripts for glueing it all together. The data is plotted in graphs and there is also a tool that creates an
animation of memory allocations as they happen in memory.

Measuring Jeff requires a rewrite of the application needing to be tested, to use the new malloc interface. The simple
solution to do so is to emulate a regular malloc, i.e. directly lock after malloc. But that would make the compact
operation no-op since no blocks can be moved. On the other hand, adapting existing code to benefit from Jeff's interface
is error-prone, it is not obvious which application would make good candidates, and finally, source code to the applications
is required, which is not always possible.

Definitions
===========
* opsfile: file created by ``translate-memtrace-to-ops.py``, contains one operation per line. See the appendix for full
  definition.

Tools
=====
For a detailed description of the tools, see the appendix.

* translate-memtrace-to-ops.py
* translate-ops-to-histogram.py
* translate-ops-to-locking-lifetime.py
* run_allocator_stats.sh (+ run_allocator_stats_payload.sh)
* run_memory_frag_animation.sh
* run_graphs_from_allocstats.py
* run_memory_frag_animation_plot_animation.py

Retrieving memory access data
==================================
Simply getting malloc/free calls is trivially done by writing a malloc wrapper and make use of Linux' ``LD_PRELOAD`` to
make the applications use our own allocator that can do logging. Unfortunately that is not enough. To get statistics on
*memory access patterns* one needs to essentially simulate the system the application runs in.  Options considered were
TEMU from the BitBlazer project, but because it would not build on my Linux system, I looked at Valgrind before trying
again. Turns out Valgrind has good instrumentation support in their tools. Valgrind was originally a tool for
detecting memory leaks in applications on the x86 platform via emulation and has since evolved to support more hardware
platforms and providing tools for doing other instrumentation tasks. Provided with Valgrind an example tool *Lackey*
does parts of what I was looking for but missing other parts. I instead ended up patching the *memcheck* tool [#]_ to capture load/store/access
operations and logging them to file, if they were in the boundaries of *lowest address allocated* to *highest address
allocated*. This will still give false positives when there are holes (lowest is only decreased and highest is only
increased) but reduces the logging output somewhat. Memory access can then be analyzed offline. Note that it will only
work for applications that use the system-malloc/free. Any applications using custom allocators must be modified to use
the system allocator, which generally means changing a setting in the source code and recompiling.

.. [#] https://github.com/mikaelj/rmalloc/commit/a64ab55d9277492a936d7d7acfb0a3416c098e81 (2014-02-09: "valgrind-3.9.0: memcheck patches")

I've written a convenience script for this purpose and then optionally creates locking data::

    #!/bin/bash

    if [[ ! -d "$theapp" ]]; then
        mkdir $theapp
    fi
    echo "$*" >> ${theapp}/${theapp}-commandline
    ../../valgrind/vg-in-place --tool=memcheck $* 2>&1 > \
        /dev/null | grep '^>>>' > ${theapp}/${theapp}

    python -u ../steve/memtrace-to-ops/translate-memtrace-to-ops.py \
        ${theapp}/${theapp}

    python -u ../steve/memtrace-to-ops/translate-ops-to-locking-lifetime.py \
      ${theapp}/${theapp}

Beware that this takes long time for complex applications, about 30 minutes on an Intel Core i3-based system to load
Opera with http://www.google.com

Simulating locking behaviour based on heuristics
==================================================
As noted above, it is not a practical solution to rewrite applications. If there is a possible of creating an automated
method for finding approximate locking behaivour of applications, it should be investigated. To begin with, let's define
the terms used in the coming algorithm:

* Op: Any memory operation: new, free, load, store, modify. Generally, load, store and modify is generalized to Access
  (A).
* Lifetime: The number of total operations, thus indirectly the time, between a New and a Free op for a specific block.
* Block: A chunk of allocated memory.

A block with a lifetime close to the total number of operations has a long lifetime and therefore created in the
beginning of the application's lifetime.  The *macro* lifetime of a block is the relation between all ops within its
lifetime through the total ops count of the application.  A block with a small macro lifetime therefore is an object
that has a short life span, whereas a block with a large macro lifetime is an object with a large life span. Typically
a large value for macro lifetime means it's a global object and can be modelled thereafter.

Depnding on the relation between ops accessing the block in question and ops accessing other objects the access pattern
of the object can be modeled.  For example, if an object has 100 ops within its lifetime and 10 of them are its own
and 90 are others', the object would probably be locked at each access, whereas if it was the other way around, it is
more likely that the object is locked throughout its entire lifetime.

Allocator driver usage
===================================
Steve does, in essense, two tasks: visualize memory and plot benchmark data. The framework allows for fairly easy
extension with more tools.

* ``run_memory_frag_animation.sh``: create an animated memory allocation visualisation.
* ``run_graphs_from_allocstats.py``:  create benchmark based on one or many allocator statistics inputs
  (generated by ``run_allocator_stats.sh``)

The tools are described in more detail in the next section.

All alloc drivers are linked to the same main program and have the same command line parameters:

* ``--peakmem opsfile``
    
    Prints out therotical heap size allocated as reported by the allocator driver. ``--allocstats`` passes this data to
    benchmark data files for later processing by the graphing tool.

    Parameters:

    - opsfile - operations file created by ``translate-memtrace-to-ops.py``.

* ``--allocstats opsfile resultfile killpercent oplimit peakmemsize theoretical_heap_size``

    Generates a file in JSON format in the following format. Header::

        driver = "jemalloc"
        opsfile = "result.program-ops"
        heap_size = 13544700
        theoretical_heap_size = 4514900
        opmode = 'allocstats'
        alloc_stats = [

    Then, per line a dictionary with the following keys::

        {'op_index':        <sequene number>,
         'free':            <bytes: integer>,
         'used':            <bytes: integer>,
         'overhead':        <bytes: integer>,
         'maxmem':          <bytes: integer>,
         'current_op_time': <microseconds: integer>,
         'oom_time':        <microsecond: integer>,
         'optime_maxmem':   <microsecond: integer>,
         'op':              <operation <- N, F, A, L, U: char>,
         'size':            <bytes: integer>
        }
    
    Parameters:

    - opsfile: Operations file created by ``translate-memtrace-to-ops.py``.
    - resultfile: Statistics output file, convention is to use file stem of opsfile (without "-ops") and append
      "-allocstats"
    - killpercent: Optionally rewind and randomly free *killpercent* (0-100) of all headers at EOF, to simulate an application that destroys and creates new documents. The value 100'000 means no rewinding or killing takes place, i.e. just one round of the data gathered by running the application to be benchmarked.
    - oplimit: Which operation ID (0 .. *total ops count*) to write alloctaion stats for. The special value 0 is for writing the original header.
      Typically the driver application is called in a for loop from 0 to the number of operations, i.e. number of lines
      in the opsfile.

* ``--memplot opsfile [heap_size]``

    For each operation, call out ``run_memory_frag_animation_plot_animation.py`` to create a PNG of the heap at that
    point in time.  The driver application only needs to be run once.

    .. Also creates output similar to ``--allocstats``. (TODO: deprecate this!)

    Parameters:

    - opsfile - operations file created by ``translate-memtrace-to-ops.py``.
    - (optional) heap_size - maximum heap size to use


These are not called directly, but instead called from by the tools described below.

At startup the mode of operation of the allocator driver is set to one of these. All modes perform follow the same basic
flow:

#. Allocate heap according to specified heap size or use predefined size (currently 1 Gb).
   If heap allocation fails, decrease by 10% until success.
#. Allocate and initialize colormap as 1/4 of heap size. (more on colormap later)
#. Initialize driver.
#. Initialize randomness with compile-time set seed.
#. Open opsfile.
#. Run mode's main loop.
#. Save statistics created by mode's main loop.
#. Destroy driver.

The main loop follows the same basic structure:

#. Scan a line of the ops file and put in the variables handle, *op*, *address* and *size*.
#. Switch on op:

   - Op is N (New): Call ``user_malloc`` with the size. On OOM, call ``user_handle_oom`` and call ``user_malloc`` again if
     successfully handled. Make sure that there was no OOM on the final malloc. Retrieve the highest address in use by
     ``user_highest_address``. Store object pointer (that may or may not be a directly accessible memory address) and
     memory address (if available) from malloc along with size in maps keyed on the handle id.
   - Op is F (Free): Retrieve the object pointer and call ``user_free``.
   - Op is L (Lock): Retreive the object pointer and all ``user_lock``.
   - Op is U (Unlock): Retreive the object pointer and all ``user_unlock``.

   Access (load, store, modify) operations are not handled in the loop since their use is limited to calculating
   lifetime statistics and locking behaviour.

#. Exit on EOF.

Next, I'll describe the specifics on the three main loops (peakmem, allocstats, memplot) and then the tools that use them.

Driver modes
=============
peakmem
~~~~~~~~~~~~~
Find the largest amount of memory during the driver's lifetime for a specific opsfile, as calculated by the highest
address+size of a block minus the start address of the heap. This number is used as a theoretical maximum heap size to
mesaure the amount of overhead. 

Used by the tool ``run_allocator_stats.sh``. 

allocstats
~~~~~~~~~~~~~~~~~~~~~~~~~~
Adds rewinding of the input file and random free of a certain percentage, if requested, of the allocated objects on opsfile EOF. The
purpose is to allow for the driver application to run several rounds of the application data, as explained above, to do
a rough simulation of an application creating and destroying documents.
It augments new and free with the time the operation takes and stores information about the operation in a list for
later processing.

Used by the tool ``run_allocator_stats.sh``.

memplot
~~~~~~~~~~~~~~~~~~~~~~~~~~
Also adds non-optional rewinding to run until OOM. At each operation, a *colormap* is updated with all known objects. In
order to retrieve the physical memory address they are locked (throuh ``user_lock``) and the pointer is registered.

Colormap is 25% of the heap size, such that each 4-byte word maps onto a byte. The colormap is initially filled with
white (for overhead), with a new operation painted as red and free painted as green. The heap is corresondingly filled
with HEAP_INITIAL (``0xDEADBEEF``) initially, and newly created blocks are filled with HEAP_ALLOC (``0xBEEFBABE``) and
blocks that are just about to be freed are filled with HEAP_FREE (``0xDEADBABE``).

Now, by scanning the heap for values that are not in the set HEAP_INITIAL, HEAP_ALLOC nor HEAP_FREE, it can be concluded
that this is overhead (i.e. allocator-internal structures). Paint the corresponding memory location in the colormap with
white (for overhead).

Tested Allocators
=================================
The allocator often used by Linux and elsewhere in the open-source world is Doug Lea's Malloc *dlmalloc*, that performs
well in the average case. For FreeBSD, Poul-Henning Kamp wrote an allocator that he aptly named *pkhmalloc*. *dlmalloc*
aims to be good enough for most single-threaded use cases and is well-documented, therefore attractive to anyone in need
of an allocator.  It does not perform optimally in multi-threaded applications because of the coarse (operation-level)
locking.  Other allocators are designed to be used in a mutli-threaded application where locking is performed on a finer
level, not blocking other threads trying to use the allocator at the same time.

In fact, at Opera, *dlmalloc* was used internally to better tune allocator characteristics for memory-constrained
devices, where all available memory was requested at startup and then used by the internal malloc.

rmmalloc (Jeff)
~~~~~~~~~~~~~~~~~~~~~
Maps all ``user_...`` calls to the corresponding calls in Jeff. For the compacting version, ``user_handle_oom`` always performs a full compact, and on the non-compacting version, ``user_handle_oom`` is a no-op.

The workings of Jeff is described earlier in this paper.

jemalloc (v1.162 2008/02/06)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
jemalloc is an allocator written by Jason Evans, originally written for a custom development environment circa 2005, later
integrated into FreeBSD for its multi-threading capabilities and later further adapted in 2007 for use by the Firefox
project to deal with fragmentation issues. It's since been adapted for heavy-duty use in the Facebook servers [#]_.
As of 2010, it still performs better than the system-provided allocators in MacOS, Windows and Linux. [#]_ 

.. [#] https://github.com/jemalloc/jemalloc/wiki/History
.. [#] http://www.quora.com/Who-wrote-jemalloc-and-what-motivated-its-creation-and-implementation

TODO: fill in more information about jemalloc: goal, design

Alloc and free calls mapped to the corresponding function call. Handle OOM is a no-op. Configured to use sbrk (``opt_dss
= true``), but not mmap (``opt_mmap = false``).

dlmalloc v2.8.6
~~~~~~~~~~~~~~~~~~~~~~
dlmalloc is an allocator written by Doug Lea and is used by the GNU standard C library, glibc.  The source code states
the following about its goal:
    
    This is not the fastest, most space-conserving, most portable, or most tunable malloc ever written. However it is
    among the fastest while also being among the most space-conserving, portable and tunable.  Consistent balance across
    these factors results in a good general-purpose allocator for malloc-intensive programs.

TODO: fill in more information about dlmalloc: goal, design

Alloc and free calls mapped to the corresponding function call. Handle OOM is a no-op. Configured to use sbrk but not
mmap.

tcmalloc (gperftools-2.1)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
gperftools [#]_ is written by Google and includes a profiling/benchmark framework/tools. It is used by, among others,
Google Chrome, MySQL and WebKit (W. Fang, 2012), which in turn is used by many other projects such
as Apple's Safari.

.. [#] http://code.google.com/p/gperftools/

TODO: fill in more information about tcmalloc: goal, design

Alloc and free calls mapped to the corresponding function call. Handle OOM is a no-op. Configured to use sbrk but not
mmap.


