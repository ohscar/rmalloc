#include <stdlib.h>
#include <gtest/gtest.h>
#include "compact.c" // to get implementation specific details

#define MB(x) 1024*1024*x
#define KB(x) 1024*x
int heap_size = MB(16);

class AllocTest : public ::testing::Test {
protected:
    AllocTest() {
        storage = (void *)malloc(heap_size);
    }
    ~AllocTest() {
    }
    void SetUp() {
        cinit(storage, heap_size);
        srand(time(NULL));
    }
    void TearDown() {
        cdestroy();
    }

    void *storage;
};

TEST_F(AllocTest, Init) {
    ASSERT_EQ(g_memory_bottom, storage);
    ASSERT_EQ(g_memory_top, g_memory_bottom);
    ASSERT_EQ((void *)g_header_top, (void *)((uint32_t)g_memory_bottom+heap_size));
}

TEST_F(AllocTest, HeaderFindFree) {
    header_t *h = header_find_free();
    h->flags = HEADER_UNLOCKED;
    h->memory = (void *)1;
    ASSERT_TRUE(h != NULL);
    h = header_find_free();
    ASSERT_TRUE(h == NULL);

    ASSERT_EQ(g_header_bottom, g_header_top);
}

void test_header_set_used(header_t *h) {
    h->flags = HEADER_UNLOCKED;
    h->memory = (void *)1;
}

TEST_F(AllocTest, HeaderNew) {
    header_t *h = header_find_free();
    h->flags = HEADER_UNLOCKED;
    h->memory = (void *)1;
    ASSERT_TRUE(h != NULL);
    h = header_find_free();
    ASSERT_TRUE(h == NULL);
    h = header_new();
    ASSERT_TRUE(g_header_top == (g_header_bottom+1));
    ASSERT_TRUE(h != NULL);
    ASSERT_TRUE(h == g_header_bottom);

    // discard it so the next test's calculation will be correct
    test_header_set_used(h);

    int limit = 30000;
    for (int i=0; i<limit; i++) {
        test_header_set_used(header_new());
    }

    ASSERT_EQ(h, &g_header_bottom[limit]);
    h = &g_header_bottom[limit>>1];
    header_set_unused(h);

    // verify the new header appeared at the correct location
    header_t *h2 = header_new();
    ASSERT_EQ(h, h2);
}

// verify that memory top increases and header bottom decreases
TEST_F(AllocTest, MallocGrowsMemoryHeaders) {
    uint8_t *memory_top = (uint8_t *)g_memory_top;
    header_t *header_bottom = g_header_bottom;

    int size = 1024;
    handle_t *h = cmalloc(size);
    ASSERT_TRUE(h != NULL);
    ASSERT_EQ((uint8_t *)g_memory_top, memory_top+1024);
    // first header is always free - memory location [0]
    ASSERT_EQ(g_header_bottom, header_bottom);
    
    memory_top += 1024;
    header_bottom = g_header_bottom;
    handle_t *h2 = cmalloc(size);
    ASSERT_TRUE(h2 != NULL);
    ASSERT_EQ((uint8_t *)g_memory_top, memory_top+1024);

    ASSERT_EQ(g_header_bottom, header_bottom-1);
}

TEST_F(AllocTest, MallocExhaust) {
    int size = 1024;
    handle_t *h = cmalloc(size);
    uint8_t *memory_top = (uint8_t *)g_memory_top;
    header_t *header_bottom = g_header_bottom;
    while ((uint8_t *)g_header_bottom - (uint8_t *)g_memory_top > size+sizeof(header_t)) {
        h = cmalloc(size);

        ASSERT_EQ((uint8_t *)g_memory_top, memory_top+1024);
        ASSERT_EQ(g_header_bottom, header_bottom-1);
                
        memory_top = (uint8_t *)g_memory_top;
        header_bottom = g_header_bottom;
    }
    h = cmalloc(size);
    ASSERT_TRUE(h == NULL);
}

TEST_F(AllocTest, FreeAndMergeSimple) {
    handle_t *h, *h2, *h3, *h5;
    int size = 1024;
    h = cmalloc(size);
    h2 = cmalloc(size);
    h3 = cmalloc(size);
    h = cmalloc(size);
    h5 = cmalloc(size);
    h = cmalloc(size);

    ASSERT_TRUE(g_free_list_root == NULL);
    ASSERT_TRUE(g_free_list_end == NULL);
    header_t *f2 = (header_t *)h2;
    cfree(h2);

    free_memory_block_t *block2 = block_from_header(f2);
    ASSERT_TRUE(g_free_list_root == block2);
    ASSERT_TRUE(g_free_list_end == block2);
    ASSERT_TRUE(block2->header == f2);
    ASSERT_TRUE(block2->next == NULL);

    header_t *f5 = (header_t *)h5;
    cfree(h5);

    free_memory_block_t *block5 = block_from_header(f5);

    ASSERT_TRUE(g_free_list_root == block2);
    ASSERT_TRUE(block2->next == block5);
    ASSERT_TRUE(g_free_list_end == block5);
    ASSERT_TRUE(block5->header == f5);
    ASSERT_TRUE(block5->next == NULL);

    // killing off h3 means merging with h2!
    header_t *f3 = (header_t *)h3;
    uint8_t *end3 = (uint8_t *)f3->memory + f3->size;
    cfree(h3);
    
    // shall be the same.
    ASSERT_TRUE(g_free_list_root == block2);
    ASSERT_TRUE(g_free_list_end == block5);

    ASSERT_EQ(end3, (uint8_t *)f2->memory+f2->size);
    ASSERT_TRUE(header_is_unused(f3));

    // didn't touch anything else did we?
    ASSERT_TRUE(block5->header == f5);
    ASSERT_TRUE(block2->header == f2);
}

