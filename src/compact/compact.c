#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>

#include "compact.h"
#include "compact_internal.h"

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
int g_free_block_hits = 0;
uint32_t g_free_block_alloc = 0;

/* header */
// headers grow down in memory
header_t *g_header_top;
header_t *g_header_bottom;
header_t *g_header_root; // linked list
int g_header_used_count; // for spare headers in compact
header_t *g_last_free_header = NULL;

header_t *g_unused_header_root = NULL;

static bool g_debugging = false;

#if __x86_64__
typedef uint64_t ptr_t;
#else
typedef uint32_t ptr_t;
#endif

#define fprintf(...) 
#define fputc(...)

// code

/* utility */
#if 0
uint32_t log2_(uint32_t n)
{
    int r = 0;
    while (n >>= 1)
        r++;
    return r;
}
#endif
// http://stackoverflow.com/questions/994593/how-to-do-an-integer-log2-in-c
// and http://gcc.gnu.org/onlinedocs/gcc-4.4.2/gcc/Other-Builtins.html
uint32_t log2_(uint32_t n)
{
    return __builtin_ctz(n);
}

/* header */

bool header_is_unused(header_t *header) {
    // FIXME: Make use of g_free_header_root / g_free_header_end -- linked
    // list. grab the first element, relink?
    //
    // if memory <= g_header_top && memory >= g_header_bottom, then it is also
    // unused.
    //
    return header && header->memory == NULL;
}

header_t *header_set_unused(header_t *header) {

    header->memory = NULL;

    if (g_unused_header_root == NULL)
    {
        g_unused_header_root = header;
    }
    else
    {
        header->next_unused = g_unused_header_root;
        g_unused_header_root = header;
    }

    return header;
}

void freeblock_verify_lower_size() {
    for (int k=0; k<g_free_block_slot_count; k++) {
        free_memory_block_t *b = g_free_block_slots[k];
        int size = 1<<k;
        while (b) {
            if (b->header->size < size || b->header->memory == NULL) {
                fprintf(stderr, "\nfreeblock_verify_lower_size(): block %p at mem %p at k=%d has size %d < %d\n",
                        b, b->header->memory, k, b->header->size, size);

                abort();
            }

            b = b->next;
        }
    }
}

/* find first free header. which is always the *next* header.
 */
#if 0
header_t *header_find_free() {
    // FIXME: make use of g_free_header_root! the first element is always the
    // one to use?
    //
    //
    //   g_header_bottom shrinks in header_new, but always such that there are always 1 spare for compacting.
    //
    //   Be a lot smarter about this - maybe by keeping a linked list around.
    //
    //
    //
    header_t *h = g_last_free_header - 1;
    while (h > g_header_bottom) {
        if (header_is_unused(h)) {
            g_last_free_header = h;
            return h;
        }
        h--;
    }

    if (h == g_header_bottom) {
        h = g_header_top;
        while (h >= g_last_free_header) {
            if (header_is_unused(h)) {

                //g_last_free_header = h; ??
                return h;
            }
            h--;
        }
    }
    return NULL;
}
#endif

header_t *header_find_free(bool spare_one_for_compact) {
    header_t *h = NULL;
    
    if (g_unused_header_root != NULL)
    {
        h = g_unused_header_root;
        g_unused_header_root = g_unused_header_root->next_unused;
    }
    else
    {
        int limit = g_header_used_count; // worst-case scenario (XXX: which? compacting?)
        if (spare_one_for_compact)
            limit = 1; // we're called by compact, and we only need to be able to create one extra header.

        if (g_header_bottom - limit > g_memory_top) {
            g_header_bottom--;

            h = g_header_bottom;
        }
    }

#ifdef DEBUG
    if (header_is_unused(h) == false)
        abort();
#endif

    return h;
}

header_t *header_new(bool insert_in_list, bool spare_one_for_compact) {
    header_t *header = header_find_free(spare_one_for_compact);
    if (header) {
        header->flags = HEADER_UNLOCKED;
        header->memory = NULL;
        if (insert_in_list) {
            // insert in list
            if (header->next == NULL && header != g_header_root) {
                header->next = g_header_root;

                g_header_root = header;
            } // else already in list.
        } else {
            // don't insert into list. this is the new_free thingy which we manually insert at the correct
            // position in the chain based on header_t::memory.
            header->next = NULL;
        }

    }
    fprintf(stderr, "== header_new() = %p\n", header);
    return header;
}

/* memory block */

free_memory_block_t *block_from_header(header_t *header) {
    /* free_memory_block_t for a chunk of free memory is stored at the very *end* of the block.
     *
     * this is done so that a recently freed block can be mixed together with
     * the block just behind the current one, if it is a valid free block.
     */
    return (free_memory_block_t *)((uint8_t *)header->memory + header->size) - 1;
}


uint8_t header_fillchar(header_t *h) {
    return (ptr_t)h & 0xFF;
}
static bool assert_memory_contents(header_t *h)
{
    uint8_t c = header_fillchar(h);
    uint8_t *memory = (uint8_t *)h->memory;

    for (int i=0; i<h->size; i++)
        if (memory[i] != c)
            return false;

    return true;
}


static void assert_blocks() {
    header_t *node = g_header_root;

    while (node != NULL) {
        if (node->flags != HEADER_FREE_BLOCK && assert_memory_contents(node) == false)
        {
            uint8_t c = header_fillchar(node);
            abort();
        }

        node = node->next;
    }
}



header_t *freeblock_find(uint32_t size);
header_t *block_new(int size) {
    // minimum size for later use in free list: header pointer, next pointer
    if (size < sizeof(free_memory_block_t))
        size = sizeof(free_memory_block_t);

#ifdef DEBUG
    freeblock_verify_lower_size();
    assert_blocks();
#endif
    header_t *h = NULL;
 
    // XXX: Is this really the proper fix?
    if ((uint8_t *)g_memory_top+size+sizeof(header_t) < (uint8_t *)g_header_bottom) {
    //if ((uint8_t *)g_memory_top+size < (uint8_t *)g_header_bottom) {
        h = header_new(true, false);
        if (!h) {
            fprintf(stderr, "header_new: oom.\n");
            return NULL;
        }

        if ((uint8_t *)g_memory_top+size >= (uint8_t *)g_header_bottom) {
            fprintf(stderr, "memory top (%p) + size (%d) >= %p\n", g_memory_top, size, g_header_bottom);
            abort();
        }

        // just grab off the top
        h->size = size;
        h->memory = g_memory_top;
        h->flags = HEADER_UNLOCKED;

        g_header_used_count++;
        g_memory_top = (uint8_t *)g_memory_top + size;
    } else {
        // nope. look through existing blocks
        h = freeblock_find(size);

        // okay, we're *really* out of memory
        if (!h) {
            fprintf(stderr, "freeblock_find: oom\n");
            return NULL;
        }
        g_header_used_count++;
        h->flags = HEADER_UNLOCKED;

        g_free_block_hits++;
        g_free_block_alloc += size;
    }

    return h;
}

void freeblock_print() {
    for (int i=0; i<g_free_block_slot_count; i++) {
        fprintf(stderr, "%d / %d bytes / %d kb: ", i, 1<<i, (1<<i)/1024);
        free_memory_block_t *b = g_free_block_slots[i];
        while (b) {
            fprintf(stderr, "{%p size %d kb} ", b, b->header->size/1024);
            b = b->next;
        }
        fprintf(stderr, "\n");
    }
}

bool freeblock_exists_memory(void *ptr) {
    for (int i=0; i<g_free_block_slot_count; i++) {
        free_memory_block_t *b = g_free_block_slots[i];
        while (b) {
            if (b->header->memory == ptr)
                return true;

            b = b->next;
        }
    }
    return false;
}

bool freeblock_exists(free_memory_block_t *block) {
    for (int i=0; i<g_free_block_slot_count; i++) {
        free_memory_block_t *b = g_free_block_slots[i];
        while (b) {
            if (b == block)
                return true;

            b = b->next;
        }
    }
    return false;
}

