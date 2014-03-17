.. vim:tw=120

Introduction
======================================
Background
~~~~~~~~~~~~~~~
Computer systems can be generalized to be composed of two things: data, and code operating on said data.  In order to
perform useful calculations, real-world applications accept user data which often varies in size.  To accomodate the
differences, memory is requested dynamically, at runtime, using a memory allocator.  The basic interface to the
allocator is the two functions *malloc(size)* and *free(pointer)*: the application gives malloc a size and retrieves a
pointer to a chunk of memory guaranteed to be at least *size* bytes. The operation *free(pointer)*, on the other hand
gives the memory back to the system.

The allocator in turn calls out to the operating system-provided memory mapping function, providing the allocator with
one or more  *pages* of memory, often 4 KB each. A very simple allocator would do little more than returning the pages
as malloc() and have free() be a no-op. Obviously this is wasteful causing large internal fragmentation and if the
application code using the allocator wanted to release memory back to the system and then allocate more again, the
system would quickly run out of memory.

Therefore, an allocator needs to be more clever about managing memory. Things most efficient allocators have in common:

* metadata about each allocated block
* pool(s) of common allocation sizes

The metadata is there in order for *free(pointer)* to know where from the memory chunk was allocated from. The pool(s)
are used because of the fact that applications often allocate data in common sizes. If the allocator can group together
all e.g. 8-byte chunks into one or more pages, it will be easier to return the page to the operating system when done.
Lookup can also be more efficient, since the allocator can use offsets to find a suitable free block, instead of
iterating through a list of free blocks.

Garbage Collectors
~~~~~~~~~~~~~~~~~~~
- garbage collectors

Buddy Allocator
~~~~~~~~~~~~~~~~
There are different methods for solving these problems. The most common allocator type is the buddy allocator, and many
allocators are built on its principles: start with a single block and see if the requested chunk fits in half of the
block. If it does, split the block into two and repeat, until there no smaller block size would fit the request.

<illustration of 2^k list>

Over time, there will be more and more items of size 2^n, that are stored on a free list for that block size. Each pair
of split-up blocks is said to be two buddies. When two buddy blocks are free, they can be joined. A block of the next
larger size (n+1) can be created from these two blocks. This is repeated until the largest block, i.e. 2^k. In the worst
case, this causes 2^(n) - 1 bytes of overhead per block, also known as *internal fragmentation.* Still, this commonly
used algorithm has shown to be good enough and is often incorporated as one strategy of an allocator.

As touched upon before, all blocks must have metadata associated with them. For convenience, this is often stored in
memory just before the block itself. The minimum amount of information is the length of the block, in order for *free()*
to know where the block ends. The metadata could also be stored elsewhere, e.g. a lookup table *S(addr)* that gives the
size of the block starting with *addr*. This gives a penalty on free, however, which is not desirable, and therefore,
direct information about the block is usually stored with the block. The metadata associated with the block is normally
not accessible by user code (unless queried using specific debugging code), and is called *external fragmentation*. It
is the fragmentation between the user blocks (hence *external*), i.e. any overhead caused by information required by the
allocator, but not the user code.

Conceptually, the buddy allocator is a very simple allocator to use and implement, but not the most efficient because of
the internal fragmentation.

Commonly Used Allocators
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
- different methods for solving: speed, frag, thread perf
- jemalloc
- dlmalloc
- tcmalloc
- ...

Challenges
~~~~~~~~~~~~~~~
Long-running applications pose certain challenges:

* speed

  - TODO: single execution
  - TODO: concurrency

* efficiency

  - internal fragmentation
  - external fragmentation

Allocators are often written to solve a specific goal, while still performing well in the average case. Some allocator
are designed with the explicit goal of being best on average.  Furthermore, speed often hinders efficiency and vice
versa.

Speed
---------
Request a page and return in to the user. It would be very fast, but not very efficient since a large part of the page
would be unused for any allocation requests smaller than the page size.

Efficiency
---------------
By splitting up allocations in smaller pieces exactly the size of the requested block (plus metadata) and storing
information about freed blocks in a list, there would be little wasting of memory. On the other hand, because of the
efficiency requirement, pages would only be requested when there were no blocks of the correct size and therefore the
entire free list must be searched for a suiting block before giving up and requesting a page.


Efficiency, revisited: Fragmentation - a problem?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Despite paper N, at Opera discovered...


Hypothesis
~~~~~~~~~~~~~~~~~~~~
- avoid frag

What
----
- function signature

Measuring
~~~~~~~~~~~~
Steve

Jeff: Implementation
~~~~~~~~~~~~~~~~~~~~~~~~~
- choices throughout the entire code
- why not, in the end (large per-block structures -- too big overhead)

Steve: Implementation
~~~~~~~~~~~~~~~~~~~~~~~~~
- valgrind
- ops-mapper
