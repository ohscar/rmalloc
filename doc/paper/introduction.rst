.. vim:tw=120

Hyopthesis
===================
Given an allocator with the following interface::

    handle_t malloc(size_t n);
    void *lock(handle_t h);
    void unlock(handle_t h);
    void free(handle_t h);

The purpose of the lock/unlock operations is to introduce an indirection for memory access that gives the ability to
move data blocks around when not in used (*unlocked* state), specifically to cope with fragmentation problems by
compacting the heap. 

Can such an allocator be efficient in space and time?

Introduction
======================================
Allocators
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

Buddy Allocator
~~~~~~~~~~~~~~~~
The most common allocator type is the buddy allocator, and many allocators are built on its principles, or at least
incorporate them in some way: start with a single block and see if the requested chunk fits in half of the block. If it
does, split the block into two and repeat, until there no smaller block size would fit the request.

TODO: <illustration of 2^k list>

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
internal fragmentation.

Pool Allocators
~~~~~~~~~~~~~~~~
Certain applications use a large amount of objects of the same size that are allocated and freed continuously. This
information can be used to create specialized pool allocators for different object sizes, where each pool can be easily
stored in an array of *N \* sizeof(object)* bytes, allowing for fast lookup by alloc and free.

Arena Alocators
~~~~~~~~~~~~~~~~
Code with data structures that are related to each other can be allocated from the same arena, or region, of memory, for
example a document in a word processor. Instead of allocating memory from the system, the allocator requests data from a
pre-allocated larger chunk of data (possibly allocated from the system). The main point of this type of allocator is
quick destruction of all memory related to the working data (i.e. the document), without having to traverse each
individual structure associated with the document to avoid memory leaks from not properly freeing all memory. In
applications where a document/task has a well-defined set of objects associated with it, with relatively short lifetime
but of the document, but many documents created/destroyed over the total application lifetime, there is a large speed
benefit to be had from making the free operation faster.

Garbage Collectors
~~~~~~~~~~~~~~~~~~~
A garbage collector is an allocator that automatically provides memory for data as-needed. There is no need to
explicitly ask for memory from the allocator, nor to free it when done. Instead, the garbage collector periodically
checks for which objects are still in use by the application (*alive*). An object is is anything that uses heap memory: a number,
a string, a collection, a class instance, and so on. There are several techniques for finding alive objects and
categorizing objects depending on their lifetime in order to more efficiently find alive objects at next pass, which I
will not cover in this paper. I recommend (BIB: Garbage Collectors) if you are interested in finding out more.

At any point in time, a garbage collector can move around the object if necessary, therefore any object access is done
indirectly via a translation. User code does not keep a pointer to the block of memory that the object is located in,
but instead this is done with support in the programming language runtime.

A garbage collector, because of the indirect access to memory, can also move objects around in a way that increases
performance in different ways. One way is to move objects closer together that are often accessed together, another way
is to move all objects closer to each other to get rid of fragmentation, which would decrease the maximum allocatable
object size.  A normal allocator couldn't do a memory defragmentation, or any other memory layout optimization, beacuse
memory is accessed directly, which would invalidate the pointer used by the application code.

Implementation Challenges
~~~~~~~~~~~~~~~~~~~~~~~~~~~~
I've touched upon internal and external fragmentation. In addition, multi-threaded applications that allocate memory
need to work without the allocator crashing or currupting data. As in all concurrency situations, care needs to be taken
to do proper locking of sensitive data structures, while not being too coarse such that performance suffers.

Fast or Efficient?
----------------------
There are many trade-offs.

Allocators are often written to solve a specific goal, while still performing well in the average case. Some allocator
are designed with the explicit goal of being best on average.  Furthermore, speed often hinders efficiency and vice
versa.

Request a page and return in to the user. It would be very fast, but not very efficient since a large part of the page
would be unused for any allocation requests smaller than the page size.

By splitting up allocations in smaller pieces exactly the size of the requested block (plus metadata) and storing
information about freed blocks in a list, there would be little wasting of memory. On the other hand, because of the
efficiency requirement, pages would only be requested when there were no blocks of the correct size and therefore the
entire free list must be searched for a suiting block before giving up and requesting a page.

Commonly Used Allocators
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The allocator often used by Linux and elsewhere in the open-source world is Doug Lea's Malloc *dlmalloc*, that performs
well in the average case. For FreeBSD, Poul-Henning Kamp wrote an allocator that he aptly named *pkhmalloc*. *dlmalloc*
aims to be good enough for most single-threaded use cases and is well-documented, therefore attractive to anyone in need
of an allocator.  It does not perform optimally in multi-threaded applications because of the coarse (operation-level)
locking.  Other allocators are designed to be used in a mutli-threaded application where locking is performed on a finer
level, not blocking other threads trying to use the allocator at the same time.

In fact, at Opera, *dlmalloc* was used internally to better tune allocator characteristics for memory-constrained
devices, where all available memory was requested at startup and then used by the internal malloc.

- TODO: discuss allocators in depth: dlmalloc, phkmalloc, jemalloc, tcmalloc (google)

Efficiency, revisited
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Is fragmentation a problem?  At Opera, that was indeed the case. Large web pages loading many small resources,
specifically images, created holes in memory when freed, such that after a few page loads, it was no longer possible to
load any more pages. On a small-memory device, such as early smart phones/feature phones, with 4-8M RAM, this was indeed
an issue. The out-of-memory situation happens despite there theoretically being enough memory available, but because of
fragmentation large enough chunks could not be allocated. This goes against the findings in
<PAPER: "The Memory Fragmentation Problem: Solved? ismm98.ps>, where in the average case, fragmentation level is good
enough. However, for Opera, that was insufficient.  By making a custom allocator with the signature outlined in the
hypothesis, they hoped to solve the fragmentation problem in the specific situations that occur in a web
browser.

- TODO: Possibly for use in a virtual machine

.. include:: jeff.rst

.. include:: steve.rst

Implementation
===================

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
