.. vim:tw=120

.. Allocators
.. ===========

.. raw:: latex

    \chapter{Introduction}

Computer systems can be generalized to be composed of two things: data, and code operating on said data.  In order to
perform useful calculations, real-world applications accept user data which often varies in size.  To accomodate the
differences, memory is requested dynamically, at runtime, using a memory allocator.  The basic interface to the
allocator is the two functions *malloc(size)* and *free(pointer)*: the application gives malloc a size and retrieves a
pointer to a chunk of memory guaranteed to be at least *size* bytes. The operation *free(pointer)*, on the other hand
gives the memory back to the system.

The allocator in turn calls out to the operating system-provided memory mapping function, providing the allocator with
one or more *pages* of memory. The page is the smallest unit of memory available to the operating system from the
processor and its memory mapping unit (MMU), and in the architectures widely used today (x86, x86_64, ARMv7, PPC)
the default size is 4 KB, and can on some architectures be increased. <REF: list of page sizes>.

On operating systems where programs' memory are protected from each other, meaning no application can overwrite any
other application's memory, the page addresses are virtual and therefore a look-up table mapping virtual (page) address
to physical memory is required, such that each process receives its own look-up table.  Since the processor needs to
keep track of each page and to whuch part of memory it is mapped, the resulting look-up table will be very large if a
small page size is used. These can then be written to disk (*swapped out*) when system memory becomes full and the pages
are not in used by the application owning the page, based on a recent-used algorithm.  These types of algorithms vary
depeding on operating system and use case.  Keeping the page size small decreases fragmentation, though, since the last
page requested from a series of pages is essentially unused in the worst case. Therefore, increasing the page size can
be tempting to reduce the look-up table, but this comes at the cost of fragmentation.  In some architectures, the
operating system can increase the page size (so-called "huge pages" in Linux terminology, other operating systems use
different names).


.. raw:: comment-reference

    http://unix.stackexchange.com/questions/128213/how-is-page-size-determined-in-virtual-address-space


    the page size is a compromise between memory usage, memory usage and speed.

    A larger page size means more waste when a page is partially used, so the system runs out of memory sooner.
    A deeper MMU descriptor level means more kernel memory for page tables.
    A deeper MMU descriptor level means more time spent in page table traversal.

    The gains of larger page sizes are tiny for most applications, whereas the cost is substantial. This is why most systems use only normal-sized pages.



A very simple allocator would do little more than returning the pages as *malloc()* and have *free()* be a no-op.
Obviously this is wasteful causing large amount of memory wasted and if the application code using the allocator wanted
to release memory back to the system and then allocate again, system memory would eventually run out.

Therefore, an allocator needs to be more clever about managing memory. At the very minimum they need to associate
metadata with each allocated block in order to later free the blocks.  Moreover, the allocators I'vve tested keep one or
more pools of memory split up in different size ranges, such as "small blocks", "medium-sized blocks" and "large
blocks". By analyzing the runtime requirements of various applications, the most common case (i.e., block size) can be
optimized for speed, space or both.

The metadata is there in order for *free(pointer)* to know where from the memory chunk was allocated from. The pool(s)
are used because of the fact that applications often allocate data in common sizes. If the allocator can group together
all e.g. 8-byte chunks into one or more pages, it will be easier to return the page to the operating system when done.
Lookup can also be more efficient, since the allocator can use offsets to find a suitable free block, instead of
iterating through a list of free blocks.

Objectives
===================
Design and implement an allocator with the following interface::

    handle_t malloc(size_t n);
    void *lock(handle_t h);
    void unlock(handle_t h);
    void free(handle_t h);

The purpose of the lock/unlock operations is to introduce an indirection for memory access that gives the ability to
move data blocks around when not in used (*unlocked* state), specifically to cope with fragmentation problems by
compacting the heap.  This is done in Chapter :ref:`chapter-jeff`.

Collect a variety of applications that can be modified to use the different allocation interface for benchmarking
purposes. This is done Chapter :ref:`chapter-steve`.

In order to compare my allocator against others, I test some of the most common allocators and discuss the results in
Section :ref:`tested-allocators`.

Definitions
============
* **Internal fragmentation**: The amount of memory wasted inside a block.
* **External fragmentation**: The amount of memory wasted by allocator metadata.
* **Op**: Any memory operation: new, free, load, store, modify, lock, unlock. Generally, load, store and modify is generalized to
  access. These are sometimes abbreviated to N for new, F for free, A for access, L for load, U for unlock.
* **Memtrace**: File created by *Lackey* that contains triplets of *(op, address, size)*. See the appendix for full
  definition.
* **Opsfile**: File created by ``translate-memtrace-to-ops.py``, contains one operation per line. See the appendix for full
  definition.
* **Lifetime**: The number of total operations, thus indirectly the time, between a New and a Free op for a specific block.
* **Block**: A chunk of allocated memory.
* **EOF**: End of file.
* **Opaque type**: A way of hiding the contents of an object (data structure) from application code, by only providing a
  pointer to the object without giving its definition. Commonly used where the object is only meant to be modified from
  the library.
  
Challenges
============================================
There are many trade-offs when writing an allocator, which I'll describe in the following section.

I've touched upon internal and external fragmentation. In addition, multi-threaded applications that allocate memory
need to work without the allocator crashing or currupting data. As in all concurrency situations, care needs to be taken
to do proper locking of sensitive data structures, while not being too coarse such that performance suffers.

Allocators are often written to solve a specific goal, while still performing well in the average case. Some allocator
are designed with the explicit goal of being best on average.  Furthermore, speed often hinders efficiency and vice
versa.

Request a page and return in to the user. It would be very fast, but not very efficient since a large part of the page
would be unused for any allocation requests smaller than the page size.

By splitting up allocations in smaller pieces exactly the size of the requested block (plus metadata) and storing
information about freed blocks in a list, there would be little wasting of memory. On the other hand, because of the
efficiency requirement, pages would only be requested when there were no blocks of the correct size and therefore the
entire free list must be searched for a suiting block before giving up and requesting a page.


Efficiency
======================================
The question *Is fragmentation a problem?* is asked by (M. S. Johnstone, P. R. Wilson, 1998). At Opera circa 1997, that
was indeed the case. Large web pages loading many small resources, specifically images, created holes in memory when
freed, such that after a few page loads, it was no longer possible to load any more pages. On a small-memory device,
such as early smart phones/feature phones, with 4-8 MB RAM, this was indeed an issue. The out-of-memory situation happens
despite there theoretically being enough memory available, but because of fragmentation large enough chunks could not be
allocated. This goes against the the authors findings, where in the average case,
fragmentation level is good enough. However, for Opera, that was insufficient.  By making a custom allocator with the
signature outlined in the hypothesis, they hoped to solve the fragmentation problem in the specific situations that
occur in a web browser. It was also to be used as the allocator in an in-house virtual machine running a custom
language.