void assert_memory_is_free(void *ptr) {
    /*
     * assert that there are no non-free blocks in which this pointer address exists.
     *
     * free_memory_block_t is always placed at the end of a free memory chunk,
     * i.e. [free memory area of N bytes | free_memory_block_t]
    */
    header_t *h = g_header_root;
    ptr_t p = (ptr_t)ptr;
    while (h != NULL) {
        if (h->flags != HEADER_FREE_BLOCK) {
            ptr_t start = (ptr_t)h->memory;
            ptr_t end = start + h->size;

            if (p >= start && p < end)
                abort();
        }
        h = h->next;
    }
}

void freeblock_assert_sane(free_memory_block_t *block) {
    if (block != block_from_header(block->header)) {
        int diff;
        if ((uint8_t *)block > (uint8_t *)block_from_header(block->header))
            diff = (uint8_t *)block - (uint8_t *)block_from_header(block->header);
        else
            diff = (uint8_t *)block_from_header(block->header) - (uint8_t *)block;

        fprintf(stderr, "freeblock_assert_sane(%p size %d): diff %d bytes\n", block, block->header->size, diff);
        abort();
    }
}

bool freeblock_checkloop(free_memory_block_t *block) {
    free_memory_block_t *a = block;
    while (block != NULL) {
        block = block->next;
        if (block == a) {
            fprintf(stderr, "loop in memory block %p slot %d\n", block, log2_(block->header->size));
            abort();
            return true;
        }
    }
    return false;
}

/* 1. mark the block's header as free
 * 2. insert block info
 * 3. extend the free list
 */
header_t *block_free(header_t *header) {
    if (!header || header->flags == HEADER_FREE_BLOCK)
        return header;

#ifdef DEBUG
    freeblock_verify_lower_size();
    //assert_blocks();
#endif

    bool in_free_list = false;

    // FIXME: merge with previous block places blocks in a too small slot.

    // TODO: merge cannot work, period, since the block is already in the free list
    // and thus has the incorrect address.
#if 0
    // are there blocks before this one?
    free_memory_block_t *prevblock = (free_memory_block_t *)header->memory - 1;
    if (prevblock >= g_memory_bottom) {
        // is it a valid header?
        if (prevblock->header >= g_header_bottom
            && prevblock->header <= g_header_top
            && prevblock->header->flags == HEADER_FREE_BLOCK) {

            // does it point to the same block?
            if ((uint8_t *)prevblock->header->memory + prevblock->header->size == header->memory) {
                //fprintf(stderr, "merging previous block %p with block %p\n", prevblock, block_from_header(header));

                fprintf(stderr, "\nmerging block headers %p (%d bytes) and %p (%d bytes)  to new size %d, block %p\n", header, header->size, prevblock->header, prevblock->header->size, prevblock->header->size+header->size, block_from_header(prevblock->header));

                // yup, merge previous and this block
                prevblock->header->size += header->size;

                // kill off the current block
                header_set_unused(header);

                // set new current block
                header = prevblock->header;

                // put the extended block info in place at the end
                free_memory_block_t *endblock = block_from_header(header);
                endblock->header = header;
                endblock->next = prevblock->next;
                //memcpy(endblock, prevblock, sizeof(free_memory_block_t));

                freeblock_assert_sane(endblock);

                // it's already in the free list, no need to insert it again.
                in_free_list = true;

                // NOTE: do not insert it into the free slot list -- move it
                // to the right location at alloc, if needed.
            }
        }
    }
#endif

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

    // alright, no previous or next block to merge with.
    // update the free list
    free_memory_block_t *block = block_from_header(header);
    //fprintf(stderr, "block from header in free(): %p\n", block);

    // header's tracking a block in the free list
    header->flags = HEADER_FREE_BLOCK;

    // FIXME: block->header = header won't do?
    /* crash at the line below.
     *
     * output from gdb:
     * (gdb) print block
     * $8 = (free_memory_block_t *) 0x90909088
     *
     * which is just the *contents* of block. That's wrong. Block should point
     * to the address!
     *
     * also, header is wrong to begin with:
     *
     * (gdb) print header
     * $12 = (header_t *) 0xaecf0498
     * (gdb) print header->memory
     * $13 = (void *) 0x48484848
     * (gdb) print header->size
     * $14 = 1212696648
     * (gdb) 
     * (gdb) print g_header_bottom
     * $16 = (header_t *) 0xaecf0498 <-- same as header
     * (gdb) print g_memory_top
     * $17 = (void *) 0xaecf0179
     */

    // FIXME: I want to change the values of 'block'. should not that be
    // block->foo = 42?  or do I have to memcpy()
    //
    // after a block is freed and the free_memory_block_t is written 
    free_memory_block_t b;
    b.header = header;
    b.next = NULL;
    memcpy((void *)block, (void *)&b, sizeof(free_memory_block_t));

    ///block->header = header;
    //block->next = NULL;

    if (block->header->size + (uint8_t *)block->header->memory >= (void *)g_header_bottom)
        abort();


    //fprintf(stderr, "block_free(): block = %p, block->header = %p (header = %p) size %d memory %p\n", block, block->header, header, block->header->size, block->header->memory);

    // insert into free size block list, at the start.
    int index = log2_(header->size);

    if (block->header->size != header->size)
        abort();

    block->next = g_free_block_slots[index];
    g_free_block_slots[index] = block;

    /*
    free_memory_block_t *current = g_free_block_slots[index];
    //if (g_free_block_slots[index] && g_free_block_slots[index]->next) fprintf(stderr, "block_free(), current = %p g_free_block_slots[%d] = %p next = %p block = %p\n", current, index, g_free_block_slots[index], g_free_block_slots[index]->next, block);
    //else fprintf(stderr, "block_free(), current = %p g_free_block_slots[%d] = %p block = %p\n", current, index, g_free_block_slots[index], block);

    g_free_block_slots[index] = block;
    //fprintf(stderr, "block_free(), current = %p g_free_block_slots[%d] = %p block = %p\n", current, index, g_free_block_slots[index], block);
    g_free_block_slots[index]->next = current;
    //fprintf(stderr, "block_free(), current = %p g_free_block_slots[%d] = %p next = %p block = %p\n", current, index, g_free_block_slots[index], g_free_block_slots[index]->next, block);
    */


    // FIXME: crashes here. at some point, this block is overwritten, i.e.
    // it's been used even though it's freed. Try disabling cfree()'s prev block merge?

    //freeblock_checkloop(g_free_block_slots[index]);

#ifdef DEBUG
    freeblock_checkloop(block);
    //assert_blocks();
#endif

#if 0 // FUTURE WORK for forward merges
    // insert duplicate back-pointers for large blocks
    if (header->size >= 12)
        memcpy(header->memory, header, sizeof(header_t));
#endif


#if 0 // FUTURE WORK
    // mark header as free in 'free header' bitmap
#endif
    g_header_used_count--;
    return header;
}

/* free block list */

/* insert item at the appropriate location.
 * don't take into consideration that it can exist elsewhere
 */
void freeblock_insert(free_memory_block_t *block) {

    if (block->header->size + (uint8_t *)block->header->memory >= (void *)g_header_bottom)
        abort();

    int k = log2_(block->header->size);

    /*
    free_memory_block_t *b = g_free_block_slots[k];
    g_free_block_slots[k] = block;
    g_free_block_slots[k]->next = b;
    */
    block->next = g_free_block_slots[k];
    g_free_block_slots[k] = block;

#ifdef DEBUG
    freeblock_verify_lower_size();
#endif

#if 0
    if (b) {
        g_free_block_slots[k]->next = b;
        //fprintf(stderr, "inserting block %p at slot %d before block %p (size %d kb), slot[k] = %p next = %p\n", block, k, b, b->header->size/1024, g_free_block_slots[k], g_free_block_slots[k]->next);
    } else {
        //fprintf(stderr, "inserting block %p at slot %d, slot[k] = %p next = %p\n", block, k, g_free_block_slots[k], g_free_block_slots[k]->next);
        g_free_block_slots[k]->next = NULL;
    }
#endif
}

