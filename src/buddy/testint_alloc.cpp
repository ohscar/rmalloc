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
    EXPECT_TRUE(false);
}

TEST_F(AllocTest, RequestChunkZeroLevel) {
    chunk_item *ci = request_chunk(groot, groot->k);
    ASSERT_TRUE(ci != NULL);
}

TEST_F(AllocTest, RequestChunkOneLevel) {
    chunk_item *ci = request_chunk(groot, groot->k - 1);
    ASSERT_TRUE(ci != NULL);
}

#if 0 
TEST_F(AllocTest, Destroy) {
    ASSERT_TRUE(groot != NULL);
    int n = up2(heap_size);
    ASSERT_EQ(groot->k, log2(n/MIN_CHUNK_SIZE));
}
#endif