TEST_F(AllocTest, FreeAndMergeEverySecond) {
    handle_t *h1, *h2;

    int size = 1024;
    int count = g_memory_size/(size+sizeof(header_t));
    bool done = false;
    uint8_t *memtop = (uint8_t *)g_memory_top;
    for (int i=0; i<count/2; i++) {
        h1 = cmalloc(size);
        ASSERT_EQ((uint8_t *)g_memory_top, memtop+size);
        memtop += size;

        h2 = cmalloc(size);
        ASSERT_EQ((uint8_t *)g_memory_top, memtop+size);
        memtop += size;

        cfree(h2);
    }

    int free_blocks = 0;
    free_memory_block_t *free_block = g_free_list_root;
    while (free_block != g_free_list_end) {
        free_memory_block_t *this_block = block_from_header(free_block->header);
        ASSERT_EQ(this_block, free_block);

        free_block = free_block->next;
        free_blocks++;
    }
    if (g_free_list_end != g_free_list_root)
        free_blocks++;

    ASSERT_EQ(free_blocks, count/2);
}

// alloc, free, free later, alloc, free, free later
// the free later are freed at a later pass.
TEST_F(AllocTest, FreeOneMergeTwo) {
    handle_t *h1, *h2;
    handle_t **free_later;

    int size = 1024;
    int count = g_memory_size/(size+sizeof(header_t));

    free_later = (handle_t **)malloc(count/3 * sizeof(handle_t *));
    int later_i=0;
    bool done = false;
    uint8_t *memtop = (uint8_t *)g_memory_top;
    int allocs = 0;
    for (int i=0; i<count/3; i++) {
        h1 = cmalloc(size);
        ASSERT_EQ((uint8_t *)g_memory_top, memtop+size);
        memtop += size;
        allocs++;

        h2 = cmalloc(size);
        ASSERT_EQ((uint8_t *)g_memory_top, memtop+size);
        memtop += size;
        allocs++;

        ASSERT_FALSE(h1 == h2);

        header_t *f2 = (header_t *)h2;
        //printf("freeing header %p memory %p\n", f2, f2->memory);
        void *mem2 = f2->memory;
        cfree(h2);

        free_later[later_i] = cmalloc(size);

        header_t *hl = (header_t *)free_later[later_i];
        void *memlater = hl->memory;
        ASSERT_FALSE(mem2 == memlater);

        ASSERT_EQ((uint8_t *)g_memory_top, memtop+size);
        memtop += size;
        allocs++;

        later_i++;
    }

    int free_blocks = 0;
    free_memory_block_t *free_block = g_free_list_root;
    while (free_block != g_free_list_end) {
        //printf("first checking block %d at header %p memory %p\n", free_blocks, free_block->header, free_block->header->memory);
        free_block = free_block->next;
        free_blocks++;
    }
    if (g_free_list_end != g_free_list_root)
        free_blocks++;

    // this is supposed to merge with the already freed blocks. there will be
    // exactly the same number of free blocks before and after.
    // their new size will be size*2
    for (int i=0; i<later_i; i++) {
        header_t *h = (header_t *)free_later[i];
        //printf("#%d freeing header %p memory %p\n", i, h, h->memory);
        cfree(free_later[i]);
    }

    int free_size = 0;
    int free_blocks_after = 0;
    free_block = g_free_list_root;
    while (free_block != g_free_list_end) {
        free_size += free_block->header->size;
        //printf("second checking block %d at header %p memory %p\n", free_blocks_after, free_block->header, free_block->header->memory);
        ASSERT_EQ(free_block->header->size, size*2);

        free_block = free_block->next;
        free_blocks_after++;
    }
    if (g_free_list_end != g_free_list_root) {
        free_blocks_after++;
        free_size += free_block->header->size;
    }

    ASSERT_EQ(free_size, free_blocks*size*2);

    ASSERT_EQ(free_blocks, free_blocks_after);

    uint32_t total = stat_total_free_list();
    printf("total free list size = %u kb (%u mb)\n", total/1024, total/1048576);
}