/* splits up a block by size.
 * returns rest iff rest-size >= sizeof(free_memory_block_t)
 * input block is always the block that's going to be used in client code
 *
 * input:  [                        block]
 * output: [     rest|              block]
 */
free_memory_block_t *freeblock_shrink_with_header(free_memory_block_t *block, header_t *h, uint32_t size) {
    if (!block)
        return NULL;

    int diff = block->header->size - size;
    if (diff < sizeof(free_memory_block_t)) {
        fprintf(stderr, "    1. freeblockshrink withheader block->header->size %d - size %d = diff = %d\n",
                block->header->size, size, diff);
        return NULL;
    }

    if (!h) {
        h = header_new(true, false);
        if (h == NULL) {
            fprintf(stderr, "    2. couldn't allocate new header.\n");
            return NULL;
        }
    }

    if (size > block->header->size)
        abort();

    if (h == block->header)
        fprintf(stderr, "ERROR: freeblock_shrink, new header %p same as block header %p\n", h, block->header);

#ifdef DEBUG
    freeblock_assert_sane(block);
    //assert_blocks();
#endif


    fprintf(stderr, "freeblockshrink: address of block->memory = %p with size = %d, address of block = %p == %p (or error!)\n", block->header->memory, block->header->size, block, (uint8_t *)block->header->memory + block->header->size - sizeof(free_memory_block_t));

    h->flags = HEADER_FREE_BLOCK;
    h->memory = block->header->memory;
    h->size = diff;

    block->header->memory = (uint8_t *)block->header->memory + diff;
    block->header->size = size;

    //fprintf(stderr, "freeblock_shrink, h memory %p size %d block h memory %p size %p\n", h->memory, h->size, block->header->memory, block->header->size);

    free_memory_block_t *b = block_from_header(h);
    b->next = NULL; 
    b->header = h;

    fprintf(stderr, "    3. freeblockshrink withheader h: %p  %d  %p  %d\n", h, h->size, h->memory, h->flags);

    fprintf(stderr, "    4. freeblockshrink withheader block %p header %p size %d\n", block, block->header, block->header->size);

    if (b == block)
        fprintf(stderr, "ERROR: freeblock_shrink, new block %p (memory %p size %d) old block %p (memory %p size %d)\n",
                b, b->header->memory, b->header->size,
                block, block->header->memory, block->header->size);

    if (block->header->size != size) {
        fprintf(stderr, "ERROR: freeblock_shrink, new block's header %p (h = %p) size %d not new size %d\n", b->header, h, b->header->size, size);
        abort();
    }

    return b;
}
free_memory_block_t *freeblock_shrink(free_memory_block_t *block, uint32_t size) {
    return freeblock_shrink_with_header(block, NULL, size);
}


/* look for a block of the appropriate size in the 2^k list.
 *
 * any block that are larger than the slot's size will be moved upon traversal!
 */
header_t *freeblock_find(uint32_t size) {
    // there can be blocks of 2^k <= n < 2^(k+1)
    int target_k = log2_(size)+1;
    int k = target_k;
   
    // any blocks of >= upper_size will be moved be de-linked and moved to the
    // appropriate slot
    int upper_size = 1<<(k+1);

    free_memory_block_t *block = NULL;
    free_memory_block_t *found_block = NULL;
    free_memory_block_t *fallback_block = NULL;

#ifdef DEBUG
    freeblock_verify_lower_size();
#endif

    // slot too large, need to do a full scan.
    if (k == g_free_block_slot_count) {
        k--;
        block = g_free_block_slots[k];
        free_memory_block_t *prevblock = block, *nextblock = block;
        while (block) {
            if (block->header->size >= size) {
                // found the block. remove it from the list.
                if (block == g_free_block_slots[k])
                    g_free_block_slots[k] = block->next;
                else
                    prevblock->next = block->next;

                found_block = block;
                break; 
            }

            prevblock = block;
            block = block->next;
        }
    } else {
        // k is within the bounds of the slots

        // does this slot have any free blocks?
        block = g_free_block_slots[k];
        //fprintf(stderr, "freeblock_find() block at %d = %p (at pos %p)\n", k, block, &block);
        while (!block && k < g_free_block_slot_count) {
            // nope, move up to the next block
            k++;
            if (k < g_free_block_slot_count) {
                upper_size = 1<<(k+1);
                block = g_free_block_slots[k];
                //fprintf(stderr, "freeblock_find() block at %d = %p (at pos %p)\n", k, block, &block);
            } 
        }

        if (block) {
            // yeah, there's a block here. it's also guaranteed to fit.
            
            // remove the item from list.
            block = g_free_block_slots[k];
            g_free_block_slots[k] = block->next;

            //fprintf(stderr,"*1. %p -> %p (%c)?\n", block, block->header, (block->header&0x000000FF));

            if (block->header->size > upper_size) {
                // current next block. when moved, the next block will point to something else.
                free_memory_block_t *nextblock = block->next;
                fallback_block = block;

                freeblock_insert(block);

                block = nextblock;
            } else {
                if (block->header->size < size) {
                    fprintf(stderr, "block %p too small (%d vs %d) in slot %d vs actual k = %d\n",
                            block, block->header->size, size, k, log2_(size));
                    abort();
                }

                found_block = block;
            }
        } else {
            // didn't find anything. do a full scan of the actual sized-k.
            k = log2_(size);
            upper_size = 1<<(k+1);

            fprintf(stderr, "freeblock_find(%d) scanning in %d\n", size, k);

            block = g_free_block_slots[k];
            free_memory_block_t *prevblock = block, *nextblock = block;
            while (block) {
                // there's a block here. it's also guaranteed to fit.

                //fprintf(stderr,"*2. %p -> %p (%c) size %d (%s)?\n", block, block->header, (uint32_t)block->header&0x000000FF, block->header->size, block->header->size >= size ? "yes" : "no");

#if 1 // does not work, freeblock_insert()
                if (block->header->size >= size) {
                    fprintf(stderr, "freeblock_find: block->header->size %d >= (requested) size %d\n", block->header->size, size);
                    // remove from the root? (easier)
                    if (g_free_block_slots[k] == block) {
                        // figure out what to do with the block
                        if (block->header->size > upper_size) {
                            fprintf(stderr, "-> root, too large, moving %p, next = %p.\n", block, block->next);
                            fallback_block = block;
                            g_free_block_slots[k] = block->next;

                            freeblock_insert(block);

                            block = g_free_block_slots[k];
                            fprintf(stderr, "->-> block = %p\n", block);
                        } else {
                            // found it!
                            fprintf(stderr, "-> root, correct size.\n");
                            found_block = block;
                            g_free_block_slots[k] = block->next;
                            break;
                        }
                    } else {
                        // not at root. (trickier)

                        // figure out what to do with the block
                        if (block->header->size > upper_size) {
                            fprintf(stderr, "-> not root, too large, moving %p, next = %p.\n", block, block->next);
                            fallback_block = block;

                            nextblock = block->next;
                            prevblock->next = nextblock;

                            freeblock_insert(block);

                            block = nextblock;
                            fprintf(stderr, "->-> block = %p\n", block);

                            if (prevblock->next != block) abort();
                        } else {
                            // found it!
                            fprintf(stderr, "-> not root, correct size.\n");
                            prevblock->next = block->next;
                            found_block = block;
                            break;
                        }
                    } 
                } else {
                    prevblock = block;
                    block = block->next;
                }
#endif
            }
        }
    }

    if (found_block) {
        // resize & insert
        fprintf(stderr, "-> shrinking found_block (header %p size %d) to new size %d\n",
                found_block->header, found_block->header->size, size);
        free_memory_block_t *rest = NULL;
        rest = freeblock_shrink(found_block, size);
        fprintf(stderr, "-> after shrinkage, found_block size %d, returning header %p\n", found_block->header->size, found_block->header);
        if (rest)
            freeblock_insert(rest);

        return found_block->header;
    } else if (fallback_block) {
        fprintf(stderr, "fallback block of header %p.\n", fallback_block->header);
        // rats, no block found.
        int fallback_k = log2_(fallback_block->header->size);

        // since we just inserted the fallback block, we know that it's
        // placed at the very start of this slot.  remove it by relinking the
        // slot, and use it.
        g_free_block_slots[fallback_k] = fallback_block->next;

        //fprintf(stderr, "No go, using fallback block %p of %d kb\n", fallback_block, fallback_block->header->size/1024);
        free_memory_block_t *rest = NULL;
        rest = freeblock_shrink(fallback_block, size);
        if (rest)
            freeblock_insert(rest);

        return fallback_block->header;
    }
    

    fprintf(stderr, "freeblock_find(): no block found.\n");
    // no block found.
    return NULL;
}


