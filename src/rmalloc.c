/*
 * rmmalloc.c
 *
 * Relocatable Memory Allocator
 *
 * Mikael Jansson <mail@mikael.jansson.be>
 */
#include "rmalloc.h"
#include <stdlib.h>
#include <stdio.h>

#define RMALLOC_INIT_RAM_SIZE (1 * 1024*1024)

/****************************************************************************/

static uint8_t *g_ram;
static uint8_t *g_ram_end;
static uint8_t *g_top; // memory grows up
static size_t g_ram_size;

/****************************************************************************/

struct memory_block_t {
    void *ptr;
    uint8_t used : 1;
    size_t size;
    memory_block_t *previous;
    memory_block_t *next;
};

static memory_block_t *g_root;
static memory_block_t *g_root_end;

static memory_block_t *mb_alloc(size_t size);
static status_t mb_free(void *ptr);
static memory_block_t *mb_find(void *ptr);

/* push a pointer onto the root
 *
 * return: the newly allocated block.
 * modify: g_top, g_root_end, g_root
 * depend: -
 */
memory_block_t *mb_alloc(size_t size) {
    memory_block_t *mb;
    if (g_top+size+sizeof(memory_block_t) < g_ram_end) {
        mb = (memory_block_t *)g_top;
        g_top += sizeof(memory_block_t);

        mb->ptr = g_top;
        mb->used = 1;
        mb->size = size;
        mb->previous = g_root_end;
        mb->next = NULL;

        g_top += size;

        return mb;
    } else
        return NULL;
}

/* find a memory block in the list
 *
 * return: the found block or NULL
 * modify: -
 * depend: g_root
 */
memory_block_t *mb_find(void *ptr) {
    memory_block_t *root;
    for (root = g_root;
         root != NULL && root->ptr != ptr;
         root = root->next);

    return root;
}


/* delete a memory block, i.e., mark as unused.
 *
 * return: always RM_OK
 * modify: g_root
 * depend: -
 */
status_t mb_delete(memory_block_t *mb) {
    mb->used = 0;
}

/****************************************************************************/
void *rmalloc_ram_end(void) {
    return g_ram_end;
}
void *rmalloc_ram_top(void) {
    return g_top;
}

status_t rmalloc_init(void) {
    g_ram_size = RMALLOC_INIT_RAM_SIZE;
    g_ram = (uint8_t *)malloc(g_ram_size);
    g_top = g_ram;
    g_ram_end = g_ram+g_ram_size;

    /* initialize in a special order, because
     * memory block's previous will be set to the root end in mb_alloc.
     * Which, for the very first allocation, will be NULL.
     */
    g_root_end = NULL;
    g_root = mb_alloc(0);

    return g_root != NULL ? RM_OK : RM_ERROR;
}

status_t rmalloc_destroy(void) {
     
}

void rmalloc_print(memory_t *memory) {
    fprintf(stderr, "* Handle at %d has block at %d of size %d bytes\n",
            (int)memory-(int)g_ram,
            (int)(memory->block)-(int)g_ram,
            memory->block->size);
}

/* allocate a chunk of memory
 *
 * separates a memory block (internal structure) from a memory handle (user
 * space code). this could potentially be used for aliasing (?) memory blocks,
 * by pointing two different memory handles to the same memory block.
 *
 * both memory blocks and handles live in the same memory space, and are
 * layout in memory as follows (with memory growing down the page)::
 *
 * rmalloc(size) => 
 * memory handle    : sizeof(memory_handle_t) + size
 * +- memory block  : sizeof(memory_block_t) + size
 *    +- ptr        : size
 *    +- size       : sizeof(size_t)
 *    +- previous   : sizeof(*)
 *    +- next       : sizeof(*)
 * +- locks         : 1
 *
 * Assuming a 32-bit architecture:
 *
 *  handle = 4
 *    locks = 1
 *    block = 4
 *      ptr = 4
 *      size = 4 + size
 *      previous = 4
 *      next = 4
 *  
 *  = 4+1+4+4+4+4+4 = 25 for a memory_t. to that comes the size of the memory
 *  buffer
 *
 */
status_t rmalloc(memory_t **memory, size_t size) {
    if (g_top+sizeof(memory_t)+sizeof(memory_block_t)+size < g_ram_end) {
        memory_t *m = (memory_t *)g_top;
        g_top += sizeof(memory_t);

        m->block = mb_alloc(size);
        if (m->block) {
            m->locks = 0;
            *memory = m;
            rmalloc_print(*memory);
            return RM_OK;
        } 
        g_top -= sizeof(memory_t);

        *memory = NULL;
        return RM_ERROR;
    }
    fprintf(stderr, "Failed allocating %d bytes with %d bytes left.\n",
            size,
            g_ram_end-g_top);
    return RM_INSUFFICIENT_MEMORY;
}

/* yay for naive solutions.
 */
status_t rmrealloc(memory_t **new, memory_t **old, size_t size) {
    rmfree(*old);
    return rmalloc(new, size);
}

status_t rmlock(memory_t *memory, void **ptr) {
    if (memory != NULL) {
        // XXX: what happens when there are more than 255 locks?
        if (memory->locks < 255)
            memory->locks++;
        
        *ptr = memory->block->ptr;

        return RM_OK;
    }
    return RM_ERROR;
}

status_t rmunlock(memory_t *memory) {
    // XXX: Signal an error if unlocked too many times?
    if (memory != NULL && memory->locks != 0) {
        memory->locks--;
        return RM_OK;
    }
    return RM_ERROR;
}

status_t rmfree(memory_t *memory) {
    if (memory != NULL) {
        memory->block->used = 0;
        memory->locks = 0;
    }
    return RM_ERROR;
}
