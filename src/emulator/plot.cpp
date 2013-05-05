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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include <map>

#define die(x...) {printf(x); exit(1);}

typedef std::map<uint32_t, void *> handle_pointer_map_t;
typedef std::map<void *, uint32_t> pointer_size_map_t;
typedef std::map<uint32_t, uint32_t> handle_count_map_t;

static handle_pointer_map_t g_handles;
static pointer_size_map_t g_sizes;

static handle_count_map_t g_handle_free;
static pointer_size_map_t g_pointer_free;
static handle_count_map_t g_free_block_count;
static handle_count_map_t g_used_block_count;

FILE *fpstats = NULL;
unsigned long long g_counter = 0;
uint32_t g_memory_usage = 0;

#define HEAP_SIZE (512  * 1024*1024)

uint32_t g_heap_size = HEAP_SIZE;
uint8_t *g_heap = NULL;
uint8_t *g_colormap = NULL;
uint32_t g_colormap_size = HEAP_SIZE/4;

uint8_t *g_highest_address = 0;

char *g_opsfile = NULL;

void scan_block_sizes(void);
int colormap_print(char *output);
void calculate_fragmentation_percent(void);

/*
 * scan through the colormap for blocks and update the free/used block map accordingly.
 */
void scan_block_sizes(void) {
    int start = 0, end = 0;
    bool inside = false;
    uint8_t color = COLOR_WHITE;
    int length = 0;
    for (int i=0; i<g_colormap_size; i++) {
        if (g_colormap[i] != color) {
            if (!inside) {
                inside = true;
                start = i;
                color = g_colormap[i];
            } else {
                inside = false;
                length = i-start;

                if (color == COLOR_GREEN) {
                    if (g_free_block_count.find(length) == g_free_block_count.end())
                        g_free_block_count[length] = 1;
                    else
                        g_free_block_count[length] += 1;
                } else if (color == COLOR_RED) {
                    if (g_used_block_count.find(length) == g_used_block_count.end())
                        g_used_block_count[length] = 1;
                    else
                        g_used_block_count[length] += 1;
                }

                color = g_colormap[i];
            }
        }
    }
}

void calculate_fragmentation_percent(void) {
    scan_block_sizes();

    {
    printf("\nBlock statistics.\n");
    handle_count_map_t::iterator it;
    for (it=g_free_block_count.begin(); it != g_free_block_count.end(); it++)
        printf("Free block size %d has %d items\n", it->first, it->second);
    printf("\n");
    for (it=g_used_block_count.begin(); it != g_used_block_count.end(); it++)
        printf("Used block size %d has %d items\n", it->first, it->second);
    }

    /*
     * (1 - S / Sn) * Ps
     *
     * S = block size
     * Sn = total number of units as a multiple of the smallest block size
     * Ps = contribution of block size <s> to whole.
     *
     * Ps = Ns/Sn, Ns = number of units of size <s>
     * e.g. Sn = 20, s = 5, T5 = 3, N5 = 15 => Ps = 15/20 = 0.75
     */
    uint32_t smallest_block_size = UINT_MAX;
    double sum = 0;
    {
    handle_count_map_t::iterator it;
    for (it=g_free_block_count.begin(); it != g_free_block_count.end(); it++) {
        if (it->first < smallest_block_size)
            smallest_block_size = it->first;
        sum += (it->first * it->second);
    }
    }

    double Sn = sum/smallest_block_size;
    handle_count_map_t N;

    {
    handle_count_map_t::iterator it;
    for (it=g_free_block_count.begin(); it != g_free_block_count.end(); it++) {
        N[it->first] = it->first * it->second;
    }
    }
    
    handle_count_map_t F;
    double total_frag = 0;
    {
    handle_count_map_t::iterator it;
    for (it=N.begin(); it != N.end(); it++) {
        double S = it->first;
        double Ns = it->second;
        double Ps = Ns / Sn; // Ps = Ns / Sn
        double f = (1.0 - (double)S/Sn) * Ps;
        F[it->first] = (int)(f*10000.0);
        total_frag += f;
    }
    }

    {
    printf("\nFragmentation statistics: %u free units.\n", (uint32_t)sum);
    handle_count_map_t::iterator it;

    for (it=g_free_block_count.begin(); it != g_free_block_count.end(); it++)
        printf("Block size %d (block sum %d from %d blocks, of total %d units) contributes to fragmentation by %.2f%%\n",
                it->first,
                N[it->first],
                g_free_block_count[it->first],
                (uint32_t)Sn,
                (float)F[it->first] / 100.0);

    printf("\nTotal fragmentation: %.2f%%\n", total_frag*100.0);
    }

}

void heap_colormap_init() {
    uint32_t *h = (uint32_t *)g_heap;
    for (int i=0; i<g_heap_size/4; i++) {
        h[i] = PAINT_INITIAL_COLOR;
    }

    // keep this in sync w/ plot_fragmetn_image
    memset(g_colormap, COLOR_WHITE, g_colormap_size);
}

void register_op(int op, int handle, void *ptr, int ptrsize) {
    // ptr will be within g_heap
    uint32_t offset = (uint32_t)ptr - (uint32_t)g_heap;
    uint32_t size = g_sizes[ptr];

    int cs = ceil((float)size/4.0); 
    uint32_t co = offset/4;

    // mark area with initial as a cleanup, otherwise too many areas will be falsely marked as overhead below.
    int ps = size/4; // floor, not to overwrite data.
    uint32_t *pp = (uint32_t *)ptr;
    uint8_t *p = (uint8_t *)ptr;

    //printf("marking handle %d ptr %x of size %d (and small size: %d)\n", handle, (uint32_t)ptr, size, ps);

    uint32_t heap_fill = (op == OP_ALLOC) ? HEAP_ALLOC : HEAP_FREE; 
    for (int i=0; i<ps; i++)
        pp[i] = heap_fill;

    uint8_t color = (op == OP_ALLOC) ? COLOR_RED : COLOR_GREEN;
    memset((void *)((uint32_t)g_colormap+co), color, ps);
}

