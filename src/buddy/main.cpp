#include "buddy.h"
#include <gtest/gtest.h>

class AllocTest : public ::testing::Test {
protected:
    AllocTest() {
        binit(MB(100));
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
    ASSERT_NE(foo, NLUL);
}

#define MB(x) 1024*1024*x

int main() {
    void *chunk;


    test_allocate_fail();
    chunk = balloc(1024);

    bdestroy();

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
