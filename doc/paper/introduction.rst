.. vim:tw=120

.. Allocators
.. ===========

.. raw:: latex

    \chapter{Introduction}

Computer systems can be generalized to be composed of two things: data, and code operating on said data.  In order to
perform useful calculations, real-world applications accept user data which often varies in size.  To accommodate the
differences, memory is requested dynamically (at runtime) using a memory allocator.  The basic interface to the
allocator consists of the two functions *malloc(size)* and *free(pointer)*: the application gives malloc a size and retrieves a
pointer to a chunk of memory guaranteed to be at least *size* bytes. The operation *free(pointer)*, on the other hand,
gives the memory back to the system.

The allocator in turn calls out to the operating system-provided memory mapping function, providing the allocator with
one or more *pages* of memory. The page is the smallest unit of memory available to the operating system from the
processor and its memory mapping unit (MMU), and in the architectures widely used today (x86, x86_64, ARMv7, PPC)
the default size is 4 KB, and can on some architectures be increased.

.. <REF: list of page sizes>.

On operating systems where application memory is protected from other applications' memory, meaning no application can
overwrite any other application's memory, the page addresses are virtual and therefore a kernel-space [#]_ look-up table
mapping virtual (page) address to physical memory is required, such that each process has its own look-up table.  Since
the processor needs to keep track of each page and to which part of memory it is mapped, the resulting look-up table
will be very large if a small page size is used. These can then be written to disk (*swapped out*) when system memory
becomes full and the pages are not in use by the application owning the page, based on a recently-used algorithm.  The
algorithm varies depending on operating system and use case.  In the worst case, the last page in a series of requested
pages is largely unused, causing 4096-1 bytes to go to waste. Keeping the page size small lowers this waste, which is
also known as *internal fragmentation*.  Increasing the page size can be tempting to speed up the lookup
table, but this comes at the cost of fragmentation.  In some architectures, the operating system can increase the page
size (so-called "huge pages" in Linux terminology, other operating systems use different names).  