// alloc, free, free later, alloc, free, free later
// the free later are freed at a later pass.
// compact
TEST_F(AllocTest, RandomAllocFreeCompact) {
    const int maxsize = 128*1024;
    int largest = 0;

    bool done = false;
    while (!done) {
        int size = rand()%maxsize;
        handle_t *h = cmalloc(size);

        if (h == NULL) {
            done = true;
            break;
        }
        if (largest < size)
            largest = size;

        // free by 20% probability
        if (rand()%5 == 0) {
            cfree(h);
        }
    }
    printf("largest block allocated %d kb\n", largest/1024);

    compact();
}
#if 0
TEST_F(AllocTest, MakeInfoItem) {
    int n = up2(20000);
    ASSERT_EQ(n, 32768);
    info_item_t *ii = make_info_item(n);
    ASSERT_EQ(ii->k, log2(n/MIN_CHUNK_SIZE)); // MIN_CHUNK_SIZE==4096 -> 4=0, 8=1, 16=2, 32=3
    ASSERT_TRUE(ii->larger == NULL);
    ASSERT_TRUE(ii->smaller == NULL);
    ASSERT_TRUE(ii->free_list == NULL);
}

TEST_F(AllocTest, SplitRootChunk) {
    // Split the available (a or b) of the root chunk
    info_item_t *ii = split_chunk_item(groot, groot->free_list, NULL);
    ASSERT_TRUE(ii != NULL);
    ASSERT_TRUE(ii->free_list != NULL);
    ASSERT_EQ(ii->free_list->a, groot->free_list->a);
    ASSERT_EQ(ii->free_list->b, (void *)((uint8_t *)ii->free_list->a+chunk_size(ii)));
}

TEST_F(AllocTest, SplitChunkTwice) {
    // Split both a and b of a chunk
    info_item_t *ii = split_chunk_item(groot, groot->free_list, NULL);

    info_item_t *i1 = split_chunk_item(ii, ii->free_list, NULL);
    ASSERT_TRUE(i1 != NULL);
    ASSERT_TRUE(i1->free_list != NULL);
    ASSERT_EQ(i1->free_list->a, ii->free_list->a);
    ASSERT_EQ(i1->free_list->b, (void *)((uint8_t *)i1->free_list->a+chunk_size(i1)));

    info_item_t *i2 = split_chunk_item(ii, ii->free_list, NULL);
    ASSERT_TRUE(i2 != NULL);
    ASSERT_TRUE(i2->free_list != NULL);
    ASSERT_EQ(i2->free_list->a, ii->free_list->b);
    ASSERT_EQ(i2->free_list->b, (void *)((uint8_t *)i2->free_list->a+chunk_size(i2)));

    info_item_t *i3 = split_chunk_item(groot, groot->free_list, NULL);
    ASSERT_TRUE(i3 == NULL);
}

TEST_F(AllocTest, ExtendFreelist) {
    // Add a chunk to an existing freelist
    info_item_t *ii = split_chunk_item(groot, groot->free_list, NULL);

    info_item_t *i1 = split_chunk_item(ii, ii->free_list, NULL);
    ASSERT_TRUE(i1 != NULL);
    ASSERT_TRUE(i1->free_list != NULL);
    ASSERT_EQ(i1->free_list->a, ii->free_list->a);
    ASSERT_EQ(i1->free_list->b, (void *)((uint8_t *)i1->free_list->a+chunk_size(i1)));

    info_item_t *i2 = split_chunk_item(ii, ii->free_list, NULL);
    ASSERT_TRUE(i2 != NULL);
    ASSERT_TRUE(i2->free_list != NULL);
    ASSERT_EQ(i2->free_list->a, ii->free_list->b);
    ASSERT_EQ(i2->free_list->b, (void *)((uint8_t *)i2->free_list->a+chunk_size(i2)));

    info_item_t *i3 = split_chunk_item(groot, groot->free_list, NULL);
    ASSERT_TRUE(i3 == NULL);
}

TEST_F(AllocTest, FindFreeChunk) {
    //EXPECT_TRUE(false);
}

#define CHUNK_EQUAL(i, p, c) (p->a == c->a || p->b == c->a) && (c->b == (uint8_t *)(c->a)+chunk_size(i))

#define CHUNK_EQUAL_A(i, p, c) {ASSERT_EQ(p->a, c->a); ASSERT_EQ(c->b, (uint8_t *)(c->a)+chunk_size(i));}
#define CHUNK_EQUAL_B(i, p, c) {ASSERT_EQ(p->b, c->a); ASSERT_EQ(c->b, (uint8_t *)(c->a)+chunk_size(i));}

TEST_F(AllocTest, RequestChunkZeroLevel) {
    chunk_item *ci = request_chunk(groot, groot->k);
    ASSERT_TRUE(ci != NULL);
    ASSERT_TRUE(ci == groot->free_list);
}

TEST_F(AllocTest, RequestChunkOneLevel) {
    int newk = groot->k - 1;
    chunk_item *ci = request_chunk(groot, newk);
    ASSERT_TRUE(ci != NULL);
    ASSERT_EQ(groot->smaller->k, newk);

    chunk_item_t *chunk = groot->smaller->free_list;
    ASSERT_EQ(ci, chunk);

    ASSERT_TRUE(CHUNK_EQUAL(groot->smaller, groot->free_list, chunk));
}

