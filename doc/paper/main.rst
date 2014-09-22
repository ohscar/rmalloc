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

