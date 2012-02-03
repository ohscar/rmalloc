#include "compact.h"
#include <stdio.h>
#include <stdlib.h>

/* header, see compact.h
 */

#define HEADER_UNUSED       0
#define HEADER_FREE_BLOCK   1
#define HEADER_UNLOCKED     2
#define HEADER_LOCKED       3
#define HEADER_WEAK_LOCKED  4

typedef struct {
    void *memory;
    int size;
    int flags;
} header_t;

/* free memory block, see compact.h
 */
typedef struct free_memory_block_t {
    header_t *header;
    struct free_memory_block_t *next; // null if no next block.
} free_memory_block_t;

/* memory layout
 */
void *g_memory_bottom;
void *g_memory_top;
uint32_t g_memory_size;

// top since the headers shrink in memory
#define HEADER_MAP_SIZE 64
header_t *g_header_top;
header_t *g_header_bottom;
uint8_t g_header_free_map[HEADER_MAP_SIZE];
int g_header_items_per_bit;
char g_header_bitmap_free_index[256];

// not sorted
free_memory_block_t *g_free_list_root;
free_memory_block_t *g_free_list_end;

/* header */

/* find first free header. which is always the *next* header.
 */
header_t *header_find_free() {
#if 0 // FUTURE WORK check header free bitmap for 0-bits.
    // optimization: unroll?
    int pos = -1;
    int byte = 0;
    for (int i=0; i<HEADER_MAP_SIZE; ++) {
        uint8_t freemap = g_header_free_map[i];
        if (freemap != 0xFF) {
            pos = g_header_bitmap_free_index[freemap];
            byte = i;
            break;
        }
    }
#endif
    header_t *h = g_header_top;
    while (h >= g_header_bottom) {
        if (h->flags == HEADER_UNUSED)
            return h;
        h--;
    }
    return NULL;
}

header_t *header_new() {
    header_t *header = header_find_free();
    if (!header) {
        g_header_bottom--;
        if ((void *)g_header_bottom <= g_memory_top)
            header = NULL;
        else
            header = g_header_bottom;
    }
    if (header) {
        header->flags = HEADER_UNLOCKED;
        header->memory = NULL;
    }
    return header;
}

header_t *header_set_unused(header_t *header) {
    header->flags = HEADER_UNUSED;
    header->memory = NULL;
    header->size = 0;

    return header;
}

/* memory block */

free_memory_block_t *block_from_header(header_t *header) {
    return (free_memory_block_t *)((uint8_t *)header->memory + header->size) - 1;
}

header_t *block_new(int size) {
    // minimum size for later use in free list: header pointer, next pointer
    if (size < 8)
        size = 8;

    if ((uint8_t *)g_memory_top+size >= (uint8_t *)g_header_bottom)
        return NULL;

    header_t *h = header_new();
    if (!h)
        return NULL;

    h->size = size;
    h->memory = g_memory_top;
    h->flags = HEADER_UNLOCKED;

    g_memory_top = (uint8_t *)g_memory_top + size;

    return h;
}

/* 1. mark the block's header as free
 * 2. insert block info
 * 3. extend the free list
 */
header_t *block_free(header_t *header) {
    if (!header || header->flags == HEADER_FREE_BLOCK)
        return header;

    bool in_free_list = false;

    // are there blocks before this one?
    free_memory_block_t *prevblock = (free_memory_block_t *)header->memory - 1;
    if (prevblock >= g_memory_bottom) {
        // is it a valid header?
        if (prevblock->header >= g_header_bottom && prevblock->header <= g_header_top && prevblock->header->flags == HEADER_FREE_BLOCK) {
            // does it point to the same block?
            if ((uint8_t *)prevblock->header->memory + prevblock->header->size == header->memory) {
                // yup, merge previous and this block
                prevblock->header->size += header->size;

                //printf("merging block headers %p and %p at memory %p and %p to new size %d\n", header, prevblock->header, header->memory, prevblock->header->memory, prevblock->header->size);

                // kill off the current block
                header_set_unused(header);

                // set new current block
                header = prevblock->header;

                // put the extended block info in place at the end
                free_memory_block_t *endblock = block_from_header(header);
                memcpy(endblock, prevblock, sizeof(free_memory_block_t));

                // it's already in the free list, no need to insert it again.
                in_free_list = true;
            }
        }
    }

#if 0 // TODO: Future work - needs a prev pointer!
    // are there blocks after this one?
    free_memory_block_t *nextblock = (free_memory_block_t *)((uint8_t *)header->memory+header->size) + 1;
    if (nextblock <= g_memory_top) {
        // is it a valid header?
        if (nextblock->header >= g_header_bottom && nextblock->header <= g_header_top && nextblock->header->flags == HEADER_FREE_BLOCK) {
            // does it point to the same block?
            if ((uint8_t *)nextblock->header->memory == nextblock) {
                // yup, merge this block and the next.
                header->size += nextblock->header->size;

                // kill off the next block
                header_set_unused(nextblock);

                // set new current block
                header = prevblock->header;

                // it's already in the free list, no need to insert it again.
                in_free_list = true;
            }
        }
    }
#endif

    // our work is done here
    if (in_free_list)
        return header;

    // alright, no previous or next block to merge with
    // update the free list
    free_memory_block_t *block = block_from_header(header);
    if (g_free_list_root == NULL) {
        g_free_list_root = block;
    }

    if (g_free_list_end == NULL) {
        g_free_list_end = g_free_list_root;
    } else {
        g_free_list_end->next = block;
        g_free_list_end = block;
    }

    // header's tracking a block in the free list
    header->flags = HEADER_FREE_BLOCK;

    block->header = header;
    block->next = NULL;

#if 0 // FUTURE WORK for forward merges
    // insert duplicate back-pointers for large blocks
    if (header->size >= 12)
        memcpy(header->memory, header, sizeof(header_t));
#endif


#if 0 // FUTURE WORK
    // mark header as free in 'free header' bitmap
#endif
    return header;
}

uint32_t stat_total_free_list() {
    uint32_t total = 0;
    free_memory_block_t *b = g_free_list_root;
    while (b != NULL) {
        total += b->header->size;
        b = b->next;
    }
    return total;
}

/* compacting */
void compact() {

    // TODO
}

/* client code */
void cinit(void *heap, uint32_t size) {
    g_memory_size = size;
    g_memory_bottom = heap;
    g_memory_top = g_memory_bottom;

    g_free_list_root = NULL;
    g_free_list_end = NULL;

    g_header_items_per_bit = 64;
    g_header_top = (header_t *)((uint32_t)heap + size);
    g_header_bottom = g_header_top;

    header_set_unused(g_header_top);

    memset(heap, 0, size);
}

void cdestroy() {
}

handle_t *cmalloc(int size) {
    header_t *h = block_new(size);

    return (handle_t *)h;
}

void cfree(handle_t *h) {
    block_free((header_t *)h);
}

void *clock(handle_t *h) {
    header_t *f = (header_t *)h;
    f->flags = HEADER_LOCKED;
}
void *cweaklock(handle_t *h) {
    header_t *f = (header_t *)h;
    f->flags = HEADER_WEAK_LOCKED;;
}

void cunlock(handle_t *h) {
    header_t *f = (header_t *)h;
    f->flags = HEADER_UNLOCKED;
}

