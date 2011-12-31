#include <stdlib.h>
#include <gtest/gtest.h>
#include "buddy.h"
#include "buddy.c" // to get implementation specific details

#define MB(x) 1024*1024*x
int heap_size = MB(10);

class AllocTest : public ::testing::Test {
protected:
    AllocTest() {
    }
    ~AllocTest() {
    }
    void SetUp() {
        binit(heap_size);
    }
    void TearDown() {
        bdestroy();
    }
    
    void *storage;
};

TEST_F(AllocTest, Init) {
    ASSERT_TRUE(groot != NULL);
    int n = up2(heap_size);
    //ASSERT_EQ(groot->k, log2(n/MIN_CHUNK_SIZE));
}

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

int chunk_count(chunk_item_t *ci) {
    if (!ci)
        return 0;
    else
        return 1 + chunk_count(ci->next);
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

    // adjacent nibbles, i.e. ci's A and B
    merge_nibbles(groot->smaller, ci, ci);

    ASSERT_EQ(groot->smaller->free_list->flags, 0);
    ASSERT_EQ(ci->a, (void *)NULL);
    ASSERT_EQ(ci->b, (void *)NULL);
}

TEST_F(AllocTest, Destroy) {
    //EXPECT_TRUE(false);
}

