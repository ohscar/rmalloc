.. vim:tw=120

.. raw:: latex

    \chapter{Jeff: The Compacting Allocator
        \label{chapter-jeff}}

Overview
========
In order to achieve compacting, memory must be accessed indirectly. This is the signature::

    void rminit(void *heap, size_t heap_size);
    handle_t rmmalloc(size_t nbytes);
    void rmfree(handle_t *handle);
    void *lock(handle_t handle);
    void unlock(handle_t handle);

    void compact(uint32_t compact_time_max_ms);

``handle_t`` is an opaque type. To get the actual memory pointed to at by the handle, call `lock()` to obtain a regular
pointer to memory. During the time a block pointed out by a handle is locked, the compact operation is not allowed to
move it. If it could be moved, the pointer obtained by the client code would no longer be invalid. This also puts
certain limitations on the compactor, since it needs to deal with possibly locked blocks.  This forces client code needs
to be adapted to this allocator, such that memory is always appropriately locked/unlocked as needd. The compacting
operation is discussed in more detail in section :ref:`rmcompact`.

Implementation
==============
In the previous sections I described the general functionality. This section will in more details describe how
each of the key parts work, including memory layout and performance metrics.

.. - TODO: describe O(...) of all complex operations.

rminit
~~~~~~
Recall the signature::

    void rminit(void *heap, size_t heap_size);

``heap`` and ``heap_size`` is the client-supplied heap. Jeff is self-contained within the heap and requires no
additional storage except for stack space.

Internal structures are initialized:

* Boundaries (``g_memory_bottom``, ``g_memory_top``)
* Header blocks (``g_header_root``, ``g_unused_header_root``)
* Free block slots (``g_free_block_slots``)

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
bytes and ``flags`` is 1 byte. The header itself is a linked list (``next``) that can be sorted in memory order in the
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

The optional member ``next_unused`` is a compile-time optimization for speeding up the *O(n)* find header operation to
*O(1)* at the expense of an extra memory. ``g_unused_header_root`` is set to header newly marked unused and the next
pointer is set to the old unused header root.  Setting ``memory`` to ``NULL`` indicates an unused header. 

``g_header_root`` points to the latest used header. At compact time, it's sorted in memory order.

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
``header->memory`` against the block, we know it's a valid free memory block. The next field points to the next block in the
same size range (explained next).

There are :math:`log_2(heap_size)` (rounded up) slots. Freeing a block of size 472 bytes means placing it at the start of the
linked list at index 9 and hanging the previous list off the new block's next pointer, i.e. a stack, and is rebuilt at
compact time. Adding a free block takes constant time.

rmmalloc
~~~~~~~~~
Minimum allocatable size is ``sizeof(free_memory_block_t)`` for keeping information about the block for the free list.
I'll go through the process of allocation step by step.

There are two cases: either there is space left after top of the memory for a header and the requested memory, in which
case the fast path is taken where a header is allocated, ``g_memory_top`` is bumped and the header is associated with
the newly created memory and returned to the client. Allocating a header means searching the header array for an unused
block, or if the optimization described above, following ``g_unused_header_root``. If none is found, ``g_header_bottom``
grows downward if there is space, but there is always two headers left for compacting (more on that in the section on
compacting).

In the other case, there is no space left after ``g_memory_top`` and the free block list must be scanned for an appropriate
block. This is the most complex part of alloc/free.

The time complexity of the simple case with the aferementioned optimization is *O(1)*, or *O(n)* (in terms of number of
handles in the system) in the unoptimized case. In the case where memory can't grow up (see Section
:ref:`find-free-block` below), the time complexity is worst case  *O(n)* (in terms of the number of blocks of the
specific size) and best case *O(1)*.

Find free block
----------------
Calculate the index *k* into the free block slots list from :math:`log_2(size)+1`. As previously explained, the free block
slot list has a stack (implemented as a singly linked list) hanging off each slot, such that finding a suiting block
will be a fast operation. The exeption is for requests of blocks in the highest slot have to be searched in full, since
the first block found is not guaranteed to fit the size request, as the slot *k* stores free blocks :math:`2^{k-1} <= n < 2^k`
and there is no larger :math:`k+1` slot to search in.

In the normal case the free block list is looked up at *k* for a suiting block. If the stack is empty, *k* is increased
and the free block list again is checked until a block is found.  Finally, if there was no block found, the actual index
:math:`log_2(size)` is searched for a block that will fit. Remember that the blocks in a specific slot can be :math:`2^{k-1} <= n < 2^k`
and therefore there could be free blocks in slot *k* that are large enough for the request. When a block is found, it's
shrunk into two smaller blocks if large enough, one of the requested size and the remainder. Minimum size for a block to
be shrunk is having one extra header available and that the found block is ``sizeof(free_memory_block_t)`` bytes larger
than the requested size. Otherwise, the block is used as-is causing a small amount of internal fragmentation. The
remainder of the shrunk block is then inserted into the tree at the proper location.

Returns ``NULL`` if no block was found.

Shrink block
------------
Adjusts size of current block, allocates a new header for the remainder and associates it with a ``free_memory_block_t``
and stores it in the shrunk block.

rmfree
~~~~~~
Mark the block as unused. 

rmcompact
~~~~~~~~~
The compacting operation consists of setup, compacting and finish.

