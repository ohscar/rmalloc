/* rmalloc
 *
 * buddy allocator, simplest possible implementation.
 */
#include "buddy.h"

#include <stdlib.h>
#include <math.h>

static void *gheap = NULL;
static void *gend = NULL;
static int gheap_size = 0;

#define MIN_CHUNK_SIZE 4096

#define CI_A_USED 1
#define CI_B_USED 2
#define CI_A_SPLIT 4
#define CI_B_SPLIT 8

typedef struct chunk_item {
    void *a;
    void *b;
    uint8_t flags;

    struct chunk_item *next;
} chunk_item_t;

// holds the free list for each chunk size
typedef struct info_item {
    int k; // 2^k * MIN_CHUNK_SIZE, i.e. 0..<gheap_size>
    chunk_item_t *free_list;

    struct info_item *larger;
    struct info_item *smaller;
} info_item_t;

/* groot is the largest block, i.e. larger will always be NULL
 * new blocks are added as smaller.
 */
static info_item_t *groot = NULL;

/////////////////////////////////////////////////////////////////////////////

void ci_set_flag(chunk_item_t *item, int flag)
{
    item->flags |= flag;
}
void ci_reset_flag(chunk_item_t *item, int flag)
{
    item->flags &= ~flag;
}
bool ci_flag(chunk_item_t *item, int flag)
{
    return item->flags & flag;
}


uint32_t chunk_size(info_item_t *ii)
// actual size from k
{
    return pow(2, ii->k)*MIN_CHUNK_SIZE;
}

uint32_t up2(uint32_t n)
// From http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
{
    n--;
    n |= n>>1;
    n |= n>>2;
    n |= n>>4;
    n |= n>>8;
    n |= n>>16;
    n++;

    return n;
}
#define log2 shadowed_math_log2
uint32_t log2(uint32_t n)
{
    int r = 0;
    while (n >>= 1)
        r++;
    return r;
}

info_item_t *make_info_item(uint32_t n)
// n is guaranteed to be a multiple of 2
{
    info_item_t *ii = (info_item_t *)malloc(sizeof(info_item_t));
    ii->k = log2(n / MIN_CHUNK_SIZE);
    ii->smaller = NULL;
    ii->larger = NULL;
    ii->free_list = NULL;

    return ii;
}

chunk_item_t *make_chunk_item()
{
    chunk_item_t *chunk = (chunk_item_t *)malloc(sizeof(chunk_item_t));

    chunk->a = NULL;
    chunk->b = NULL;
    chunk->flags = 0;
    chunk->next = NULL;

    return chunk;
}

bool ci_a_free(chunk_item_t *ci) {
    return ci->a && !ci_flag(ci, CI_A_USED) && !ci_flag(ci, CI_A_SPLIT);
}
bool ci_b_free(chunk_item_t *ci) {
    return ci->b && !ci_flag(ci, CI_B_USED) && !ci_flag(ci, CI_B_SPLIT);
}

chunk_item_t *find_free_chunk(info_item_t *ii)
// first chunk in the list that has a free a or b, or NULL
{
    chunk_item_t *chunk = ii->free_list;
    chunk_item_t *prev = chunk;
    while (chunk) {
        if (ci_a_free(chunk) || ci_b_free(chunk))
            break;

        prev = chunk;
        chunk = chunk->next;
    }
    return prev;
}

info_item_t *split_chunk_item(info_item_t *ii, chunk_item_t *ci, info_item_t *into)
// split the free memory chunk into a and b
// optionally insert into 'into', NULL to create a new
// returns: NULL when chunk cannot be split
{
    uint32_t n = chunk_size(ii) >> 1;
    info_item_t *info = into == NULL ? make_info_item(n) : into;
    void *memory;
    int flag;
    if (ci_a_free(ci)) {
        // A free
        memory = ci->a;
        flag = CI_A_SPLIT;
    } else if (ci->b && !ci_flag(ci, CI_B_USED) && !ci_flag(ci, CI_B_SPLIT)) {
        // B free
        memory = ci->b;
        flag = CI_B_SPLIT;
    } else {
        return NULL;
    }
    chunk_item_t *chunk = make_chunk_item();
    ci_set_flag(ci, flag);

    // actual splitting
    chunk->a = memory;
    chunk->b = (uint8_t *)memory+n;
    if (into) {
        chunk_item_t *c = info->free_list;
        while (c->next) {
            c = c->next;
        }
        c->next = chunk;
    } else
        info->free_list = chunk;

    return info;
}

chunk_item_t *request_chunk(info_item_t *root, int targetk)
// split next available chunk in info block to the k:th item, unless available
// the info blocks are latched onto root, final block is returned.
{
    // do we already have a chunk of size targetk?
    info_item_t *info = root;
    chunk_item_t *ci = NULL;
    while (info->smaller && info->k > targetk)
        info = info->smaller;
    if (info->k == targetk && (ci = find_free_chunk(info)))
        return ci;

    // find the smallest block that has free chunks
    // note: there can be filled block lines between blocks w/ free chunks
    info_item_t *smallest_block = NULL;
    chunk_item_t *smallest_chunk = NULL;

    info_item_t *ii = root;
    chunk_item_t *chunk = ii->free_list;
    do {
        chunk = find_free_chunk(ii);
        if (chunk) {
            smallest_block = ii;
            smallest_chunk = chunk;
        }
        ii = ii->smaller;
    } while (ii && ii->k > targetk);

    // did we find a spare chunk to split?
    if (smallest_block) {
        info_item_t *block = smallest_block;
        chunk_item_t *chunk = smallest_chunk;

        // continue until sought k is reached
        while (block->k > targetk) {
            if (block->smaller) 
                split_chunk_item(block, chunk, block->smaller);
            else {
                // no smaller block available, create a new k block line
                ii = split_chunk_item(block, chunk, NULL);
                if (ii)
                    block->smaller = ii;
                else
                    // out of memory
                    return NULL;
            }

            block = block->smaller;
            chunk = find_free_chunk(block);
        }
        
    } else
        // out of memory
        return NULL;

    return chunk;
}

chunk_item_t *make_root_chunk_item(void *heap)
{
    chunk_item_t *ci = (chunk_item_t *)malloc(sizeof(chunk_item_t));
    ci->a = heap;
    ci->b = NULL;
    ci->next = NULL;
    ci->flags = 0;
    groot->free_list = ci;

    return ci;
}

void destroy_chunk_item(chunk_item_t *item)
{
    while (item) {
        ci_reset_flag(item, CI_A_USED);
        ci_reset_flag(item, CI_B_USED);
        
        chunk_item_t *next = item->next;
        free(item);
        item = next;
    }
}

void destroy_info_item(info_item_t *item)
{
    while (item) {
        destroy_chunk_item(item->free_list);

        info_item_t *next = item->smaller;
        free(item);

        item = next;
    }
}

void binit(uint32_t heap_size)
{
    uint32_t n = up2(heap_size);
    groot = make_info_item(n);
    void *heap = (void *)malloc(n);
    make_root_chunk_item(heap);
}
void bdestroy()
{
    destroy_info_item(groot);
}

void *balloc(uint32_t n)
{
    return NULL;
}

void bfree(void *p)
{
}

