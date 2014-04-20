.. vim:tw=120

Jeff
====
... is a compacting allocator.

Overview
========
TODO:
- different signature

In order to achieve compacting, memory must be accessed indirectly. This is the signature::

    void rminit(void *heap, size_t heap_size);
    handle_t *rmmalloc(size_t nbytes);
    void rmfree(handle_t *handle);
    void *lock(handle_t handle);
    void unlock(handle_t handle);

    void compact(uint32_t compact_time_max_ms);

`handle_t` is an opaque type. To get the actual memory pointed to at by the handle, call `lock()` to obtain a regular
pointer to memory. During the time a block pointed out by a handle is locked, the compact operation is not allowed to
move it. If it could be moved, the pointer obtained by the client code would no longer be invalid. This also puts
certain limitations on the compactor, since it needs to deal with possibly locked blocks.  More on `compact()` later.
Also, client code needs to be adapted to this allocator.

TODO:- requires modifications of application
  + indirect memory access through handles
  + benchmark w/ modified apps? time-consuming
  + enter Steve for automating testing
  + locked/unlocked objects (based on heuristics, Steve)

To get started with my allocator, I started implementing a buddy allocator. Along with it, I developed tests using
<LINK: Google Test Library> to make sure no regressions were introduced during development.  More on that later.

During the development, it quickly became apparent that the internal data structures of the allocator must match the
buddy allocator memory layout closely.  Picking the wrong data structure for storing the block list made the merge-block
operation slow and error-prone, instead of being easy to implement if proper care is taken to align code and structures
with physical memory layout. Indeed, I made the incorrect decision which made the compacting operationg difficult to
implement. The buddy allocator prototype was discarded and work started with the actual allocator that was to be the end
result, with the lessons about taking care in the design phase learned.

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
maintained even though it might be the case for real-world applications that the worst case occurs seldom. (REF-STEVE,
FUTURE-WORK).

When an allocation request comes in, the size of the request is checked against the top pointer and the end of the heap.
A request that fits is associated with a new handle and returned. If there is no space left at the top, the free list is
searched for a block that fits.

TODO:
- why is block merging not possible?

Freeing a block marks it as unused and adds it to the free list, for malloc to find later as needed.
The free list is an index array of 2^3..k-sized blocks with a linked list at each slot. All free blocks are guaranteed
to be at least 2^n, but smaller than 2^(n-^1), bytes in size. Unlike the buddy allocator, blocks are not merged on free. <FUTURE-WORK: block merging possible?>. 

.. figure:: graphics/jeff-free-blockslots.png
   :scale: 50%

   Example slots in free list.

TODO
- explain Lisp-2

Compacting is a greedy Lisp-2-style compacting, where blocks are moved closer to bottom of the heap (if possible),
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
In the previous overview section I described the general functionality. This section will in more details describe how
each of the key parts work, including memory layout and performance metrics.

TODO: describe O(...) of all complex operations.

rminit
~~~~~~
Recall the signature::

    void rminit(void *heap, size_t heap_size);

``heap`` and ``heap_size`` is the client-supplied heap. Jeff is self-contained within the heap and requires no
additional storage except for stack space.

Internal structures are initialized:

* Boundaries (g_memory_bottom/g_memory_top)
* Header blocks (g_header_root and g_unused_header_root)
* Free block slots (g_free_block_slots)

I'll go through each one of them below, and their uses will be clarified as I touch upon them later in the other parts
of the allocator.

Boundaries (g_memory_bottom/g_memory_top)
-----------------------------------------
Bottom of memory is the bottom of the heap and top is the highest used memory address. Compacting resets the top to the
highest used memory address.

Header blocks (g_header_root and g_unused_header_root)
--------------------------------------------------------------
The opaque type ``handle_t`` is a pointer to a ``header_t`` structure::

    typedef struct header_t {
        void *memory;
        uint32_t size;
        uint8_t flags;

        struct header_t *next;
    #if JEFF_MAX_RAM_VS_SLOWER_MALLOC == 0
        struct header_t *next_unused;
    #endif
    };