TEST_F(AllocTest, RequestChunkTwoLevel) {
    int newk = groot->k - 2;
    chunk_item *ci = request_chunk(groot, newk);

    ASSERT_TRUE(ci != NULL);
    ASSERT_EQ(groot->smaller->smaller->k, newk);
    ASSERT_EQ(ci, groot->smaller->smaller->free_list);
    ASSERT_TRUE(CHUNK_EQUAL(groot->smaller->smaller, groot->smaller->free_list, groot->smaller->smaller->free_list));
}

TEST_F(AllocTest, RequestChunkBottomLevel) {
    chunk_item *ci = request_chunk(groot, 0);
    ASSERT_TRUE(ci != NULL);

    info_item_t *ii = groot;
    info_item_t *pii = ii;
    while (ii->smaller) {
        pii = ii;
        ii = ii->smaller;
    }

    // just check the last level
    ASSERT_TRUE(CHUNK_EQUAL(ii, pii->free_list, ii->free_list));
}

TEST_F(AllocTest, RequestChunkTwoLevelTwice) {
    int newk = groot->k - 2;
    chunk_item *ci1 = request_chunk(groot, newk);
    chunk_item *ci2 = request_chunk(groot, newk);
    ASSERT_EQ(ci1, ci2);

    ASSERT_EQ(1, chunk_count(groot->smaller->free_list));
    ASSERT_EQ(1, chunk_count(groot->smaller->smaller->free_list));

    // A used => same chunk
    ci_set_flag(ci1, CI_A_USED);
    ci2 = request_chunk(groot, newk);
    ASSERT_EQ(ci1, ci2);
    ASSERT_EQ(1, chunk_count(groot->smaller->smaller->free_list));

    // B used => same chunk
    ci_clear_flag(ci1, CI_A_USED);
    ci_set_flag(ci1, CI_B_USED);
    ci2 = request_chunk(groot, newk);
    ASSERT_EQ(ci1, ci2);
    ASSERT_EQ(1, chunk_count(groot->smaller->smaller->free_list));

    // A and B used => new chunk
    ci_set_flag(ci1, CI_A_USED);
    ci_set_flag(ci1, CI_B_USED);
    ci2 = request_chunk(groot, newk);
    ASSERT_NE(ci1, ci2);
    ASSERT_EQ(2, chunk_count(groot->smaller->smaller->free_list));

    info_item_t *pii = groot->smaller;
    info_item_t *ii = groot->smaller->smaller;

    ASSERT_EQ(ii->free_list, ci1);

    CHUNK_EQUAL_A(ii, pii->free_list, ci1);
    CHUNK_EQUAL_B(ii, pii->free_list, ci2);
}

chunk_item_t *chunk_discard(chunk_item_t *chunk) {
    ci_set_flag(chunk, CI_A_USED);
    ci_set_flag(chunk, CI_B_USED);
    return chunk;
}

/* allocate each block in each level
 */
TEST_F(AllocTest, RequestChunkNLevelInfinite) {
    int level = 1;
    int count = 1;
    int newk = groot->k - level;
    chunk_item_t *ci = request_chunk(groot, newk);
    info_item_t *ri = groot->smaller;
    ASSERT_EQ(ci, ri->free_list);

    for (level=2; level<10; level++) {
        newk = groot->k - level;
        chunk_item_t *ci = request_chunk(groot, newk);
        ri = ri->smaller;
        ASSERT_EQ(ci, ri->free_list);
        count = 1;
        while (ci) {
            // mark as used
            chunk_discard(ci);

            ASSERT_EQ(chunk_count(ri->free_list), count);

            ci = request_chunk(groot, newk);
            count++;
        }
        count--;
        /*
        fprintf(stderr, "%d chunks (of %d) size %dK, k = %d => max %.0f\n",
                chunk_count(ri->free_list), count, chunk_size(ri)/1024,
                newk, pow(2, level-1));
        */
        ASSERT_EQ(pow(2, level-1), count);

        // un-discard the chunks in this level
        ci = ri->free_list;
        while (ci) {
            ci->flags = 0;
            ci = ci->next;
        }
    }
}

/* merge all chunks for a specific k.
 */
