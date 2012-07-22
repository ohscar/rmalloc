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

/* client code */

// first argument is either a real void* or malloc-specific handle type

void print_after_malloc_stats(void *, int address, int size);
void print_after_free_stats(int address, int size);

bool user_init(unsigned int heap_size, char *name);
void user_destroy();
bool user_handle_oom(int size); // number of bytes tried to be allocated
void *user_malloc(int size);
void user_free(void *);
void user_lock(void *);
void user_unlock(void *);

/* driver code */

// to be called from all ops that possibly modify memory, i.e. alloc, free, compact
void plot_report(long memory_delta, unsigned long free_bytes, unsigned long largest_allocatable_block, int fragmentation_percent, suseconds_t op_time, int caused_oom);

#endif // PLOT_H

