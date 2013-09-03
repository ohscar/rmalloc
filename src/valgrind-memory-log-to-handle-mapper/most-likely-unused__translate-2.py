#!/usr/bin/env python

"""
translate-2.py -- write C allocation code

Reads tuples and generates mallock / lock/unlock / free instructions.

Types.

('new', size, address)
('free', address)

Access:
('store', address, size)
('load', address, size)
('modify', address, size)


All 'new' are collected as start-end pairs, referensed as 'id'. Each access op
is checked against the start-end pairs, and if one is found, the (op, id)
tuple is added to a global list of ops.

The list will look something like:

[(0, 'new', size),
 (0, 'store')
 (0, 'modify'),
 (1, 'new', size),
 (0, 'free),
 (1, 'modify')
 ...]

Maximum heap usage is naively determined by looking at the difference between
the lowest and the highest address times 1.5. This is what cmalloc() will be
initialized with, other allocators will work as usual (where lock/unlock will
be NOPs.)

The list is translated to C: a memory_t* array and a bunch of lock/unlock()
calls.

FIXME: When/where to insert compact() calls?
"""

import numpy as np
import matplotlib.mlab as mlab
import matplotlib.pyplot as plt

import sys

#import blocks
blocks.g_ops_filename = None
blocks.g_ops_file = None

# out-of-bounds ops. do something with them later.
g_out_of_bounds = []

# largest address + size
g_heap_top = 0

# how much of the heap is used, not counting alignment, structures and
# similar.
g_perfect_bytes_used = 0

def process_ops():
    """Calculate various interesting things from the gathered data:

        * How many ops per handle
        * figure out how many ops in total
        * TODO calculate life time (handle_op_count / all_op_count (within lifetime))
    """
    handle_ops = {}
    total_ops = 0
    for op in blocks.g_ops:
        if op[1] == 'N':
            handle_ops[op[0]] = []
        elif op[1] in ['L', 'S', 'M']:
            try:
                handle_ops[op[0]].append(op[1])
                total_ops += 1
            except:
                print "Handle", op[0], "has no previous new associated:", op

    lifetime_ops = {}
    for op in blocks.g_ops:
        if op[1] == 'N':
            # live, own count, other count
            lifetime_ops[op[0]] = [True, 0, 0]
        elif op[1] == 'F':
            lifetime_ops[op[0]][0] = False
        #elif op[1] in ['L', 'S', 'M']:
        else:
            try:
                # increase the 'other count' for all live handles
                for key in lifetime_ops.keys():
                    if lifetime_ops[key][0]:
                        if key == op[0]:
                            lifetime_ops[key][1] += 1
                        else:
                            lifetime_ops[key][2] += 1
            #print "lifetime op other", lifetime_ops[key]
            except:
                print "Handle", op[0], "has no previous new associated:", op

    return handle_ops, lifetime_ops, total_ops


def plot_histogram(xs, title):
    n, bins, patches = plt.hist(xs, bins=75, histtype='stepfilled')

    plt.xlabel("Lifetime")
    plt.ylabel("Handle count")
    plt.title("Histogram of memory area lifetime: " + title)
    #plt.axis([100, 0, 0, len(bins)*0.75])
    plt.axis('tight')
    plt.grid(True)
    #plt.show()
    plt.savefig(title + "-histogram.pdf")

"""
    # the histogram of the data
    n, bins, patches = plt.hist(x, 50) # , normed=1, facecolor='green', alpha=0.75)

    # add a 'best fit' line
    #y = mlab.normpdf( bins, mu, sigma)
    #l = plt.plot(bins, y, 'r--', linewidth=1)

    plt.xlabel('Smarts')
    plt.ylabel('Probability')
    plt.title(r'$\mathrm{Histogram\ of\ IQ:}\ \mu=100,\ \sigma=15$')
    plt.axis([40, 160, 0, 0.03])
    plt.grid(True)

    plt.show()
"""

def main():
    global g_out_of_bounds
    fname = "standard-input"
    mode = 'generate'
    if len(sys.argv) == 3:
        if sys.argv[1].startswith('--'):
            fname = sys.argv[2]
            f = file(fname, "r")
            if sys.argv[1] == '--asciiplot':
                mode = 'asciiplot'
        else:
            print "bad params."
            sys.exit(1)
    elif len(sys.argv) == 2:
        if sys.argv[1].startswith('--'):
            f = sys.stdin()
            if sys.argv[1] == '--asciiplot':
                mode = 'asciiplot'
        else:
            fname = sys.argv[1]
            #f = file(fname, "r")
    else:
        print "bad params."
        sys.exit(1)


    #blocks.g_ops_filename = fname + "-ops"
    #blocks.g_ops_file = open(blocks.g_ops_filename, "rt")
    g_ops_filename = fname + "-ops"
    g_ops_file = open(g_ops_filename, "rt")

    handle_count = 0

    #print >> sys.stderr, "reading ops from file", blocks.g_ops_filename
    print >> sys.stderr, "reading ops from file", g_ops_filename
    i = 0
    bytes_read = 0
    skipped = 0
    chars = "-\|/"
    chari = 0
    lh, lo, la, ls = -1, 'x', -1, -1

    cout = open(fname + "-allocations.c", "wt", buffering=8*1024*1024)
    print >> sys.stderr, "generating allocations to", fname + "-allocations.c"
    print >> cout, "void malloc_lock_test() {"
     
    #for line in blocks.g_ops_file.xreadlines():
    for line in g_ops_file.xreadlines():
        if i % 500000 == 0:
            chari += 1
            print " ->", chars[chari % 4], "%lu MB (%d K skipped)" % (bytes_read / 1048576.0, skipped/1000.0),"\r",

        i += 1

        bytes_read += len(line)

        ph, po, pa, ps = lh, lo, la, ls
        
        try:
            lh, lo, la, ls = line.split()
        except:
            continue
        lh, lo, la, ls = int(lh), lo, int(la), int(ls)

        # skip duplicates
        if ph == lh and lo in ['L', 'S', 'M'] and po in ['L', 'S', 'M']:
            skipped += 1
            continue
        else:
            # write allocation data to file
            if lo == 'N':
                print >> cout, "    g_h[%d] = u_m(%d);" % (lh, ls)
                print >> cout, "    p_m(%d, %d);" % (lh, ls)
            elif lo == 'F':
                print >> cout, "    u_f(g_h[%d]);" % lh
                print >> cout, "    p_f(%d);" % (lh)
            else:
                print >> cout, "    u_l(g_h[%d]);" % lh
                print >> cout, "    u_u(g_h[%d]);" % lh

        if handle_count < lh:
            handle_count = lh

    #blocks.g_ops_file.close()
    g_ops_file.close()

    print >> cout, "memory_t *g_h[%d];" % handle_count
    print >> cout, "}"
    cout.close()

main()

