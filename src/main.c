/*
 * rmalloc/main.c
 *
 * Test application.
 *
 * Mikael Jansson <mail@mikael.jansson.be>
 */
#include "rmalloc.h"

#include <stdio.h>

int main(void) {
    uint8_t *ptr;
    memory_t *memory[10];
    int i, n;

    rmalloc_init();
    for (i=0; i<10; i++) {
        n = 128*1024;
        if (RM_OK == rmalloc(&memory[i], n)) {
            rmalloc_print(memory[i]);
            rmfree(memory[i]);
        }
        else
            fprintf(stderr, "Failed allocating %d bytes with %d bytes left.\n",
                    n,
                    rmalloc_ram_end()-rmalloc_ram_top());
    }

}
 
