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
#include "compact.h"

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
#include <list>

// to get reproducability
#define INITIAL_RANDOM_SEED 0x42424242

#define KILLPERCENT_FOR_NOT_REWINDING 1000

// exit codes:
// 0 = ok, 
// 1 = unused,
// 2 = oom
#define die(x...) {printf(x); exit(1);}
#define oom(x...) {printf(x); exit(2);}


// XXX: ugly, and really only for debugging (?)
__attribute__ ((weak)) void rmstat_print_headers(bool){}

typedef std::map<uint32_t, void *> handle_pointer_map_t;
typedef std::map<void *, uint32_t> pointer_size_map_t;
typedef std::map<uint32_t, uint32_t> handle_count_map_t;
typedef std::map<uint32_t, float> operation_percent_map_t;
typedef std::map<void *, void *> block_address_map_t;

static handle_pointer_map_t g_handle_to_address;
static handle_pointer_map_t g_handles;
static handle_count_map_t g_sizes;

static handle_count_map_t g_handle_free;
static pointer_size_map_t g_pointer_free;
static handle_count_map_t g_free_block_count;
static handle_count_map_t g_used_block_count;
static handle_count_map_t g_overhead_block_count;

static operation_percent_map_t g_fragmentation;

// <sum(used), sum(free), sum(overhead), fragmentation%, N/A, N/A, N/A, N/A, ' ', size> for fragmentation
// <sum(free), sum(used), sum(overhead), maxmem, current_op_time, oom_time, current_maxmem_time, op, size> for maxmem
// op is 'N', 'F'
//
// status of the allocator at a specific time.
typedef std::tuple<int, int, int, float, uint32_t, uint32_t, uint32_t, unsigned char, uint32_t> alloc_time_stat_t;
typedef std::vector<alloc_time_stat_t> alloc_stat_t;
std::list<uint32_t> g_ops_order;

alloc_stat_t g_alloc_stat;
alloc_stat_t g_maxmem_stat;

FILE *fpstats = NULL;
unsigned long long g_counter = 0;
uint32_t g_memory_usage = 0;


enum {
    OPMODE_MEMPLOT, // produce memplot pngs/movie
    OPMODE_ALLOCSTATS, // produce graph of max alloc'able mem at each point in time
    OPMODE_PEAKMEM, // print largest memory address minus g_heap
};

uint8_t g_operation_mode;
uint32_t g_oplimit = 0; // ./plot_foo --allocstats result.app-ops <n> # oplimit, 0 initial, then 1..N.


#define HEAP_SIZE (256 * 1024*1024) // 1 GB should be enough.


uint32_t g_heap_size = HEAP_SIZE;
uint8_t *g_heap = NULL;
uint8_t *g_colormap = NULL;
uint32_t g_colormap_size = HEAP_SIZE/4 + 1;

uint8_t *g_highest_address = 0; // Currently ONLY used for --peakmem

char *g_opsfile = NULL;
char *g_resultsfile = NULL;
float g_killpercent = 0;

void scan_block_sizes(void);
int colormap_print(char *output, int sequence);
void calculate_fragmentation_percent(uint8_t op);


uint32_t g_total_memory_consumption = 0;
uint32_t g_theoretical_heap_size = 0;
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


    // The code below is used only by --allocstats

    // XXX: heap_size should be the highest address, i.e. double run, in order to properly calculate the theoretical free space


    // Works somewhat OK for dlmalloc, not very accurate for rmalloc. 
    // Or really dlmalloc now either.
    //g_theoretical_free_space = g_total_memory_consumption - g_theoretical_used_space - g_theoretical_overhead_space;
    g_theoretical_free_space = g_heap_size;
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

    g_alloc_stat.push_back(alloc_time_stat_t(sum_free, sum_used, sum_overhead, frag, /*current_op_time*/0, /*oom_time*/0, /*optime_maxmem*/0, /*op*/' ', /*size*/0));

    sequence++;

}

void colormap_init() {
    // keep this in sync w/ plot_fragment_image
    memset(g_colormap, COLOR_WHITE, g_colormap_size);
}

void heap_colormap_init() {
    uint32_t *h = (uint32_t *)g_heap;
    for (int i=0; i<g_heap_size/4; i++) {
        h[i] = PAINT_INITIAL_COLOR;
    }

    colormap_init();
}

