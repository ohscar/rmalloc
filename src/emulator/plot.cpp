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

#include <tuple>
#include <vector>
#include <map>

// exit codes:
// 0 = ok, 
// 1 = unused,
// 2 = oom
#define die(x...) {printf(x); exit(2);}

typedef std::map<uint32_t, void *> handle_pointer_map_t;
typedef std::map<void *, uint32_t> pointer_size_map_t;
typedef std::map<uint32_t, uint32_t> handle_count_map_t;
typedef std::map<uint32_t, float> operation_percent_map_t;

static handle_pointer_map_t g_handles;
static pointer_size_map_t g_sizes;

static handle_count_map_t g_handle_free;
static pointer_size_map_t g_pointer_free;
static handle_count_map_t g_free_block_count;
static handle_count_map_t g_used_block_count;
static handle_count_map_t g_overhead_block_count;

static operation_percent_map_t g_fragmentation;

// <sum(used), sum(free), sum(overhead), fragmentation%> for fragmentation
// <sum(used), sum(free), sum(overhead), maxmem> for maxmem
// status of the allocator at a specific time.
typedef std::tuple<int, int, int, float> alloc_time_stat_t;
typedef std::vector<alloc_time_stat_t> alloc_stat_t;

alloc_stat_t g_alloc_stat;
alloc_stat_t g_maxmem_stat;

FILE *fpstats = NULL;
unsigned long long g_counter = 0;
uint32_t g_memory_usage = 0;


enum {
    OPMODE_FRAGMENTATION, // produce fragmentation graphs
    OPMODE_MAXMEM, // produce graph of max alloc'able mem at each point in time
    OPMODE_PEAKMEM, // print largest memory address minus g_heap
};

uint8_t g_operation_mode;
uint32_t g_oplimit = 0; // ./plot_foo --maxmem result.app-ops <n> # oplimit, 0 initial, then 1..N.


#define HEAP_SIZE (64  * 1024*1024)


uint32_t g_heap_size = HEAP_SIZE;
uint8_t *g_heap = NULL;
uint8_t *g_colormap = NULL;
uint32_t g_colormap_size = HEAP_SIZE/4;

uint8_t *g_highest_address = 0;

char *g_opsfile = NULL;

void scan_block_sizes(void);
int colormap_print(char *output);
void calculate_fragmentation_percent(uint8_t op);


uint32_t g_total_memory_consumption = 0;
uint32_t g_theoretical_free_space; // calculated by scan_block_sizes
uint32_t g_theoretical_used_space; // calculated by scan_block_sizes
uint32_t g_theoretical_overhead_space; // calculated by scan_block_sizes

/*
 * scan through the colormap for blocks and update the free/used block map accordingly.
 */
void scan_block_sizes(void) {
    int start = 0, end = 0;
    bool inside = false;
    uint8_t color = COLOR_WHITE;
    int block_size = 0;

    g_theoretical_free_space = 0;
    g_theoretical_used_space = 0;

    g_free_block_count.clear();
    g_used_block_count.clear();
    g_overhead_block_count.clear();

    for (int i=0; i<g_colormap_size; i++) {
        if (g_colormap[i] != color) {
            if (!inside) {
                inside = true;
                start = i;
                color = g_colormap[i];
            } else {
                inside = false;
                block_size = i-start;

                if (color == COLOR_GREEN) {
                    if (g_free_block_count.find(block_size) == g_free_block_count.end())
                        g_free_block_count[block_size] = 1;
                    else
                        g_free_block_count[block_size] += 1;
                    g_theoretical_free_space += block_size;

                } else if (color == COLOR_RED) {
                    if (g_used_block_count.find(block_size) == g_used_block_count.end())
                        g_used_block_count[block_size] = 1;
                    else
                        g_used_block_count[block_size] += 1;
                    g_theoretical_used_space += block_size;
                } else if (color == COLOR_WHITE) {
                    if (g_overhead_block_count.find(block_size) == g_overhead_block_count.end())
                        g_overhead_block_count[block_size] = 1;
                    else
                        g_overhead_block_count[block_size] += 1;
                    g_theoretical_overhead_space += block_size;
                }

                color = g_colormap[i];
            }
        }
    }


    // The code below is used only by --maxmem

    // XXX: heap_size should be the highest address, i.e. double run, in order to properly calculate the theoretical free space
    // Doing g_highest_address - g_heap does _not_ work -- it will give a zero difference.
    //g_theoretical_free_space = (g_highest_address-g_heap) - g_theoretical_used_space - g_theoretical_overhead_space;

    //g_theoretical_free_space = g_heap_size - g_theoretical_used_space - g_theoretical_overhead_space;

    g_theoretical_free_space = g_total_memory_consumption - g_theoretical_used_space - g_theoretical_overhead_space;
}


