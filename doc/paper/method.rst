.. raw:: latex

    \chapter{Method
        \label{chapter-method}}


.. raw:: comment-wip

    X X X (gres - kräver litteratur/mer annan information för att beskriva vad metoden är)
    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    * DONE: Jag skulle gärna se en tydligare diskussion av metodiken och en formellare beskrivning av den.  
    * Vilka alternativ till metoder finns det (om det finns flera alternativ), och varför är den metoden du valt den mest lämpade till studien? 
    * DONE: Hur har insamling av data skett?  
    * DONE: Hur har analysen gjorts?
    * DONE: Som jag skrev ovan hade jag personligen lagt hypotes och forskningsfrågor i detta kapitel, och lagt kapitlet direkt efter introduction.

The method I used for designing and implementing the allocator (Jeff) is an iterative design process based on experimental designs
and verification thereof, along with theoretical calculations using pen and paper. In particular, the compact operation
went through several iterations in my sketch book before I arrived at the final version. For other parts, I used
the profiling tools found in *GCC* to measure bottlenecks and gradually improving the code until there were no
obvious bottlenecks left. A bottleneck in this case is a piece of code that gets called many times and is slow. I've
aimed to write the code to execute reasonably quick, given the algorithm in use.

Thanks to the rigorous testing framework in place, I could readily modify code without fearing malfunction since the
tests would pick up any errors. Limitations of testing is that it can never prove correctness, only absence of the bugs
the testing framework was designed to find.  The testing framework is described in more detail in Chapter
:ref:`chapter-jeff`.

Steve is the name of the benchmark tool that I designed to test algorithms for Jeff and comparing Jeff to other allocators. In Steve, I've
developed heuristics for calculating locking/unlocking based on runtime data of unmodified applications. The tool for doing so grew
from a small script into a larger collection of tools related to data collection, analysis and benchmarking. It does not
test for correctness.

Data for use by Jeff, and other calculators, is collected by various tools in the benchmark tool. The types of data
used are:

* raw memtrace, from running unmodified applications in my modified Valgrind tool
* ops file, by mapping memory access data to objects
* locking ops file, as above, with lock/unlock operations in place
* benchmark output, by running allocators against ops files

The benchmark output is used to both produce graphs of allocator performance and can together produce internal rankings
between the allocators.

This is described in greater detail in chapters :ref:`chapter-simulating-application-runtime`, :ref:`chapter-steve` and
section :ref:`input-data`.

Development Environment
=========================
The main development system is a Linux-based system (32-bit Ubuntu 13.04), but could likely be adapted to other
UNIX-like systems, such as OS X. Porting it to 64-bit systems requires changing the manual type casting from and to
integer types that assumes a pointer will fit in a 32-bit integer.

The allocator is written in C, and the benchmark tool (Steve) consists of mostly Python code with Cython [#]_ (a Python
framework for interfacing in C and compiling Python into C code) for tight inner loops such as the memtrace to ops
calculation, plus some Bash scripts for glueing it all together.  The data is plotted in graphs and there is also a tool
that creates an animation of memory allocations as they happen in memory.