uint32_t rmstat_total_free_list() {
    uint32_t total = 0;
    for (int i=0; i<g_free_block_slot_count; i++) {
        free_memory_block_t *b = g_free_block_slots[i];
        free_memory_block_t *a = b;
        while (b != NULL) {
            total += b->header->size;
            b = b->next;
            if (a == b) {
                fprintf(stderr, "stat_total_free_list(), panic - found a loop in slot %d item %p!\n", i, a);
                freeblock_print();
            }
        }
    }
    return total;
}

uint32_t rmstat_largest_free_block() {
    uint32_t largest = 0;
    for (int i=0; i<g_free_block_slot_count; i++) {
        free_memory_block_t *b = g_free_block_slots[i];
        free_memory_block_t *a = b;
        while (b != NULL) {
            if (b->header->size > largest)
                largest = b->header->size;
            b = b->next;
            if (a == b) {
                fprintf(stderr, "stat_total_free_list(), panic - found a loop in slot %d item %p!\n", i, a);
                freeblock_print();
            }
        }
    }
    return largest;
}

void *rmstat_highest_used_address() {
    void *highest = NULL;
    
    header_t *h = g_header_root;

    //printf("Highest: ");
    while (h != NULL) {
        if (h->flags != HEADER_FREE_BLOCK) {
            //printf("*%p ", h->memory);
            if (h->size + (uint8_t *)h->memory > highest)
                highest = h->size + (uint8_t *)h->memory;
        } else
            ;//printf("%p ", h->memory);
        h = h->next;
    }
    //printf("\n");

    return highest;
}

void header_sort_all() {
    fprintf(stderr, "g_header_root before header_sort_all(): %p\n", g_header_root);
    //g_header_root = header__sort(g_header_root, header__cmp);
    //header_t *header__sort(header_t *list, int is_circular, int is_double, compare_cb cmp) {
    g_header_root = header__sort(g_header_root, 0, 0, header__cmp);
}

static uint32_t get_block_count(uint32_t *free_count, uint32_t *locked_count, uint32_t *unlocked_count, uint32_t *weaklocked_count)
{
    header_t *node = g_header_root;
    uint32_t count = 0;

    while (node != NULL) {
        if (node->flags == HEADER_FREE_BLOCK && free_count != NULL)
            *free_count += 1;
        else if (node->flags == HEADER_LOCKED && locked_count != NULL)
            *locked_count += 1;
        else if (node->flags == HEADER_UNLOCKED && unlocked_count != NULL) {
            *unlocked_count += 1;
            count++;
        }
        else if (node->flags == HEADER_WEAK_LOCKED && weaklocked_count != NULL)
            *weaklocked_count += 1;

        node = node->next;
    }
    return count;
}

static uint32_t /*size*/ get_free_header_range(header_t *start, header_t **first, header_t **last)
{
    header_t *h = start;
    
    // Find first free block.

    while (start != NULL && start->flags != HEADER_FREE_BLOCK)
    {
        start = start->next;
    }

    if (start == NULL) {
        *first = NULL;
        *last = NULL;
        
        return 0;
    }

    *first = start;

    uint32_t size = 0;
    while (start != NULL && start->flags == HEADER_FREE_BLOCK)
    {
        *last = start;
        size += start->size;

        start = start->next;
    }

    return size;
}

static uint32_t /*size*/ get_unlocked_header_range(header_t *start, header_t **first, header_t **last)
{
    header_t *h = start;
    
    // Find first unlocked block.

    while (start != NULL && start->flags != HEADER_UNLOCKED)
    {
        start = start->next;
    }

    if (start == NULL) {
        *first = NULL;
        *last = NULL;
        
        return 0;
    }

    *first = start;

    uint32_t size = 0;
    while (start != NULL && start->flags == HEADER_UNLOCKED)
    {
        *last = start;
        size += start->size;

        start = start->next;
    }

    return size;
}

static uint32_t /*size*/ header_memory_offset(header_t *first, header_t *last)
{
    ptr_t f = (ptr_t)first->memory;
    ptr_t l = (ptr_t)last->memory;

    if (f > l)
        return 0;
   
    return l - f;
}

static header_t *find_last_nonfree_header(header_t *root)
{
    header_t *last_nonfree = NULL;
    while (root != NULL && root->flags != HEADER_FREE_BLOCK)
    {
        last_nonfree = root;
        root = root->next;
    }

    if (last_nonfree && last_nonfree->flags == HEADER_FREE_BLOCK)
        last_nonfree = NULL;

    return last_nonfree;
}

static void rebuild_free_block_slots() {

    // rebuild free list
    memset((void *)g_free_block_slots, 0, sizeof(free_memory_block_t *) * g_free_block_slot_count);

    uint32_t steps = 0;

    uint32_t free_block_count = 0;
    header_t *h = g_header_root;
    while (h != NULL) {
        if (!h->memory || h->flags != HEADER_FREE_BLOCK) {
            h = h->next;
            steps++;
            continue;
        }
        steps++;
        int k = log2_(h->size);

        if (k == 11 && h == (void *)0xb7cad928)
        {
            k = 11;
        }

        // just let the smaller headers be, in case there are any.
        // there should not be any, and so this test is invalid.
        if (h->size >= sizeof(free_memory_block_t)) {

            free_block_count++;

            free_memory_block_t *block = block_from_header(h);

            if (block == (free_memory_block_t *)0x77d0eb0c)
            {
                block = (free_memory_block_t *)0x77d0eb0c;
            }

            block->header = h; 
            block->next = NULL;

#ifdef DEBUG
            // this should _always_ point to (h->memory+h->size - sizeof(free_block_memory_t))
            assert_memory_is_free((void *)block);
#endif

            // crash on g_free_block_slots[k]->next->header->size, because ->next->header == 0
            if (g_free_block_slots[k] == NULL)
                g_free_block_slots[k] = block;
            else {
                if (block == g_free_block_slots[k])
                {
                    block = g_free_block_slots[k];
                }


                block->next = g_free_block_slots[k];
                g_free_block_slots[k] = block;
            }
        }

        h = h->next;
    }

#ifdef DEBUG
    assert_blocks();
#endif
}

