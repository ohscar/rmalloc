#include <stdlib.h>
#include <gtest/gtest.h>
#include "buddy.h"

#define MB(x) 1024*1024*x
int heap_size = MB(100);

class AllocTest : public ::testing::Test {
protected:
    AllocTest() {
        binit(heap_size);
    }
    ~AllocTest() {
        bdestroy();
    }
    void SetUp() {
    }
    void TearDown() {
    }
    
    void *storage;
};

TEST_F(AllocTest, TestAllocOK) {
    void *foo = (void *)balloc(heap_size);
    ASSERT_TRUE(foo != NULL);
    bfree(foo);
}

