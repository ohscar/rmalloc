/* plot_tcmalloc.cpp
 *
 * uses tcmalloc from a user heap by overriding sbrk().
 */
#include "compact.h"

#include "plot.h"

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <map>

#define ALLOC_NAME "tcmalloc"


extern "C" {
extern void *tc_malloc(size_t);
extern void tc_free(void *);
}

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

    TIMER_DECL;

    TIMER_START;
    void *ptr = tc_malloc(size);
    TIMER_END;
    if (op_time)
        *op_time = TIMER_ELAPSED;
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

    if (memaddress != NULL)
        *memaddress = (void *)((ptr_t)ptr);

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

    TIMER_DECL;

    TIMER_START;
    tc_free(ptr);
    TIMER_END;
    if (op_time)
        *op_time = TIMER_ELAPSED;
}

void *user_lock(void *h) {
}

void user_unlock(void *h) {
}

void user_destroy() {
    sanity();
}

bool user_handle_oom(int size, uint32_t *op_time) {
    if (op_time) *op_time = 0;
    return true;
}

void user_paint(void) {
    
}

bool user_init(uint32_t heap_size, void *heap, char *name) {
    strcpy(name, ALLOC_NAME);

    g_heap = heap;
    g_original_size = heap_size;
    g_heap_size = heap_size;
    g_heap_end = g_heap;
    g_heap_top = (uint8_t *)((ptr_t)g_heap_end + heap_size);
    //g_colormap = colormap;
}

void user_reset(void) {
    char buffer[100];
    g_handles.clear();
    g_count.clear();
    user_init(g_original_size, g_heap, buffer);
}

void *user_highest_address(bool full_calculation) {
    return NULL;
}

bool user_has_heap_layout_changed() {
    return false;
}

uint32_t user_get_used_block_count() {
    return 0;
}

void user_get_used_blocks(ptr_t *blocks) {
}

/////////////////////////////////////////////////////////////////////////////////////////////

/* sbrk() for use with tcmalloc
 *
 * use this to return data from _our_ heap.
 */
#ifdef __cplusplus
extern "C" {
#endif

void *user_sbrk(int incr)
{
    static ptr_t end_of_heap = (ptr_t)g_heap;

    if (end_of_heap + incr < (ptr_t)g_heap + g_heap_size)
    {
        ptr_t prev = end_of_heap;
        end_of_heap += incr;
        return (void *)prev;
    }

    errno = ENOMEM;
    return (void *)-1;
}
#ifdef __cplusplus
}
#endif


