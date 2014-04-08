.. vim:tw=120

====
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
<LINK: Google Test Library> to make sure no regressions were introduced during development. 

TOOD: a lot more about how the tests were designed, what type of bugs they picked up and how useful they were.

During the development, it quickly became apparent that the internal data structures of the allocator must match the
buddy allocator memory layout closely.  Picking the wrong data structure for storing the block list made the merge-block
operation slow and error-prone, instead of being easy to implement if proper care is taken to align code and structures
with physical memory layout. Indeed, I made the incorrect decision which made the compacting operationg difficult to
implement. The buddy allocator prototype was discarded and work started with the actual allocator that was to be the end
result, with the lessons about taking care in the design phase learned.

TODO:
- quick malloc, quick free, slow compact

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

TODO:
- malloc() description

When an allocation request comes in, the size of the request is checked against the top pointer and the end of the heap.
A request that fits is associated with a new handle and returned. If there is no space left at the top, the free list is
searched for a block that fits.

TODO:
- free() and free list description
- why is block merging not possible?

Freeing a block marks it as unused and adds it to the free list, for malloc to find later as needed.
The free list is an index array of 2^3..k-sized blocks with a linked list at each slot. All free blocks are guaranteed
to be at least 2^n, but smaller than 2^(n-^1), bytes in size. Unlike the buddy allocator, blocks are not merged on free. <FUTURE-WORK: block merging possible?>. 

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
~~~~
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


rmfree
~~~~~~

rmcompact
~~~~~~~~~

rmdestroy
~~~~~~~~~



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
  + double indirection creates memory overhead
- header list: design choices (describe layout of internal house-keeping structures)
- original idea of simple malloc, simple free not possible due to locked-blocks-at-end.
- compacting based on lisp-2(?) naive greedy allocator 
  - sorting (possible future optimization)
- benchmark (see Steve)
- discarded ideas
  + notification on low memory for user compact (spent much time trying to work out algorithm before there was working
    code, premature optimization)
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