void register_op(int op, int handle, void *ptr, int ptrsize, uint32_t op_time) {
    //fprintf(stderr, "op at handle %d\n", handle);

    ptr_t aligned_ptr = (ptr_t)ptr;
    ptr_t aligned_size = (ptr_t)ptrsize;

    // not aligned?
    if ((ptr_t)ptr & 3 != 0) {
        aligned_ptr = ((ptr_t)ptr+4) & ~3;
        aligned_size = ptrsize - (aligned_ptr - (ptr_t)ptr);
    }

    if (aligned_size >= 4) {
        // ptr will be within g_heap
        ptr_t offset = (ptr_t)aligned_ptr - (ptr_t)g_heap;
        //uint32_t size = g_sizes[ptr]; // XXX: What is this for?!
        uint32_t size = aligned_size;

        int cs = ceil((float)aligned_size/4.0); 
        uint32_t co = offset/4;
        ptr_t cmptr = (ptr_t)g_colormap+co;

        // mark area with initial as a cleanup, otherwise too many areas will be falsely marked as overhead below.
        int ps = size/4; // floor, not to overwrite data.
        uint32_t *pp = (uint32_t *)aligned_ptr;

        //printf("marking handle %d ptr %x of size %d (and small size: %d)\n", handle, (uint32_t)ptr, size, ps);

        uint32_t heap_fill = (op == OP_ALLOC) ? HEAP_ALLOC : HEAP_FREE; 

        for (int i=0; i<ps; i++)
            pp[i] = heap_fill;

        uint8_t color = (op == OP_ALLOC) ? COLOR_RED : COLOR_GREEN;
        memset((void *)cmptr, color, ps);
    }

#if 0 // unaligned code
    // ptr will be within g_heap
    ptr_t offset = (ptr_t)ptr - (ptr_t)g_heap;
    //uint32_t size = g_sizes[ptr]; // XXX: What is this for?!
    uint32_t size = ptrsize;

    int cs = ceil((float)size/4.0); 
    uint32_t co = offset/4;
    ptr_t cmptr = (ptr_t)g_colormap+co;

    // mark area with initial as a cleanup, otherwise too many areas will be falsely marked as overhead below.
    int ps = size/4; // floor, not to overwrite data.
    uint32_t *pp = (uint32_t *)ptr;
    uint8_t *p = (uint8_t *)ptr;

    //printf("marking handle %d ptr %x of size %d (and small size: %d)\n", handle, (uint32_t)ptr, size, ps);

    uint32_t heap_fill = (op == OP_ALLOC) ? HEAP_ALLOC : HEAP_FREE; 

    for (int i=0; i<ps; i++)
        pp[i] = heap_fill;

    uint8_t color = (op == OP_ALLOC) ? COLOR_RED : COLOR_GREEN;
    memset((void *)cmptr, color, ps);
#endif

}


int g_sequence = 0;
void scan_heap_update_colormap(bool create_plot) {

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

    if (1) // XXX: This doesn't work if allocated data is not dword-aligned, leave it out for now.
    {

    uint32_t *vh = (uint32_t *)g_heap;
    if ((ptr_t)vh & 3 != 0)
        vh = (uint32_t *)(((ptr_t)vh + 4) & ~3);
    uint32_t size = g_colormap_size;
    if ((ptr_t)vh > (ptr_t)g_heap)
        size -= 1;
    for (int i=0; i<size; i++) {
        if (vh[i] != HEAP_INITIAL &&
            vh[i] != HEAP_ALLOC &&
            vh[i] != HEAP_FREE)

            // well, it's got to be changed then.
            g_colormap[i] = COLOR_WHITE;
    }

    }
    
#endif

    if (create_plot) {
        // presto, a fresh colormap with appropriate values for green, red and white.
        char buf[256];
        snprintf(buf, sizeof(buf), "%s-plot-%.6d.png", g_opsfile, g_sequence);
        colormap_print(buf, g_sequence);

        g_sequence++;
    }
}

/* Only called from --allocstats!
 *
 * In case of compacting, or other layout-changing operation,
 * recalculate the colormap based on whatever live handles are around at the moment.
 *
 * That amounts to reinitializing colormap and iterating over all handles and calling register_op().
 */
void recalculate_colormap_from_current_live_handles() {
    /*
    int32_t count = user_get_used_block_count();
    if (count == 0)
        return;

    ptr_t *blocks = (ptr_t *)malloc(sizeof(ptr_t) * count);
    user_get_used_blocks(blocks);
    */
    
    fprintf(stderr, "* Recalculate: start\n");
    ptr_t highest = 0;

    colormap_init();
    handle_pointer_map_t::iterator it;
    for (it=g_handles.begin(); it != g_handles.end(); it++) {
        uint32_t h = it->first;
        void *memhandle = it->second;
        uint32_t size = g_sizes[h];

        if (size != 0) {
            void *ptr = user_lock(memhandle);

            if ((ptr_t)ptr + size > (ptr_t)highest)
                highest = (ptr_t)ptr + size;

            register_op(OP_ALLOC, h, ptr, size, /*op_time*/0);
            //scan_heap_update_colormap(false/*create_plot*/);
            user_unlock(memhandle);
        }
    }
    g_highest_address = (uint8_t *)highest;


    /*

    g_highest_address används för att rita ut colormap.
    varför blir det en massa vitt efter det röda, för att sedan börja om på rött igen? det borde vara rött hela vägen i så fall.
    är det bara felaktigt färgat, kanske?  hänger inte markörerna i heapen med?
    */




    // not touched.

    fprintf(stderr, "* Recalculate: done, create plot.\n");
    scan_heap_update_colormap(true/*create_plot*/);

    /*
    free(blocks);
    */
}

