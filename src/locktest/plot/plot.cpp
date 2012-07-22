/* plot.cpp
 *
 * uses plot_common.h, plot_<allocator>.c
 *
 * driver for loading alloc data and printing useful output (python format, for post-processing.)
 *
 * input format:
 * <handle> <op> <address> <size>
 * handle ::= {integer}
 * op ::= {F, N, S, L, M} (F = free, N = new, SLM = access} 
 * address ::= {integer}
 *
 * duplicate entries (two or more successive SLM on the same handle) are discarded.
 */
#include "plot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define die(x...) {printf(x); exit(1);}

typedef void memory_t;

memory_t *handles[500*1000];
FILE *fpstats = NULL;
unsigned long long g_counter = 0;

/* parses ops file and calls into user alloc functions. */
void alloc_driver(FILE *fp, int num_handles) {
    bool done = false;
    
    int handle, address, size, old_handle=1; /* ops always start from 0 */
    char op, old_op=0;
    while (!done && !feof(fp)) {
        fscanf(fp, "%d %c %lu %lu\n", &handle, &op, &address, &size); 
        // for now, don't care about the difference between load/modify/store
        if (op == 'L' || op == 'M' || op == 'S')
            op = 'A';

        if (handle == old_handle && op == 'A' && old_op == 'A') {
            // skip
        } else {
            switch (op) {
                case 'O': // Open = lock
                    user_lock(handles[handle]);
                    break;
                case 'C': // Close = unlock
                    user_unlock(handles[handle]);
                    break;
                case 'A':
                    user_lock(handles[handle]);
                    user_unlock(handles[handle]);
                    break;
                case 'N':
                    handles[handle] = user_malloc(size);
                    if (!handles[handle]) {
                        if (user_handle_oom(size)) {
                            handles[handle] = user_malloc(size);
                            if (!handles[handle]) {
                                die("\n\nOOM!\n");
                            }
                        } else {
                            die("\n\nOOM!\n");
                        }
                    }
                    print_after_malloc_stats(handles[handle], address, size);
                    break;
                case 'F':
                    user_free(handles[handle]);
                    print_after_free_stats(address, size);
                    break;
            }
        }
    }

}

void plot_report(long memory_delta, unsigned long free_bytes, unsigned long largest_allocatable_block, int fragmentation_percent, suseconds_t op_time, int caused_oom) {
    fprintf(fpstats, "#%llu MD%ld FB%lu LAB%lu FP%d T%llu OOM%d\n", g_counter, memory_delta, free_bytes, largest_allocatable_block, fragmentation_percent, op_time, caused_oom);
    g_counter++;
}

int main(int argc, char **argv) {
    FILE *fpops = NULL;
    char *opsfile = NULL;
    char driver[512];
    unsigned int heap_size = 2048*1024*1024;

    if (argc != 2) {
        die("usage: %s opsfile\n", argv[0])
    }

    opsfile = argv[1];
    fpops = fopen64(opsfile, "rt");
    if (!fpops) {
        die("%s: couldn't open opsfile %s: strerror() = %s\n", argv[0], opsfile, strerror(errno));
    }
    
    user_init(heap_size, driver);
    strcat(driver, ".alloc-stats");

    fpstats = fopen(driver, "wt");
    fprintf(fpstats, "HS%lu\n", heap_size);

    alloc_driver(fpops, 500*1000);
    user_destroy();

    fclose(fpstats);
    fclose(fpops);
}

