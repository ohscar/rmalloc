/* plot_optimal.cpp
 *
 * no actual allocation, just statistics on what would be an optimal
 * allocator.
 */
#include "../../compact/compact.h"

#include "plot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALLOC_NAME "optimal"

static unsigned long g_heap_size;
static unsigned long g_memory_usage;
static unsigned long g_handles[500*1000];
static unsigned long g_handle_counter = 0;

void print_after_malloc_stats(void *handle, int address, int size) {
    //printf("after alloc size %d to address %p / handle %p\n", size, address, handle);
}

void print_after_free_stats(int address, int size) {
    //printf("after free %p of size %d\n", address, size);
}

void *user_malloc(int size) {
    unsigned long ptr = g_handle_counter;
    g_memory_usage += size;
    g_handles[g_handle_counter] = size;
    g_handle_counter++;

    plot_report(size, g_heap_size-g_memory_usage, g_heap_size-g_memory_usage,
            /*fragmentation_percent*/0, /*op_timep (usec)*/1, /*caused_oom*/0);
    return (void *)ptr;
}

void user_free(void *h) {
    unsigned long size = g_handles[(unsigned long)h];
    g_memory_usage -= size;
    plot_report(size*(-1), g_heap_size-g_memory_usage, g_heap_size-g_memory_usage,
            /*fragmentation_percent*/0, /*op_timep (usec)*/0,
            /*caused_oom*/0);
}

void user_lock(void *h) {
}

void user_unlock(void *h) {
}

bool user_init(unsigned int heap_size, char *name) {
    g_heap_size = heap_size;
    strcpy(name, ALLOC_NAME);
}
void user_destroy() {
}

bool user_handle_oom(int size) {
    return true;
}


