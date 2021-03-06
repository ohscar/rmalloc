/* plot_dlmalloc.cpp
 *
 * uses dlmalloc from a user heap by overriding sbrk().
 */
#include "compact.h"
#include "compact_internal.h"
//#include "dlmalloc-2.8.6.h"

#include "plot.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <map>

#define ALLOC_NAME "rmmalloc"

static unsigned long g_heap_size;
static unsigned long g_memory_usage;
static unsigned long g_handle_counter = 0;

static void *g_heap = NULL;
static void *g_heap_end = NULL;
static void *g_heap_top = NULL; // g_heap <= end of heap < g_heap_top
static void *g_colormap = NULL; 
static uint32_t g_original_size = 0;

static bool g_has_compacted = false;

typedef std::map<handle_t, uint32_t> pointer_size_map_t;
typedef std::map<uint32_t, handle_t> handle_block_map_t;
typedef std::map<uint32_t, uint32_t> int_int_map_t;


static int_int_map_t g_count;
static pointer_size_map_t g_handles;
static pointer_size_map_t g_handle_pointer;
static handle_block_map_t g_handle_to_block;
int g_free_call_count = 0;
int g_malloc_call_count = 0;

void print_after_malloc_stats(void *handle, int address, int size) {
    //printf("after alloc size %d to address %p / handle %p\n", size, address, handle);
}

void print_after_free_stats(int address, int size) {
    //printf("after free %p of size %d\n", address, size);
}

void sanity() {
    int_int_map_t::iterator it;
    int nonzero = 0;
    for (it=g_count.begin(); it != g_count.end(); it++) {
        if (it->second != 0) {
            //printf("handle %d, ptr 0x%X is non-zero: %d\n", g_handle_pointer[it->first], (uint32_t)it->first, it->second);
            //printf("handle %d is non-zero\n", (uint32_t)it->first);
            nonzero++;
        }
    }
    fprintf(stderr, "Total %d pointers, %d dangling, %d total free() calls, %d total malloc() calls.\n", g_count.size(), nonzero, g_free_call_count, g_malloc_call_count);
}


static void full_compact(void)
{
#ifdef COMPACTING
    int COMPACT_TIME = 0;
    rmcompact(COMPACT_TIME);
    g_has_compacted = true;
#endif // COMPACTING
}




// FIXME: user_malloc() must return handle_t, not the pointer, since the pointer will be invalid after the first compact.  Take specific care of this in plot.cpp in the driver, because it uses the pointer address to figure out e.g. g_highest_address.  That'll be completely wrong, and it should instead query the driver.



void *user_malloc(int size, uint32_t handle, uint32_t *op_time, void **memaddress) {
    //printf("user_malloc(): size = %d, handle = %d\n", size, handle);
    TIMER_DECL;

    TIMER_START;
    handle_t block = rmmalloc(size);
    TIMER_END;
    if (op_time)
        *op_time = TIMER_ELAPSED;

    if (block == NULL)
    {
        return NULL;
    }

    if (memaddress != NULL)
        *memaddress = (void *)((ptr_t)(((header_t *)block)->memory));

    g_handle_to_block[handle] = block;
    g_count[handle] = 1;
    /*
    if (g_count.find(handle) == g_count.end())
        g_count[handle] = 1;
    else {
        g_count[handle] += 1;
        if (g_count[handle] > 1)
            fprintf(stderr, "Double malloc for handle %d\n", handle); // FIXME: this test should be here.
    }
    */
    //fprintf(stderr, "==> NEW %3d => 0x%X\n", handle, block);
    
    g_malloc_call_count++;

    return block;
}

void user_free(void *ptr, uint32_t handle, uint32_t *op_time) {
    handle_t block = g_handle_to_block[handle];
    if ((void *)block != ptr)
    {
        fprintf(stderr, "user_free(0x%X, %d): bad mapping: got 0x%X\n", (ptr_t)ptr, handle, (ptr_t)block);
    }

    //g_count[handle] -= 1;
    g_count[handle] = 0;

    //fprintf(stderr, "<== FRE %3d => 0x%X\n", handle, block);
    

    TIMER_DECL;

    TIMER_START;
    rmfree(block);
    TIMER_END;
    if (op_time)
        *op_time = TIMER_ELAPSED;

    // is compacting considered cheating?
    //full_compact();
    //

    g_free_call_count++;
}

void *user_lock(void *h) {
    rmlock(h);
}

void user_unlock(void *h) {
    rmunlock(h);
}

void user_destroy() {
    sanity();
}

bool user_handle_oom(int size, uint32_t *op_time) {
    fprintf(stderr, "user_handle_oom!\n");
    TIMER_DECL;

    TIMER_START;
    full_compact();
    TIMER_END;
    if (op_time)
        *op_time = TIMER_ELAPSED;

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

    rminit(heap, heap_size);
}

void user_reset(void) {
    char buffer[100];
    g_handles.clear();
    g_count.clear();
    user_init(g_original_size, g_heap, buffer);
}

void *user_highest_address(bool full_calculation) {

    return rmstat_highest_used_address(full_calculation);
    //return NULL;
}


bool user_has_heap_layout_changed() {
    return g_has_compacted;
}

uint32_t user_get_used_block_count() {
    //return rmstat_get_used_block_count();
    return 0;
}

void user_get_used_blocks(ptr_t *blocks) {
    //return rmstat_get_used_blocks(blocks);
}