This is the minimum amount of memory used by a block. Assuming a 32-bit system, ``memory`` is 4 bytes, ``size`` is 4
bytes and ´`flags`` is 1 byte. The header itself is a linked list (``next``) that can be sorted in memory order in the
compact step, since the handles themselves cannot be moved as they're used (in disguise) by the client code. Flags can have one of the following values:

* Free (0)
* Unlocked (1)
* Locked (2)
* Weakly locked (4) (currently unused)

A weakly locked block can be treated as unlocked in the compacting phase so it can be reclaimed. Care needs to be taken
by the client code since compacting invalidates the pointer to memory.

The array of header items grows down from the top of the client-supplied heap. New handles searched for starting at
``g_memory_top`` and down until ``g_memory_bottom``. If there is no free header when requested and there is no overlap
between existing memory (including the newly requested size in case of a malloc), ``g_memory_bottom`` is decreased and a
fresh handle is returned. 

The optional member ``next_unused`` is a compile-time optimization for speeding up the O(n) find header operation to
O(1) at the expense of an extra memory. ``g_unused_header_root`` is set to header newly marked unused and the next
pointer is set to the old unused header root.  Setting ``memory`` to ``NULL`` indicates an unused header. 

``g_header_root`` points to the latest used header. At compact time, it's sorted in memory order.

<FUTURE-WORK have a callback for when moving a locked block?>
<FUTURE-WORK possible optimization: next_unused reduce to to just store offset into the header array>
<FUTURE-WORK possible optimization: use some bits of memory to store flags?>

Free block slots (g_free_block_slots)
-------------------------------------
As touched upon previously, this contains the memory blocks that have been freed and not yet merged into unused space
by a compact operation::

    typedef struct free_memory_block_t {
        header_t *header;
        struct free_memory_block_t *next; // null if no next block.
    } free_memory_block_t;

When a block is freed, a ``free_memory_block_t`` is stored in the first bytes. Therefore, the minimum block size is
(again, 32-bit system) 8 bytes. The header member stores the actual information about the block. By checking
header->memory against the block, we know it's a valid free memory block. The next field points to the next block in the
same size range (explained next).

There are log2(heap_size) (rounded up) slots. Freeing a block of size 472 bytes means placing it at the start of the
linked list at index 9 and hanging the previous list off the new block's next pointer, i.e. a stack.

It's rebuilt at compact time.

rmmalloc
~~~~~~~~~
Minimum allocatable size is ``sizeof(free_memory_block_t)`` for keeping information about the block for the free list.
I'll go through the process of allocation step by step.

There are two cases: either there is space left after top of the memory for a header and the requested memory, in which
case the easy path is taken where a header is allocated, ``g_memory_top`` is bumped and the header is associated with
the newly created memory and returned to the client. Allocating a header means searching the header array for an unused
block, or if the optimization described above, following ``g_unused_header_root``. If none is found, ``g_header_bottom``
grows downward if there is space, but there is always two headers left for compacting (more on that in the section on
compacting).

In the other case, there is no space left after ``g_memory_top`` and the free block must be searched for an appropriate
block. This is the most complex part of alloc/free.

find free block
----------------
TODO: describe O(...) of all complex operations.

Calculate the index *k* into the free block slots list from *log2(size)+1*. As previously explained, the free block
slot list has a stack (implemented as a singly linked list) hanging off each slot, such that finding a suiting block
will be a fast operation. The exeption is for requests of blocks in the highest slot have to be searched in full, since
the first block found is not guaranteed to fit the size request, as the slot *k* stores free blocks *2^(k-1) <= n < 2^k*
and there is no larger *k+1* slot to search in.

In the normal case the free block list is looked up at  *k* for a suiting block. If the stack is empty, *k* is increased
and the free block list again is checked until a block is found.  Finally, if there was no block found, the actual index
*log2(size)* is searched for a block that will fit. Remember that the blocks in a specific slot can be *2^k <= n < 2^k*
and therefore there could be free blocks in slot *k* that are large enough for the request. When a block is found, it's
shrunk into two smaller blocks if large enough, one of the requested size and the remainder. Minimum size for a block to
be shrunk is having one extra header available and that the found block is *sizeof(free_memory_block_t)* bytes larger
than the requested size. Otherwise, the block is used as-is causing a small amount of internal fragmentation. The
remainder of the shrunk block is then inserted into the tree at the proper location.

Returns NULL if no block was found.

shrink block
------------
Adjusts size of current block, allocates a new header for the remainder and associates it with a ``free_memory_block_t``
and stores it in the shrunk block.

rmfree
~~~~~~
Mark the block as unused. <FUTURE-WORK automatic merge with adjacent prev/next block?>

rmcompact
~~~~~~~~~
The compacting operation consists of setup, compacting and finish.

Start with sorting all memory headers by pointer address, such that ``g_root_header`` points to the lowest address in
memory and by following the ``next`` pointer until NULL all blocks can be iterated. All blocks have a header associated
with them, regardless of flags.  This step only has to be done once each call to ``rmcompact()``.

Actual compacting is done in passes so it can be optionally time limited, with a granularity of the time it takes to
perform a single pass.

XXX: pretty pictures

One pass of moving blocks around
------------------------------------
.. raw:: comment

    // [F1 | F2 | F3 | F4 | X1/C | X2/B | U1 | U2 | A]
    // =>
    // [U1 | U2 | F5 | X1/C | X2/B | (possible too big block U3) | F6 | A]
    //
    // * Create F6
    // *
    // * Possible too big block U3?
    // * - Link B to U3
    // * - Link U3 to F6
    // * Else:
    // * - Link B to F6
    //
    // * Link F6 to A
    //
    // A * Create F5
    //   * Link LU to F5
    //   * Link F5 to C
    // B * Extend LU
    //   * Link LU to C

* Get closest range of free headers (or stop if no headers found)

   +  If block directly after free header is locked, set a max size on unlocked blocks.

* Get closest range of unlocked headers (respecting max size if set)

   + No blocks found and limitation set on max size: if free blocks were passed searching for unlocked blocks, try
     again from the block directly after the free headers, else stop.
   + Set adjacent flag if last free's next is first unlocked

* Calculate offset from free area to unlocked area
* Squish free headers into one header and associate memory with the header
* Move unlocked blocks too free area

  - Memmove data
  - Adjust used header pointers

* Adjacent: relink blocks so unlocked headers is placed before what's left of free area, and free area pointing to header
  directly following previous position of last unlocked header's next header.

.. figure:: graphics/compact-adjacent-relink-0.png
   :scale: 50%

   Initial configuration with blocks Unlocked 1-4, Free 1-4, Rest

.. figure:: graphics/compact-adjacent-relink-1.png
   :scale: 50%

   Move all used blocks back (i.e. to the left), relink free blocks.

.. figure:: graphics/compact-adjacent-relink-2.png
   :scale: 50%

   Squish free block.

* Non-adjacent: similar to adjacent, except blocks can't just be simply memmov'ed because of the locked blocks. Instead,
  only the blocks that fit in the free space can be moved.

.. figure:: graphics/compact-nonadjacent-relink-0.png
   :scale: 50%

   Initial configuration with blocks Free 1-3, Locked 1-2, Unlocked 1-3, Rest

.. figure:: graphics/compact-nonadjacent-relink-1.png
   :scale: 50%

   Create free block 6 in the area where the used blocks are now.

.. figure:: graphics/compact-nonadjacent-relink-2a.png
   :scale: 50%

   a): block U3 is too large to fit in the free area.

.. figure:: graphics/compact-nonadjacent-relink-2b.png
   :scale: 50%

   b): block U3 fits in the free area.

.. figure:: graphics/compact-nonadjacent-relink-3a.png
   :scale: 50%

   a): After, with a new block Free 5 with left-overs from Free 1-3 and F6 from the space between U1-U3 and Rest

.. figure:: graphics/compact-nonadjacent-relink-3b.png
   :scale: 50%

   b): Unlocked 3 fits, but not enough size to create a full block F5 -- instead extend size of Unlocked 3 with
   0 < n < sizeof(free_memory_block_t) bytes.

* Continue to next round, repeating until time limit reached or done (if no time limit set)

Finishing
-----------
At the end of the compacting, after the time-limited iterations, finishing calculations are done: calculate the highest
used address and mark all (free) headers above that as unused, adjust ``g_header_bottom`` and finally rebuild the free
block slots by iterating through ``g_header_root`` and placing free blocks in their designated slots.

rmdestroy
~~~~~~~~~
Doesn't do anything - client code owns the heap.

Testing
===========
Unit testing
~~~~~~~~~~~~~
All applications should be bug-free, but for an allocator it is extra important that there are no bugs. Luckily, an
allocator has a small interface for which tests can be easily written. In particular, randomized testing is easy, which
although not guaranteed to catch all bugs gives a good coverage.

I decided to use Google's GTest <REF: GTest> since it was easy to setup, use and the results are easy to read. It's
similar in style to the original SUnit <REF: SUNit> that is popular to use.  During the development of the allocator I
wrote tests and code in parallell, similar to test-driven development in order to verify that each change did not
introduce a regression. Of the approximately 2500 lines of code in the allocator and tests, about half are tests. In
addition to randomized unit testing there are consistency checks and asserts that can be turned on with at compile-time,
to make sure that e.g. (especially) the compact operation is non-destructive.

In the unit tests, the basic style of testing was to initialize the allocator with a randomly selected heap size and
then run several tens of thousands of allocations/frees and make sure no other data was touched.  This is done by
filling the allocated data with a constant byte value determined by the address of the returned handle.  Quite a few
bugs were found this way, many of them not happening until thousands of allocations.  That shows randomized testing in
large volume is a useful technique for finding problems in complex data structures, such as an allocator.

XXX: describe test strategy more in detail?

Real-world testing
~~~~~~~~~~~~~~~~~~~~
Since the allocator does have the interface of standard allocators client code needs to be rewritten. In order to do
testing and benchmarking of real-world applications, applications need to be rewritten. The two major problems with this
is that it requires access to source code, and rewriting much of the source code. Instead, I've developed heuristics for
calculating locking/unlocking based on runtime data of unmodified applicaions. The tool for doing so grew from a
small script into a larger collection of tools related to data collection, analysis and benchmarking. This is described
in greater detail in the chapter on <REF: Steve>.

Profiling
==========
The GNU tool ``gprof`` was used to find code hotspots, where the two biggest finds were:

* *log2()*
* *header_find_free()*

In the spirit of first getting things to work, then optimize, the original *log2* implementation was a naive bitsift
loop. Fortunately, there's a GCC extension *__builtin_clz()* (Count Leading Zeroes) that is efficiently translated into
efficient machine code that can be used to write a fast *log2(n)*: ``sizeof(n)*8 - 1 - clz(n)``. The hotspots in the
rest of the code were evenly distributed and no single point was more CPU-intense than another, except for
*header_find_free()*. As described above, there's a compile-time optimization that cuts down time from *O(n)* to *O(1)*,
which helped cut down execution time yet some more at the expense of higher memory usage per block.

More details and benchmarks in the chapter on <REF: Steve>.


- detailed breakdown of
  + rminit
  + rmmalloc -> newblock -> find free header -> find free block -> ...
  + rmfree -> add to free list
  + rmcompact -> find blocks
  + rmdestroy

- based on buddy allocator
- requires modifications of application
  + indirect memory access through handles
  + benchmark w/ modified apps? time-consuming
  + enter Steve for automating testing
  + locked/unlocked objects (based on heuristics, Steve)
- unknown since first time writing allocator, iterations w/ problems
  + first iteration build a plain buddy allocator to get a feel for problems, proved devil is in the details
  + gtest in beginning to find regressions
  + naive malloc/compact cycle doesn't work w/ locked block at the end

    - need proper free list and splitting, describe free list
    - not considered in original design

  + double indirection creates memory overhead <STEVE>
- header list: design choices (describe layout of internal house-keeping structures)
- original idea of simple malloc, simple free not possible due to locked-blocks-at-end.
- compacting based on lisp-2(?) naive greedy allocator 
- sorting (possible future optimization)
- benchmark (see Steve)
- discarded ideas
  + notification on low memory for user compact (spent much time trying to work out algorithm before there was working
  code, premature optimization) <FUTURE-WORK>
- possible optimizations (future work)
  - speed is good enough
  - memory usage: make it more specific to save memory per-handle
  - weak locking

* existing work
* fragmentation issue
* how it works
  + alloc
  + free
  + compacting
* compare w/ others (results)
* conclusion
* future work
* design choices during implementation, including discarded code (e.g. fragmentation formula in sketch book)