void calculate_fragmentation_percent(uint8_t op) {
    static int sequence = 0;

    // Graph looks the same even if the free op isn't logged, 
    // since it's performed anyway.  What if it's not performed? That would look strange.
    // Or, perform it but don't plot throughs? Hmm.
    //if (op == OP_FREE) return;

    /*

    {
    printf("\nBlock statistics.\n");
    handle_count_map_t::iterator it;
    for (it=g_free_block_count.begin(); it != g_free_block_count.end(); it++)
        printf("Free block size %d has %d items\n", it->first, it->second);
    printf("\n");
    for (it=g_used_block_count.begin(); it != g_used_block_count.end(); it++)
        printf("Used block size %d has %d items\n", it->first, it->second);
    }
    */

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
    double sum_free = 0, sum_used = 0, sum_overhead = 0;
    {
    handle_count_map_t::iterator it;
    for (it=g_free_block_count.begin(); it != g_free_block_count.end(); it++) {
        if (it->first < smallest_block_size)
            smallest_block_size = it->first;
        sum_free += (it->first * it->second);
    }
    }

    {
    handle_count_map_t::iterator it;
    for (it=g_used_block_count.begin(); it != g_used_block_count.end(); it++) {
        sum_used += (it->first * it->second);
    }
    }

    {
    handle_count_map_t::iterator it;
    for (it=g_overhead_block_count.begin(); it != g_overhead_block_count.end(); it++) {
        sum_overhead += (it->first * it->second);
    }
    }


    // XXX: Which is most correct? Size of one unit, i.e. smallest_block_size, /usually/ ends up being 1, but maybe not always.
    // Smallest block size is also 4 bytes, which is silly, but that's apparently what clients allocate...
    //double Sn = sum/smallest_block_size;

    // XXX: Should sum be the _total_ number of units, including overhead and used? Now it's only free.
    double Sn = sum_free;

    handle_count_map_t N;

    {
    handle_count_map_t::iterator it;
    for (it=g_free_block_count.begin(); it != g_free_block_count.end(); it++) {
        N[it->first] = it->first * it->second;
    }
    }
    
    /*  Simple calculation #1
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
    */

    // Simple calculation #2:
    //  (S*Ns) / T
    //
    // Geometric average, i.e. * instead of +
    handle_count_map_t F;
    long double total_frag = 1.;
    long double total_frag_harm = 0.;
    uint32_t count = 0;
    {
    handle_count_map_t::iterator it;
    for (it=N.begin(); it != N.end(); it++) {
        double S = it->first;

        // XXX: "N" = number of units of size S. But it->second is total size for size S!
        // test g_free_block_count instead.
        //double Ns = it->second;
        //double f = (S * Ns) / sum_free;

        double f = it->second / sum_free;
        
        //double Ps = Ns / Sn; // Ps = Ns / Sn
        //double f = (1.0 - (double)S/Sn) * Ps;


        F[it->first] = (int)(f*10000.0);
        total_frag *= f;
        total_frag_harm += f;
        count += 1;
    }
    }

    /*
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

    */

    // XXX: Add this to report! Harmonic was tried but did not look very good. Very "jagged" (up, down, up, down) graph.

    // Simple calculation #1 (harmonic)
    //g_fragmentation[sequence] = total_frag * 100.0;
    
    // Simple calculation #2 (geometric)
    //long double frag = pow(total_frag, (long double)1.0/(long double)count) * 100.0;
    long double p = pow(total_frag, (long double)1.0/(long double)count);
    long double frag = (1.0 - p) * 100.0;

    g_fragmentation[sequence] = frag;
    if (isinf(frag) == 1 || isinf(frag) == -1)
        fprintf(stderr, "Fragmentation at %4d = inf, from total_frag = %Lf, count = %Lf, 1/count = %Lf\n",
                sequence, total_frag, (long double)count, (long double)1.0/(long double)count);

    fprintf(stderr, "Fragmentation at %4d = %.2Lf %%, total frag = %Lf, total_frag_harm = %Lf\n", sequence, frag, total_frag, total_frag_harm);

    g_alloc_stat.push_back(alloc_time_stat_t(sum_free, sum_used, sum_overhead, frag));

    sequence++;

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

void scan_heap_update_colormap(bool create_plot) {
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

    if (create_plot) {
        // presto, a fresh colormap with appropriate values for green, red and white.
        char buf[256];
        snprintf(buf, sizeof(buf), "%s-plot-%.6d.png", g_opsfile, sequence++);
        colormap_print(buf);
    }
}

/* parses ops file and calls into user alloc functions. */
void alloc_driver_fragmentation(FILE *fp, int num_handles, uint8_t *heap, uint32_t heap_size, uint8_t *colormap) {
    bool done = false;
    
    int handle, address, size, old_handle=1; /* ops always start from 0 */
    char op, old_op=0;
    uint32_t op_time;

    //frame_t *current_frame = colormap_statistics();

    while (!done && !feof(fp)) {
        char line[128];
        char *r = fgets(line, 127, fp);
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
                    //putchar('.');
                    void *ptr = user_malloc(size, handle, &op_time);
                    //fprintf(stderr, "NEW handle %d of size %d to 0x%X\n", handle, size, (uint32_t)ptr);
                    if ((uint32_t)ptr > (uint32_t)g_highest_address)
                        g_highest_address = (uint8_t *)ptr;

                    g_handles[handle] = ptr;
                    g_sizes[g_handles[handle]] = size;

                    // XXX when to call register_op() and do coloring?
                    if (ptr == NULL) {
                        if (user_handle_oom(size)) {
                            ptr = user_malloc(size, handle, &op_time);
                            if (NULL == ptr) {
                                die("\n\nOOM!\n");
                            }
                            g_handles[handle] = ptr;
                            register_op(OP_ALLOC, handle, ptr, size);
                            g_sizes[g_handles[handle]] = size;
                        } else {
                            die("\n\nOOM!\n");
                        }
                    } else {
                        register_op(OP_ALLOC, handle, ptr, size);
                    }
                    scan_heap_update_colormap(true);
                    print_after_malloc_stats(g_handles[handle], address, size);

                    scan_block_sizes();
                    calculate_fragmentation_percent(op);
                } break;
                case 'F': {
                    //putchar('.');
                    void *ptr = g_handles[handle];
                    int s = g_sizes[g_handles[handle]];
                    //fprintf(stderr, "FREE handle %d of size %d at 0x%X\n", handle, s, (uint32_t)ptr);

                    register_op(OP_FREE, handle, ptr, s);
                    user_free(ptr, handle, &op_time);
                    scan_heap_update_colormap(true);

                    print_after_free_stats(address, s);

                    scan_block_sizes();
                    calculate_fragmentation_percent(op);
                } break;
            }
        }
    }
}

