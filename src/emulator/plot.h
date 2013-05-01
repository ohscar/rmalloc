/* plot.h
 *
 * hooks to be implemented by the client.
 *
 * reporting functionality for giving feedback from client to driver for
 * plotting purposes.
 *
 * at each alloc/free point, report (non-exhaustive list):
 *
 * - time per alloc/free. lock too?
 * - bytes consumed of heap (usage in percent?)
 * - fragmentation
 * - largest alloc'able block (req. backtracking)
 */
#ifndef PLOT_H
#define PLOT_H

#include <sys/time.h>
#include <inttypes.h>

/* client code */
/*
 * colormap is a byte array of sizeof(g_heap)/4.
 *
 * g_heap (word aligned) is filled 0xDEADBEEF, then colormap is mapped to a color depending on whether it's 
 * known to be used, overhead of the allocator, known free, or other (unknown), as per the enum.
 */

#define PAINT_INITIAL_COLOR 0xDEADBEEF
enum {
    COLOR_GREEN = 0, // known free
    COLOR_RED, // known used
    COLOR_WHITE, // overhead
};


enum {
    OP_ALLOC,
    OP_FREE,
};

#define HEAP_INITIAL_VALUE (uint32_t)0xDEADBEEF

// first argument is either a real void* or malloc-specific handle type

void print_after_malloc_stats(void *, int address, int size);
void print_after_free_stats(int address, int size);

/*
 * 1. color as initial
 * 2. paint heap
 * 3. operation:
 * a. free() => mark heap location as free.
 * b. alloc() => mark heap location as used (zero out alloc'd data?)
 * 4. paint heap
 */

// repaint colormap by scanning or doing other things if the allocator's internal workings are known.
void user_paint(void);

extern bool user_init(uint32_t heap_size, void *heap, void *colormap, char *name);
extern void user_destroy();
extern bool user_handle_oom(int size); // number of bytes tried to be allocated, return true if <size> bytes could be compacted.
extern void *user_malloc(int size, uint32_t handle);
extern void user_free(void *, uint32_t handle);
extern void user_lock(void *);
extern void user_unlock(void *);

/* driver code */

// to be called from all ops that possibly modify memory, i.e. alloc, free, compact
void plot_report(long memory_delta, unsigned long free_bytes, unsigned long largest_allocatable_block, int fragmentation_percent, suseconds_t op_time, int caused_oom);

#endif // PLOT_H