void rmcompact(int maxtime) {

    // sort headers in ascending memory order. headers with ->memory == NULL are in the end.
    header_sort_all();

#ifdef DEBUG
    uint32_t start_free = 0, start_locked = 0, start_unlocked = 0;
    get_block_count(&start_free, &start_locked, &start_unlocked, NULL);
#endif

    if (g_debugging)
    {
        rmstat_print_headers(false);
    }

    header_t *root = g_header_root;

    struct timespec start_time, now;
    int time_diff;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    bool done = false;
    while (!done) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        time_diff = now.tv_sec*1000 + now.tv_nsec/1000000 - start_time.tv_sec*1000 - start_time.tv_nsec/1000000;

        if (maxtime && time_diff >= maxtime) {
            done = true;
            break;
        } 
        
        // only run once!
        //if (root != g_header_root) break;

        ///////////////////////////////////////////////////////////////////////////////////////////

        // Find first free
        header_t *root_last_nonfree = find_last_nonfree_header(root);

        // Find ranges of free and unlocked blocks

        header_t *free_first, *free_last;
        uint32_t free_size = get_free_header_range(root, &free_first, &free_last);
        if (free_size == 0)
        {
            break;
        }

        header_t *start = free_last->next;
        header_t *unlocked_first, *unlocked_last;
        uint32_t unlocked_size = get_unlocked_header_range(start, &unlocked_first, &unlocked_last);
        if (unlocked_size == 0)
        {
            break;
        }

        // Move unlocked blocks, squish free blocks.
         
        uint32_t used_offset = header_memory_offset(free_first, unlocked_first);
        if (used_offset == 0)
        {
#ifdef DEBUG
            abort();
#endif
            break;
        }

        // Move used blocks

        header_t *h = unlocked_first;
        while (h != NULL && h != unlocked_last->next)
        {
            ptr_t src = (ptr_t)h->memory;
            ptr_t dest = src - used_offset;
            h->memory = (void *)dest;

            memmove((void *)dest, (void *)src, h->size);
            h = h->next;
        }

        // Squish free blocks

        ptr_t free_memory_start = (ptr_t)free_first->memory;

        h = free_first;
        while (h && h != free_first->next)
        {
            header_set_unused(h);

            h = h->next;
        }

        header_t *free_memory = header_new(/*insert_in_list*/false, true);
        free_memory->flags = HEADER_FREE_BLOCK;
        free_memory->memory = (void *)(free_memory_start + unlocked_size);
        free_memory->size = free_size;
        memset(free_memory->memory, 0, free_size);

        if (free_memory->memory == (void *)0x77d0e0d8 && free_size == 2620)
        {
            free_memory->size = free_size;
        }

        if (free_memory == (void *)0xb7cad928)
        {
            free_memory->size = free_size;
        }

        // Place free blocks last

        header_t *unlocked_last_next = unlocked_last->next;
        unlocked_last->next = free_memory;
        free_memory->next = unlocked_last_next;

        // Next round

        if (root_last_nonfree == NULL)
        { // first block after root is free. this means we've changed it.

            // FIXME: g_header_root _or_ previous->next.
            g_header_root = unlocked_first;
            
        }
        else
            root_last_nonfree->next = unlocked_first;

        root = unlocked_last;
    }

    if (g_debugging)
    {
        rmstat_print_headers(false);
    }

    rebuild_free_block_slots();

#ifdef DEBUG
    uint32_t end_free = 0, end_locked = 0, end_unlocked = 0;
    get_block_count(&end_free, &end_locked, &end_unlocked, NULL);

    if (start_unlocked != end_unlocked) {
        printf("unlocked headers: start %d != end %d\n", start_unlocked, end_unlocked);
        abort();
    }
#endif
}


void rmstat_set_debugging(bool enable)
{
    g_debugging = enable;
}

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define WITH_ITER(h, init, body...) {header_t *h = init; while (h != NULL) {body; h = h->next;}}

void rmstat_print_headers(bool only_type)
{
    // decide smallest block size for printing
    uint32_t smallest=1<<31, largest=0, total=0;
    WITH_ITER(h, g_header_root,
        //if (h->flags == HEADER_FREE_BLOCK) {
            if (h->size < smallest)
                smallest = h->size;
            if (h->size > largest)
                largest = h->size;
            total += h->size;
        //}
    )
    printf("\n\n\n\n==========================================================================\n");

    int total_header_size = 0;
    // print map
    WITH_ITER(h, g_header_root, 
        //int count = MAX(1024, h->size/smallest);
        int count = MAX(1, h->size/smallest);
        for (int i=0; i<count; i++) {
            if (i==0) {
                if (only_type == false) {
                    putchar(h->flags == HEADER_FREE_BLOCK ? '_' : 'O');
                    printf("(%p)(%x)(%p)", h, *(uint8_t *)h->memory, h->memory);
                } else {
                    putchar('.');
                    if (h->flags == HEADER_FREE_BLOCK)
                        putchar('_');
                    else if (h->flags == HEADER_UNLOCKED)
                        putchar('|');
                    else if (h->flags == HEADER_LOCKED)
                        putchar('X');
                }
            }
            else if (h->flags == HEADER_FREE_BLOCK)
                putchar('_');
            else if (h->flags == HEADER_UNLOCKED)
                putchar('|');
            else if (h->flags == HEADER_LOCKED)
                putchar('X');
            
        }
        if (header_is_unused(h))
            total_header_size += h->size;
    ) putchar('\n');
}


