.. raw:: latex

    \chapter{Allocator Types
        \label{chapter-allocator-types}}

In this chapter I'll describe the most common styles of allocator implementation strategies.

Buddy Allocator
================
The most common allocator type is the buddy allocator [#]_, and many allocators are built on its principles, or at least
incorporate them in some way: start with a single block and see if the requested chunk fits in half of the block. If it
does, split the block into two and repeat, until no smaller block size would fit the request. The allocator described in
this paper does away with merging blocks directly on free, by moving free blocks together and coalescing them in the compacting step.

.. figure:: graphics/buddyalloc-2k-list.png
   :scale: 50%

   :label:`buddyalloc2klist` The free list in a buddy allocator

.. [#] http://en.wikipedia.org/wiki/Buddy_memory_allocation

Over time, there will be more and more items of size :math:`2^n`, stored on a free list for that block size, as shown in Figure :ref:`buddyalloc2klist` Each pair
of split-up blocks is said to be two buddies. When two buddy blocks are free, they can be joined. A block of the next
larger size (:math:`n+1`) can be created from these two blocks. The merge repeated until the largest block, i.e. :math:`2^k`. In the worst
case, this causes :math:`2^n - 1` bytes of overhead per block, also known as *internal fragmentation.* Still, this commonly
used algorithm has shown to be good enough in many cases and is often incorporated as one strategy of an allocator.

.. XXX (gres)
.. ~~~~~~~~~~
.. Nedanstående stycke skulle må bra av en bild. "Läs det som 'tog ett tag att fatta'"

All blocks must have metadata associated with them.  The minimum amount of information is the length of the block, in
order for *free()* to know where the block ends.  For convenience, this is often stored in memory just before the block
itself.  The metadata could also be stored elsewhere, e.g. a lookup table *S(addr)* that gives the size of the block
starting with *addr*. 

The metadata associated with the block is normally not accessible by user code (unless queried using allocator-specific
debugging code, when available) and adds to external fragmentation.  It is the fragmentation between the user blocks
(hence *external*), i.e. any overhead caused by information required by the allocator, but not the user code.

Conceptually, the buddy allocator is a very simple allocator to use and implement, but not the most efficient because of
internal fragmentation.

Pool Allocator
==================
Certain applications use a large amount of objects of the same size that are allocated and freed continuously. This
information can be used to create specialized pool allocators for different object sizes, where each pool can be easily
stored in an array of :math:`N*sizeof(object)` bytes, allowing for fast lookup by malloc and free. For example, in an action
computer game where both the player and the enemies shoot projectiles with weapons, said projectiles must be kept track of by means
of an object in memory. In a game where there could potentially exist a very large amount of projectiles in action at the
same time, an equal amount of allocation and freeing is done, often randomly. In order to optimize usage of system
memory, all allocation of projectile objects would be contained to the same  *pool* of memory.  A very simple such allocator
would be a simple list of objects and the allocator could just return the index to the next unused block,
leading to malloc and free that both have :math:`O(1)` in time complexity with very little overhead and fragmentation.  The
usual strategies for growing the list apply, such as doubling the size of the list when all items in the list are used.

Arena Allocator
==================
Code with data structures that are related to each other can be allocated from the same arena, or memory region, for
example a document in a word processor. Instead of allocating memory from the system, the allocator requests data from a
pre-allocated larger chunk of data (allocated earlier from the system). The main point of this type of allocator is
quick destruction of all memory related to the working data (i.e. the document), without having to traverse each
individual structure associated with the document to avoid memory leaks from not properly freeing all memory. In
applications where a document/task has a well-defined set of objects associated with it, with relatively short lifetime
of the document, but many documents created/destroyed over the total application lifetime, there is a large speed
benefit to be had from making the free operation faster.

Garbage Collector
==================
A garbage collector is an allocator that automatically provides memory for data as needed. There is no need to
explicitly ask for memory from the allocator, nor to free it when done. Instead, the garbage collector periodically
checks for which objects are still in use by the application (*alive*). An object is is anything that uses heap memory: a number,
a string, a collection, a class instance, and so on. There are several techniques for finding alive objects and
categorizing objects depending on their lifetime in order to more efficiently find alive objects at next pass.
This is described in detail by Jones & Lins (1987).

At any point in time, a garbage collector can move the object around if necessary, therefore any object access is done
indirectly via a translation. User code does not keep a pointer to the block of memory that the object is located in,
but instead this is done with support in the programming language runtime.

A garbage collector, because of the indirect access to memory, can also move objects around in a way that increases
performance in different ways. One way is to move objects closer together that are often accessed together, another way
is to move all objects closer to each other to get rid of fragmentation, which would decrease the maximum allocatable
object size.  A normal allocator cannot do memory defragmentation, or any other memory layout optimization, because
memory is accessed directly, which would invalidate the pointer used by the application code.