/*
 * try allocating as large a block as theoretically possible,
 * decreasing a a step size each time.
 */
#define MAXMEM_STEP 8 // bytes

uint32_t calculate_maxmem(uint8_t op) {
    int32_t size = g_theoretical_free_space;
    //int32_t size = g_theoretical_free_space * 10;
    uint32_t handle = g_theoretical_free_space;

    void *p = NULL;
    while (size > 0) {
        uint32_t dummy_op_time;
        p = user_malloc(size, handle, &dummy_op_time);
        if (p != NULL)
            break;
        size -= MAXMEM_STEP;
        handle--;
    }

    if (size < 0)
        p = NULL;

    return p == NULL ? 0 : size;
}

void alloc_driver_maxmem(FILE *fp, int num_handles, uint8_t *heap, uint32_t heap_size, uint8_t *colormap) {
    bool done = false;
    
    int handle, address, size, old_handle=1; /* ops always start from 0 */
    char op, old_op=0;
    uint32_t op_time;

    // For each op, try to allocate as large a block as possible.
    // Then, go to next op.
    uint32_t current_op = 0;

    while (!done && !feof(fp)) {
        bool was_oom = false;
        char line[128];
        char *r = fgets(line, 127, fp);
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
                    //putchar('.');
                    void *ptr = user_malloc(size, handle, &op_time);
                    //fprintf(stderr, "NEW handle %d of size %d to 0x%X\n", handle, size, (uint32_t)ptr);
                    if ((uint32_t)ptr > (uint32_t)g_highest_address)
                        g_highest_address = (uint8_t *)ptr;

                    g_handles[handle] = ptr;
                    g_sizes[g_handles[handle]] = size;

                    // XXX when to call register_op() and do coloring?
                    if (ptr == NULL) {
                        if (user_handle_oom(size)) {
                            ptr = user_malloc(size, handle, &op_time);
                            if (NULL == ptr) {
                                was_oom = true;
                            } else {
                                g_handles[handle] = ptr;
                                register_op(OP_ALLOC, handle, ptr, size);
                                g_sizes[g_handles[handle]] = size;
                            }
                        } else {
                            was_oom = true;
                        }
                    } else {
                        register_op(OP_ALLOC, handle, ptr, size);
                    }
                    print_after_malloc_stats(g_handles[handle], address, size);

                    current_op++;
                    if (current_op > g_oplimit) {
                        // do calculations
                        scan_heap_update_colormap(false);
                        scan_block_sizes();
                        fprintf(stderr, "Op #%d: Largest block from %'6u kb theoretical: ", current_op-1, g_theoretical_free_space/1024);
                        
                        // XXX: Do something with was_oom here?
                        was_oom = false;

                        uint32_t maxsize = 0;
                        if (was_oom == false)
                            maxsize = calculate_maxmem(op);
                        else
                            fprintf(stderr, "\n\nOOM!\n");
                        fprintf(stderr, "maxmem: %9u bytes (%6u kbytes = %3.2f%%)\n", 
                                maxsize, (int)maxsize/1024, 100.0 * (float)maxsize/(float)g_theoretical_free_space);

                        g_maxmem_stat.push_back(alloc_time_stat_t(
                            g_theoretical_free_space, g_theoretical_used_space, g_theoretical_overhead_space, maxsize));
                        return;
                    }

                } break;
                case 'F': {
                    //putchar('.');
                    void *ptr = g_handles[handle];
                    int s = g_sizes[g_handles[handle]];
                    //fprintf(stderr, "FREE handle %d of size %d at 0x%X\n", handle, s, (uint32_t)ptr);

                    register_op(OP_FREE, handle, ptr, s);
                    user_free(ptr, handle, &op_time);

                    print_after_free_stats(address, s);

                    current_op++;
                    if (current_op > g_oplimit) {
                        // do calculations
                        scan_heap_update_colormap(false);
                        scan_block_sizes();
                        fprintf(stderr, "Op #%d: Largest block from %'6u kb theoretical: ", current_op-1, g_theoretical_free_space/1024);

                        // FIXME: base g_theoretical_free_space on g_highest_address-g_heap, not g_heap_size!
                        // Otherwise, the difference will be too small in the graph, because g_heap_size can be very large.

                        //uint32_t maxsize = g_theoretical_free_space; //= calculate_maxmem(op);
                        uint32_t maxsize = calculate_maxmem(op);

                        fprintf(stderr, "%9u bytes (%6u kbytes = %3.2f%%)\n", 
                                maxsize, (int)maxsize/1024, 100.0 * (float)maxsize/(float)g_theoretical_free_space);

                        g_maxmem_stat.push_back(alloc_time_stat_t(g_theoretical_free_space,
                                    g_theoretical_used_space, g_theoretical_overhead_space, maxsize));
                        return;
                    }

                } break;
            }
        }
    }
}

