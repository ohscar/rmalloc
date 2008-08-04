#include <stdio.h>
#include "rmalloc.h"
#include "rmalloc_internal.h"

int test_shrink() {
    
    memory_t *big_block;
    memory_t *leftover_block;

    if (RM_OK == rmalloc(&big_block, 2000)) {
        rmalloc_print(big_block);
    }
}

int main(int argc, char **argv) {
    fprintf(stderr, "rmalloc_init: %s\n", rmstatus(rmalloc_init()));

    test_shrink();

    rmalloc_destroy();
}

