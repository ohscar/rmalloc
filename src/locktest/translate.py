#!/usr/bin/env python
"""
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

import blocks

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
            f = file(fname, "r")
    else:
        print "bad params."
        sys.exit(1)


    blocks.g_ops_filename = fname + "-ops"
    blocks.g_ops_file = open(blocks.g_ops_filename, "wt")

    print >> sys.stderr, "reading from file", fname
    i = 0
    bytes = 0
    chars = "-\|/"
    chari = 0
    for line in f.xreadlines():
        if i % 500000 == 0:
            chari += 1
            print " ->", chars[chari % 4], "%lu MB" % (bytes / 1048576.0), "\r",

        i += 1

        bytes += len(line)
        blocks.process_line(line)

    f.close()

    blocks.g_ops_file.close()

    """
    print >> sys.stderr, "saving translated copy of block list."

    blocksfile = open(fname + "-blocks", "wt")
    for memory_id in xrange(blocks.length()):
        #b = g_blocks[memory_id]
        b = blocks.get(memory_id)
        #print >> blocksfile, "%lu %lu %lu" % (b[0], b[1], b[2])
        print >> blocksfile, "%lu %lu" % (b[0], b[1])
    blocksfile.close()

    print >> sys.stderr, "saving translated copy of op list."

    ops = open(fname + "-ops", "wt")
    for op in blocks.g_ops:
        print >> ops, op
    ops.close()
    """

    #sys.exit(0)

    print >> sys.stderr, "processing."

    handle_ops, lifetime_ops, total_count = process_ops()

    length = blocks.length()
    del blocks.g_blocks

    #if mode == 'generate':
    if True:
        cout = open(fname + "-allocations.c", "wt")
        print >> cout, "memory_t *g_handles[%d];" % blocks.length()
        print >> cout, "void malloc_lock_test() {"
        for op in blocks.g_ops:
            if op[1] == 'N':
                print >> cout, "    g_handles[%d] = user_malloc(%d);" % (op[0], op[2])
                print >> cout, "    print_after_malloc_stats(%d, %d); // params: handle_index, size" % (op[0], op[2])
            elif op[1] == 'F':
                print >> cout, "    user_free(g_handles[%d]);" % op[0]
                print >> cout, "    print_after_free_stats(%d); // params: handle_index" % (op[0])
            else:
                print >> cout, "    user_lock(g_handles[%d]);" % op[0]
                print >> cout, "    user_unlock(g_handles[%d]);" % op[0]
        print >> cout, "}"
        cout.close()
    #elif mode == 'asciiplot':
    if True:
        st = open(fname + "-statistics", "wt")

        s = []
        for op in blocks.g_ops:
            if op[1] == 'N':
                s.append('O')
            elif op[1] == 'F':
                s.append('X')
            else:
                s.append('-')

        print >> st, "".join(s)

        stats = []
        for key in handle_ops.keys():
            stats.append((key, len(handle_ops[key])))
        print >> st, "\nOps per handle (sorted by time):"
        stats.sort(key=lambda x: x[0])
        for handle, opcount in stats:
            print >> st, "# %d: %d" % (handle, opcount)
        print >> st, "\nOps per handle (sorted by op count):"
        stats.sort(key=lambda x: x[1], reverse=True)
        for handle, opcount in stats:
            print >> st, "# %d: %d" % (handle, opcount)

        stats = []
        for key in lifetime_ops.keys():
            own = lifetime_ops[key][1]
            other = lifetime_ops[key][2]
            micro_lifetime = float(other)/float(own)
            macro_lifetime = float(other+own)/float(total_count)
            stats.append((key, micro_lifetime, macro_lifetime, own, other))
        print >> st, "\nOps per handle (sorted by micro lifetime):"
        stats.sort(key=lambda x: x[1])
        for handle, micro_lifetime, macro_lifetime, own, other in stats:
            print >> st, "# %d: %.2f (own = %d, other = %d)" % (handle, micro_lifetime, own, other)

        print >> st, "\nOps per handle (sorted by macro lifetime):"
        stats.sort(key=lambda x: x[2])
        xs = []
        for handle, micro_lifetime, macro_lifetime, own, other in stats:
            print >> st, "# %d: %d%% (own = %d, other = %d)" % (handle, macro_lifetime*100.0, own, other)
            xs.append(int(macro_lifetime*100.0))

        print >> st, "\nTotal number of ops:", total_count

        print >> st, "\nOut of bounds ops (%d ops):" % len(g_out_of_bounds)
        print >> st, g_out_of_bounds

        st.close()

        plot_histogram(xs, fname)

main()