TEST_F(AllocTest, MergeOneLevel) {
    int level = 2;
    
    int newk = groot->k - level;

    // first, make sure the parent A block is split
    chunk_item_t *ci = request_chunk(groot, newk);
    ASSERT_EQ(groot->free_list->flags, CI_A_SPLIT);

    dump_root(groot, 3);

    chunk_item_t *old = ci;

    // adjacent nibbles, i.e. ci's A and B
    //fprintf(stderr, "%s:%d ci = %p\n", __FILE__, __LINE__, ci);
    chunk_item_t *adjchunk = merge_nibbles(groot->smaller, &ci, &ci);
    ASSERT_TRUE(adjchunk != NULL);
    ASSERT_TRUE(ci == NULL);
    //fprintf(stderr, "%s:%d ci = %p\n", __FILE__, __LINE__, ci);

    ASSERT_EQ(groot->smaller->free_list->flags, 0);

    dump_root(groot, 3);

    ci = request_chunk(groot, newk);

    dump_root(groot, 3);

    ci_set_flag(ci, CI_A_USED);
    ci_set_flag(ci, CI_B_USED);
    chunk_item_t *ci2 = request_chunk(groot, newk);
    ASSERT_NE(ci, ci2);

    // a = used, A = free
    // a | B - A | b == can merge nibbles B and A
    ci->flags = 0;
    ci2->flags = 0;

    dump_root(groot, 3);

    int s = chunk_size(groot->smaller->smaller);
    ASSERT_EQ((uint8_t *)ci->b+s, (uint8_t *)ci2->a);

    void *cib = ci->b;
    void *ci2a = ci2->a;
    // ci's B and ci2's A
    // a B | A b
    chunk_item_t *nc = merge_nibbles(groot->smaller, &ci, &ci2);
    ASSERT_TRUE(nc != NULL);
    ASSERT_EQ(nc->a, cib);

    dump_root(groot, 3);

    // mark the rest of those two chunks as used
    // there'll be a new chunk from the parent chunk we just created,
    // with the nibble addresses of the old chunks.
    ci_set_flag(ci, CI_A_USED); 
    ci_set_flag(ci2, CI_B_USED);
    chunk_item_t *c2 = request_chunk(groot, newk);

    dump_root(groot, 3);

    //fprintf(stderr, "(nc=%p, %p %p from %p : %p) c2 %p: %p %p\n", nc, nc->a, nc->b, cib, ci2a, c2, c2->a, c2->b);
    ASSERT_EQ(c2->a, cib);
    ASSERT_EQ(c2->b, ci2a);
}

// remove chunks that no longer point to any children, i.e. all child chunk nibbles = NULL
TEST_F(InternalAllocTest, BlockCleanup) {
    SetupMemory();
    dump_root(groot, 3);
/*
/===================
K 12 = {0x8da6260  4: 0xa6533008 /      (nil)}
K 11 = {0x8da6290  c: 0xa6533008 / 0xa6d33008} {0x8da6488  4: 0xa6933008 /      (nil)}
K 10 = {0x8da62c0  1: 0xa6533008 /      (nil)} {0x8da6470  2:      (nil) / 0xa7133008} {0x8da64a0  0: 0xa6933008 / 0xa6d33008}
===================
*/
    int count_10 = chunk_count(groot->smaller->smaller->free_list);
    clean_unused_chunks(groot->smaller->smaller);
    ASSERT_EQ(count_10, chunk_count(groot->smaller->smaller->free_list));

    int count_11 = chunk_count(groot->smaller->free_list);
    clean_unused_chunks(groot->smaller);

    ASSERT_TRUE(groot->smaller->free_list->next == NULL);
    ASSERT_EQ(count_11-1, chunk_count(groot->smaller->free_list));
}

/* merge all chunks for k and k-1
 */
#if 0
TEST_F(AllocTest, MergeTwoLevel) {
    int level = 2;
    
    int newk = groot->k - level;

    // first, make sure the parent A block is split
    chunk_item_t *ci = request_chunk(groot, newk);
    ASSERT_EQ(groot->free_list->flags, CI_A_SPLIT);

    chunk_item_t *old = ci;

    // adjacent nibbles, i.e. ci's A and B
    //fprintf(stderr, "%s:%d ci = %p\n", __FILE__, __LINE__, ci);
    chunk_item_t *adjchunk = merge_nibbles(groot->smaller, &ci, &ci);
    ASSERT_TRUE(adjchunk != NULL);
    ASSERT_TRUE(ci == NULL);
    //fprintf(stderr, "%s:%d ci = %p\n", __FILE__, __LINE__, ci);

    ASSERT_EQ(groot->smaller->free_list->flags, 0);

    // for the next chunk to have another address
    void *throwaway = malloc(400);

    ci = request_chunk(groot, newk);
    //fprintf(stderr, "%s:%d ci = %p\n", __FILE__, __LINE__, ci);
    ASSERT_NE(old, ci);

    ci_set_flag(ci, CI_A_USED);
    ci_set_flag(ci, CI_B_USED);
    chunk_item_t *ci2 = request_chunk(groot, newk);
    ASSERT_NE(ci, ci2);

    // a = used, A = free
    // a | B - A | b == can merge nibbles B and A
    ci->flags = 0;
    ci2->flags = 0;

    int s = chunk_size(groot->smaller->smaller);
    ASSERT_EQ((uint8_t *)ci->b+s, (uint8_t *)ci2->a);

    void *cib = ci->b;
    // ci's B and ci2's A
    chunk_item_t *nc = merge_nibbles(groot->smaller, &ci, &ci2);
    ASSERT_TRUE(nc != NULL);
    ASSERT_EQ(nc->a, cib);
}
#endif