void scan_heap_update_colormap() {
    static int sequence = 0;

#if 0 // METHOD 1
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
#endif
#if 1 // METHOD 2
    uint32_t *vh = (uint32_t *)g_heap;
    for (int i=0; i<g_colormap_size; i++) {
        if (vh[i] != HEAP_INITIAL &&
            vh[i] != HEAP_ALLOC &&
            vh[i] != HEAP_FREE)

            // well, it's got to be changed then.
            g_colormap[i] = COLOR_WHITE;
    }
#endif

    // presto, a fresh colormap with appropriate values for green, red and white.
    char buf[256];
    snprintf(buf, sizeof(buf), "%s-plot-%.6d.png", g_opsfile, sequence++);
    colormap_print(buf);
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
                    printf("NEW handle %d of size %d to 0x%X\n", handle, size, (uint32_t)ptr);
                    if ((uint32_t)ptr > (uint32_t)g_highest_address)
                        g_highest_address = (uint8_t *)ptr;

                    g_handles[handle] = ptr;
                    g_sizes[g_handles[handle]] = size;

                    // XXX when to call register_op() and do coloring?
                    if (ptr == NULL) {
                        if (user_handle_oom(size)) {
                            ptr = user_malloc(size, handle);
                            g_handles[handle] = ptr;
                            register_op(OP_ALLOC, handle, ptr, size);
                            g_sizes[g_handles[handle]] = size;
                            if (!g_handles[handle]) {
                                die("\n\nOOM!\n");
                            }
                        } else {
                            die("\n\nOOM!\n");
                        }
                    } else {
                        register_op(OP_ALLOC, handle, ptr, size);
                    }
                    scan_heap_update_colormap();
                    print_after_malloc_stats(g_handles[handle], address, size);
                } break;
                case 'F': {
                    void *ptr = g_handles[handle];
                    int s = g_sizes[g_handles[handle]];
                    printf("FREE handle %d of size %d at 0x%X\n", handle, s, (uint32_t)ptr);

                    register_op(OP_FREE, handle, ptr, s);
                    user_free(ptr, handle);
                    scan_heap_update_colormap();

                    print_after_free_stats(address, s);
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

int colormap_print(char *output) {
    int end = ((uint32_t)g_highest_address-(uint32_t)g_heap)/4;
    //int end = g_colormap_size;
#define putchar(x) (void)x
    putchar("\n"); putchar('[');
    FILE *f = fopen("/tmp/fragmentplot.txt", "wt");
    for (int i=0; i<end; i++) {
        switch (g_colormap[i]) {
        case COLOR_GREEN: fputc(CHAR_GREEN, f); putchar(CHAR_GREEN); break;
        case COLOR_RED: fputc(CHAR_RED, f); putchar(CHAR_RED); break;
        case COLOR_WHITE: fputc(CHAR_WHITE, f); putchar(CHAR_WHITE); break;
        default: break;
        }
    }
    putchar(']'); putchar("\n"); 
#undef putchar
    fclose(f);

    char cmd[256];
    sprintf(cmd, "python plot_fragment_image.py /tmp/fragmentplot.txt %s", output);
    system(cmd);
    //printf("Plot data saved in %s\n", output);
}

int main(int argc, char **argv) {
    FILE *fpops = NULL;
    char driver[512];
    g_heap = (uint8_t *)malloc(g_heap_size);
    g_colormap = (uint8_t *)malloc(g_colormap_size);

    if (argc != 2) {
        die("usage: %s opsfile\n", argv[0])
    }

    g_opsfile = argv[1];
    fpops = fopen64(g_opsfile, "rt");
    if (!fpops) {
        die("%s: couldn't open opsfile %s: strerror() = %s\n", argv[0], g_opsfile, strerror(errno));
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


    printf("pure memory usage: %d bytes = %d kbytes = %d megabytes\n",
            g_memory_usage, g_memory_usage/1024, g_memory_usage/1048576);
    printf("writing alloc stats data to %s\n", driver);



    {
    pointer_size_map_t::iterator it;
    for (it=g_pointer_free.begin(); it != g_pointer_free.end(); it++) 
        if (it->second != 1)
            printf("Pointer %x free'd %d times\n", (uint32_t)it->first, it->second);
    }
    {
    handle_count_map_t::iterator it;
    for (it=g_handle_free.begin(); it != g_handle_free.end(); it++) 
        if (it->second != 1)
            printf("Handle %d free'd %d times\n", it->first, it->second);
    }

    printf("highest address: 0x%X adjusted for heap start = %d kb\n", (uint32_t)g_highest_address, ((uint32_t)g_highest_address - (uint32_t)g_heap) / 1024);


    {
    printf("\nBlock statistics.\n");
    handle_count_map_t::iterator it;
    for (it=g_free_block_count.begin(); it != g_free_block_count.end(); it++)
        printf("Free block size %d has %d items\n", it->first, it->second);
    printf("\n");
    for (it=g_used_block_count.begin(); it != g_used_block_count.end(); it++)
        printf("Used block size %d has %d items\n", it->first, it->second);
    }


    calculate_fragmentation_percent();

















    fclose(fpstats);
    fclose(fpops);

    free(g_colormap);
    free(g_heap);
}