void alloc_driver_peakmem(FILE *fp, int num_handles, uint8_t *heap, uint32_t heap_size, uint8_t *colormap) {
    bool done = false;
    
    int handle, address, size, old_handle=1; /* ops always start from 0 */
    char op, old_op=0;
    uint32_t op_time;

    // For each op, try to allocate as large a block as possible.
    // Then, go to next op.
    uint32_t current_op = 0;

    while (!done && !feof(fp)) {
        char line[128];
        char *r = fgets(line, 127, fp);
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
                    //putchar('.');
                    void *ptr = user_malloc(size, handle, &op_time);
                    //fprintf(stderr, "NEW handle %d of size %d to 0x%X\n", handle, size, (uint32_t)ptr);
                    if ((uint32_t)ptr > (uint32_t)g_highest_address)
                        g_highest_address = (uint8_t *)ptr;

                    g_handles[handle] = ptr;
                    g_sizes[g_handles[handle]] = size;

                    // XXX when to call register_op() and do coloring?
                    if (ptr == NULL) {
                        if (user_handle_oom(size)) {
                            ptr = user_malloc(size, handle, &op_time);
                            if (NULL == ptr) {
                                die("\n\nOOM!\n");
                            }
                            if ((uint32_t)ptr > (uint32_t)g_highest_address)
                                g_highest_address = (uint8_t *)ptr;
                            g_handles[handle] = ptr;
                            register_op(OP_ALLOC, handle, ptr, size);
                            g_sizes[g_handles[handle]] = size;
                        } else {
                            die("\n\nOOM!\n");
                        }
                    } else {
                        register_op(OP_ALLOC, handle, ptr, size);
                    }

                } break;
                case 'F': {
                    //putchar('.');
                    void *ptr = g_handles[handle];
                    int s = g_sizes[g_handles[handle]];
                    //fprintf(stderr, "FREE handle %d of size %d at 0x%X\n", handle, s, (uint32_t)ptr);

                    register_op(OP_FREE, handle, ptr, s);
                    user_free(ptr, handle, &op_time);

                    print_after_free_stats(address, s);

                } break;
            }
        }
    }
}