TEST_F(AllocTest, Destroy) {
    //EXPECT_TRUE(false);
}


TEST_F(AllocTest, UserAllocFixedSize) {
    void *ptr;
    uint32_t size = KB(512);
    void *c1a = balloc(size);
    void *c1b = balloc(size);
    ASSERT_TRUE(c1a != NULL);
    ASSERT_TRUE(c1b != NULL);
    ASSERT_EQ((uint8_t *)c1a+size, c1b);

    dump_root(groot, 6);

    void *c2a = balloc(size);
    void *c2b = balloc(size);
    ASSERT_TRUE(c2a != NULL);
    ASSERT_TRUE(c2b != NULL);
    ASSERT_EQ((uint8_t *)c1b+size, c2a);
    ASSERT_EQ((uint8_t *)c2a+size, c2b);

    // we can allocate 16*2=32 512kb blocks. Let's try it!
    for (int i=0; i<up2(heap_size)/size-2*2; i++) {
        ptr = balloc(size);
        ASSERT_TRUE(ptr != NULL);
    }

    ptr = balloc(size);
    ASSERT_TRUE(ptr == NULL);
}

// allocate one nibble for each k will allocate allocate all memory except the
// last chunk size
TEST_F(AllocTest, UserAllocOneInEach) {
    void *ptr;
    uint32_t size = up2(heap_size)>>1; // start with level 11
    void *firstptr = NULL;
    bool first = false;

    while (size >= MIN_CHUNK_SIZE) {
        ptr = balloc(size);
        ASSERT_TRUE(ptr != NULL);
        if (!first) {
            first = true;
            firstptr = ptr;
        }
        size >>= 1;
    }
    // last block starts at the start of the first 4096 block. there are room
    // for a total of two more blocks => last pointer at -2*4096 from end
    ASSERT_EQ((uint8_t *)firstptr+up2(heap_size)-MIN_CHUNK_SIZE*2, ptr);
    ptr = balloc(size);
    ASSERT_TRUE(ptr != NULL);
    ptr = balloc(size);
    ASSERT_TRUE(ptr == NULL);
}

TEST_F(AllocTest, UserAllocAll) {
    // allocate only the first three or so to make it easier to see the chunks
    // and info blocks.
    int maxsize = up2(heap_size);
    int n = maxsize/MIN_CHUNK_SIZE;
    void **pointers = (void **)malloc(n);
    info_item_t *root = groot;

    uint32_t size = pow(2, 12)*MIN_CHUNK_SIZE;
    pointers[0] = balloc(size);

    //dump_root(groot, 1);
}

TEST_F(AllocTest, UserAllocAll16384) {
    int i, k;
    int maxsize = up2(heap_size);
    int n = maxsize/MIN_CHUNK_SIZE;
    void **pointers = (void **)malloc(n);
    info_item_t *root = groot;

    uint32_t size = 16384;
    int count = maxsize/size;
    for (i=0; i<count; i++) {
        pointers[i] = balloc(size);
        ASSERT_TRUE(pointers[i] != NULL);
    }
    //dump_root(groot, 12-k+1);
    while (i >= 0)
        bfree(pointers[i--]);
}

TEST_F(AllocTest, UserAllocAll8192) {
    int i, k;
    int maxsize = up2(heap_size);
    int n = maxsize/MIN_CHUNK_SIZE;
    void **pointers = (void **)malloc(n * sizeof(void *));
    info_item_t *root = groot;

    uint32_t size = 8192;
    int count = maxsize/size;
    for (i=0; i<count; i++) {
        if (i == 1026)
            dump_root(groot, 12);
        pointers[i] = balloc(size);
        ASSERT_TRUE(pointers[i] != NULL);
    }
    //dump_root(groot, 12-k+1);
    while (i >= 0)
        bfree(pointers[i--]);
}
TEST_F(AllocTest, UserAllocAllInEach) {
    int maxsize = up2(heap_size);
    int n = maxsize/MIN_CHUNK_SIZE;
    void **pointers = (void **)malloc(n*sizeof(void *));
    info_item_t *root = groot;

    for (int k=12, i; k>=0; k--) {
        uint32_t size = pow(2, k)*MIN_CHUNK_SIZE;
        int count = maxsize/size;
        for (i=0; i<count; i++) {
            pointers[i] = balloc(size);
            ASSERT_TRUE(pointers[i] != NULL);
        }
        void *p = balloc(size);
        ASSERT_TRUE(p == NULL);
        i--;
        while (i >= 0)
            bfree(pointers[i--]);
        dump_root(groot, 12);
    }
}