.. [#] The operating mode of the operating system where the kernel and hardware drivers run.

.. raw:: comment-reference

    http://unix.stackexchange.com/questions/128213/how-is-page-size-determined-in-virtual-address-space


    the page size is a compromise between memory usage, memory usage and speed.

    A larger page size means more waste when a page is partially used, so the system runs out of memory sooner.
    A deeper MMU descriptor level means more kernel memory for page tables.
    A deeper MMU descriptor level means more time spent in page table traversal.

    The gains of larger page sizes are tiny for most applications, whereas the cost is substantial. This is why most systems use only normal-sized pages.



A very simple user-space [#]_ allocator would
do little more than *malloc()* returning the pages and have *free()* do nothing at all.  Clearly this causes large
amounts of memory to be wasted, since no memory would actually be released to the system.  Eventually, the system would
run out of memory. 

Therefore, an allocator needs to be more clever about managing memory. At the very minimum it needs to associate
metadata with each allocated block in order to later free the blocks.  The metadata is there in order for
*free()* to know where the memory chunk was allocated from. Moreover, the allocators I've tested keep one or
more pools of memory split up in different size ranges, such as "small blocks", "medium-sized blocks" and "large
blocks". By analyzing the runtime requirements of various applications, the most commonly use cases, such as a specific
block size dominating all other requests, can be optimized for speed, space or both. The pool(s) are used because of the
fact that applications often allocate data of particular sizes.

If the allocator can group together all e.g. 8-byte chunks into one or more pages, it will be easier to return the page
to the operating system when all blocks are freed.  Lookup can also be more efficient, since the allocator can use offsets to find a
suitable free block, instead of iterating through a list of free blocks.

.. [#] The operating mode of the operating system where normal applications run.

Thesis Statement and Contributions
=======================================================
The purpose of this thesis is to design and implement an allocator with the following interface that can move around allocated blocks of memory::

    handle_t malloc(size_t n);
    void *lock(handle_t h);
    void unlock(handle_t h);
    void free(handle_t h);

The purpose of the lock/unlock operations is to introduce indirection for memory access that gives the allocator the ability to
move data blocks around when not in use (*unlocked* state), specifically by compacting the heap cope with fragmentation problems.

In Chapter :ref:`chapter-allocator-types` I present an overview of the allocator I compare my work with.  Method and
Design are in Chapters :ref:`chapter-method` and :ref:`chapter-design`. 

I have developed a method of simulating runtime behaviour of application using heuristics and show that it is possible
to test performance of locking/unlocking allocators without access to source code. This is done in Chapter
:ref:`chapter-simulating-application-runtime`.

I show that randomized testing in large volume is a useful technique for finding problems in
complex data structures, such as an allocator. This is done in Chapter :ref:`chapter-jeff`.

I have collected a variety of applications that can be modified to use the different allocation interface for benchmarking
purposes. This is done Chapter :ref:`chapter-steve`. The results from benchmarking the allocators, can be
found in Chapter :ref:`chapter-results` which is finally discussed in Chapter :ref:`chapter-conclusion`. 

.. raw:: comment-done 

    Thesis Outline (X X X: kanske inte egen rubrik utan löpande)
    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    http://phdcomics.com/comics/archive/phd071713s.gif

    http://www.butte.edu/library/documents/Research%20Paper%20Outline%20Examples.pdf

    http://www.cs.toronto.edu/~sme/presentations/thesiswriting.pdf


Definitions
============
* **Opaque type**: A way of hiding the contents of an object (data structure) from application code, by only providing a
  pointer to the object without giving its definition. Commonly used where the object is only meant to be modified from
  the library.
* **Valgrind**: A debugging tool used to detect memory leaks and memory overwriting in applications, by emulating the
  target CPU.
* **mmap()**: A system call for applications to ask the operating system for one or more memory pages (often 4KB) and
  map it into the application's virtual address space.
* **sbrk()**: Similar to *mmap()*, but works by extending the application's data segment size instead of asking for virtual
  memory, and is limited by the maximum data size of the application.
* **Internal fragmentation**: The amount of memory wasted inside a block.
* **External fragmentation**: The amount of memory wasted by allocator metadata.
* **Memtrace**: File created by Valgrind's *memcheck* tool (see Chapter :ref:`chapter-steve`) that contains triplets of *(op, address, size)*.
  See the appendix for the full definition.
* **Op**: Any memory operation: new, free, load, store, modify, lock, unlock. Generally, load, store and modify is generalized to
  access. These are sometimes abbreviated to N for new, F for free, A for access, L for load, U for unlock.
* **Opsfile**: File created by ``translate-memtrace-to-ops.py`` (part of Steve, see Chapter :ref:`chapter-steve`), contains one operation per line. See the appendix for the full
  definition.
* **Block**: A chunk of allocated memory.
* **Lifetime**: The number of total operations, thus indirectly the time, between a New and a Free op for a specific block.
* **Header**: An internal data structure containing the metadata about a block. It is also used as an opaque handle for
  use by the client code.
  
Challenges
============================================
There are many trade-offs when writing an allocator, which I'll describe in the following section.

Allocators are often optimized for a specific use-case or task, while still performing well in the average case. In fact, some
allocators are designed with the explicit goal of being best on average. 

.. Furthermore, speed often hinders efficiency and vice versa.

A very simple allocator would simply request one or more pages from the operating system and return in to the user. It would be
very fast, but not very efficient since a large part of the page would be unused for any allocation requests smaller
than the page size.

By splitting up allocations in parts exactly the size of the requested block (plus metadata) and storing
information about freed blocks in a list, there would be little wasting of memory. On the other hand, because of the
efficiency requirement, pages would only be requested when there were no blocks of the correct size and therefore the
entire free list must be searched for a suiting block before giving up and requesting a page.

Multi-threaded applications that allocate memory need to work without the allocator crashing or corrupting
data. As in all concurrency situations, care needs to be taken to do proper locking of sensitive data structures, while
not being so coarse such that performance suffers. I do not address the issue of locking.

Another challenge is to make the allocator work efficiently for various memory sizes. I focus on small-memory systems,
where space-efficiency is important, and I've made the trade-off (where applicable) that slower is better if it saves
memory. It is currently in use at TLab West Systems AB on an embedded computer with a total of 512 KB RAM.

Efficiency
======================================
Around 2007 at Opera, a company that produces a web browser for desktop computers, embedded computer systems and
phones, memory fragmentation became a problem after repeatedly loading and unloading web pages. Large web pages load many
small resources, specifically images, that create holes in memory when freed. After a few page loads, it is no longer
possible to load any more pages because there are no continuous blocks of memory large enough to fit a web page in.  It
happened frequently on small-memory devices, such as early smart phones and feature phones with 4-8 MB RAM.

Because of said fragmentation, large enough blocks can eventually not be allocated, even though the total amount of free
memory is greater than the requested block size.  This goes against the findings by Johnstone & Wilson (1998), where in
the average case, the level of fragmentation is good enough. However, for Opera, "good enough" was insufficient.  By
making a custom allocator with the signature outlined in the hypothesis, they hoped to solve the fragmentation problem
in the specific situations that occur in a web browser. Another use case for the allocator was for an in-house custom
programming language, where the allocator's purpose was to be used as a garbage collector. This did not happen, however,
because of delays in finishing the thesis.

Related Work 
==================
Closely related to a compacting allocator is the garbage collector, which is popular in managed languages that do not
run directly on hardware. In particular, the Java Virtual Machine (JVM) includes different garbage collector (GC) flavors depending
on the task at hand. As of version 5, there are four variants (Sun Microsystems, 2006) with different characteristics
that can be picked depending on the type of application written. Each GC flavor can be configured.
Configuration settings, including setting GC flavor, can be done at runtime via command line parameters to the JVM.

All JVM GCs use *generations*, in which objects are allocated and later moved if they survive a garbage collection. This
is mainly done as an optimization to execution time since different collection strategies can be used for "young" objects
and "old" objects (i.e. the ones that have survived a set number of collections).  A generation is implemented as
separate memory areas, and therefore, areas that are not full waste memory.  Also, application code is unaware of when
collection occurs, generations is also a means of reducing the time the application is paused, if the collection cannot
happen simultaneously with application execution.  Pausing in general is a problem GCs try to solve, see Jones & Lins
(1997).

In my thesis, I give control over pausing to the application that can decide at its own discretion when the most
appropriate time is for heap compacting. In the optimal case, where a simple *bump-the-pointer* technique can be used,
i.e. increase the heap pointer for the next chunk of memory, allocation will be very quick, at the expense of compacting
having to occur frequently. This is a deliberate trade-off, based on the assumption that there will be idle time in the
application where compacting is more appropriate. Generations would be of no benefit in this scenario.  In the worst
case, however, blocks on the heap are locked at compacting time. These blocks cannot be moved and therefore a free list
needs to be maintained, causing allocation to be slower. Being able to move these blocks to a location where they cause
less harm is left as future work.

.. raw:: comment-todo

    At the time of starting work on the thesis (2008), .Net Micro framework was not available.
    .Net Micro Framework was first released Aug 26 2010 with .NET MF PK 4.1? http://netmf.codeplex.com/releases/view/133285
    Source code download at netmf.codeplex.com via link.

    mscorlib: http://referencesource.microsoft.com/#mscorlib/system/gc.cs

    Source code was not available at the time of the start of the work. However, it is now, so I'll go through what they've
    done.  SimpleHeap implementation in .Net is a Buddy Allocator:
    http://netmf.codeplex.com/SourceControl/latest#client_v4_1/DeviceCode/pal/SimpleHeap/SimpleHeap.cpp


    Mono uses either Boehm or Precise SGen: http://www.mono-project.com/docs/advanced/garbage-collector/sgen/
    .Net Microframework http://netmf.codeplex.com/SourceControl/latest#client_v4_1/CLR/Core/GarbageCollector.cpp

    Java: http://www.azulsystems.com/sites/default/files/images/Understanding_Java_Garbage_Collection_v3.pdf