// XXX: Make this part of the user_<op> calls!
void plot_report(unsigned long largest_allocatable_block) {
    long memory_delta = 0;
    long free_bytes = 0;
    long fragmentation_percent = 0;
    long op_time = 0;
    bool caused_oom = 0;
    g_memory_usage += memory_delta;

    fprintf(fpstats, "    {'memory_delta': %9ld, 'free_bytes': %9lu, 'largest_allocatable_block': %9lu, 'fragmentation_percent': %4ld, 'op_time': %9lu, 'caused_oom': %2u},\n",  memory_delta, free_bytes, largest_allocatable_block, fragmentation_percent, op_time, caused_oom);
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
    int r = system(cmd);
    //printf("Plot data saved in %s\n", output);
}

int main(int argc, char **argv) {
    FILE *fpops = NULL;
    char driver[512];
    char statsfile[512];
    
    // XXX: The entire parameter passing is a big mess!

    if (argc < 3) {
        die("%d is too few.\nusage: %s [--maxmem|--fragmentation|--peakmem] opsfile [oplimit] [peakmemsize]\noplimit and peakmemsize for use by --maxmem\n", argc, argv[0]);
    }

    if (argv[1][0] == '-') {
        g_opsfile = argv[2];
        if (strcmp(argv[1], "--maxmem") == 0 && argc == 5) {
            g_operation_mode = OPMODE_MAXMEM;
            g_oplimit = atoi(argv[3]);
            g_total_memory_consumption = atoi(argv[4]);
        }else if (strcmp(argv[1], "--peakmem") == 0) {
            g_operation_mode = OPMODE_PEAKMEM;
            fprintf(stderr, "opmode: peakmem\n");
        }
        else {
            g_operation_mode = OPMODE_FRAGMENTATION;
            fprintf(stderr, "opmode: fragmentation\n");
        }
    }

    if (g_total_memory_consumption > 0) {
        g_heap_size = g_total_memory_consumption;
        g_colormap_size = g_heap_size/4;
    }

    g_heap = (uint8_t *)malloc(g_heap_size);
    g_colormap = (uint8_t *)malloc(g_colormap_size);

    fpops = fopen64(g_opsfile, "rt");
    if (!fpops) {
        die("%s: couldn't open opsfile %s: strerror() = %s\n", argv[0], g_opsfile, strerror(errno));
    }

    setbuf(stdout, NULL);

    heap_colormap_init();
    
    user_init(g_heap_size, (void *)g_heap, (void *)g_colormap, driver);
    strcpy(statsfile, driver);
    strcat(statsfile, ".alloc-stats");

    if (g_oplimit == 0) {
        fpstats = fopen(statsfile, "wt");
        fprintf(fpstats, "# Format of header:\n");
        fprintf(fpstats, "# HS<heap size>\n");
        fprintf(fpstats, "# Format of each op line:\n");
        fprintf(fpstats, "# <counter> MD<memory delta> FB<free bytes> LAB<largest allocatable block> FP<fragmentation percent> T<operation time> OOM<caused oom?>\n");
        fprintf(fpstats, "driver = \"%s\"\n", driver);
        fprintf(fpstats, "opsfile = \"%s\"\n", g_opsfile);
        fprintf(fpstats, "heap_size = %u\n", g_heap_size);
        fprintf(fpstats, "opmode = '%s'\n", g_operation_mode == OPMODE_FRAGMENTATION ? "fragmentation" : "maxmem");
    } else {
        fpstats = fopen(statsfile, "at");
    }

    // yum, bolognese programming...
    if (g_operation_mode == OPMODE_FRAGMENTATION) {
        alloc_driver_fragmentation(fpops, 500*1000, g_heap, g_heap_size, g_colormap);
    }
    else if (g_operation_mode == OPMODE_MAXMEM) {
        alloc_driver_maxmem(fpops, 500*1000, g_heap, g_heap_size, g_colormap);
    } else if (g_operation_mode == OPMODE_PEAKMEM) {
        alloc_driver_peakmem(fpops, 500*1000, g_heap, g_heap_size, g_colormap);
        user_destroy();
        fclose(fpops);
        fclose(fpstats);
        printf("%u\n", g_highest_address - g_heap);
        return 0;
    }

    user_destroy();

    fprintf(stderr,"Pure memory usage: %d bytes = %d kbytes = %d megabytes\n",
            g_memory_usage, g_memory_usage/1024, g_memory_usage/1048576);
    fprintf(stderr, "Writing alloc stats data to %s\n", statsfile);

    {
    pointer_size_map_t::iterator it;
    for (it=g_pointer_free.begin(); it != g_pointer_free.end(); it++) 
        if (it->second != 1)
            fprintf(stderr, "Pointer %x free'd %d times\n", (uint32_t)it->first, it->second);
    }
    {
    handle_count_map_t::iterator it;
    for (it=g_handle_free.begin(); it != g_handle_free.end(); it++) 
        if (it->second != 1)
            fprintf(stderr, "Handle %d free'd %d times\n", it->first, it->second);
    }

    fprintf(stderr, "highest address: 0x%X adjusted for heap start = %d kb\n", (uint32_t)g_highest_address, ((uint32_t)g_highest_address - (uint32_t)g_heap) / 1024);


    {
    fprintf(stderr, "\nBlock statistics.\n");
    handle_count_map_t::iterator it;
    for (it=g_free_block_count.begin(); it != g_free_block_count.end(); it++)
        fprintf(stderr, "Free block size %d has %d items\n", it->first, it->second);
    fprintf(stderr, "\n");
    for (it=g_used_block_count.begin(); it != g_used_block_count.end(); it++)
        fprintf(stderr, "Used block size %d has %d items\n", it->first, it->second);
    }




    {
    operation_percent_map_t::iterator it;
    for (it=g_fragmentation.begin(); it != g_fragmentation.end(); it++) {
        fprintf(stderr, "Time %4d: %.2f\n", it->first, it->second);
    }
    }


    // The data that actually is read by grapher.py

    if (g_oplimit == 0) fprintf(fpstats, "alloc_stats = [\n");
    if (g_operation_mode == OPMODE_FRAGMENTATION) {
        {
        alloc_stat_t::iterator it;
        for (it=g_alloc_stat.begin(); it != g_alloc_stat.end(); it++) {
            fprintf(fpstats, "    {'free': %9d, 'used': %9d, 'overhead': %9d, 'fragmentation': %7.2f}%s\n", std::get<0>(*it), std::get<1>(*it), std::get<2>(*it), std::get<3>(*it),
                    (it+1) != g_alloc_stat.end() ? ", " : " ");
        }
        }

    } else if (g_operation_mode == OPMODE_MAXMEM) {
        {
        alloc_stat_t::iterator it;
        for (it=g_maxmem_stat.begin(); it != g_maxmem_stat.end(); it++) {
            if (g_oplimit > 0)
                fputc(',', fpstats);
            fprintf(fpstats, "    {'free': %9d, 'used': %9d, 'overhead': %9d, 'maxmem': %7.2f}\n", std::get<0>(*it), std::get<1>(*it), std::get<2>(*it), std::get<3>(*it));
        }
        }
    }
    //if (g_oplimit == 0) fprintf(fpstats, "]\n");
    fprintf(stderr, "Memory stats (Python data) written to %s\n", statsfile);


    fclose(fpstats);
    fclose(fpops);

    free(g_colormap);
    free(g_heap);
}