.. [#] http://cython.org

Parallel with allocator development, I wrote tests using Google's C++ testing framework, googletest [#]_, to make sure no
regressions were introduced during development.  More on that in Section :ref:`testing`.

.. [#] http://code.google.com/p/googletest/

Testing
========
All applications should be bug-free, but for an allocator it is extra important that there are no bugs since an
allocator that does not work properly could cause data corruption. In the best case, this causes the application using
the allocator to malfunction by crashing on execution. In the worst case, an application doing data processing by
reading data into buffers allocated on the heap, doing one or more computations and then writing the data back to disk,
would completely destroy the data without the user knowing an error had occured.

Luckily, an allocator has a small interface for which tests can be easily written. In particular, randomized unit
testing is easy, which gives a good coverage.

I decided to use googletest since it was easy to setup, use and the results are easy to read. It's
similar in style to the original Smalltalk testing framework SUnit [#]_ (later popularized by Java's JUnit [#]_).  During the
development of the allocator I wrote tests and code in parallell, similar to test-driven development in order to verify
that each change did not introduce a regression. Of the approximately 2500 lines of code in the allocator and tests,
about half are tests. In addition to randomized unit testing there are consistency checks and asserts that can be turned
on at compile-time, to make sure that e.g. (especially) the compact operation is non-destructive.

In the unit tests, the basic style of testing was to initialize the allocator with a randomly selected heap size and
then run several tens of thousands of allocations/frees and make sure no other data was touched.  This is done by
filling the allocated data with a constant byte value determined by the address of the returned handle.  Many
bugs were found this way, many of them not happening until thousands of allocations.  

Fuzzing
~~~~~~~~
I've introduced bugs in the functions called from the allocation interface to see if the testing framework would pick them
up. Focus is to introduce small changes, so-called *off-by-one errors*, where (as the name suggest) a value or code path
is changed only slightly but causes errors.

|  ``free_memory_block_t *block_from_header(header_t *header)``:
|
|     ``return (free_memory_block_t *)((uint8_t *)header->memory + header->size) - 1;``

| Fuzzed
|    
|    ``return (free_memory_block_t *)((uint8_t *)header->memory + header->size);``


|  ``uint32_t log2_(uint32_t n)``:
|
|    ``return sizeof(n)*8 - 1 - __builtin_clz(n);``

| Fuzzed
| 
|    ``return sizeof(n)*8 - __builtin_clz(n);``

|  ``inline bool header_is_unused(header_t *header)``:
|
|    ``return header && header->memory == NULL;``

| Fuzzed
|
|    ``return header && header->memory != NULL;``

|  ``inline void header_clear(header_t *h)``:
|
|    ``h->memory = NULL;``
|    ``h->next = NULL;``

| Fuzzed (1)
|
|    ``//h->memory = NULL;``
|    ``h->next = NULL;``

| Fuzzed (2)
|
|    ``h->memory = NULL;``
|    ``//h->next = NULL;``

|  ``header_t *header_new(bool insert_in_list, bool spare_two_for_compact)``
|
|    ``...``
|    ``header->flags = HEADER_UNLOCKED;``
|    ``header->memory = NULL;``
|    ``...``
|    ``if ((header->next < g_header_bottom || header->next > g_header_top) && header != g_header_root) {``
|    ``...``


| Fuzzed (1)
|
|    ``...``
|    ``//header->flags = HEADER_UNLOCKED;``
|    ``header->memory = NULL;``
|    ``...``
|    ``if ((header->next < g_header_bottom || header->next > g_header_top) && header != g_header_root) {``
|    ``...``


| Fuzzed (2)
|
|    ``...``
|    ``header->flags = HEADER_UNLOCKED;``
|    ``header->memory = NULL;``
|    ``...``
|    ``if ((header->next < g_header_bottom || header->next > g_header_top)) {``
|    ``...``


|  ``header_t *block_free(header_t *header)``
|
|    ``block->next = g_free_block_slots[index];``
|    ``g_free_block_slots[index] = block;``

| Fuzzed
|
|    ``g_free_block_slots[index] = block;``
|    ``block->next = g_free_block_slots[index];``

|  ``free_memory_block_t *freeblock_shrink_with_header(free_memory_block_t, header_t *, uint32_t)``
|
|    ``h = header_new(/*insert_in_list*/true, /*force*/false);``

| Fuzzed (1)
|
|    ``h = header_new(/*insert_in_list*/false, /*force*/false);``

| Fuzzed (2)
|
|    ``h = header_new(/*insert_in_list*/true, /*force*/true);``

|  ``header_t *freeblock_find(uint32_t size)``
|
|    ``int target_k = log2_(size)+1;``

| Fuzzed
|
|    ``int target_k = log2_(size);``

|  ``rmcompact(int maxtime)``
|
|    ``uint32_t used_offset = header_memory_offset(free_first, unlocked_first);``
|    ``...``
|    ``header_t *free_memory = header_new(/*insert_in_list*/false, /*force*/true)``


| Fuzzed (1)
|
|    ``uint32_t used_offset = header_memory_offset(free_first, free_last);``

| Fuzzed (2)
|
|    ``header_t *free_memory = header_new(/*insert_in_list*/true, /*force*/true)``



.. raw:: comment-does-not-break

    | Original ``header_t *header_find_free(bool spare_two_for_compact)``
    |
    |    ``...``
    |    ``if (g_header_bottom - limit > g_memory_top) {``
    |    ``...``

    | Fuzzed:
    |
    |    ``...``
    |    ``if (g_header_bottom - limit >= g_memory_top) {``
    |    ``...``



.. [#] http://en.wikipedia.org/wiki/SUnit
.. [#] http://en.wikipedia.org/wiki/JUnit

.. raw:: comment-xxx

  X X X: Describe in-depth what the benchmark tool does, see commented-out paragraph below.

  parallel with unit tests to make sure each part works as intended. Benchmarking is done with a separate tool that allows
  the use of arbitrary applications for simulating real-world performance, and also does visualization of execution time,
  space efficiency and distribution of allocation requests.

.. Can an allocator, such as described in Objectives, be efficient in space and time? That is the question I aim to answer in this paper.

Assumptions
==============
* A1: An allocator with little extra increase in memory usage compared to the requested memory by the client application
  is efficient in space.
* A2: An allocator that has a low and constant execution time is efficient in time.

Hypothesis
==========
* H1: An allocator that performs heap compaction can be efficient in both time and space, compared to other commonly
  used allocators. By making the malloc and free operations fast and the compact operation relatively slow and and
  calling it when the system is idle it is possible to achieve this.

What are the space and time requirements of Jeff compared to other popular allocators? Is Jeff a viable alternative to other
popular allocators in real-world situations?

I aim to answer these questions in the report.

