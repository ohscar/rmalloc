===========================================================================
Jeff and Steve: A Relocatable Memory Allocator and its Benchmarking Friend
===========================================================================
:author: Mikael Jansson
:abstract:
    Memory fragmentation can be a problem in systems with limited amounts of memory. This report describes the work and
    result conducted by the author on designing and implementing a memory allocation system with compacting support,
    along a set of tools for visualizing characteristis and benchmarking the performance of user-provided allocators on
    arbitrary applications without requiring access to source code. The resulting allocator performed similar or better
    in speed but worse or similar in memory measurements.

.. role:: ref

.. role:: label

.. raw:: latex

   \newcommand*{\docutilsroleref}{\ref}
   \newcommand*{\docutilsrolelabel}{\label}

.. raw:: foo

    \chapter{Ethical Considerations
        \label{chapter-ethitical-considerations}}

.. raw:: latex

    \chapter{Introduction}

.. include:: introduction.rst


.. raw:: latex

    \chapter{Method
        \label{chapter-method}}

The method used when designind and implementing the allocator (Jeff) is an iterative design process based on experimental designs
and verification thereof, along with theoretical calculations. Experimental designs were either discarded or validated depending
on the results from the testing framework that performed continuous testing to validate correctness. Limitations of testing is
that it can never prove correctness, only absence of the bugs the testing framework was designed to find. This enabled me to
perform drastic changes in the code. The testing framework is described in more detail in Chapter :ref:`chapter-jeff` .

The benchmark tool (Jeff) that I designed to test algorithms for Jeff and comparing Jeff to other allocators. . In Steve, I've
developed heuristics for calculating locking/unlocking based on runtime data of unmodified applicaions. The tool for doing so grew
from a small script into a larger collection of tools related to data collection, analysis and benchmarking. This is described in
greater detail in chapter :ref:`chapter-steve`.

Development Environment
=========================
Parallel with allocator development, I wrote tests using Google's C++ testing framework, googletest [#]_, to make sure no
regressions were introduced during development.  More on that in Section :ref:`unit-testing`.

.. [#] http://code.google.com/p/googletest/

Main development system is a Linux-based system. The allocator is written in C, and the benchmark tool (Steve) consists of mostly
Python code with Cython [#]_ (a Python framework for interfacing in C and compiling Python into C code) for tight inner loops such
as the memtrace to ops calculation, plus some Bash scripts for glueing it all together.  The data is plotted in graphs and there
is also a tool that creates an animation of memory allocations as they happen in memory.

.. [#] http://cython.org

Testing
========
All applications should be bug-free, but for an allocator it is extra important that there are no bugs. Luckily, an
allocator has a small interface for which tests can be easily written. In particular, randomized unit testing is easy, which
although not guaranteed to catch all bugs gives a good coverage.

I decided to use googletest since it was easy to setup, use and the results are easy to read. It's
similar in style to the original Smalltalk testing framework SUnit [#]_ (later popularized by Java's JUnit [#]_).  During the
development of the allocator I wrote tests and code in parallell, similar to test-driven development in order to verify
that each change did not introduce a regression. Of the approximately 2500 lines of code in the allocator and tests,
about half are tests. In addition to randomized unit testing there are consistency checks and asserts that can be turned
on at compile-time, to make sure that e.g. (especially) the compact operation is non-destructive.

In the unit tests, the basic style of testing was to initialize the allocator with a randomly selected heap size and
then run several tens of thousands of allocations/frees and make sure no other data was touched.  This is done by
filling the allocated data with a constant byte value determined by the address of the returned handle.  Quite a few
bugs were found this way, many of them not happening until thousands of allocations.  That shows randomized testing in
large volume is a useful technique for finding problems in complex data structures, such as an allocator.

.. [#] http://en.wikipedia.org/wiki/SUnit
.. [#] http://en.wikipedia.org/wiki/JUnit

.. XXX: Describe in-depth what the benchmark tool does, see commented-out paragraph below.

..
  parallel with unit tests to make sure each part works as intended. Benchmarking is done with a separate tool that allows
  the use of arbitrary applications for simulating real-world performance, and also does visualization of execution time,
  space efficiency and distribution of allocation requests.

Hypothesis
==========
.. Can an allocator, such as described in Objectives, be efficient in space and time? That is the question I aim to answer in this paper.

An allocator with little extra increase in memory usage compared to the requested memory by the client application is efficient in
space. An allocator that has a low and constant execution time is efficient in time.

My hypothesis is that an allocator that performs heap compaction can be efficient in both time and space, compared to other
commonly used alloctaors, by making the malloc and free operations fast and the compact operation relatively slow and and calling
it when the system is idle.

Questions
~~~~~~~~~
What are the space and time requirements of Jeff compared to other popular allocators?

I aim to answer these questions in the report.

.. raw:: latex

    \chapter{Design
        \label{chapter-design}}

.. include:: design.rst

.. raw:: latex

    \chapter{Allocator Types
        \label{chapter-allocator-types}}

.. include:: allocator-types.rst

.. raw:: latex

    \chapter{Jeff: The Compacting Allocator
        \label{chapter-jeff}}

.. include:: jeff.rst

.. raw:: latex

    \chapter{Steve: The Benchmark Tool
        \label{chapter-steve}}

.. include:: steve.rst

.. raw:: latex

    \chapter{Simulating locking
        \label{chapter-simulating-locking}}

.. include:: simulating-locking.rst

.. raw:: latex

    \chapter{Results
        \label{chapter-results}}

.. include:: results.rst

.. raw:: latex

    \chapter{Conclusions
        \label{chapter-conclusion}}

.. include:: conclusion.rst

.. raw:: latex

    \chapter{References
        \label{chapter-references}}

.. include:: references.rst

.. raw:: latex

    \chapter{Appendix
        \label{chapter-appendix}}

.. include:: appendix.rst

.. raw:: foo

    .. raw:: latex

        \chapter{T O D O - X X X - CHECKLIST}

    .. include:: todo-checklist.rst