Start with sorting all memory headers by pointer address, such that ``g_root_header`` points to the lowest address in
memory and by following the ``next`` pointer until ``NULL`` all blocks can be iterated. All blocks have a header associated
with them, regardless of flags.  This step only has to be done once each call to ``rmcompact()``.

Actual compacting is done in passes so it can be optionally time limited, with a granularity of the time it takes to
perform a single pass, so it is not a hard limit. Also, the sorting in the beginning and the free block list rebuilding
in the end is not included in the time constraint.

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

#. Get closest range of free headers (or stop if no headers found)

   #.  If block directly after free header is locked, set a max size on unlocked blocks.

#. Get closest range of unlocked headers (respecting max size if set)

   #. No blocks found and limitation set on max size: if free blocks were passed searching for unlocked blocks, try
      again from the block directly after the free headers, else stop.
   #. Set adjacent flag if last free's next is first unlocked

#. Calculate offset from free area to unlocked area
#. Squish free headers into one header and associate memory with the header
#. Move unlocked blocks to free area

  #. Move data
  #. Adjust used header pointers

#. Adjacent: relink blocks so unlocked headers is placed before what's left of free area, and free area pointing to header
   directly following previous position of last unlocked header's next header, see Figure :ref:`jeffcompactadj0`, :ref:`jeffcompactadj1` and :ref:`jeffcompactadj2`.

#. Non-adjacent: similar to adjacent, except blocks can't just be simply memmov'ed because of the locked blocks. Instead,
   only the blocks that fit in the free space can be moved. See Figure :ref:`jeffcompactnonadj0`, :ref:`jeffcompactnonadj1`, :ref:`jeffcompactnonadj2a`, :ref:`jeffcompactnonadj2b`, :ref:`jeffcompactnonadj3a` and :ref:`jeffcompactnonadj3b`.

#. Continue to next round, repeating until time limit reached or done (if no time limit set)

.. figure:: graphics/compact-adjacent-relink-0.png
   :scale: 50%

   :label:`jeffcompactadj0` Initial configuration with blocks Unlocked 1-4, Free 1-4, Rest

.. figure:: graphics/compact-adjacent-relink-1.png
   :scale: 50%

   :label:`jeffcompactadj1` Move all used blocks back (i.e. to the left), relink free blocks.

.. figure:: graphics/compact-adjacent-relink-2.png
   :scale: 50%

   :label:`jeffcompactadj2` Squish free block.


.. figure:: graphics/compact-nonadjacent-relink-0.png
   :scale: 50%

   :label:`jeffcompactnonadj0` Initial configuration with blocks Free 1-3, Locked 1-2, Unlocked 1-3, Rest

.. figure:: graphics/compact-nonadjacent-relink-1.png
   :scale: 50%

   :label:`jeffcompactnonadj1` Create free block 6 in the area where the used blocks are now.

.. figure:: graphics/compact-nonadjacent-relink-2a.png
   :scale: 50%

   :label:`jeffcompactnonadj2a` a): block U3 is too large to fit in the free area.

.. figure:: graphics/compact-nonadjacent-relink-2b.png
   :scale: 50%

   :label:`jeffcompactnonadj2b` b): block U3 fits in the free area.

.. figure:: graphics/compact-nonadjacent-relink-3a.png
   :scale: 50%

   :label:`jeffcompactnonadj3a` a): After, with a new block Free 5 with left-overs from Free 1-3 and F6 from the space between U1-U3 and Rest

.. figure:: graphics/compact-nonadjacent-relink-3b.png
   :scale: 50%

   :label:`jeffcompactnonadj3b` b): Unlocked 3 fits, but not enough size to create a full block F5 -- instead extend size of Unlocked 3 with
   0 < n < sizeof(free_memory_block_t) bytes.


Finishing
-----------
At the end of the compacting, after the time-limited iterations, finishing calculations are done: calculate the highest
used address and mark all (free) headers above that as unused, adjust ``g_header_bottom`` and finally rebuild the free
block slots by iterating through ``g_header_root`` and placing free blocks in their designated slots.

rmdestroy
~~~~~~~~~
Doesn't do anything - client code owns the heap passed to ``rminit()``.

Testing
===========
As described in Chapter :ref:`chapter-method`, unit testing is utilized where applicable.

Real-World Testing
~~~~~~~~~~~~~~~~~~~~
Since the allocator does have the interface of standard allocators client code needs to be rewritten.  The two major
problems with this is that it requires access to source code, and rewriting much of the source code.  This is where
Steve is useful.


Profiling
==========
The GNU profiling tool *gprof* [#]_ was used to find code hotspots, where the two biggest finds were:

* ``log2()``
* ``header_find_free()``

In the spirit of first getting things to work, then optimize, the original ``log2()`` implementation was a naive bitsift
loop. Fortunately, there's a GCC extension ``__builtin_clz()`` (Count Leading Zeroes) that is translated into
efficient machine code on at least x86 that can be used to write a fast ``log2(n)`` as ``sizeof(n)*8 - 1 - clz(n)``. The
hotspots in the rest of the code were evenly distributed and no single point was more CPU-intense than another, except
for ``header_find_free()``. As described above, there's a compile-time optimization that cuts down time from *O(n)* to
*O(1)*, which helped cut down execution time even more at the expense of higher memory usage per block.

More details and benchmarks in Chapter :ref:`chapter-steve`.

.. [#] http://www.gnu.org/software/binutils/ 