/* compacting */
#if 0
void rmcompact(int maxtime) {
#ifdef DEBUG
    uint32_t start_free = 0, start_locked = 0, start_unlocked = 0;
    uint32_t end_free = 0, end_locked = 0, end_unlocked = 0;
    uint32_t start_count = 0, end_count = 0;
    get_block_count(&start_free, &start_locked, &start_unlocked, NULL);

    {
    header_t *h2 = g_header_root;
    start_count = 0;
    while (h2 != NULL) {
        if (h2->flags == HEADER_UNLOCKED && h2->memory != NULL)
            start_count++;
        h2 = h2->next;
    }
    }

    fprintf(stderr, "******************** compact started (g_header_root = %p).\n", g_header_root);

    // print map
    {header_t *h = g_header_root;
    while (h != NULL) {
        int count = MAX(1, h->size/1024);
        for (int i=0; i<count; i++) {
            if (i==0)
                {fputc(h->flags == HEADER_FREE_BLOCK ? 'o' : 'O', stderr);fprintf(stderr, "(%p)(%c)(%p)", h, *(uint8_t *)h->memory, h->memory);}
            else
            if (h->flags == HEADER_FREE_BLOCK)
                fputc('_', stderr);
            else if (h->flags == HEADER_UNLOCKED)
                fputc('|', stderr);
            else if (h->flags == HEADER_LOCKED)
                fputc('X', stderr);
        }

        h = h->next;
    }}
    fputc('\n', stderr);
#endif


#if 0 // FUTURE WORK: prune adjacent free block.
    header_sort_all();

    fprintf(stderr, "******************** sorted.\n");
    //
    // print map
    WITH_ITER(h, g_header_root, 
        int count = MAX(1, h->size/1024);
        for (int i=0; i<count; i++) {
            if (i==0)
                {fputc(h->flags == HEADER_FREE_BLOCK ? 'o' : 'O', stderr);fprintf(stderr, "(%p)(%c)(%p)", h, *(uint8_t *)h->memory, h->memory);}
            else
            if (h->flags == HEADER_FREE_BLOCK)
                fputc('_', stderr);
            else if (h->flags == HEADER_UNLOCKED)
                fputc('|', stderr);
            else if (h->flags == HEADER_LOCKED)
                fputc('X', stderr);
        }
    ) fputc('\n', stderr);

    fprintf(stderr, "******************** pruning.\n");

    // prune adjacent free blocks directly. Should be handled in
    // header_sort_all(), but it does not work. (FUTURE WORK) Unknown why.
    {header_t *h = g_header_root, *next = h->next;
    while (h) {
        if (h && next && h->flags == HEADER_FREE_BLOCK && next->flags == HEADER_FREE_BLOCK &&
            (uint8_t *)h->memory + h->size == next->memory) {

            fprintf(stderr, "- adjusting %p from %p\n", h, next);
            
            h->size += next->size;
            header_set_unused(next);

            next = h->next;
        } else {
            h = next;
            if (next)
                next = next->next;
        }
    }
    }
#endif



    // FUTURE WORK - see note in listsort.c about automatic adjacent free block merging.
    header_sort_all();



#ifdef DEBUG
    // print map
    WITH_ITER(h, g_header_root, 
        int count = MAX(1, h->size/1024);
        for (int i=0; i<count; i++) {
            if (i==0)
                {fputc(h->flags == HEADER_FREE_BLOCK ? 'o' : 'O', stderr);fprintf(stderr, "(%p)(%c)(%p)", h, *(uint8_t *)h->memory, h->memory);}
            else
            if (h->flags == HEADER_FREE_BLOCK)
                fputc('_', stderr);
            else if (h->flags == HEADER_UNLOCKED)
                fputc('|', stderr);
            else if (h->flags == HEADER_LOCKED)
                fputc('X', stderr);
        }
    ) fputc('\n', stderr);
    
    /*
     * debug printouts
     */

    // decide smallest block size for printing
    uint32_t smallest=1<<31, largest=0, total=0;
    WITH_ITER(h, g_header_root,
        if (h->flags == HEADER_FREE_BLOCK) {
            if (h->size < smallest)
                smallest = h->size;
            if (h->size > largest)
                largest = h->size;
            total += h->size;
        }
    )
        fprintf(stderr, "\n");

    int total_header_size = 0;
    // print map
    WITH_ITER(h, g_header_root, 
        int count = MAX(1, h->size/1024);
        for (int i=0; i<count; i++) {
            if (i==0)
                {fputc(h->flags == HEADER_FREE_BLOCK ? 'o' : 'O', stderr);fprintf(stderr, "(%p)(%c)(%p)", h, *(uint8_t *)h->memory, h->memory);}
            else
            if (h->flags == HEADER_FREE_BLOCK)
                fputc('_', stderr);
            else if (h->flags == HEADER_UNLOCKED)
                fputc('|', stderr);
            else if (h->flags == HEADER_LOCKED)
                fputc('X', stderr);
        }
        if (header_is_unused(h))
            total_header_size += h->size;
    ) fputc('\n', stderr);
#endif

#if 0
    WITH_ITER(h, g_header_root,
        fprintf(stderr, "%d kb %s\n", h->size, h->flags == HEADER_FREE_BLOCK ? "free" : "used");
    )
    fprintf(stderr, "largest free block %d kb vs total size %d kb yields coherence %.4f, relative coherence %.4f\n",
            largest, total, 1.0-(float)largest/(float)total, 1.0-(float)smallest/(float)largest);
#endif

    /**********************************
     * C O M P A C T I N G
     **********************************/

    /* make one pass.
     *
     * a(f)->b(f)->c(f)->d(u)->e(u)->f(u)->g(x/u)
     * =>
     * a(nil)->b(nil)->[d..f]->c(f)->g(x/u)
     *
     * This means we need to find:
     *  - first free
     *  - last free
     *  - first used
     *  - last used
     *  - next
     *
     * set unused of affected headers,
     * link last free-1 to first free
     * link last free to last free
     *
     */

    header_t *root_header = g_header_root;
    header_t *prev = root_header;

    struct timespec start_time, now;
    int time_diff;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    bool done = false;
    while (!done) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        time_diff = now.tv_sec*1000 + now.tv_nsec/1000000 - start_time.tv_sec*1000 - start_time.tv_nsec/1000000;

        if (maxtime && time_diff >= maxtime) {
            fprintf(stderr, "Compact timeout: %d ms\n", time_diff);
            done = true;
            break;
        } 


        
#if 0
        {
        char *filling = "ABCDEFGHIJKLMNOPQRSTUVXYZ";
        int maxfill = strlen(filling);
        fprintf(stderr, "compact(): Verifying data (g_header_bottom = %p g_memory_top = %p): ", g_header_bottom, g_memory_top);
        header_t *f = g_header_top;
        bool yesok=true;
        while (f >= g_header_bottom) {
            if (f && f->memory && f->flags == HEADER_UNLOCKED) {
                uint8_t *foo = (uint8_t *)f->memory;
                if (f->size == 771)
                    fprintf(stderr, "** header %p of size 771 ends at memory %p (diff header bottom %d, diff memory top %d)\n", 
                            f, foo + f->size, (int)((uint8_t *)g_header_bottom-foo-f->size), (int)((uint8_t *)g_memory_top-foo-f->size));
                char filler = filling[f->size % maxfill];
                for (int i=0; i<f->size; i++)
                    if (filler != foo[i]) {
                        fprintf(stderr, "ERROR: ************** header %p (size %d) at %d = %c, should be %c. memory location %p\n",
                                f, f->size, i, foo[i], filler, &foo[i]);
                        yesok=false;



                        fprintf(stderr, "full map:\n");
                        // print map
                        { int headermapbytes=0;
                        WITH_ITER(h, g_header_root, 
                            int count = MAX(1, h->size/1024);
                            for (int i=0; i<count; i++) {
                                if (i==0)
                                    {fputc(h->flags == HEADER_FREE_BLOCK ? 'o' : 'O', stderr);fprintf(stderr, "(%p)(%c)(%p)", h, *(uint8_t *)h->memory, h->memory);}
                                else
                                if (h->flags == HEADER_FREE_BLOCK)
                                    fputc('_', stderr);
                                else if (h->flags == HEADER_UNLOCKED)
                                    fputc('|', stderr);
                                else if (h->flags == HEADER_LOCKED)
                                    fputc('X', stderr);
                            }
                            if (header_is_unused(h))
                                headermapbytes += h->size;
                        )
                            if (headermapbytes != total_header_size) {
                                fprintf(stderr, "\ncompact(): ERROR: ********** full map %d bytes != initial full map %d\n",
                                        headermapbytes, total_header_size);
                            }
                        }

                            fprintf(stderr, "\n\n");












                        abort();
                        break;
                    }
            }
            
            f--;
        }
        if (yesok) fprintf(stderr, "OK!\n");
        }
#endif




        if (!root_header) {
            done = true;
            break;
        }

        header_t *h = root_header;

        fprintf(stderr, "Starting from root header %p\n", root_header);

        while (h && h->flags != HEADER_FREE_BLOCK) {
            prev = h;
            h = h->next;
        }

        // free blocks: first and last.
        header_t *first_free = h;
        header_t *last_free = h;
        int total_free = 0;
        while (h && h->flags == HEADER_FREE_BLOCK) {
            last_free = h;
            total_free += h->size;
            h = h->next;
        }

        if (!h)
        {
            fprintf(stderr, "no free blocks\n");
            root_header = NULL;
            continue;
        }
        fprintf(stderr, "First free block %p, last free %p, total free %d\n", first_free, last_free, total_free);

        if (first_free == last_free)
        {
            fprintf(stderr, "first and last free is same block: header %p\n", first_free);
        }

        bool adjacent = false;
        if (h->flags == HEADER_UNLOCKED)
            adjacent = true;

        /********************************** FIXME: handle adjacent case!!!
         *
         * If adjacent, then size does not matter - can slide it directly.
         * Only do the non-adjacent search if last_free->next->flags != HEADER_UNLOCKED
         */

        // fast forward to first used block that will fit
        header_t *first_fixed = NULL, *last_fixed = NULL;
        bool exists_free_fixed = false; // are there free blocks within the fixed area?
        if (!adjacent) {
            if (h->flags == HEADER_LOCKED || (h->flags == HEADER_UNLOCKED && h->size > total_free))
                first_fixed = h;

            while (h && false == (h->flags == HEADER_UNLOCKED && h->size < total_free)) {
                if (h->flags == HEADER_FREE_BLOCK)
                    exists_free_fixed = true;
                //fprintf(stderr, "while != HEADER_UNLOCKED size %d > %d: %p flags %d\n", h->size, total_free, h, h->flags);
                last_fixed = h;
                h = h->next;
            }
        }
        fprintf(stderr, "First fixed block %p, last fixed %p, first used %p\n", first_fixed, last_fixed, h);

        if (!h && !exists_free_fixed) {fprintf(stderr, "no fast forwarded blocks\n"); root_header = NULL; continue;}

        if (!h && exists_free_fixed) {
            prev = last_free;
            root_header = prev->next;
            continue;
        } 

        header_t *first_used = h;
        header_t *last_used = h;
        int total_used = 0;
        //if (last_free->next != first_used) adjacent = false;

#ifdef DEBUG
        if (adjacent)
            fprintf(stderr, "Free and used blocks are adjacent.\n");
        else
            fprintf(stderr, "Free and used blocks are not adjacent.\n");
#endif

        bool found_used_block = false;
        while (h && h->flags == HEADER_UNLOCKED) {
            if (adjacent) {
                //fprintf(stderr, "while == HEADER_UNLOCKED: %p flags %d, size %d\n", h, h->flags, h->size);
                found_used_block = true;
                total_used += h->size;
                last_used = h;
                h = h->next;
            } else {
                // must move, can't just push down
                if (total_used + h->size <= total_free) {
                    //fprintf(stderr, "while == HEADER_UNLOCKED: %p flags %d, size %d\n", h, h->flags, h->size);
                    found_used_block = true;
                    total_used += h->size;
                    last_used = h;
                    h = h->next;
                } else
                    break;
            }
        }

        fprintf(stderr, "First used block = %p, last used block = %p, found_used_block = %d\n", first_used, last_used, found_used_block);

        if (!found_used_block) {fprintf(stderr, "no unlocked blocks\n"); root_header = NULL; continue;}

        fprintf(stderr, "total used = %d, total free = %d\n"
                "first - last free = %p - %p, first - last used = %p - %p\n", total_used, total_free,
                first_free, last_free, first_used, last_used);

        /* ok, we now have a range of free blocks and a range of used blocks.
         *
         * if adjacent, just push them back.
         * if not adjacent, do other tricky magic.
         */
        /* 
         * * kill off free headers
         * * create new header in free space before blocking block
         * * move used blocks
         * * offset used blocks
         * * create new header w/ free block in new free space
         */

        /*
        smallest = 1024;
        fprintf(stderr, "before move: \n");
        header_t *h = prev->next;
        while (h && h != last_used->next) {
            int count = MAX(1024, h->size/smallest);
            for (int i=0; i<count; i++) {
                if (h->flags == HEADER_FREE_BLOCK) fputc('_', stderr);
                else if (h->flags == HEADER_UNLOCKED) fputc('|', stderr);
                else if (h->flags == HEADER_LOCKED) fputc('X', stderr);
            }

            h = h->next;
        }
        fprintf(stderr, "\n");
        */

#ifdef DEBUG
        fprintf(stderr, "before move: \n");
        h = first_free;
        while (h && h != last_used->next) {
            int count = MAX(1, h->size/1024);
            for (int i=0; i<count; i++) {
                if (i==0)
                    {fputc(h->flags == HEADER_FREE_BLOCK ? 'o' : 'O', stderr);fprintf(stderr, "(%p)(%c)(%p)", h, *(uint8_t *)h->memory, h->memory);}
                else
                if (h->flags == HEADER_FREE_BLOCK)
                    fputc('_', stderr);
                else if (h->flags == HEADER_UNLOCKED)
                    fputc('|', stderr);
                else if (h->flags == HEADER_LOCKED)
                    fputc('X', stderr);
            }

            h = h->next;
        }
        fprintf(stderr, "\n--------------------------------------\n");
#endif


        // easy case, just push everything back.
        void *from_addr = first_used->memory;
        void *to_addr = first_free->memory;
        int offset = (uint8_t *)from_addr - (uint8_t *)to_addr;

        /* ADJACENT
         *
         * relinking A..F free, 0..9 used
         *
         * [prev] -> A B C D -> 0 1 2 3 -> (E F) ...
         * =>
         * [prev] -> 0 1 2 3 -> G
         *
         * - G is A..D + E..F (if exists) merged.
         *
         * if prev = root, then root = A, else prev->next = A
         * 
         * NOT ADJACENT
         *
         * relinking A..H free, X locked, 0..9 used
         *
         * [prev] -> A B C D -> X -> 0 1 2 3 -> (E F) ...
         * =>
         * [prev] -> 0 1 2 3 -> H -> X -> G
         * - G is A..D merged.
         * - H is extra space before X, i.e. diff free-used
         *
         * if prev = root, then root = A, else prev->next = A
         *
         * NOT ADJACENT, SPARSE
         *
         * relinking A-W free, X-Z locked, 0-9 used
         *
         * [prev] -> A B C D -> X -> 0 1 2 3 -> (E F) ...
         * =>
         * [prev] -> 0 1 2 3 -> H -> X -> G
         *
         * - G is A..D merged.
         * - H is extra space before X, i.e. diff free-used
         *
         * if prev = root, then root = A, else prev->next = A
         */

        // null out free headers
        h = first_free;
        int free_count = 0;
        header_t *spare_used = NULL;
        while (h != last_free->next) {
            if (!adjacent && h == last_free) {
                // chomp by the remainder
                int diff = total_free - total_used;
                if (diff > 0) {
                    void *end = (uint8_t *)last_free->memory + last_free->size;
                    h->memory = (uint8_t *)end - diff;
                    h->size = diff;
                    spare_used = h;

                    fprintf(stderr, "* Chomping off remainder of last free = %p, pointing to %p - %d = %p\n",
                            h, end, diff, h->memory);
                }
            } else {
                fprintf(stderr, "* Nulling header %p\n", h);
                header_set_unused(h);
            }
            h = h->next;
        }

        // relink into place
        if (header_is_unused(g_header_root)) {
            fprintf(stderr, "* Relinking header root was %p now prev = g_header_root = %p\n", g_header_root, first_used);
            g_header_root = first_used;
            prev = g_header_root;
        } else
            prev->next = first_used;

        // chomp any free blocks following last_used

        int extra_free_size = 0;
        // XXX: 2013-09-22 -- could the extra_free_size calculation be wrong?
        header_t *extra_free_header = NULL, *prev_extra_free_header = NULL;
        extra_free_header = last_used->next;
#if 0
        fprintf(stderr, "can I chomp data? %s\n", extra_free_header ? (extra_free_header->flags == HEADER_FREE_BLOCK ? "yes" : "no") : "no");
        while (extra_free_header && extra_free_header->flags == HEADER_FREE_BLOCK) {

            fprintf(stderr, "chomping %d bytes\n", extra_free_header->size);
            extra_free_size += extra_free_header->size;
            fprintf(stderr, "* Nulling header %p\n", extra_free_header);
            header_set_unused(extra_free_header);
            prev_extra_free_header = extra_free_header;
            extra_free_header = extra_free_header->next;
        }
        fprintf(stderr, "no more chomping, header %p is flag %d\n", extra_free_header, extra_free_header ? extra_free_header->flags : 0);

#endif
        header_t *after_free_headers = extra_free_header;

        // produce new free header, move into place
        header_t *new_free;
        
        // adjacent:
        // free headers have been nulled, insert a new one at the end of the
        // (now moved) used headers.
        //
        // non-adjacent:
        // insert the previously calculated spare block (spare_used), then
        // allocate a new free block where the first_used-last_used blocks
        // used to be
        new_free = header_new(false, true);
        if (!new_free) {
            // we're done here.
            done = true;
            continue;;
        }
        new_free->flags = HEADER_FREE_BLOCK;
        if (adjacent) {
            // FIXME: This calculation _could_ be wrong in non-adjacent mode
            // XXX: to_addr does not match up with total_used.
            new_free->memory = (uint8_t *)to_addr + total_used; 
            new_free->size = total_free;
            if (extra_free_size) {
                new_free->size += extra_free_size;
                new_free->next = after_free_headers;
            } else {
                new_free->next = last_used->next;
            }
            last_used->next = new_free;

            fprintf(stderr, "* Allocated new free block %p at memory %p size %p\n", new_free, new_free->memory, new_free->size);
        } else {
            new_free->memory = first_used->memory;
            new_free->size = total_used;

            fprintf(stderr, "* Relinking (non-adj.): last_fixed %p ->next = new_free %p; %p->next = %p->%p\n", last_fixed, new_free, new_free, last_used, last_used->next);

            // 0..9 -> spare_used -> X..Z -> ... -> new_free (previously used)
            
            last_fixed->next = new_free;
            if (extra_free_size) {
                new_free->size += extra_free_size;
                new_free->next = after_free_headers;
            } else {
                new_free->next = last_used->next;
            }

            fprintf(stderr, "* Relinking (non-adj.): last_used %p ->next = %p; %p->next = %p\n", last_used, spare_used, spare_used, first_fixed);
            // 0..9 -> spare_used -> X..Z
            last_used->next = spare_used;
            spare_used->next = first_fixed;
        }




        // move used blocks
        fprintf(stderr, "* Moving memory from %p (%p) to %p (%p) size %d, offset %d (total_free %d + extra_free %d = new free size %d)\n", first_used, from_addr, first_free, to_addr, total_used, offset, total_free, extra_free_size, new_free->size);
        memmove(to_addr, from_addr, total_used);

        // offset used headers
        h = first_used;
        while (h != last_used->next) {
            fprintf(stderr, "* Offsetting used blocks: %p memory %p - %d = %p\n", h, h->memory, offset, (uint8_t *)h->memory - offset);
            h->memory = (uint8_t *)h->memory - offset;
            h = h->next;
        }

        if (prev == g_header_root)
            h = prev;
        else
            h = prev->next;

#ifdef DEBUG
        fprintf(stderr, "after move (starting from %p, new_free->next = %p): \n", h, new_free->next);

        //h = first_used;
        while (h && h != new_free->next) {
            //fprintf(stderr, "h = %p\n", h);
            int count = MAX(1, h->size/1024);
            for (int i=0; i<count; i++) {
                if (i==0)
                    {fputc(h->flags == HEADER_FREE_BLOCK ? 'o' : 'O', stderr);fprintf(stderr, "(%p)(%c)(%p)", h, *(uint8_t *)h->memory, h->memory);}
                else
                if (h->flags == HEADER_FREE_BLOCK)
                    fputc('_', stderr);
                else if (h->flags == HEADER_UNLOCKED)
                    fputc('|', stderr);
                else if (h->flags == HEADER_LOCKED)
                    fputc('X', stderr);
                else
                    fputc('?', stderr);
            }

            h = h->next;
        }
        fprintf(stderr, "\n--------------------------------------\n");
#endif

        /*
        fprintf(stderr, "full map:\n");
        // print map
        { int headermapbytes=0;
        WITH_ITER(h, g_header_root, 
            int count = MAX(1, h->size/1024);
            for (int i=0; i<count; i++) {
                if (i==0)
                    {fputc(h->flags == HEADER_FREE_BLOCK ? 'o' : 'O', stderr);fprintf(stderr, "(%p)(%c)(%p)", h, *(uint8_t *)h->memory, h->memory);}
                else
                if (h->flags == HEADER_FREE_BLOCK)
                    fputc('_', stderr);
                else if (h->flags == HEADER_UNLOCKED)
                    fputc('|', stderr);
                else if (h->flags == HEADER_LOCKED)
                    fputc('X', stderr);
            }
            if (header_is_unused(h))
                headermapbytes += h->size;
        )
            if (headermapbytes != total_header_size) {
                fprintf(stderr, "\ncompact(): ERROR: ********** full map %d bytes != initial full map %d\n",
                        headermapbytes, total_header_size);
            }
        }

            fprintf(stderr, "\n\n");
            */

        prev = last_used;
        root_header = prev->next;



    }
