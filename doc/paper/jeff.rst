.. vim:tw=120

Jeff
====
... is a compacting allocator.

Overview
~~~~~~~~~
TODO:
- different signature

In order to achieve compacting, memory must be accessed indirectly. This is the signature::

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

Freeing a block marks it as unused and adds it to the free list, for malloc to find later as needed.
The free list is an index array of 2^3..k-sized blocks with a linked list at each slot. All free blocks are guaranteed
to be at least 2^n in size. Because of the automatic merging with blocks in free, they can also be larger.

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
~~~~~~~~~~~~~~~~
- detailed breakdown of
  + init
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

