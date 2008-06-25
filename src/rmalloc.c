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

#define RMALLOC_INIT_RAM_SIZE (8 * 1024*1024)

#define RM_ASSERT(x) {uint8_t status = x; if (!status) {fprintf(stderr, "RM_ASSERT(%s) failed!\n", #x);}}
#define RM_ASSERT_RET(x, err) {uint8_t status = (x); if (!status) {fprintf(stderr, "RM_ASSERT(%s) failed!\n", #x); return err;}}

#define RM_MERGED      0
#define RM_NOT_MERGED  1

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

        mb->next = NULL;
        mb->previous = g_root_end;
        g_root_end->next = mb;

        g_root_end = mb;

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
 * modify: -
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

/* dump debug data about the allocation structures.
 * specifically, the blocks and the addresses within.
 */
void rmalloc_dump(void) {
    memory_block_t *mb = g_root->next;
    fprintf(stderr, "rmalloc_dump()\n");
    while (mb != NULL) {
        fprintf(stderr, "[%c] %6d %8p->ptr = %8p, end of pointer + memory_t = %8p = next = %8p\n",
                mb->used ? 'X' : ' ',
                mb->size,
                (uint8_t*)mb-g_ram,
                mb->ptr,
                (uint8_t*)(mb->ptr+sizeof(memory_t))-g_ram,
                (uint8_t*)mb->next-g_ram);
        mb = mb->next;
    }
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
    g_root = (memory_block_t *)g_ram; 
    g_top += sizeof(memory_block_t);
    g_root->next = NULL;
    g_root->previous = NULL;

    g_root_end = g_root;

    return g_root != NULL ? RM_OK : RM_ERROR;
}

status_t rmalloc_destroy(void) {
     
}

void rmalloc_print(memory_t *memory) {
    fprintf(stderr, "* Handle at %d has block at %d of size %d bytes\n",
            (int)memory-(int)g_ram,
            (int)(memory->block)-(int)g_ram,
            memory->block->size);
    
    fprintf(stdout, "(%s :start %d :size %d)\n",
            memory->block->used ? "alloc" : "free",
            (int)memory-(int)g_ram,
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
 *  chunk
 *
 *  Layout: memory_t, memory_block_t, chunk
 *
 */
status_t rmalloc(memory_t **memory, size_t size) {
    double top = (long int)(g_top-g_ram);
    double end = (long int)(g_ram_end-g_ram);
    float filling = 100.0*top/end;
    fprintf(stderr, "%d%% heap fullness\n", (int)filling);
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

/* Merge two adjacent (in memory) blocks, i.e.::
 *
 *      (a->ptr+1) == b
 *
 * As the information related to a memory block and the actual pointer to the
 * memory block is intertwined, it's not as easy as just bunching together the
 * two pointers, instead we have to overwrite the memory blocks themselves.
 * Specifically, block b will be overwritten.
 *
 * Make sure both memory blocks are free, i.e., used=0.
 *
 * Layout:  memory_t first, then block, then chunk.
 */
status_t mb_merge(memory_block_t *a, memory_block_t *b) {
    RM_ASSERT_RET(!a->used, RM_NOT_MERGED);
    RM_ASSERT_RET(!b->used, RM_NOT_MERGED);

    /* an easier check for now */
    /*
    RM_ASSERT(a->next == b);
    RM_ASSERT(b->previous == a);
    */
    
    /* are they adjacent? */
    fprintf(stderr, "%lu+%lu+%lu=%lu matches? %lu\n",
            a->ptr, a->size, sizeof(memory_t), a->ptr+a->size+sizeof(memory_t), b);
    /* layout in memory: size bytes memory chunk, memory_t for b, then b. */
    RM_ASSERT_RET(a->ptr+a->size+sizeof(memory_t) == b, RM_NOT_MERGED);

    a->next = b->next;
    a->size += b->size + sizeof(memory_t) + sizeof(memory_block_t);


    //return RM_NOT_MERGED;
    return RM_MERGED;
}

/* start compacting at and after the block addressed by the memory_t */
void rmalloc_compact(memory_t *memory) {
    memory_block_t *mb;
    status_t status;
    
    if (memory)
        mb = memory->block;
    else
        mb = g_root;
    
    while (mb->next != NULL) {
        if (!mb->used && !mb->next->used) {
            void *a = mb;
            void *b = mb->next;
            size_t as = mb->size;
            size_t bs = mb->next->size;
            status = mb_merge(mb, mb->next);
            if (status == RM_MERGED) {
                fprintf(stderr, "Merged %p (%d) and %p (%d) => %d bytes\n",
                        a, as, b, bs, mb->size);
            }
        }
        mb = mb->next;
    }
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
        mb_delete(memory->block);
        memory->locks = 0;
        rmalloc_print(memory);
    }
    return RM_ERROR;
}

const char *rmstatus(status_t s) {
    switch (s) {
        case RM_OK: return "OK"; 
        default:    return "error"; 
    }
    return "Unknown";
}
