Contclusions schmonklusions go here

Speed
==========

Memory
==========

Future work
===========
Limitations in Jeff
~~~~~~~~~~~~~~~~~~~~~~~
The allocator is not aligned, as is common in allocators, to keep thing single. Also, while newer CPU architectures
still have a penalty on non-aligned memory access, code works. Finally, the allocator is not thread-safe. This 

Future work
~~~~~~~~~~~~

- <FUTURE-WORK have a callback for when moving a locked block?>
- <FUTURE-WORK possible optimization: next_unused reduce to to just store offset into the header array>
- <FUTURE-WORK possible optimization: use some bits of memory to store flags?>
- <FUTURE-WORK automatic merge with adjacent prev/next block in free/new?>
- discarded ideas

  + notification on low memory for user compact (spent much time trying to work out algorithm before there was working
    code, premature optimization) <FUTURE-WORK>

- possible optimizations (future work)

  + speed is good enough
  + memory usage: make it more specific to save memory per-handle
  + weak locking

Future work in Steve
~~~~~~~~~~~~~~~~~~~~~
* colormap parameter
* theoretical free size based on colormap

