/* plot_dlmalloc.cpp
 *
 * uses dlmalloc from a user heap by overriding sbrk().
 */
#include "../compact/compact.h"
#include "dlmalloc-2.8.6.h"

#include "plot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <map>

#define ALLOC_NAME "dlmalloc"

static unsigned long g_heap_size;
static unsigned long g_memory_usage;
static unsigned long g_handle_counter = 0;

static void *g_heap = NULL;
static void *g_heap_end = NULL;
static void *g_heap_top = NULL; // g_heap <= end of heap < g_heap_top
static void *g_colormap = NULL; 
static uint32_t g_original_size = 0;

typedef std::map<void *, uint32_t> pointer_size_map_t;

static pointer_size_map_t g_handles, g_count;
static pointer_size_map_t g_handle_pointer;
int g_free = 0;
int g_malloc = 0;

void print_after_malloc_stats(void *handle, int address, int size) {
    //printf("after alloc size %d to address %p / handle %p\n", size, address, handle);
}

void print_after_free_stats(int address, int size) {
    //printf("after free %p of size %d\n", address, size);
}

void sanity() {
    pointer_size_map_t::iterator it;
    int nonzero = 0;
    for (it=g_count.begin(); it != g_count.end(); it++) {
        if (it->second != 0) {
            //printf("handle %d, ptr 0x%X is non-zero: %d\n", g_handle_pointer[it->first], (uint32_t)it->first, it->second);
            nonzero++;
        }
    }
    fprintf(stderr, "Total %d pointers, %d dangling, %d total free() calls, %d total malloc() calls.\n", g_count.size(), nonzero, g_free, g_malloc);
}


void *user_malloc(int size, uint32_t handle, uint32_t *op_time, void **memaddress) {
    g_memory_usage += size*0.9; // XXX: *0.9 is bogus, should be just size. for plot testing purposes!

    void *ptr = dlmalloc(size);
    if (ptr == NULL)
        return ptr;

    //printf("|| h == (void *)0x%X // MALLOC, heap start %x, heap end %x\n", (uint32_t)ptr, (uint32_t)g_heap, (uint32_t)g_heap_end);
    g_handles[ptr] = size;

    g_handle_pointer[ptr] = handle;

    if (g_count.find(ptr) == g_count.end())
        g_count[ptr] = 1;
    else {
        g_count[ptr] += 1;
        if (g_count[ptr] > 1)
            fprintf(stderr, "Double malloc for handle %d\n", handle); // FIXME: this test should be here.
    }

#ifdef DEBUG
    //sanity();
#endif
    
    g_malloc++;

    *op_time = 3;

    if (memaddress != NULL)
        *memaddress = (void *)((ptr_t)ptr + size);

    return ptr;
}

void user_free(void *ptr, uint32_t handle, uint32_t *op_time) {
    unsigned long size = g_handles[ptr];
    g_memory_usage -= size;

    g_count[ptr] -= 1;
    g_free++;

#ifdef DEBUG
    //sanity();
#endif

    dlfree(ptr);
}

void user_lock(void *h) {
}

void user_unlock(void *h) {
}

void user_destroy() {
    sanity();
}

bool user_handle_oom(int size) {
    return true;
}

void user_paint(void) {
    
}

bool user_init(uint32_t heap_size, void *heap, void *colormap, char *name) {
    strcpy(name, ALLOC_NAME);

    g_heap = heap;
    g_original_size = heap_size;
    g_heap_size = heap_size;
    g_heap_end = g_heap;
    g_heap_top = (uint8_t *)((ptr_t)g_heap_end + heap_size);
    g_colormap = colormap;
}

void user_reset(void) {
    char buffer[100];
    g_handles.clear();
    g_count.clear();
    user_init(g_original_size, g_heap, /*colormap, unused*/NULL, buffer);
}

void *user_highest_address(void) {
    return NULL;
}

/////////////////////////////////////////////////////////////////////////////////////////////

/* sbrk() for use with dlmalloc
 *
 * use this to return data from _our_ heap.
 */
#ifdef __cplusplus
extern "C" {
#endif
void *user_sbrk(int);
#ifdef __cplusplus
}
#endif


void *user_sbrk(int incr)
{
    void *prev_heap_end;

    prev_heap_end = g_heap_end;

    incr = (incr + 3) & ~3; // align to 4-byte boundary

    if ((ptr_t)g_heap_end + incr > (ptr_t)g_heap_top)
    {
        errno = ENOMEM;
        return (void*)-1;
    }

    g_heap_end = (void *)((ptr_t)g_heap_end + incr);

    return (caddr_t) prev_heap_end;
}
#if 0
void *user_sbrk(int increment) {
    if (increment == 0) {
        printf("user_sbrk(): requesting current heap end = 0x%X and start = 0x%X\n", (uint32_t)g_heap_end, (uint32_t)g_heap);
        return g_heap_end;
    }
    printf("user_sbrk(): requesting %d bytes.\n", increment);

    if ((uint32_t)g_heap_end + increment < (uint32_t)g_heap + g_heap_size) {
        g_heap_end = (void*)((uint32_t)g_heap_end + increment);

        printf("user_sbrk(): request OK, new heap size: %d, from 0x%X to 0x%X\n", (uint8_t *)g_heap_end-(uint8_t *)g_heap,
                (uint32_t)g_heap, (uint32_t)g_heap_end);
        return g_heap_end;
    }
    errno = ENOMEM;
    return (void *)-1;
}
#endif