/* parses ops file and calls into user alloc functions. */
void alloc_driver_memplot(FILE *fp, int num_handles, uint8_t *heap, uint32_t heap_size, uint8_t *colormap) {
    bool done = false;
    
    int handle, address, size, old_handle=1; /* ops always start from 0 */
    int highest_handle_no = 0, handle_offset = 0;
    char op, old_op=0;
    uint32_t op_time, op_time2, op_time3;
    bool was_oom = false;
    int opcounter = 0;
    int rewind_count = 1;

    //while (!done && !feof(fp)) {
    while (!done) {
        char line[128];
        char *r = fgets(line, 127, fp);
        if (line[0] == '#')
            continue;

        if (feof(fp)) {
            fprintf(stderr, "******************************** REWIND (count %d)\n", rewind_count++);
            rewind(fp);
            handle_offset += highest_handle_no;
        }

        sscanf(line, "%d %c %u %u\n", &handle, &op, &address, &size);
        // for now, don't care about the difference between load/modify/store
        if (op == 'L' || op == 'M' || op == 'S')
            op = 'A';

        handle += handle_offset; // for when looping.

        if (handle == old_handle && op == 'A' && old_op == 'A') {
            // skip
        } else {

            if (handle > highest_handle_no && handle_offset == 0)
                highest_handle_no = handle;

            was_oom = false;
            opcounter++;

            switch (op) {
                case 'L': // Lock
                    user_lock(g_handles[handle]);
                    break;
                case 'U': // Unlock
                    user_unlock(g_handles[handle]);
                    break;
                case 'A':
                    user_lock(g_handles[handle]);
                    user_unlock(g_handles[handle]);
                    break;
#if 0
                case 'O': // OOM
                    user_handle_oom(0, &op_time);
                    recalculate_colormap_from_current_live_handles();
                    break;
#endif
                case 'N': {
                    //putchar('.');
                    void *memaddress = NULL;
                    void *ptr = user_malloc(size, handle, &op_time, &memaddress);
                    //fprintf(stderr, "NEW handle %d of size %d to 0x%X\n", handle, size, (uint32_t)ptr);
                    /*
                    if ((ptr_t)memaddress > (ptr_t)g_highest_address)
                        g_highest_address = (uint8_t *)memaddress;
                    */

                    //g_handle_to_address[handle] = memaddress;
                    //g_handles[handle] = ptr;
                    //g_sizes[g_handles[handle]] = size;

                    // XXX when to call register_op() and do coloring?
                    if (ptr == NULL) {
                        if (user_handle_oom(size, &op_time2)) {
                            op_time += op_time2;
                            ptr = user_malloc(size, handle, &op_time3, &memaddress);
                            op_time += op_time3;
                            if (NULL == ptr) {
                                //oom("\n\nOOM!\n");
                                done = true;
                                break;
                            }
                            was_oom = true;

                            recalculate_colormap_from_current_live_handles();

                            g_handles[handle] = ptr;
                            g_handle_to_address[handle] = memaddress;
                            g_handles[handle] = ptr;
                            register_op(OP_ALLOC, handle, memaddress, size, op_time);
                            //g_sizes[g_handles[handle]] = size;
                            g_sizes[handle] = size;
                        } else {
                            //oom("\n\nOOM!\n");
                            done = true;
                            break;
                        }
                    } else {
                        g_sizes[handle] = size;
                        g_handle_to_address[handle] = memaddress;
                        g_handles[handle] = ptr;
                        // FIXME: Recalculate all ops after compact?
                        register_op(OP_ALLOC, handle, memaddress, size, op_time);
                    }


                    g_highest_address = (uint8_t *)user_highest_address(/*full_recalc*/false);


                    // in case there has been a compacting (for rmalloc), colormap is no longer valid.
                    // thus, recalculate colormap based on /currently live/ handles.
                    if (was_oom) {
                        recalculate_colormap_from_current_live_handles();
                    } else {
                        scan_heap_update_colormap(true/*create_plot*/);
                    }

                    print_after_malloc_stats(g_handles[handle], address, size);

                    scan_block_sizes();
                    //calculate_fragmentation_percent(op);
                } break;
                case 'F': {
                    //putchar('.');
                    void *ptr = g_handles[handle];
                    //int s = g_sizes[g_handles[handle]];
                    int s = g_sizes[handle];
                    //fprintf(stderr, "FREE handle %d of size %d at 0x%X\n", handle, s, (uint32_t)ptr);
                    void *memaddress = g_handle_to_address[handle];


                    // ORDER IS IMPORTANT!  register_op marks as free.
                    register_op(OP_FREE, handle, memaddress, s, op_time);

                    user_free(ptr, handle, &op_time);

                    // no longer in use.
                    //g_sizes[g_handles[handle]] = 0;
                    g_sizes[handle] = 0;

                    scan_heap_update_colormap(true/*create_plot*/);

                    print_after_free_stats(address, s);

                    scan_block_sizes();
                    //calculate_fragmentation_percent(op);
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

uint32_t calculate_maxmem(uint8_t op, uint32_t *op_time) {
    int32_t size = g_theoretical_free_space;
    //int32_t size = g_theoretical_free_space * 10;
    uint32_t handle = g_theoretical_free_space;

    uint32_t prel_op_time = 0;
    void *p = NULL;
    while (size > 0) {
        p = user_malloc(size, handle, &prel_op_time, NULL);
        if (p != NULL)
            break;
        size -= MAXMEM_STEP;
        handle--;
    }

    if (size < 0)
        p = NULL;
    else
    {
        if (op_time)
            *op_time = prel_op_time;
    }


    return p == NULL ? 0 : size;
}

volatile bool dommy = true;
void alloc_driver_allocstats(FILE *fp, int num_handles, uint8_t *heap, uint32_t heap_size, uint8_t *colormap) {
    bool done = false;
    
    int handle, address, size, old_handle=1; /* ops always start from 0 */
    char op, old_op=0;
    uint32_t op_time, op_time2, op_time3, op_time4, oom_time;
    uint32_t total_size = 0;
    int highest_handle_no = 0, handle_offset = 0;

    // For each op, try to allocate as large a block as possible.
    // Then, go to next op.  ONLY increase current_op at new/free.
    uint32_t current_op = 0;

    uint32_t current_used_space = 0;

    while (!done && !feof(fp)) {
        bool was_oom = false;
        char line[128];
        char *r = fgets(line, 127, fp);
        if (line[0] == '#')
            continue;

        if (feof(fp) && g_killpercent < KILLPERCENT_FOR_NOT_REWINDING) {
            // MAX RAM:
            // 5 OK = 883
            //
            // MAX SPEED:
            //
            // /4 =>
            // Driver dlmalloc has 1141 items
            // Driver rmmalloc has 1141 items
            //
            //
            int count = (int)((float)handle * g_killpercent);
            fprintf(stderr, "********************************* REWIND! Killing off %d headers\n", count);
            rewind(fp);


            // randomly kill off 1/3 of all headers.
            int killed = 0;
            while (killed < count) {
                //int h = rand() % handle;
                //int s = g_sizes[h];

                handle_count_map_t::iterator it = g_sizes.begin();
                int sz = rand() % g_sizes.size();
                if (sz == 0)
                    break;
                int rnd = rand() % sz;
                std::advance(it, rnd);
                int h = it->first;
                int s = it->second;

                if (s > 0) {
                    void *ptr = g_handles[h];
                    current_used_space -= s;
                    user_free(ptr, h, &op_time);
                    killed++;

                    g_sizes.erase(it);

                    //g_sizes[h] = 0;
                    g_handles[h] = NULL;
                    g_handle_to_address[h] = NULL;
                }
            }

            handle_offset += highest_handle_no;
            r = fgets(line, 127, fp);
            if (line[0] == '#')
                continue;
        }

        op = '\x0';

        sscanf(line, "%d %c %u %u\n", &handle, &op, &address, &size);
        // for now, don't care about the difference between load/modify/store
        if (op == 'L' || op == 'M' || op == 'S')
            op = 'A';

        if (op == 0 || r == 0)
            continue;

        handle += handle_offset; // for when looping.

        if (handle == old_handle && op == 'A' && old_op == 'A') {
            // skip
        } else {

            if (handle > highest_handle_no && handle_offset == 0)
                highest_handle_no = handle;

            switch (op) {
                case 'L': // Lock
                    
                    // XXX: when and how to do the color map diffs?
                    // result should be stored in a frame, but how do we get the data?
                    //colormap_paint(colormap);

                    user_lock(g_handles[handle]);
                    break;
                case 'U': // Unlock
                    user_unlock(g_handles[handle]);
                    break;
                case 'A':
                    user_lock(g_handles[handle]);
                    user_unlock(g_handles[handle]);
                    break;
#if 0
                case 'O': // OOM
                    user_handle_oom(0, &op_time);

                    //void *maybe_highest = user_highest_address(/*full_calculation*/false);
                    void *maybe_highest = user_highest_address(/*full_calculation*/true);
                    if (maybe_highest != NULL) {
                        ptr_t highest = (ptr_t)maybe_highest - (ptr_t)g_heap;
                        g_highest_address = (uint8_t *)maybe_highest;
                    } else
                    {
                        //fprintf(stderr, "NEW handle %d of size %d to 0x%X\n", handle, size, (uint32_t)ptr);
                        if ((ptr_t)memaddress + size > (ptr_t)g_highest_address)
                            g_highest_address = (uint8_t *)memaddress + size;
                    }
#endif

                    break;
                case 'N': {
                    oom_time = op_time = op_time2 = op_time3 = op_time4 = 0;

                    current_used_space += size;

                    g_ops_order.push_back(handle);

                    //putchar('.');
                    void *memaddress = NULL;
                    void *ptr = user_malloc(size, handle, &op_time, &memaddress);
                    //fprintf(stderr, "NEW handle %d of size %d to 0x%X\n", handle, size, (uint32_t)ptr);

                    // heap size should at this point be large enough to accomodate all memory request.
                    // there should be no OOMs here, hence we don't really handle it.

                    if (ptr == NULL) {
                        if (user_handle_oom(size, &op_time2)) {
                            ptr = user_malloc(size, handle, &op_time3, NULL);
                            if (NULL == ptr) {
                                was_oom = true;
                            }
                        } else {
                            was_oom = true;
                        }
                    }

                    if (was_oom == false) {
                        total_size += size;

                        //void *maybe_highest = user_highest_address(/*full_calculation*/false);
                        void *maybe_highest = user_highest_address(/*full_calculation*/true);
                        if (maybe_highest != NULL) {
                            ptr_t highest = (ptr_t)maybe_highest - (ptr_t)g_heap;
                            g_highest_address = (uint8_t *)maybe_highest;
                        } else
                        {
                            //fprintf(stderr, "NEW handle %d of size %d to 0x%X\n", handle, size, (uint32_t)ptr);
                            if ((ptr_t)memaddress + size > (ptr_t)g_highest_address)
                                g_highest_address = (uint8_t *)memaddress + size;
                        }

                        // XXX

                        g_handle_to_address[handle] = memaddress;
                        g_handles[handle] = ptr;
                        //g_sizes[g_handles[handle]] = size;
                        g_sizes[handle] = size;
                        //register_op(OP_ALLOC, handle, memaddress, size, op_time);
                    }
                    else {
                        
                        done = true;
                        rmstat_print_headers(/*only_type*/true);

                        oom("OOM: last handle: %d (offset = %d, highest = %d)\n", handle, handle_offset, highest_handle_no);
                        //oom("\n\nallocstats: couldn't recover trying to alloc %d bytes at handle %d (total alloc'd %u).\n", size, handle, total_size);
                        break;

                        // FIXME: What if?
                    }

                    print_after_malloc_stats(g_handles[handle], address, size);

                    current_op++;
                    if (current_op > g_oplimit) {

                        // Colormap is broken when using compacting().
                        // XXX: BUT MENTION IN THESIS!!!
                        //
#if 0
                        scan_heap_update_colormap(false);
                        scan_block_sizes();
#endif

                        fprintf(stderr, "Op #%d: Largest block from %'6u kb theoretical: ", current_op-1, g_theoretical_free_space/1024);
                        
                        // XXX: Do something with was_oom here?
                        was_oom = false;

                        g_theoretical_free_space = g_heap_size - current_used_space;

                        uint32_t optime_maxmem = 0;
                        uint32_t maxsize = 0;
                        if (was_oom == false)
                            maxsize = calculate_maxmem(op, &optime_maxmem);
                        else
                            fprintf(stderr, "\n\nOOM!\n");

                        if (maxsize == 0) {
                            if (user_handle_oom(0, &op_time4)) {
                                maxsize = calculate_maxmem(op, &optime_maxmem);
                            }
                        }


                        fprintf(stderr, "allocstats: %9u bytes (%6u kbytes = %3.2f%%)\n", 
                                maxsize, (int)maxsize/1024, 100.0 * (float)maxsize/(float)g_theoretical_free_space);

                        /*
                         * did we perform a cleanup?
                         * yes, set the first op to be part of cleanup, and the final malloc as the real op.
                         */
                        if (op_time2 > 0 && op_time3 > 0)
                        {
                            //oom_time = op_time2; // first ops, before the real one.
                            oom_time = op_time + op_time2 + op_time4; // first ops, before the real one.
                            op_time = op_time3;
                        }

                        // skip complicated colormap calculation
                        /*
                        g_maxmem_stat.push_back(alloc_time_stat_t(
                            g_theoretical_free_space, g_theoretical_used_space, g_theoretical_overhead_space, maxsize));
                        */
                        g_maxmem_stat.push_back(alloc_time_stat_t(
                            g_theoretical_free_space, current_used_space, g_theoretical_overhead_space, maxsize,
                            op_time, oom_time, optime_maxmem, op, size));

                        fprintf(stderr, "Finished: last handle: %d (offset = %d, highest = %d)\n", handle, handle_offset, highest_handle_no);
                        return;
                    }

                } break;
                case 'F': {
                    oom_time = op_time = op_time2 = op_time3 = 0;

                    //putchar('.');
                    void *ptr = g_handles[handle];
                    //int s = g_sizes[g_handles[handle]];
                    int s = g_sizes[handle];
                    //fprintf(stderr, "FREE handle %d of size %d at 0x%X\n", handle, s, (uint32_t)ptr);
                    
                    current_used_space -= s;

                    void *memaddress = g_handle_to_address[handle];
                    user_free(ptr, handle, &op_time);
                    //register_op(OP_FREE, handle, memaddress, s, op_time);

                    //g_sizes[ptr] = 0;
                    //g_sizes[handle] = 0;
                    g_sizes.erase(handle);
                    g_handles[handle] = NULL;

                    print_after_free_stats(address, s);

                    current_op++;
                    if (current_op > g_oplimit) {

                        // Colormap is broken when using compacting().
                        // XXX: BUT MENTION IN THESIS!!!
                        //
#if 0
                        scan_heap_update_colormap(false);
                        scan_block_sizes();
#endif

                        g_theoretical_free_space = g_heap_size - current_used_space;

                        fprintf(stderr, "Op #%d: Largest block from %'6u kb theoretical: ", current_op-1, g_theoretical_free_space/1024);

                        // FIXME: base g_theoretical_free_space on g_highest_address-g_heap, not g_heap_size!
                        // Otherwise, the difference will be too small in the graph, because g_heap_size can be very large.

                        //uint32_t maxsize = g_theoretical_free_space; //= calculate_maxmem(op);
                        uint32_t optime_maxmem = 0;
                        uint32_t maxsize = calculate_maxmem(op, &optime_maxmem);


                        fprintf(stderr, "%9u bytes (%6u kbytes = %3.2f%%)\n", 
                                maxsize, (int)maxsize/1024, 100.0 * (float)maxsize/(float)g_theoretical_free_space);

                        // skip complicated colormap calculation
                        /*
                        g_maxmem_stat.push_back(alloc_time_stat_t(
                            g_theoretical_free_space, g_theoretical_used_space, g_theoretical_overhead_space, maxsize));
                        */
                        g_maxmem_stat.push_back(alloc_time_stat_t(
                            g_theoretical_free_space, current_used_space, g_theoretical_overhead_space, maxsize,
                            op_time, /*oom_time*/0, optime_maxmem, op, s));
                        return;
                    }

                } break;
            }
        }
    }



//    user_handle_oom(0, &op_time);

    fprintf(stderr, "Last handle: %d (offset = %d, highest = %d)\n", handle, handle_offset, highest_handle_no);


}

void alloc_driver_peakmem(FILE *fp, int num_handles, uint8_t *heap, uint32_t heap_size, uint8_t *colormap) {
    bool done = false;
    
    int handle, address, size, old_handle=1; /* ops always start from 0 */
    int the_handle = 0;
    char op, old_op=0;
    uint32_t op_time, op_time2, op_time3;

    // For each op, try to allocate as large a block as possible.
    // Then, go to next op.
    uint32_t current_op = 0, current_free = 0;
    uint32_t current_op_at_free = 0, current_op_at_new = 0, current_op_at_compact = 0;

    int32_t theo_used = 0;

    while (!done && !feof(fp)) {
        char line[128];
        char *r = fgets(line, 127, fp);
        if (line[0] == '#')
            continue;

        op = '\x0';

        sscanf(line, "%d %c %u %u\n", &handle, &op, &address, &size);
        // for now, don't care about the difference between load/modify/store
        // XXX: L is not Load, it's now Lock.
        //if (op == 'L' || op == 'M' || op == 'S') op = 'A';

        if (op == 0 || r == 0)
            continue;

        if (current_op % 10000 == 0)
            fprintf(stderr, "\nOp %d - heap usage %d K  - theo heap usage %d K                              ", current_op, (g_highest_address-g_heap)/1024, theo_used/1024);

        if (handle == old_handle && op == 'A' && old_op == 'A') {
            // skip
        } else {
            current_op++;
            switch (op) {
                case 'L': // Lock
                    
                    // XXX: when and how to do the color map diffs?
                    // result should be stored in a frame, but how do we get the data?
                    //colormap_paint(colormap);

                    user_lock(g_handles[handle]);
                    break;
                case 'U': // Unlock
                    user_unlock(g_handles[handle]);
                    break;
                case 'A':
                    user_lock(g_handles[handle]);
                    user_unlock(g_handles[handle]);
                    break;
                case 'N': {
                    //putchar('.');
                    void *memaddress = NULL;
                    theo_used += size;

                    if (handle == 3150)
                    {
                        op_time = current_op;
                    }

                    if (handle == 25024)
                    {
                        op_time = current_op;
                    }
                    void *ptr = user_malloc(size, handle, &op_time, &memaddress);

                    if (ptr == (void *)0xb7c84cd8)
                    {
                        the_handle = handle;
                    }

                    // XXX when to call register_op() and do coloring?
                    if (ptr == NULL) {
                        if (user_handle_oom(size, &op_time2)) {
                            op_time += op_time2;
                            ptr = user_malloc(size, handle, &op_time3, &memaddress);
                            op_time += op_time3;
                            if (NULL == ptr) {
                                oom("\n\nOOM!\n");
                            }
                        } else {
                            oom("\n\nOOM!\n");
                        }
                    }

#if 1
                    void *maybe_highest = user_highest_address(/*full_calculation*/false);
                    //void *maybe_highest = user_highest_address(/*full_calculation*/true);
                    if (maybe_highest != NULL) {
                        ptr_t highest = (ptr_t)maybe_highest - (ptr_t)g_heap;
                        g_highest_address = (uint8_t *)maybe_highest;
                    } else
                    {
                        //fprintf(stderr, "NEW handle %d of size %d to 0x%X\n", handle, size, (uint32_t)ptr);
                        if ((ptr_t)memaddress + size> (ptr_t)g_highest_address)
                            g_highest_address = (uint8_t *)memaddress + size;
                    }
#else
                        if ((ptr_t)memaddress > (ptr_t)g_highest_address)
                            g_highest_address = (uint8_t *)memaddress;
#endif

                    g_handle_to_address[handle] = memaddress;
                    g_handles[handle] = ptr;
                    //g_sizes[g_handles[handle]] = size;
                    g_sizes[handle] = size;

                    //register_op(OP_ALLOC, handle, memaddress, size, op_time);

                    current_op_at_new = current_op;
                } break;
                case 'F': {

                    if (current_op == 20750) 
                    {
                        /*nop*/ current_op_at_free = current_op;
                    }
                    if (current_op == 20850) 
                    {
                        /*nop*/ current_op_at_free = current_op;
                    }

                    //putchar('.');
                    void *ptr = g_handles[handle];
                    //int s = g_sizes[g_handles[handle]];
                    int s = g_sizes[handle];
                    //theo_used -= s;
                    //fprintf(stderr, "FREE handle %d of size %d at 0x%X\n", handle, s, (uint32_t)ptr);

                    void *memaddress = g_handle_to_address[handle];

                    user_free(ptr, handle, &op_time);
                    //register_op(OP_FREE, handle, memaddress, s, op_time);

#if 0
                    if (current_free++ % 10000 == 0) {
                        current_op_at_compact = current_op;
                        //fprintf(stderr, "user_handle_oom(0) / rmcompact\n");
                        //user_handle_oom(size);
                        user_handle_oom(0);
                    }
#endif

                    print_after_free_stats(address, s);

                    //g_sizes[g_handles[handle]] = 0;
                    g_sizes[handle] = 0;
                    g_handles[handle] = NULL;
                    g_handle_to_address[handle] = NULL;

                    current_op_at_free = current_op;



                    // XXX: Should not be here. Just for test!
                    //user_handle_oom(0);
                    //fprintf(stderr, "compact!\n");




                } break;
                default: break;
            }
            old_op = op;

        }
    }
    user_handle_oom(0, &op_time);
    void *maybe_highest = user_highest_address(/*full_calculation*/true);
    if (maybe_highest != NULL) {
        ptr_t highest = (ptr_t)maybe_highest - (ptr_t)g_heap;
        g_highest_address = (uint8_t *)maybe_highest;
    }
    fprintf(stderr, "\nOp %d - final heap usage %d K  - theo heap usage (w/o free()) %d K                              ", current_op, (g_highest_address-g_heap)/1024, theo_used/1024);

    // ****************************************************
    // 
    // To get the same working heap size each time, use the theoretical max heap size instead of "actual"
    //
    // ****************************************************
    g_highest_address = (uint8_t *)((ptr_t)g_heap + theo_used);
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

int colormap_print(char *output, int sequence) {
    //int end = ((ptr_t)g_highest_address-(ptr_t)g_heap)/4;
    int end = g_colormap_size;
#define putchar(x) (void)x
    putchar("\n"); putchar('[');
    char buf2[256];
    sprintf(buf2, "/tmp/fragmentplot-%.6d.txt", sequence);
    FILE *f = fopen(buf2, "wt");
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
    sprintf(cmd, "python run_memory_frag_animation_plot_animation.py %s %s", buf2, output);
    int r = system(cmd);
    //fprintf(stderr, "Plot data saved in %s (result = %d)\n", output, r);
}

int main(int argc, char **argv) {
    FILE *fpops = NULL;
    char driver[512];
    char statsfile[512];
    
    // XXX: The entire parameter passing is a big mess!

    if (argc < 3) {
        die("%d is too few.\n"
            "usage: %s --allocstats opsfile resultfile killpercent oplimit peakmemsize theoretical_heap_size\n"
            "       killpercent - 0-100 how many percent of all handles to free at each rewind.\n"
            "       oplimit = 0 => write header to <driver>.alloc_stats\n"
            "       oplimit > 0 => write free/used/overhead/maxmem per op.\n"
            "\n"
            "       %s --peakmem opsfile\n"
            "       Prints out therotical heap size used.\n"
            "\n"
            "       %s --memplot opsfile\n"
            , argc, argv[0], argv[0], argv[0]);
    }

    if (argv[1][0] == '-') {
        g_opsfile = argv[2];
        if (strcmp(argv[1], "--allocstats") == 0) {
            if (argc < 8)
                die("too few arguments\n.");
            g_resultsfile = argv[3];
            g_operation_mode = OPMODE_ALLOCSTATS;
            g_killpercent = (float)atoi(argv[4]) / 100.0;
            g_oplimit = atoi(argv[5]);
            g_total_memory_consumption = atoi(argv[6]);
            g_theoretical_heap_size = atoi(argv[7]);
            fprintf(stderr, "opmode: allocstats\n");
        }else if (strcmp(argv[1], "--peakmem") == 0) {
            g_operation_mode = OPMODE_PEAKMEM;
            fprintf(stderr, "opmode: peakmem\n");
        }
        else if (strcmp(argv[1], "--memplot") == 0) {
            g_operation_mode = OPMODE_MEMPLOT;
            if (argc == 4) {
                g_heap_size = atoi(argv[3]);
                g_colormap_size = g_heap_size/4;
                fprintf(stderr, "Restricting heap size to %d kb\n", g_heap_size/1024);
            }
            fprintf(stderr, "opmode: memplot\n");
        } else {
            die("bad argument %s, exiting.\n", argv[1]);
        }
    }

    if (g_total_memory_consumption > 0) {
        g_heap_size = g_total_memory_consumption;
        g_colormap_size = g_heap_size/4 + 1;
    }

    g_heap = (uint8_t *)malloc(g_heap_size);
    while (g_heap == NULL) {
        g_heap_size = (int)(0.9 * (float)g_heap_size);
        g_heap = (uint8_t *)malloc(g_heap_size);
    }
    fprintf(stderr, "heap size: %u\n", g_heap_size);
    g_highest_address = g_heap;


    g_colormap = (uint8_t *)malloc(g_colormap_size);

    fpops = fopen64(g_opsfile, "rt");
    if (!fpops) {
        die("%s: couldn't open opsfile %s: strerror() = %s\n", argv[0], g_opsfile, strerror(errno));
    }

    setbuf(stdout, NULL);

    heap_colormap_init();
    
    //user_init(g_heap_size, (void *)g_heap, (void *)g_colormap, driver);
    user_init(g_heap_size, (void *)g_heap, driver);
    if (g_resultsfile == NULL) {
        strcpy(statsfile, driver);
        strcat(statsfile, ".alloc-stats");
    }
    else
        strcpy(statsfile, g_resultsfile);

    if (g_oplimit == 0) {
        fpstats = fopen(statsfile, "wt");
        //fprintf(fpstats, "# Format of header:\n");
        //fprintf(fpstats, "# HS<heap size>\n");
        //fprintf(fpstats, "# Format of each op line:\n");
        //fprintf(fpstats, "# <counter> MD<memory delta> FB<free bytes> LAB<largest allocatable block> FP<fragmentation percent> T<operation time> OOM<caused oom?>\n");
        fprintf(fpstats, "driver = \"%s\"\n", driver);
        fprintf(fpstats, "opsfile = \"%s\"\n", g_opsfile);
        fprintf(fpstats, "heap_size = %u\n", g_heap_size);
        fprintf(fpstats, "theoretical_heap_size = %u\n", g_theoretical_heap_size);
        fprintf(fpstats, "opmode = '%s'\n", g_operation_mode == OPMODE_MEMPLOT ? "memplot" : "allocstats");
    } else {
        char buffer[512];
        if (g_resultsfile != NULL)
            sprintf(statsfile, "%s.part_%d", g_resultsfile, g_oplimit);
        fpstats = fopen(statsfile, "at");
    }

    srand(INITIAL_RANDOM_SEED); 


    // yum, bolognese programming...
    if (g_operation_mode == OPMODE_MEMPLOT) {
        alloc_driver_memplot(fpops, 500*1000, g_heap, g_heap_size, g_colormap);
    }
    else if (g_operation_mode == OPMODE_ALLOCSTATS) {
        alloc_driver_allocstats(fpops, 500*1000, g_heap, g_heap_size, g_colormap);
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
            fprintf(stderr, "Pointer %x free'd %d times\n", (ptr_t)it->first, it->second);
    }
    {
    handle_count_map_t::iterator it;
    for (it=g_handle_free.begin(); it != g_handle_free.end(); it++) 
        if (it->second != 1)
            fprintf(stderr, "Handle %d free'd %d times\n", it->first, it->second);
    }

    fprintf(stderr, "highest address: 0x%X adjusted for heap start (0x%X) = %d kb\n", (ptr_t)g_highest_address, (ptr_t)g_heap, ((ptr_t)g_highest_address - (ptr_t)g_heap) / 1024);


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
    if (g_operation_mode == OPMODE_MEMPLOT) {
        {
        alloc_stat_t::iterator it;
        for (it=g_alloc_stat.begin(); it != g_alloc_stat.end(); it++) {
            fprintf(fpstats, "    {'op_index': %10d, 'free': %9d, 'used': %9d, 'overhead': %9d, 'fragmentation': %7.2f}%s\n", g_oplimit, std::get<0>(*it), std::get<1>(*it), std::get<2>(*it), std::get<3>(*it),
                    (it+1) != g_alloc_stat.end() ? ", " : " ");
        }
        }

    } else if (g_operation_mode == OPMODE_ALLOCSTATS) {
        {
        alloc_stat_t::iterator it;
        for (it=g_maxmem_stat.begin(); it != g_maxmem_stat.end(); it++) {
            if (g_oplimit > 0)
                fputc(',', fpstats);
            fprintf(fpstats, "    {'op_index': %6d, 'free': %6d, 'used': %6d, 'overhead': %6d, 'maxmem': %7.2f, 'current_op_time': %6d, 'oom_time': %6d, 'optime_maxmem': %6d, 'op': '%c', 'size': %6d}\n", g_oplimit,
                    std::get<0>(*it), std::get<1>(*it), std::get<2>(*it), std::get<3>(*it),
                    std::get<4>(*it), std::get<5>(*it), std::get<6>(*it), std::get<7>(*it),
                    std::get<8>(*it));
        }
        }
    }
    //if (g_oplimit == 0) fprintf(fpstats, "]\n");
    fprintf(stderr, "Memory stats (Python data) written to %s\n", statsfile);


    fclose(fpstats);
    fclose(fpops);

    free(g_colormap);
    free(g_heap);

    return 0;
}

