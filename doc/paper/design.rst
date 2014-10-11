.. raw:: latex

    \chapter{Design
        \label{chapter-design}}

The implementation of the allocator (Jeff) is described in detail in Chapter :ref:`chapter-jeff`, and the implementation of the
benchmark tool (Steve) is described in detail in Chapter :ref:`chapter-steve`.

Background
============
To get started with my allocator, I started implementing a buddy allocator since the basics of the buddy allocator is
used in the allocators I've included in my comparison. (Evans 2006) Since a buddy allocator's two main modes of operation is
splitting and joining blocks, care needs to be taken that these two operations are as quick as possible.

During development, I quickly realized the fact that picking the wrong data structure for storing the block list made
splitting and joining operations slow and error-prone. 

Indeed, I made the incorrect decision which made said operations difficult to implement. I discarded the buddy allocator
prototype and started on the actual allocator that was to be the end result, with lessons learned
from the prototype incorporated into the design phase.

Allocator Design
=================
.. raw:: comment-done (gres)

    jag skulle gärna se ökat tydlighet på designprocessen här: 
    vad började du med för antaganden?

    * varför fungerade/ fungerade de inte
    * vilka beslut tog du som konsekvens av detta
        
    Anledningen är att "According to Sandelowski and Barroso, the format and general ”sense of rightness and feeling of
    comfort” is an important part how the reader evaluate the validity and trustworthiness." (klippt ur ett av mina papper),
    du kommer att slippa massor av obefogad kritik vid försvaret.

    ----

I assume that there willl be idle time when the application is not doing any other processing. Since malloc and free must
be interactive, i.e. return immediately with a block of memory that the client code can use, anything that can be
off-loaded to for batch processing at a later point in time when there is idle time in the application. Moreover, since
I have the freedom to move objects around in memory transparently for client code, any logic in free/malloc that handles
memory layout, e.g. for optimization purposes (either space or time efficiency) should be handled in the idle time to as
a high extent as possible..

From this, we arrive at my original idea of a quick malloc, a quick free and a slow compact, the latter performing any
batch processing postponed from free and malloc.  I envisioned this as a malloc that would basically grow the top pointer of the
malloc-associated heap: store information about the requested block size, increase the top pointer and return the block.
The free operation would mark the block as not in use anymore. Eventually, the top pointer would reach the end of the
eap, at which point the compact operation would go through the heap and reclaim previously freed memory and reset the
top pointer to end of allocated memory leaving the freed memory as a large block of memory ranging to the end.

However, that idea turned out incorrect because blocks can, of course, be locked at the time of compact. Remember that locking a block
gives the client code the actual pointer to memory, and unlocking the blocks invalidates the pointer. Therefore, the
worst-case scenario is that a block at the very top of the heap is locked compact is invoked even though all
unlocked free blocks are coerced into a single free block, a locked block at or close to the top would make
subsequent malloc calls to fails.  Therefore, a free list needs to be maintained even though it might be the case for
real-world applications that the worst case seldom occurs.  I have not studied the frequency of this happening in this
report. It would make for good future work.

Free In More Detail
~~~~~~~~~~~~~~~~~~~~
When an allocation request comes in, the size of the request is checked against the top pointer and the end of the heap.
A request that fits is associated with a new handle and returned. If there is no space left at the top, the free list is
searched for a block that fits.

Freeing a block marks it as unused and adds it to the free list, for malloc to find later as needed.  The free list is
an index array of :math:`2^{3..k}`-sized blocks with a linked list at each slot. All free blocks are guaranteed to be at least
:math:`2^n`, but smaller than :math:`2^{n-1}`, bytes in size. Unlike the buddy allocator, blocks are not merged on free. (See
future work in section :ref:`jeff-future-work` for a brief discusson.)

.. figure:: graphics/jeff-free-blockslots.png
   :scale: 50%

   :label:`jeffexampleblockslots` Example slots in free list.

An example free blockslots list is given in Figure :ref:`jeffexampleblockslots`.

