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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include <map>

#define die(x...) {printf(x); exit(1);}

typedef std::map<uint32_t, void *> handle_pointer_map_t;
typedef std::map<void *, uint32_t> pointer_size_map_t;

static handle_pointer_map_t g_handles;
static pointer_size_map_t g_sizes;
FILE *fpstats = NULL;
unsigned long long g_counter = 0;
uint32_t g_memory_usage = 0;

#define HEAP_SIZE (512 * 1024*1024)

uint32_t g_heap_size = HEAP_SIZE;
uint8_t *g_heap = NULL;
uint8_t *g_colormap = NULL;
uint32_t g_colormap_size = HEAP_SIZE/4;

void heap_colormap_init() {
    uint32_t *h = (uint32_t *)g_heap;
    for (int i=0; i<g_heap_size/4; i++) {
        h[i] = PAINT_INITIAL_COLOR;
    }

    memset(g_colormap, COLOR_GREEN, g_colormap_size);
}

void register_op(int op, int handle) {
    // ptr will be within g_heap
    void *ptr = g_handles[handle];
    uint32_t offset = (uint32_t)g_heap - (uint32_t)ptr;
    uint32_t size = g_sizes[ptr];

    int cs = ceil((float)size/4.0); 
    uint32_t co = offset/4;

    // mark area with initial as a cleanup, otherwise too many areas will be falsely marked as overhead below.
    int ps = size/4; // floor, not to overwrite data.
    uint32_t *pp = (uint32_t *)ptr;
    for (int i=0; i<ps; i++) {
        pp[i] = PAINT_INITIAL_COLOR;
    }

    uint8_t color = (op == OP_ALLOC) ? COLOR_RED : COLOR_GREEN;
    memset(g_colormap, color, ps);

    /* go through colormap, and for each pixel that is non-green and non-red,
     * look at the corresponding value in the heap.  if it is not initial color, color as overhead.
     *
     * XXX: but, overhead can be non-overhead. otoh, does it matter? b/c if it's used for memory, we will be notified
     *      about the new memory block and can color it appropriately.  so, mark as overhead to begin with!
     */
    uint32_t *vh = (uint32_t *)g_heap;
    for (int i=0; i<g_colormap_size; i++) {
        if (g_colormap[i] != COLOR_GREEN && g_colormap[i] != COLOR_RED) {
            // alright, what's the status of the heap at this position?
            if (vh[i] != PAINT_INITIAL_COLOR) 
                g_colormap[i] = COLOR_WHITE;
        }
    }

    // presto, a fresh colormap with appropriate values for green, red and white.
}

/* parses ops file and calls into user alloc functions. */
void alloc_driver(FILE *fp, int num_handles, uint8_t *heap, uint32_t heap_size, uint8_t *colormap) {
    bool done = false;
    
    int handle, address, size, old_handle=1; /* ops always start from 0 */
    char op, old_op=0;

    //frame_t *current_frame = colormap_statistics();

    while (!done && !feof(fp)) {
        char line[128];
        fgets(line, 127, fp);
        if (line[0] == '#')
            continue;

        sscanf(line, "%d %c %u %u\n", &handle, &op, &address, &size);
        // for now, don't care about the difference between load/modify/store
        if (op == 'L' || op == 'M' || op == 'S')
            op = 'A';

        if (handle == old_handle && op == 'A' && old_op == 'A') {
            // skip
        } else {

            if (op == 'N' || op == 'F')
                printf("%c handle %d of size %d\n", op, handle, size);
            switch (op) {
                case 'O': // Open = lock
                    
                    // XXX: when and how to do the color map diffs?
                    // result should be stored in a frame, but how do we get the data?
                    //colormap_paint(colormap);

                    user_lock(g_handles[handle]);
                    break;
                case 'C': // Close = unlock
                    user_unlock(g_handles[handle]);
                    break;
                case 'A':
                    user_lock(g_handles[handle]);
                    user_unlock(g_handles[handle]);
                    break;
                case 'N': {
                    void *ptr = user_malloc(size, handle);
                    g_handles[handle] = ptr;
                    g_sizes[g_handles[handle]] = size;

                    if (ptr == NULL) {
                        if (user_handle_oom(size)) {
                            g_handles[handle] = user_malloc(size, handle);
                            g_sizes[g_handles[handle]] = size;
                            if (!g_handles[handle]) {
                                die("\n\nOOM!\n");
                            }
                        } else {
                            die("\n\nOOM!\n");
                        }
                    } else {
                        //register_op(OP_ALLOC, handle);
                    }
                    print_after_malloc_stats(g_handles[handle], address, size);
                } break;
                case 'F': {
                    void *ptr = g_handles[handle];
                    user_free(ptr, handle);
                    print_after_free_stats(address, size);
                } break;
            }
        }
    }

}

void plot_report(long memory_delta, unsigned long free_bytes, unsigned long largest_allocatable_block, int fragmentation_percent, suseconds_t op_time, int caused_oom) {
    g_memory_usage += memory_delta;

    fprintf(fpstats, "%llu MD%ld FB%lu LAB%lu FP%d T%lu OOM%d\n", g_counter, memory_delta, free_bytes, largest_allocatable_block, fragmentation_percent, op_time, caused_oom);
    g_counter++;
}

int main(int argc, char **argv) {
    FILE *fpops = NULL;
    char *opsfile = NULL;
    char driver[512];
    g_heap = (uint8_t *)malloc(g_heap_size);
    g_colormap = (uint8_t *)malloc(g_colormap_size);

    if (argc != 2) {
        die("usage: %s opsfile\n", argv[0])
    }

    opsfile = argv[1];
    fpops = fopen64(opsfile, "rt");
    if (!fpops) {
        die("%s: couldn't open opsfile %s: strerror() = %s\n", argv[0], opsfile, strerror(errno));
    }

    heap_colormap_init();
    
    user_init(g_heap_size, (void *)g_heap, (void *)g_colormap, driver);
    strcat(driver, ".alloc-stats");

    fpstats = fopen(driver, "wt");
    fprintf(fpstats, "# Format of header:\n");
    fprintf(fpstats, "# HS<heap size>\n");
    fprintf(fpstats, "# Format of each op line:\n");
    fprintf(fpstats, "# <counter> MD<memory delta> FB<free bytes> LAB<largest allocatable block> FP<fragmentation percent> T<operation time> OOM<caused oom?>\n");
    fprintf(fpstats, "HS%u\n", g_heap_size);

    alloc_driver(fpops, 500*1000, g_heap, g_heap_size, g_colormap);
    user_destroy();

    fclose(fpstats);
    fclose(fpops);

    free(g_colormap);
    free(g_heap);

    printf("pure memory usage: %d bytes = %d kbytes = %d megabytes\n",
            g_memory_usage, g_memory_usage/1024, g_memory_usage/1048576);
    printf("writing alloc stats data to %s\n", driver);
}

