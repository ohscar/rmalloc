emulator/

For emulating running an application using different allocators and gathering statistics about their characteristics.

Input data is collected by running an app through Valgrind's memcheck w/ my patches for malloc/free calls. Then,
translated into handles.  Input data then fed into a "memory emulator", running different allocators and looking at the
resulting memory map. The memory map is pre-allocated, and the allocators do not use mmap().

Run::

    $ plot_dlmalloc opsfile 

writes to ``/tmp/fragmentplot.txt`` and calls ``plot_fragment_image.py`` to generate memory dump (opsfile-NNNNNN.png).