TEST_F(AllocTest, UserRandomAlloc) {
    uint32_t max_size = up2(heap_size);
    int32_t bytes_left = max_size;
    uint32_t spill = 0;
    int all[4096];

    int allocs=0;
    const int threshold = MIN_CHUNK_SIZE;//MIN_CHUNK_SIZE*2-1;
    while (bytes_left >= threshold) {
        uint32_t size = rand()%(max_size>>1);
        int32_t bytes = up2(size);
        if (size < MIN_CHUNK_SIZE)
            bytes = MIN_CHUNK_SIZE;
        if (bytes_left - bytes >= 0) {
            void *p = balloc(size);
            dump_root(groot, 12);
            if (!p) {
#ifdef DEBUGLOGGING
                fprintf(stderr, "alloc failed. spill = %lu bytes (%lu kbytes)\n", spill, spill/1024);
                for (int i=0; i<allocs; i++)
                    fprintf(stderr, "%d (%d); ", all[i], up2(all[i]));
                fprintf(stderr, "\n");
#endif
            }
            else
            {
                bytes_left -= bytes;
                all[allocs] = size;
            }
            ASSERT_TRUE(p != NULL);
            allocs++;
            spill += bytes - size;
        }
    }
#ifdef DEBUGLOGGING
    fprintf(stderr, "spill = %lu bytes (%lu kbytes)\n", spill, spill/1024);
    for (int i=0; i<allocs; i++)
        fprintf(stderr, "%d (%d); ", all[i], up2(all[i]));
    fprintf(stderr, "\n");
#endif
}

TEST_F(AllocTest, UserRandomAllocMax512K) {
    uint32_t max_size = up2(heap_size);
    int32_t bytes_left = max_size;
    uint32_t spill = 0;
    int all[4096];

    int allocs=0;
    const int threshold = MIN_CHUNK_SIZE;//MIN_CHUNK_SIZE*2-1;
    while (bytes_left >= threshold) {
        // max alloc block size = 512K = 16/32
        uint32_t size = rand()%(max_size>>5);
        int32_t bytes = up2(size);
        if (size < MIN_CHUNK_SIZE)
            bytes = MIN_CHUNK_SIZE;
        if (bytes_left - bytes >= 0) {
            void *p = balloc(size);
            if (!p) {
#ifdef DEBUGLOGGING
                fprintf(stderr, "alloc failed. spill = %lu bytes (%lu kbytes)\n", spill, spill/1024);
                for (int i=0; i<allocs; i++)
                    fprintf(stderr, "%d (%d); ", all[i], up2(all[i]));
                fprintf(stderr, "\n");
#endif
            }
            else
            {
                bytes_left -= bytes;
                all[allocs] = size;
            }
            ASSERT_TRUE(p != NULL);
            allocs++;
            spill += bytes - size;
        }
    }
#ifdef DEBUGLOGGING
    fprintf(stderr, "spill = %lu bytes (%lu kbytes)\n", spill, spill/1024);
    for (int i=0; i<allocs; i++)
        fprintf(stderr, "%d (%d); ", all[i], up2(all[i]));
    fprintf(stderr, "\n");
#endif
    dump_root(groot, 12);
}

TEST_F(AllocTest, UserRandomAllocThenFree) {
    // copy'n'paste from above
    uint32_t max_size = up2(heap_size);
    int32_t bytes_left = max_size;
    uint32_t spill = 0;
    void *all[4096];

    int allocs=0;
    const int threshold = MIN_CHUNK_SIZE;
    while (bytes_left >= threshold) {
        // max allocatable block size = 16/3 = 512 kb
        uint32_t size = rand()%(max_size>>5);
        int32_t bytes = up2(size);
        if (size < MIN_CHUNK_SIZE)
            bytes = MIN_CHUNK_SIZE;
        if (bytes_left - bytes >= 0) {
            void *p = balloc(size);
            if (!p) {
                break;
            }
            else
            {
                all[allocs] = p;
                bytes_left -= bytes;
            }
            ASSERT_TRUE(p != NULL);
            allocs++;
            spill += bytes - size;
        }
    }

    // go through the list and count used nibbles
    info_item_t *root = groot;
    int total_nibbles = 0;
    while (root) {
        chunk_item_t *c = root->free_list;
        while (c) {
            // free nibble
            if (c->flags & CI_A_USED) total_nibbles++;
            if (c->flags & CI_B_USED) total_nibbles++;

            c = c->next;
        }

        root = root->smaller;
    }

    LOG("bytes left after alloc %lu\n", bytes_left);
    dump_root(groot, 12);

    // go through the list and count free nibbles
    root = groot;
    int free_nibbles = 0;
    while (root) {
        chunk_item_t *c = root->free_list;
        while (c) {
            if (ci_a_free(c)) free_nibbles++;
            if (ci_b_free(c)) free_nibbles++;

            c = c->next;
        }

        root = root->smaller;
    }

    ASSERT_EQ(free_nibbles, 0);

    // free half
    int frees = 0;
    while (frees < allocs/2) {
        int which = rand()%allocs;
        if (all[which]) {
            void *p = all[which];
            bfree(all[which]);
            all[which] = NULL;
            frees++;

            // go through the list and count free nibbles
            root = groot;
            free_nibbles = 0;
            while (root) {
                chunk_item_t *c = root->free_list;
                while (c) {
                    if (ci_a_free(c)) free_nibbles++;
                    if (ci_b_free(c)) free_nibbles++;

                    c = c->next;
                }

                root = root->smaller;
            }

            fprintf(stderr, "after free pos %d = %p, frees: %d, free nibbles %d\n", which, p, frees, free_nibbles);

            if (free_nibbles != frees)
                dump_root(groot, 12);

            ASSERT_EQ(free_nibbles, frees);

        }
    }

    // go through the list and count free nibbles
    root = groot;
    free_nibbles = 0;
    while (root) {
        chunk_item_t *c = root->free_list;
        while (c) {
            if (ci_a_free(c)) free_nibbles++;
            if (ci_b_free(c)) free_nibbles++;

            c = c->next;
        }

        root = root->smaller;
    }

    int total_nibbles_adjusted = total_nibbles % 2 ? total_nibbles-1 : total_nibbles;
    fprintf(stderr, "total nibbles %d, adjusted %d, free nibbles %d, frees = %d\n", total_nibbles, total_nibbles_adjusted, free_nibbles, frees);
    dump_root(groot, 12);

    ASSERT_EQ(frees, free_nibbles);

    LOG("after free 50%%\n");

    ASSERT_EQ(free_nibbles, total_nibbles_adjusted/2);

    // free other half
    for (int i=0; i<allocs; i++) {
        if (all[i]) {
            bfree(all[i]);
            all[i] == NULL;
        }
    }

    LOG("after free rest\n");
    dump_root(groot, 12);

    root = groot;
    while (root) {
        chunk_item_t *c = root->free_list;
        while (c) {
            ASSERT_FALSE(c->flags & CI_A_USED);
            ASSERT_FALSE(c->flags & CI_B_USED);
            
            c = c->next;
        }

        root = root->smaller;
    }
}

