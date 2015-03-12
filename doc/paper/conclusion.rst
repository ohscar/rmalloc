.. raw:: latex

    \chapter{Conclusions
        \label{chapter-conclusion}}


Speed
==========
Calculate the penalty for the fields *penalty*, *best*, *worst* and *average* per application gives each allocator a sum
of penalties for each field.  By taking the avage of these penalties, we can tell the position of each allocator.
Allocators that did not finish is given the maximum penalty 5.

This is summarized in Table :ref:`table:speed-positions` below, and we can make a final scoring of the allocators:

#. rmalloc-c (Jeff: compacting)
#. rmalloc (Jeff: plain)
#. dlmalloc
#. jemalloc
#. tcmalloc
#. rmalloc-c-m (Jeff: compacting, maxmem)

.. raw:: latex

   \begin{table}[!ht]
   \begin{tabular}{r | c c c c | c}
   \hline
   \multicolumn{6}{c}{\bf Speed} \\
   \hline
   {\bf Driver} & {\bf Penalty} & {\bf Best} & {\bf Worst} & {\bf Average} & {\bf Average penalty} \\
   \hline
   rmalloc     & 12  & 14  & 2   & 7   & 8.8 \\
   rmalloc-c   & 8   & 9   & 2   & 5   & 6.0 \\
   rmalloc-c-m & 27  & 27  & 20  & 22  & 24.0 \\
   dlmalloc    & 12  & 11  & 6   & 8   & 9.3 \\
   jemalloc    & 8   & 9   & 11  & 23  & 12.8 \\
   tcmalloc    & 23  & 22  & 24  & 23  & 23.0 \\
   \hline
   \end{tabular}
   \caption{Positions of allocators for speed}
   \label{table:speed-positions}
   \end{table}


Memory
==========
Calculated the same way as speed. Because of the extra indirection layour, there will always be more memory used per
allocated block. Summary in Table :ref:`table:memory-positions` below with scoring of the allocators:

#. dlmalloc
#. rmalloc-c-m (Jeff: compacting, maxmem)
#. rmalloc (Jeff: plain)
#. rmalloc-c (Jeff: compacting)
#. tcmalloc
#. jemalloc

.. raw:: latex

   \begin{table}[!ht]
   \begin{tabular}{r | c c c | c}
   \hline
   \multicolumn{5}{c}{\bf Memory} \\
   \hline
   {\bf Driver} & {\bf Penalty} & {\bf Best} & {\bf Worst} & {\bf Average penalty} \\
   \hline
   rmalloc     & 13  & 10  & 0   & 7.6 \\
   rmalloc-c   & 21  & 7   & 0   & 9.3 \\
   rmalloc-c-m & 7   & 7   & 0   & 4.6 \\
   dlmalloc    & 7   & 3   & 0   & 3.3 \\
   jemalloc    & 27  & 9   & 6   & 14.0 \\
   tcmalloc    & 15  & 13  & 10  & 12.6 \\
   \hline
   \end{tabular}
   \caption{Positions of allocators for memory}
   \label{table:memory-positions}
   \end{table}

Discussion
============
Important to note is that tcmalloc was not able to finish all runs when making a decision on which allocator to use.
Also, the tested allocators were designed to use ``mmap()`` for memory allocation along with ``sbrk()`` which 
likely skewed the results. In particular jemalloc performs badly, which could be caused by it being optimized for
``mmap()``.

Noteworthy is that dlmalloc still performs better than Jeff with compacting and specific support for maximum available
memory.  It is possible that fitting Jeff's interface on top of an existing tested and quick allocator, e.g. dlmalloc,
would have given better runtime characteristics in both space and time.  Jeff is a very simplistic implementation of a
buddy-style allocator without any pools for small objects and similar feats found in most modern allocators.

Another conclusion to be drawn from the graphs is that there are cases where a fairly naive allocator, such as Jeff, still
performs almost as good as a more complex allocator, such as dlmalloc.  There might be cases where the trade-off in code
size versus memory efficiency and speed might are worth it, e.g. when the amount of code storage media is limited,
again, common in embedded systems with only kilobytes of code ROM.

Jeff still does perform quite well, which means the idea itself could be expanded on in the future. Due to time
constraints, larger applications that are more similar to real-life situations could not be tested since the lockops
calculation took too long time.  Speed and memory characteristics could very well differ for such an application,
esecially if it was running for a longer time.


Limitations and Future Work 
================================
Jeff: Limitations
~~~~~~~~~~~~~~~~~~~~~~~
In order to keep the code simple, I made two decisions in the beginning:

* The allocator does not align memory of allocated chunks to boundaries. On older computer architectures, accessing
  non-aligned memory will cause an access violation. In newer architectures, the code runs with a small speed penalty.
* No thread-safety. This means that the behaviour of calling any functions exposed by the allocator from different
  threads at the same time is undefined, and will likely cause data corruption.

Jeff: Future Work
~~~~~~~~~~~~~~~~~~~~~~
Features
-------------------------
* Have a callback for when moving a locked block, for simpler compact operation and easier client code where memory does
  not have to be locked/unlocked. Instead, they could be locked during their entire lifetime. On the other hand, there
  is a risk that it would lead to the lookup table being on the client side instead of in the allocator. Depends on
  use case.
* Use bits of pointer to memory block, if size is limited. In practice, a special-purpose allocator such as Jeff will
  likely work with less than the full 32 bits. (For example, limiting to max 1 GB heap gives two extra bits for flags.)
* Weak locking
* Introduce a mature generation for blocks that have been locked for *n* compactions. This would require application
  co-operation in updating any references to the block.

Implementation Optimizations
--------------------------------------------
* Similar to the earlier point, reduce next_unused store offset into heap array. This would limit the maximum number of
  live blocks to :math:`2^{sizeof(next\_unused\_offset)}`, which might not be an issue. It could be a compile-time setting.
* Automatic merge with adjacent prev/next block in free/new. This would cause the free list slots contain too large
  blocks for its index.
* Quick free block find overwrites itself, issue #1 in the Github rmalloc issue tracker. 

.. + discarded: notification on low memory for user compact (spent much time trying to work out algorithm before there was working
    code, premature optimization) <FUTURE-WORK>

Steve: Limitations
~~~~~~~~~~~~~~~~~~~~~~~
As noted in the discussion, the only mechanism for retrieving data from the system for the tested allocators is using ``sbrk()``.
Moreover, there are no hand-tuned reference applications, where optimal locking/unlocking is manually inserted.  This
was not done because of time constraints, but would be interesting to do in the future to establish a baseline to which
other allocators could be compared.

Steve: Future Work
~~~~~~~~~~~~~~~~~~~~~
Simplification
-----------------
* Simplify running tests, specifically setting ``CORES``, ``ALLOCATOR`` and ``KILLPERCENT``.
* Load allocators as shared libraries instead of linking to ``plot.cpp``.
* Restart simulation
* Don't use part files, if possible.

Possible Features
--------------------
* Reintroduce colormap for calculating theoretical free size from overhead marked in the colormap.
* Measure how high part of the total number of blocks are locked at compacting time.
* Investigate stack-based behaviour of computation (and thus allocation) for a possibly more realistic heuristic for
  calculating locking.