Compacting
~~~~~~~~~~~~
.. X X X (gres, DONE): jag har ju algoritm-adhd, så det kanske bara är jag, men fundera över om en bild hade varit bra här.

Compacting uses a greedy Lisp-2-style compacting algorithm Jones & Lins (1997), see section :ref:`compact-heap`
for a step-by-step version and section :ref:`rmcompact` for
an explanation with figures. In short, blocks are moved closer to bottom of the heap (if possible), otherwise the first
block (or blocks) to fit in the unused space is moved there. The first case happens if there are no locked blocks
between the unused space and next used (but not locked) block, simply moving the memory blocks and updating pointers is
enough. A quick operation that leaves no remainding holes. If however there are any locked blocks between the unused
space and the next used block, obviously only blocks with a total length of less than or equal the size of the unused
space can be moved there. The algorithm is greedy and takes the first block that fits. More than one adjacent block that
fits within the unused space will be moved together. In the case that there are no blocks that fit the unused space and
there is a locked block directly after, scanning is restarted beginning with the block directly following the last free
block found. The process is continued until there are no unused blocks left or top of memory is reached.

Allocator Algorithm
====================
Initialization
~~~~~~~~~~~~~~~~~~~~~~~~~~
#. We're passed a heap of a given size from the client
#. Set boundaries of the header list growing down from top of heap
#. Initialize the free block slot list

Allocation Request
~~~~~~~~~~~~~~~~~~~~~~~~~~
#. Request a new header to associate with the block

   #. If built with unused header list, grab the first one in list and relink root
   #. Else, scan the header list for unused header. If not available, move bottom down one header.
   #. If bottom clashes with space occupied by a block, fail.

#. If there is available space for the allocation request, use it and associate with the block.
#. Else, find a free block within the free block slot list:

   #. Search in the slot associated with the math:`log_2´-size of the request for a free block.
   #. Else, repeat the previous step in higher slots until top is reached. If there are still no blocks found, fail.

#. Split the block as needed, insert the rest into the free block slots and return the rest.

Free Block
~~~~~~~~~~~~~~~~~~~~
#. Mark the header as free
#. Overwrite the block with a free memory block structure pointing to the header location, with the struct's memory
   member pointing to ``NULL``.
#. Insert the block into the appropriate location in the free block slots list.

Compact Heap
~~~~~~~~~~~~~~~~~
#. Sort the header list items' next pointers in memory order.
#. Starting from start of the heap: while there are unoccupied spaces in the rest of the heap or compacting has reached
   its time limit, do the following.
#. Scan for the first unlocked [#]_ memory block.
#. If there are no locked blocks between the unoccupied space and the first unlocked memory block, move the memory by
   the offset betwen locked and unused memory.
#. If there are any locked inbetween, move only as much memory as will fit into the unlocked space. Create a free block
   of the rest of the memory inside the unoccupied space.
#. Restart from point 2.
#. Merge all adjacent free blocks and mark the headers not in used as unused.
#. Rebuild the free block slots by scanning the free header blocks and inserting them at the appropriate locations in
   the list.

.. [#] Only unlocked memory blocks can be moved. Clients have references to locked blocks and therefore cannot be
   changed.

Benchmark Tool Design
======================
Manually modifying applications to adhere to Jeff's allocation interface is error-prone and time consuming, and moreover it is not certain
that the chosen application is a good candidate for demonstrating performance since it might not stress the allocator. The number
of requests could be small and the total memory usage could be low. 

Measuring Jeff requires a rewrite of the application needing to be tested, to use the new malloc interface. The simple
solution to do so is to emulate a regular malloc, i.e. directly lock after malloc. But that would make the compact
operation no-op since no blocks can be moved. On the other hand, adapting existing code to benefit from Jeff's interface
is error-prone. It is also not obvious which application would make good candidates. Automating the modifications, if
possible, would save much time.  Finally, source code to the applications would be required for manual adaptions, which
is not always available. I have therefore not done any manual adaptions of an application.

The specifics of how data is collected can be found in chapters :ref:`chapter-simulating-application-runtime` and :ref:`chapter-steve`.

