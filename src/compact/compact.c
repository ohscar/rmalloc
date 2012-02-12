#include "compact.h"
#include "compact_internal.h"
#include <stdio.h>
#include <stdlib.h>

/* memory layout
 */
void *g_memory_bottom;
void *g_memory_top;
uint32_t g_memory_size;

/* linked list at each position
 * each stores 2^k - 2^(k+1) sized blocks
 */
free_memory_block_t **g_free_block_slots;
short g_free_block_slot_count; // log2(heap_size)

/* header */
// headers grow down in memory
header_t *g_header_top;
header_t *g_header_bottom;

header_t *g_free_header_root;
header_t *g_free_header_end; // always NULL as its last element.

// code

/* utility */
uint32_t log2_(uint32_t n)
{
    int r = 0;
    while (n >>= 1)
        r++;
    return r;
}

/* header */

bool header_is_unused(header_t *header) {
    return header && header->memory == NULL;
}

header_t *header_set_unused(header_t *header) {

    // 1. root == NULL => end == NULL; root = end = header;
    // 2. root == end => end->memory = header; end = header
    // 3. root != end => end->memory = header; end = header

    if (!g_free_header_root) {
        g_free_header_root = header;
        g_free_header_end = header;
    } else {
        g_free_header_end->memory = header;
        g_free_header_end = header;
    }

    header->memory = NULL;
    return header;
}
/* find first free header. which is always the *next* header.
 */
header_t *header_find_free() {
    header_t *h = g_header_top;
    while (h >= g_header_bottom) {
        if (header_is_unused(h))
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
        header->next = NULL;
    }
    return header;
}

/* free block list */

/* insert item at the appropriate location.
 * don't take into consideration that it can exist elsewhere
 */
void freeblock_insert(free_memory_block_t *block) {
    int k = log2_(block->header->size);
    free_memory_block_t *b = g_free_block_slots[k];
    g_free_block_slots[k] = block;
    if (b)
        block->next = b;
}

/* splits up a block by size.
 * returns rest iff rest-size >= sizeof(free_memory_block_t)
 */
free_memory_block *freeblock_shrink(free_memory_block_t *block, uint32_t size) {
    if (!block)
        return NULL;

    if (block->header->size - size < sizeof(free_memory_block_t))
        return NULL;

    header_t *h = header_new();
    if (!h)
        return NULL;

    h->memory = (uint8_t *)block->header->memory + size;
    h->size = block->header->size - size;
    block->header->size = size;

    free_memory_block_t *b = block_from_header(h);

    // note: block has no next pointer.
    b->header = h;
    return b;
}

/* look for a block of the appropriate size in the 2^k list.
 *
 * any block that are larger than the slot's size will be moved upon traversal!
 */
handle_t *freeblock_find(uint32_t size) {
    // there can be blocks of 2^k <= n < 2^(k+1)
    int target_k = log2_(size);
    int k = target_k;
   
    // any blocks of >= upper_size will be moved be de-linked and moved to the
    // appropriate slot
    int upper_size = 1<<(k+1);

    // does this slot have any free blocks?
    free_memory_block *block = g_free_block_slots[k];
    while (!block && k < g_free_block_slot_count) {
        // nope, move up to the next block
        k++;
        upper_size = 1<<(k+1);
        block = g_free_block_slots[k];
    }
    if (!block) 
        return NULL;

    // in case we don't find an apropriately sized lock, but do find a larger
    // block
    free_memory_block_t *fallback_block = NULL;
    free_memory_block_t *prevblock = block;

    // alright, we've found ourselves a list to search in.
    //
    // 1. k == target_k
    // 1a. large block => remove from list, move to correct location,
    // remember location, next block.
    // 1b. ok block => remove from list, return block
    //
    // 2. k > target_k
    // 2a. all blocks are large => remove from list, shrink, move shrinked,
    // return block
    if (k == target_k) {
        while (block != NULL) {
            // remove from list:
            // if it's too large, then remove it anyway
            // if it's oK, then remove it since it'll be used
            if (block == g_free_block_slots[k])
                g_free_block_slots[k] = block->next;
            else
                prevblock->next = block->next;

            if (block->header->size > upper_size) {
                freeblock_insert(block);
                fallback_block = block;
            } else {
                return block;
            }

            prevblock = block;
            block = block->next;
        }
        
        // rats, no block found? use the fallback block.
        int fallback_k = log2_(fallback_block->header->size);
        g_free_block_slots[fallback_k] = fallback_block->next;

        free_memory_block_t *rest = freeblock_shrink(fallback_block, size);
        if (rest)
            freeblock_insert(rest);

        return fallback_block;

    } else if (k > target_k) {
        // we know this is the first block
        g_free_block_slots[k] = block->next;
        free_memory_block_t *rest = freeblock_shrink(block, size);
        if (rest)
            freeblock_insert(rest);
        return block;
    }

    printf("this must never happen. i'm confused.\n");
    return NULL;
}

