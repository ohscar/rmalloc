/* rmalloc
 *
 * buddy allocator, simplest possible implementation.
 */
#include "buddy.h"

#include <stdlib.h>
#include <math.h>

#define DEBUG(x...) fprintf(stderr, x)
char g_error[1024];

static void *gheap = NULL;
static void *gend = NULL;
static int gheap_size = 0;

#define MIN_CHUNK_SIZE 4096

#define CI_A_USED 1 // allocated by client code
#define CI_B_USED 2
#define CI_A_SPLIT 4 // split into child blocks
#define CI_B_SPLIT 8
#define CI_A1_ELSEWHERE 16 // this block is a sub-block within another (parent) chunk
#define CI_A2_ELSEWHERE 32 
#define CI_B1_ELSEWHERE 64 
#define CI_B2_ELSEWHERE 128 // FIXME: merge with CI_A_USED?
#define CI_A_ELSEWHERE (CI_A1_ELSEWHERE|CI_A2_ELSEWHERE)
#define CI_B_ELSEWHERE (CI_B1_ELSEWHERE|CI_B2_ELSEWHERE)
#define CI_ELSEWHERE (CI_A_ELSEWHERE|CI_B_ELSEWHERE)

typedef struct chunk_item {
    // two nibbles
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
uint32_t chunk_size(info_item_t *ii)
// actual size from k
{
    return pow(2, ii->k)*MIN_CHUNK_SIZE;
}

void ci_set_flag(chunk_item_t *item, int flag)
{
    item->flags |= flag;
}
void ci_clear_flag(chunk_item_t *item, int flag)
{
    item->flags &= ~flag;
}
bool ci_flag(chunk_item_t *item, int flag)
{
    return item->flags & flag;
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

bool adjacent_nibbles(void *a, void *b, info_item_t *ii) {
    int n = chunk_size(ii);
    DEBUG("adjacent? %p vs %p\n", (uint8_t *)a+n, b);
    return (uint8_t *)a+n == b;
}

chunk_item_t *parent_chunk(info_item_t *parent, chunk_item_t *ci) {
    chunk_item_t *chunk = parent->free_list;
    while (chunk && ci->a != chunk->a && ci->a != chunk->b) {
        chunk = chunk->next;
    }
    return chunk;
}

chunk_item_t *add_chunk(info_item_t *parent, chunk_item_t *ci) {
    chunk_item_t *chunk = parent->free_list;
    while (chunk->next) {
        chunk = chunk->next; 
    }
    chunk->next = ci;

    return ci;
}

chunk_item_t *merge_nibbles(info_item_t *parent, chunk_item_t *ci1, chunk_item_t *ci2) {
    // easy case first
    if (ci1 == ci2) {
        if (ci_a_free(ci1) && ci_b_free(ci1) &&
                adjacent_nibbles(ci1->a, ci1->b, parent->smaller)) {

            // find the parent chunk with an A or a B that starts with ci1's A
            chunk_item_t *pc = parent_chunk(parent, ci1);
            DEBUG("parent chunk to ci1 (adj.): %p\n", pc);

            /*
            chunk_item_t *chunk = parent->free_list;
            bool found = false;
            while (chunk) {
                DEBUG("merge_nibbles: chunk = %p or %p against %p\n",
                        chunk->a, chunk->b, ci1->a);
                if (chunk->a == ci1->a || chunk->b == ci1->a) {
                    DEBUG("found chunk->a = %p!\n", chunk->a);
                    found = true;
                    break;
                }
                chunk = chunk->next;
            }
            if (found) {
            */

            if (pc) {
                pc->flags = 0;
                destroy_chunk_item(ci1);

                return pc;
            }
        } 
    } else {
        /* two scenarios:
         * 1. a B | A' b'
         * 2. a' B' | A b
         */
        DEBUG("a B | A' b': ci1->b+chunk_size = %p, ci2->a = %p, ci1 b free? %d, ci2 a free? %d\n",
                ci1->b+chunk_size(parent->smaller), ci2->a,
                ci_b_free(ci1), ci_a_free(ci2));
        DEBUG("a' B' | A b: ci2->b+chunk_size = %p, ci1->a = %p, ci2 b free? %d, ci1 a free? %d\n",
                ci2->b+chunk_size(parent->smaller), ci1->a,
                ci_b_free(ci2), ci_a_free(ci1));

        if (ci_b_free(ci1) && ci_a_free(ci2) && ci1->b+chunk_size(parent->smaller) == ci2->a) {
            chunk_item_t *pc1 = parent_chunk(parent, ci1);
            chunk_item_t *pc2 = parent_chunk(parent, ci2);
            DEBUG("parent chunk 1 & 2 = %p, %p.\n", pc1, pc2);
            if (pc1 && pc2) {
                // left to right
                if (ci1->a == pc1->a)
                    ci_set_flag(pc1, CI_A2_ELSEWHERE);
                else if (ci1->a == pc1->b)
                    ci_set_flag(pc1, CI_B2_ELSEWHERE);

                if (ci2->a == pc1->a)
                    ci_set_flag(pc2, CI_A1_ELSEWHERE);
                else if (ci2->a == pc2->b)
                    ci_set_flag(pc2, CI_B1_ELSEWHERE);

                chunk_item_t *chunk = make_chunk_item();
                chunk->a = ci1->b;
                chunk->b = NULL;
                return add_chunk(parent, chunk);
            }
        } else if (ci_b_free(ci2) && ci_a_free(ci1) && ci2->b+chunk_size(parent->smaller) == ci1->a) {
            chunk_item_t *pc1 = parent_chunk(parent, ci1);
            chunk_item_t *pc2 = parent_chunk(parent, ci2);
            if (pc1 && pc2) {
                // left to right
                if (ci2->a == pc1->a)
                    ci_set_flag(pc2, CI_A2_ELSEWHERE);
                else if (ci2->a == pc2->b)
                    ci_set_flag(pc2, CI_B2_ELSEWHERE);

                if (ci1->a == pc1->a)
                    ci_set_flag(pc1, CI_A1_ELSEWHERE);
                else if (ci1->a == pc1->b)
                    ci_set_flag(pc1, CI_B1_ELSEWHERE);

                chunk_item_t *chunk = make_chunk_item();
                chunk->a = ci2->b;
                chunk->b = NULL;
                return add_chunk(parent, chunk);
            }
        }

    }
    
    return NULL;
}

chunk_item_t *find_free_chunk(info_item_t *ii)
// first chunk in the list that has a free a or b, or NULL
{
    chunk_item_t *chunk = ii->free_list;
    chunk_item_t *prev = chunk;
    bool found = false;
    while (chunk) {
        //DEBUG("find free chunk in k=%d: chunk %p: free a %d, free b %d\n",
        //        ii->k, chunk, ci_a_free(chunk), ci_b_free(chunk));
        if (ci_a_free(chunk) || ci_b_free(chunk)) {
            found = true;
            break;
        }

        chunk = chunk->next;
    }
    return found ? chunk : NULL;
}

info_item_t *split_chunk_item(info_item_t *ii, chunk_item_t *ci, info_item_t *into)
// split the free memory chunk into a and b
// optionally insert into 'into', NULL to create a new
// returns: NULL when chunk cannot be split
{
    uint32_t n = into == NULL ? chunk_size(ii) >> 1 : chunk_size(into);
    info_item_t *info = into == NULL ? make_info_item(n) : into;
    void *memory;
    int flag;
    if (ci_a_free(ci)) {
        // A free
        memory = ci->a;
        flag = CI_A_SPLIT;
    } else if (ci_b_free(ci)) {
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
    DEBUG("requesting chunk k %d starting from %d\n", targetk, root->k);
    // do we already have a chunk of size targetk?
    info_item_t *info = root;
    chunk_item_t *ci = NULL;
    while (info->smaller && info->k > targetk)
        info = info->smaller;
    if (info->k == targetk && (ci = find_free_chunk(info))) {
        DEBUG("found free chunk %p in %p, returning.\n", ci, info);
        return ci;
    }

    // find the smallest block that has free chunks
    // note: there can be filled block lines between blocks w/ free chunks
    info_item_t *smallest_block = NULL;
    chunk_item_t *smallest_chunk = NULL;

    info_item_t *block = NULL;
    info_item_t *ii = root;
    chunk_item_t *chunk = ii->free_list;
    do {
        chunk = find_free_chunk(ii);
        if (chunk) {
            DEBUG("found free chunk %p (flags %d) from block %p at level %d\n",
                    chunk, chunk->flags, ii, ii->k);
            smallest_block = ii;
            smallest_chunk = chunk;
        }
        ii = ii->smaller;
    } while (ii && ii->k > targetk);

    // did we find a spare chunk to split?
    if (smallest_block) {
        block = smallest_block;
        chunk = smallest_chunk;

        // continue until sought k is reached
        while (block->k > targetk) {
            DEBUG("block->k %d > targetk %d\n", block->k, targetk);
            if (block->smaller) {
                DEBUG("split, smaller k available under block %p chunk %p flags %d\n",
                        block, chunk, chunk->flags);
                split_chunk_item(block, chunk, block->smaller);

                // FIXME: flags = 4 => A is split = OK! we can use b
            }
            else {
                // no smaller block available, create a new k block line
                ii = split_chunk_item(block, chunk, NULL);
                if (ii) {
                    block->smaller = ii;
                    DEBUG("split, created a smaller k in block %p chunk %p flags %d\n",
                            block, chunk, chunk->flags);
                }
                else {
                    sprintf(g_error, "OOM from split chunk.");
                    // out of memory
                    return NULL;
                }
            }


            block = block->smaller;
            chunk = find_free_chunk(block);

            DEBUG("chunk after split, block = %p, chunk = %p (%d)\n", block, chunk, chunk->flags);
        }
        
    } else {
        sprintf(g_error, "OOM: all parent blocks exhausted\n");
        // out of memory
        return NULL;
    }

    DEBUG("chunk after split, returning chunk = %p\n", chunk);

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

void destroy_chunk_items(chunk_item_t *root)
{
    while (item) {
        ci_clear_flag(item, CI_A_USED);
        ci_clear_flag(item, CI_B_USED);
        
        chunk_item_t *next = item->next;
        free(item);
        item = next;
    }
}
void destroy_chunk_items(chunk_item_t *root)
{
    while (item) {
        ci_clear_flag(item, CI_A_USED);
        ci_clear_flag(item, CI_B_USED);
        
        chunk_item_t *next = item->next;
        free(item);
        item = next;
    }
}

void destroy_info_items(info_item_t *item)
{
    while (item) {
        destroy_chunk_items(item->free_list);

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

