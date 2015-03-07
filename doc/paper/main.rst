===========================================================================
Jeff and Steve: A Relocatable Memory Allocator and its Benchmarking Friend
===========================================================================
:author: Mikael Jansson
:abstract:
    Memory fragmentation is a problem in computer systems with limited amounts of memory, such as embedded systems. This
    report describes the work and results from designing and implementing a compacting memory allocation system and a
    benchmark suite, that aims to deal with the problem of memory fragmentation.  The benchmark suite is a set of
    tools for visualizing characteristics and benchmarking performance of memory allocators. It can be used in any
    application, without source code to it, as long as the application calls the standard system malloc and free
    functions.  Each allocator to be tested in the benchmark suite need only implement a thin wrapper API.  The
    allocator designed in this report performed similar or better in speed but similar or worse in memory, compared to
    the other allocators tested in this report.

.. role:: ref
.. role:: label
.. raw:: latex

   \newcommand*{\docutilsroleref}{\ref}
   \newcommand*{\docutilsrolelabel}{\label}

.. include:: introduction.rst
.. include:: allocator-types.rst
.. include:: method.rst
.. include:: design.rst
.. include:: simulating-locking.rst
.. include:: jeff.rst
.. include:: steve.rst
.. include:: results.rst
.. include:: conclusion.rst
.. include:: references.rst
.. include:: appendix.rst

.. raw:: foo

    .. raw:: latex

        \chapter{T O D O - X X X - CHECKLIST}

    .. include:: todo-checklist.rst

