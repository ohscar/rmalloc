=========================================
rmalloc: Relocatable memory allocator
=========================================

Gives you defragmentation within a custom allocator::

   handle_t rmalloc(int size);
   void rmfree(handle_t *);
   void *rmlock(handle_t *);
   void rmunlock(handle_t *);

Lock a handle to get the memory address, unlock it when done. Unlocked memory blobs can be moved around at defragmentation time.

Requires modifications to code using a normal malloc(), but can potentially be quicker and more memory efficient.

