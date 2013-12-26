/* plot_cmalloc.cpp
 */
#include "compact/compact.h"

#include "plot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALLOC_NAME "cmalloc"

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
    memory_t *handle = rmmalloc(size);

    int free_bytes = g_heap_size - rmstat_total_free_list();
    int lab = rmstat_largest_block();

    plot_report(size, free_bytes, lab,
            /*fragmentation_percent*/0, /*op_timep (usec)*/1, /*caused_oom*/0);

    return (void *)handle;
}

void user_free(void *h) {
    memory_t *handle = (memory_t *)h;
    rmfree(handle);

    plot_report(size*(-1), g_heap_size-g_memory_usage, g_heap_size-g_memory_usage,
            /*fragmentation_percent*/0, /*op_timep (usec)*/0,
            /*caused_oom*/0);
}

void user_lock(void *h) {
    memory_t *handle = (memory_t *)h;
    rmlock(handle);
}

void user_unlock(void *h) {
    memory_t *handle = (memory_t *)h;
    rmunlock(handle);
}

bool user_init(unsigned int heap_size, char *name) {
    strcpy(name, ALLOC_NAME);

    g_heap_size = heap_size;
    rminit(malloc(heap_size), heap_size);
}
void user_destroy() {
    rmdestroy();
}

bool user_handle_oom(int size) {
    return true;
}

uint32_t user_get_used_block_count() {
    return 0;
}

void user_get_used_blocks(ptr_info_t *blocks) {
}

