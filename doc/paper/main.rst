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

.. raw:: latex

    \chapter{Introduction}

.. include:: introduction.rst

.. raw:: latex

    \chapter{Method
        \label{chapter-method}}

Experimentally design and draw algorithms in parallel with testing them out using actual code, while continously running
unit tests to ensure correct functionality.

Actual work started with writing a simple buddy allocator as a quick way of better understanding the challenges and what
aspects of an allocator needs special care. Based on that experience, design the final allocator bottom-to-top in
parallel with unit tests to make sure each part works as intended. Benchmarking is done with a separate tool that allows
the use of arbitrary applications for simulating real-world performance, and also does visualization of execution time,
space efficiency and distribution of allocation requests.

.. raw:: latex

    \chapter{Design and implementation
        \label{chapter-design-and-implementation}}

Design
========
Buddy allocator
~~~~~~~~~~~~~~~
To get started with my allocator, I started implementing a buddy allocator. Along with it, I developed tests using
Google's C++ testing framework, googletest [#]_, to make sure no regressions were introduced during development.  More
on that in Section :ref:`unit-testing`.

.. [#] http://code.google.com/p/googletest/

During the development, it quickly became apparent that the internal data structures of the allocator must match the
buddy allocator memory layout closely.  Picking the wrong data structure for storing the block list made the merge-block
operation slow and error-prone, instead of being easy to implement if proper care is taken to align code and structures
with physical memory layout. Indeed, I made the incorrect decision which made the compacting operationg difficult to
implement. The buddy allocator prototype was discarded and work started with the actual allocator that was to be the end
result, with the lessons about taking care in the design phase learned.

Quick malloc, quick free, slow compact
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
My original idea was to have a quick malloc, a quick free and a quick compact. Compact could then be run at times when
the client application was waiting for user input or otherwise not doing computations, such that the malloc would be
very cheap. I had originally envisioned this as a malloc that would basically grow the top pointer of the
malloc-associated heap: store information about the requested chunk size, increase the top pointer and return the chunk.
The free operation would mark the block as not in use anymore. Eventually, the top pointer would reach the end of the
eap, at which point the compact operation would go through the heap and reclaim previously freed memory and reset the
top pointer to end of allocated memory leaving the freed memory as a large chunk of memory ranging to the end.

However, chunks can be locked. Remember that locking a chunk gives the client code the actual pointer to memory, and
unlocking the chunks invalidates the pointer. Therefore, the worst-case scenario is that a block at the very top of the
heap is locked when the compact occurs: even though all unlocked free blocks would be coerced into a single free block,
a locked block at or close to the top would make subsequent malloc calls to fails. Therefore, a free list needs to be
maintained even though it might be the case for real-world applications that the worst case occurs seldom.

Free in more detail
~~~~~~~~~~~~~~~~~~~~~~~~~
When an allocation request comes in, the size of the request is checked against the top pointer and the end of the heap.
A request that fits is associated with a new handle and returned. If there is no space left at the top, the free list is
searched for a block that fits.

Freeing a block marks it as unused and adds it to the free list, for malloc to find later as needed.  The free list is
an index array of *2^3..k*-sized blocks with a linked list at each slot. All free blocks are guaranteed to be at least
*2^n*, but smaller than *2^(n-^1)*, bytes in size. Unlike the buddy allocator, blocks are not merged on free. (See
:ref:`future-work-in-jeff` for a brief discusson.)

.. figure:: graphics/jeff-free-blockslots.png
   :scale: 50%

   :label:`jeffexampleblockslots` Example slots in free list.

An example free blockslots list is given in Figure :ref:`jeffexampleblockslots`.

Compacting
~~~~~~~~~~~~
Compacting uses a greedy Lisp-2-style compacting algorithm (R. Jones, R. Lins, 1997), where blocks are moved
closer to bottom of the heap (if possible),
otherwise the first block (or blocks) to fit in the unused space is moved there. The first case happens if there are no
locked blocks between the unused space and next used (but not locked) block. Simply performing a memmove and updating
pointers is enough. A quick operation that leaves no remainding holes. If however there are any locked blocks between
the unused space and the next used block, obviously only blocks with a total length of less than or equal the size of
the unused space can be moved there. The algorithm is greedy and takes the first block that fits. More than
one adjacent block that fits within the unused space will be moved together. In the case that there are no blocks that
fit the unused space (and there is a locked block directly after), scanning is restarted beginning with the block
directly following the last free block found. The process is continued until there are no unused blocks left or top is
reached.

Implementation
==============
Described in detail in Chapter :ref:`chapter-jeff` and Chapter :ref:`chapter-steve`.

.. raw:: latex

    \chapter{Jeff
        \label{chapter-jeff}}

.. include:: jeff.rst

.. raw:: latex

    \chapter{Steve
        \label{chapter-steve}}

.. include:: steve.rst

.. raw:: latex

    \chapter{Results
        \label{chapter-results}}

.. include:: results.rst

.. raw:: latex

    \chapter{Conclusion
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

