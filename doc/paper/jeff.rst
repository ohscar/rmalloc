.. vim:tw=120

Jeff
====
... is a compacting allocator.

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

My original idea was to have a quick malloc, a quick free and a quick compact. Compact could then be run at times when
the client application was waiting for user input or otherwise not doing computations, such that the malloc would be
very cheap.

- different signature
- quick malloc, quick free, slow compact
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

