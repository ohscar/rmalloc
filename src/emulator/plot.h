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

#if __x86_64__
typedef uint64_t ptr_t;
#else
typedef uint32_t ptr_t;
#endif

#define TIMER_DECL struct timespec start_time, end_time
#define TIMER_START clock_gettime(CLOCK_MONOTONIC, &start_time)
#define TIMER_END clock_gettime(CLOCK_MONOTONIC, &end_time)
#define TIMER_ELAPSED (end_time.tv_sec*1000000 + end_time.tv_nsec/1000 - start_time.tv_sec*1000000 - start_time.tv_nsec/1000)

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
    CHAR_GREEN = ' ',
    CHAR_RED = '#',
    CHAR_WHITE = '.'
};

enum {
    OP_ALLOC = 'N',
    OP_FREE = 'F',
};

enum {
    HEAP_INITIAL = 0xDEADBEEF,
    HEAP_ALLOC = 0xBEEFBABE,
    HEAP_FREE = 0xDEADBABE
};

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
extern void user_reset(); // basically destroy + init
extern bool user_handle_oom(int size, uint32_t *op_time); // number of bytes tried to be allocated, return true if <size> bytes could be compacted.
extern void *user_malloc(int size, uint32_t handle, uint32_t *op_time, void **memaddress);
extern void user_free(void *, uint32_t handle, uint32_t *op_time);
extern void *user_lock(void *); // takes whatever's returned from user_malloc()
extern void user_unlock(void *); // takes whatever's returned from user_malloc() 
extern void *user_highest_address(bool full_calculation); // what is the highest address allocated? NULL if not accessible.
extern bool user_has_heap_layout_changed();

/* driver code */

// to be called from all ops that possibly modify memory, i.e. alloc, free, compact
//void plot_report(long memory_delta, unsigned long free_bytes, unsigned long largest_allocatable_block, int fragmentation_percent, suseconds_t op_time, int caused_oom);
void plot_report(unsigned long largest_allocatable_block);

#endif // PLOT_H

