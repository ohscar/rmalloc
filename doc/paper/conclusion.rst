Speed
==========
Calculate the penalty for the fields *penalty*, *best*, *worst* and *average* per application gives each allocator a sum
of penalties for each field.  By taking the avage of these penalties, we can tell the position of each allocator. This
is summarized in Table :ref:`table:speed-positions` below, and we can make a final scoring of the allocators:

#. rmalloc (compacting)
#. rmalloc
#. dlmalloc
#. jemalloc
#. tcmalloc
#. rmalloc (compacting, maxmem)

.. raw:: latex

   \begin{table}[!ht]
   \begin{tabular}{r | c c c c c | c}
   \hline
   \multicolumn{6}{c}{\bf Speed} \\
   \hline
   {\bf Driver} & {\bf Penalty} & {\bf Best} & {\bf Worst} & {\bf Average} & {\bf Average penalty} & {\bf Position} \\
   \hline
   rmalloc     & 9   & 11  & 2   & 5   & 6.75    & 2 \\
   rmalloc-c   & 6   & 7   & 2   & 4   & 4.75    & 1 \\
   rmalloc-c-m & 23  & 23  & 18  & 18  & 20.5    & 6 \\
   dlmalloc    & 12  & 11  & 6   & 8   & 9.25    & 3 \\
   jemalloc    & 7   & 8   & 10  & 20  & 11.25   & 4 \\
   tcmalloc    & 18  & 17  & 19  & 18  & 18      & 5 \\
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
#. rmalloc (compacting, maxmem)
#. rmalloc
#. rmalloc (compacting)
#. tcmalloc
#. jemalloc

.. raw:: latex

   \begin{table}[!ht]
   \begin{tabular}{r | c c c c | c}
   \hline
   \multicolumn{6}{c}{\bf Memory} \\
   \hline
   {\bf Driver} & {\bf Penalty} & {\bf Best} & {\bf Worst} & {\bf Average penalty} & {\bf Position} \\
   \hline
   rmalloc     & 11  & 7   & 0   & 6       & 3 \\
   rmalloc-c   & 18  & 6   & 0   & 8       & 4 \\
   rmalloc-c-m & 6   & 5   & 0   & 3,67    & 2 \\
   dlmalloc    & 7   & 3   & 0   & 3,33    & 1 \\
   jemalloc    & 23  & 6   & 5   & 11,33   & 6 \\
   tcmalloc    & 10  & 8   & 10  & 9,33    & 5 \\
   \hline
   \end{tabular}
   \caption{Positions of allocators for memory}
   \label{table:memory-positions}
   \end{table}

Discussion
============
Important to note is that tcmalloc was not able to finish all runs when making a decision on which allocator to use.
Also, the tested allocators were designed to use ``mmap()`` for memory allocation along with ``sbrk()`` which very
likely skewed the results.

Noteworthy is that dlmalloc still performs better than Jeff with compacting and specific support for maximum available
memory.  It is possible that fitting Jeff's interface on top of an existing tested and quick allocator, e.g. dlmalloc,
would have given better runtime characteristics in both space and time.  Jeff is a very simplistic implementation of a
buddy-style allocator without any pools for small objects and similar feats found in most modern allocators.

Jeff still does perform quite well, which means the idea itself could be expanded on in the future. Due to time
constraints, larger applications that are more similar to real-life situations could not be tested since the lockops
calculation took too long time.  Speed and memory characteristics could very well differ for such an application,
esecially if it was running for a longer time.

Future work
===========
Limitations in Jeff
~~~~~~~~~~~~~~~~~~~~~~~
In order to keep the code simple, I made two decisions in the beginning:

* The allocator does not align memory of allocated chunks to boundaries. On older computer architectures, accessing
  non-aligned memory will cause an access violation. In newer architectures, the code runs with a small speed penalty.
* No thread-safety. This means that the behaviour of calling any functions exposed by the allocator from different
  threads at the same time is undefined, and will likely cause data corruption.

Limitations in Steve
~~~~~~~~~~~~~~~~~~~~~~~
As noted in the discussion, the only mechanism for retrieving data from the system for the tested allocators is using `sbrk()``.

Future work in Jeff
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

Implementation optimizations
--------------------------------------------
* Similar to the earlier point, reduce next_unused store offset into heap array. This would limit the maximum number of
  live blocks to *2^sizeof(next_unused_offset)*, which might not be an issue. It could be a compile-time setting.
* Automatic merge with adjacent prev/next block in free/new. This would cause the free list slots contain too large
  blocks for its index.

.. + discarded: notification on low memory for user compact (spent much time trying to work out algorithm before there was working
    code, premature optimization) <FUTURE-WORK>

Future work in Steve
~~~~~~~~~~~~~~~~~~~~~
Simplification
-----------------
* Simplify running tests, specifically setting ``CORES``, ``ALLOCATOR`` and ``KILLPERCENT``.
* Load allocators as shared libraries instead of linking to ``plot.cpp``.
* Restart simulation
* Don't use part files, if possible.

Possible features
--------------------
Reintroduce colormap for calculating theoretical free size from overhead marked in the colormap.