#ifdef DEBUG
            fprintf(stderr, "******************** compact done in %d ms\n", time_diff);
            // print map
            WITH_ITER(h, g_header_root, 
                int count = MAX(1, h->size/1024);
                for (int i=0; i<count; i++) {
                    if (i==0)
                        //{fputc(h->flags == HEADER_FREE_BLOCK ? 'o' : 'X', stderr);fprintf(stderr, "(%p)(%c)(%p)", h, *(uint8_t *)h->memory, h->memory);}
                        {fputc(h->flags == HEADER_FREE_BLOCK ? 'o' : 'X', stderr);fprintf(stderr, "(%p)(%p)\n", h, h->memory);}
                    else
                    if (h->flags == HEADER_FREE_BLOCK)
                        fputc('_', stderr);
                    else if (h->flags == HEADER_UNLOCKED)
                        fputc('|', stderr);
                    else if (h->flags == HEADER_LOCKED)
                        fputc('X', stderr);
                }
            ) fputc('\n', stderr);
#endif


    // rebuild free list
    memset((void *)g_free_block_slots, 0, sizeof(free_memory_block_t *) * g_free_block_slot_count);

    uint32_t free_block_count = 0;
    header_t *h = g_header_root;
    while (h != NULL) {
        if (!h->memory || h->flags != HEADER_FREE_BLOCK) {
            h = h->next;
            continue;
        }

        // just let the smaller headers be, in case there are any.
        // there should not be any, and so this test is invalid.
        if (h->size >= sizeof(free_memory_block_t)) {

            free_block_count++;

            int k = log2_(h->size);
            free_memory_block_t *block = block_from_header(h);


            // XXX: added 2013-09-18. 
            // Since the free memory block slots is cleared, the blocks don't point at anything. 
            // How did this ever work?
            block->header = h; 
            block->next = NULL;

            // this should _always_ point to (h->memory+h->size - sizeof(free_block_memory_t))
            assert_memory_is_free((void *)block);

            // crash on g_free_block_slots[k]->next->header->size, because ->next->header == 0
            if (g_free_block_slots[k] == NULL)
                g_free_block_slots[k] = block;
            else {
                block->next = g_free_block_slots[k];
                g_free_block_slots[k] = block;
            }
        }

        h = h->next;
    }