/* memory block */

free_memory_block_t *block_from_header(header_t *header) {
    return (free_memory_block_t *)((uint8_t *)header->memory + header->size) - 1;
}

header_t *block_new(int size) {
    // minimum size for later use in free list: header pointer, next pointer
    if (size < sizeof(free_memory_block_t))
        size = sizeof(free_memory_block_t);

    if ((uint8_t *)g_memory_top+size <= (uint8_t *)g_header_bottom) {
        header_t *h = header_new();
        if (!h)
            return NULL;

        // just grab off the top
        h->size = size;
        h->memory = g_memory_top;
        h->flags = HEADER_UNLOCKED;

        g_memory_top = (uint8_t *)g_memory_top + size;

        return h;
    } else {
        // nope. look through existing blocks
        handle_t *h = freeblock_find(size);
        // okay, we're *really* out of memory
        if (!h)
            return NULL;
        
        // if required
        freeblock_trim(h);
        
        return h;
    }
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
        if (prevblock->header >= g_header_bottom
            && prevblock->header <= g_header_top
            && prevblock->header->flags == HEADER_FREE_BLOCK) {

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

                // NOTE: do not insert it into the free slot list -- move it
                // to the right location at alloc, if needed.
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

    // header's tracking a block in the free list
    header->flags = HEADER_FREE_BLOCK;

    block->header = header;
    block->next = NULL;

    // insert into free size block list, at the start.
    int index = log2_(header->size);
    free_memory_block_t *current = g_free_block_slots[index];
    if (current) {
        g_free_block_slots[index] = block;
        block->next = current;
    } else
        g_free_block_slots[index] = block;

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
    for (int i=0; i<g_free_block_slot_count; i++) {
        free_memory_block_t *b = g_free_block_slots[i];
        while (b != NULL) {
            total += b->header->size;
            b = b->next;
        }
    }
    return total;
}

/* compacting */
void compact() {
}
#if 0
void compact() {
    /* the super-greedy find first block algorithm!
     *
     * since block_new() is silly, we want to move as much out of the way from
     * the end of our memory block space. let's do so!
     */

    // find the largest free block that also starts fairly early
    // cut-off point at 50%? make that a configurable variable to be testable
    // in benchmarks!
    free_memory_block_t *largest_block = g_free_list_root;
    free_memory_block_t *largest_block_prev = g_free_list_root;
    free_memory_block_t *block = g_free_list_root, *prev = NULL;
    void *lowfree = block->header->memory, *highfree = block->header->memory;
    float cutoff_ratio = 0.5;
    void *cutoff = NULL;

    // find the boundaries of the memory blocks
    while (block) {
        if (block->header->memory < lowfree)
            lowfree = block->header->memory;

        if (block->header->memory > highfree)
            highfree = block->header->memory;

        block = block->next;
    }

    cutoff = (void *)((uint32_t)lowfree + (uint32_t)(((uint8_t *)highfree-(uint8_t *)lowfree)*cutoff_ratio));

    block = g_free_list_root;
    prev = g_free_list_root;

    while (block) {
        if (block->header->size > largest_block->header->size && block->header->memory < cutoff) {
            largest_block_prev = prev;
            largest_block = block;
        } 

        prev = block;
        block = block->next;
    }

    // Panic. Can't happen, unless above is wrong. Which it isn't...?
    if (largest_block_prev->next != largest_block)
        fprintf(stderr, "******************* PREV->next != BLOCK!!! (%p -> %p)\n", largest_block_prev, largest_block);

    uint32_t size = (uint8_t *)highfree - (uint8_t *)lowfree;
    printf("free block range: lowest %p to highest %p (%d K) with cutoff %p\n", lowfree, highfree, size/1024, cutoff);
    printf("best suited free block at %p (%d kb from lowest) of size %d kb\n", largest_block->header->memory, ((uint32_t)largest_block->header->memory - (uint32_t)lowfree)/1024, largest_block->header->size/1024);

    // we have the largest free block.
    // memory grows up: look for highest addresses that will fit.
    header_t *h = g_header_top;
    header_t *highest = g_header_top;
    while (h != g_header_bottom) {
        if (h->flags == HEADER_UNLOCKED /* || h->flags == HEADER_WEAK_LOCKED */
            && h->memory > highest->memory && h->size <= largest_block->header->size) {
            // a winner!

            printf("larger header h %d, size %d kb, memory %p\n", h->flags, h->size/1024, h->memory);

            highest = h;
        } else
            printf("smaller header h %d, size %d kb, memory %p\n", h->flags, h->size/1024, h->memory);

        h--;
    }

    // 1. copy the used block to the free block.
    // 2. point the used block header to the free header's starting address
    // 3a. if free block minus used block larger than sizeof(free_memory_block)
    //     * adjust the free header's start adress and size
    // 3b. if free header less than sizeof(free_memory_block_t):
    //     * add that space to the used block (internal fragmentation)
    //     * mark free block as unused
    //     * point the free block's previous block's next to point to the free block's next block

    // 1. copy the used block to the free block.
    header_t *free_header = largest_block->header;
    free_memory_block_t *largest_block_next = largest_block->next;

    printf("moving block %p (size %d kb) to free block %p (size %d kb)\n", highest->memory, highest->size/1024, free_header->memory, free_header->size/1024);

    memcpy(free_header->memory, highest->memory, highest->size);

    // 2. point the used block header to the free header's starting address
    highest->memory = free_header->memory;
    
    // 3a. if free header larger than sizeof(free_memory_block)
    int diff = free_header->size - highest->size;
    if (diff >= sizeof(free_memory_block_t)) {
        free_header->memory = (void *)((uint32_t)free_header->memory + highest->size);
        free_header->size = diff;
    } else {
        // 3b. if free header less than sizeof(free_memory_block_t):
        highest->size = free_header->size;
        header_set_unused(free_header);

        largest_block_prev->next = largest_block_next;
    } 

}
#endif

/* client code */
void cinit(void *heap, uint32_t size) {
    g_memory_size = size;

    // +1 to round up. e.g. log2(15)==3
    // => 0, 1, 2, but later log2(13) would map to 3!
    // in practice, will there be such a large block?
    g_free_block_slot_count = log2_(size) + 1; 
    g_free_block_slots = (free_memory_block_t **)heap;
    memset((void *)g_free_block_slots, 0, sizeof(free_memory_block_t *)*g_free_block_slot_count);

    g_memory_bottom = (void *)((uint32_t)heap + (uint32_t)(g_free_block_slot_count * sizeof(free_memory_block_t *)));
    g_memory_top = g_memory_bottom;

    g_free_header_root = NULL;
    g_free_header_end = NULL;

    g_header_top = (header_t *)((uint32_t)heap + size);
    g_header_bottom = g_header_top;

    header_set_unused(g_header_top);

    memset(heap, 0, size);
}

void cdestroy() {
    // nop.
    return;
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