TEST_F(AllocTest, UserLock) {
    memory_handle_t *handle;

    handle = Balloc(8192);
    void *p = lock(handle);
    ASSERT_TRUE(p != NULL);
    bool status = unlock(handle);
    ASSERT_TRUE(status);

    p = lock(handle);
    p = lock(handle);
    ASSERT_TRUE(p == NULL);
    status = unlock(handle);
    ASSERT_TRUE(status);

    status = unlock(handle);
    ASSERT_FALSE(status);
}

TEST_F(AllocTest, UserFragmentedAllocFail) {
    const int max_allocs = 4096;
    memory_handle_t *allocs[max_allocs];

    int count = 0;
    for (count=0; count<max_allocs; count++) {
        allocs[count] = Balloc(MIN_CHUNK_SIZE); 
        ASSERT_TRUE(allocs[count] != NULL);
    }
    dump_root(groot, 12);
    for (count=0; count<max_allocs; count += 2) {
        Bfree(allocs[count]->memory);
    }
    dump_root(groot, 12);

    memory_handle_t *h = Balloc(MIN_CHUNK_SIZE*2);
    ASSERT_TRUE(h == NULL);
}

TEST_F(AllocTest, InternalLargestBlockFree) {
}

TEST_F(AllocTest, UserFragmentedAllocSuccess) {
    const int max_allocs = 4096;
    memory_handle_t *allocs[max_allocs];

    int count = 0;
    for (count=0; count<max_allocs; count++) {
        allocs[count] = Balloc(MIN_CHUNK_SIZE); 
        ASSERT_TRUE(allocs[count] != NULL);
    }
    //dump_root(groot, 12);

    for (count=1; count<max_allocs; count+=2) {
        Bfree(allocs[count]->memory);
    }

    bcompact(1);

    memory_handle_t *h = Balloc(MIN_CHUNK_SIZE*2);
    if (h) 
        fprintf(stderr, "Test: allocated chunk of %d = %p at memory %p\n", MIN_CHUNK_SIZE*2, h->chunk, h->memory);
    ASSERT_TRUE(h != NULL && h->chunk != NULL);

    memory_handle_t *h2 = Balloc(MIN_CHUNK_SIZE*2);
    if (h2) {
        fprintf(stderr, "Test: allocated chunk that should be NULL: %p at memory %p\n", h2->chunk, h2->memory);
    }
    ASSERT_TRUE(h2 == NULL);

    bcompact(1);

    memory_handle_t *h3 = Balloc(MIN_CHUNK_SIZE*2);
    ASSERT_TRUE(h3 != NULL && h3->chunk != NULL);
}

TEST_F(AllocTest, UserFragmentedAllocHeavy) {
    const int max_allocs = 4096;
    memory_handle_t *allocs[max_allocs];

    int count = 0;
    for (count=0; count<max_allocs; count++) {
        allocs[count] = Balloc(MIN_CHUNK_SIZE); 
        ASSERT_TRUE(allocs[count] != NULL);
    }

    for (count=1; count<max_allocs; count+=2) {
        Bfree(allocs[count]->memory);
    }

    dump_root(groot, 12);

    bcompact(max_allocs>>1);

    dump_root(groot, 12);
}
#endif