#if 0 // #ifdef DEBUG
    for (int i=0; i<g_free_block_slot_count; i++)
    {
        fprintf(stderr, "free block slot [%d] (size %d) = %p (header size %d)\n", i, 1<<i, g_free_block_slots[i],
                (g_free_block_slots[i] != NULL && g_free_block_slots[i]->header != NULL) ? g_free_block_slots[i]->header->size : 0);
           
    }

    freeblock_verify_lower_size();

#endif


#ifdef DEBUG
    {
    header_t *h3 = g_header_root;
    end_count = 0;
    while (h3 != NULL) {
        if (h3->flags == HEADER_UNLOCKED && h3->memory != NULL)
            end_count++;
        h3 = h3->next;
    }
    }

    if (end_count != start_count)
        abort();

    get_block_count(&end_free, &end_locked, &end_unlocked, NULL);

    //if (end_locked != start_locked || end_unlocked != start_unlocked)
    if (end_unlocked != start_unlocked)
        abort();

    assert_blocks();
#endif
}
#endif

/* client code */
void rminit(void *heap, uint32_t size) {
    g_memory_size = size;

    // +1 to round up. e.g. log2(15)==3
    // => 0, 1, 2, but later log2(13) would map to 3!
    // in practice, will there be such a large block?
    g_free_block_slot_count = log2_(size) + 1; 
    g_free_block_slots = (free_memory_block_t **)heap;
    uint32_t count = sizeof(free_memory_block_t *)*g_free_block_slot_count;
    memset((void *)g_free_block_slots, 0, count);

    g_memory_bottom = (void *)((ptr_t)heap + (g_free_block_slot_count * sizeof(free_memory_block_t *)));
    g_memory_top = g_memory_bottom;

    // header top is located at the top of the heap space and grows downward.
    // header bottom points to the bottom, including the last one!
    g_header_top = (header_t *)((ptr_t)heap + size - sizeof(header_t));
    g_header_bottom = g_header_top;
    g_header_root = g_header_top;
    g_header_root->next = NULL;
    g_header_used_count = 0;

    // newly unused headers are prepended, i.e. placed first, and g_unused_header_root is re-pointed.
    g_unused_header_root = g_header_top;
    g_unused_header_root->next_unused = NULL;

    header_set_unused(g_header_top);

    memset(heap, 0, size);
}

void rmdestroy() {
    // nop.
    return;
}

handle_t rmmalloc(int size) {
    header_t *h = block_new(size);

    if (h == NULL) {
        fprintf(stderr, "h = NULL.\n");
        return NULL;
    }

#ifdef DEBUG
    memset(h->memory, header_fillchar(h), h->size);
    //rebuild_free_block_slots();
    //assert_blocks();
#endif

    return (handle_t)h;
}

void rmfree(handle_t h) {
    block_free((header_t *)h);

#ifdef DEBUG
    //rebuild_free_block_slots();
    //assert_blocks();
#endif
}

void *rmlock(handle_t h) {
    header_t *f = (header_t *)h;
    f->flags = HEADER_LOCKED;

    return f->memory;
}
void *rmweaklock(handle_t h) {
    header_t *f = (header_t *)h;
    f->flags = HEADER_WEAK_LOCKED;
    
    return f->memory;
}

void rmunlock(handle_t h) {
    header_t *f = (header_t *)h;
    f->flags = HEADER_UNLOCKED;
}

