/*
 * rmalloc/main.c
 *
 * Test application.
 *
 * Mikael Jansson <mail@mikael.jansson.be>
 */
#include "rmalloc.h"

#include <stdio.h>

#ifdef ALLOC_TEST
#include "logparsed.c"
#endif

#define MEMORY_SLOTS 20

int main(void) {
    uint8_t *ptr;
    memory_t *memory[MEMORY_SLOTS];
    int i, j, n;

    fprintf(stderr, "rmalloc_init: %s\n", rmstatus(rmalloc_init()));

#ifdef ALLOC_TEST
    alloc_test();
#else
    for (i=0, j=0; i<MEMORY_SLOTS; i++) {
        n = 128*1024;
        if (RM_OK == rmalloc(&memory[j], n)) {
            rmalloc_print(memory[j]);
            if (i % 3 == 0 || i % 7 == 0)
                rmfree(memory[j]);
            j++;
        }
        else
            fprintf(stderr, "Failed allocating %d bytes with %d bytes left.\n",
                    n, rmalloc_ram_end()-rmalloc_ram_top());
    }
#endif

    rmalloc_dump();

    rmalloc_compact(NULL);

    rmalloc_dump();

    rmalloc_destroy();
}
 
