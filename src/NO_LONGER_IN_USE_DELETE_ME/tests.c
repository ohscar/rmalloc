#include <stdio.h>
#include "rmalloc.h"
#include "rmalloc_internal.h"

int test_shrink() {
    status_t status;
    memory_t *big_block;
    memory_t *leftover_block;
    size_t large_size = 2000;
    size_t small_size = large_size/2;

    if (RM_OK == rmalloc(&big_block, large_size)) {
        rmalloc_print(big_block);
        
        status = mb_shrink(MEMORY_AS_BLOCK(big_block), small_size, MEMORY_AS_BLOCK_OUT(leftover_block));

        rmalloc_print(big_block);
        rmalloc_print(leftover_block);
    }
}

int main(int argc, char **argv) {
    fprintf(stderr, "rmalloc_init: %s\n", rmstatus(rmalloc_init()));

    test_shrink();

    rmalloc_destroy();
}

