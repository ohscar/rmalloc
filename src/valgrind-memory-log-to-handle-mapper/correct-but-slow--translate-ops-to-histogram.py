#!/usr/bin/env python
"""
translate-3-2.py -- generate lifetime plot in a fast way.

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
import os.path

g_ops_filename = None
g_ops_file = None

MACRO_LIFETIME_THRESHOLD = 0.9

# out-of-bounds ops. do something with them later.
g_out_of_bounds = []

# largest address + size
g_heap_top = 0

# how much of the heap is used, not counting alignment, structures and
# similar.
g_perfect_bytes_used = 0

#LOWER_UPPER_SCALE = 1000
LOWER_UPPER_SCALE = 1

def plot_histogram(xs, filename, title, xlabel,      lower, upper):
    old_lower, old_upper = lower, upper
    n, bins, patches = plt.hist(xs, bins=75, histtype='stepfilled')

    plt.xlabel(xlabel)
    plt.ylabel("Handle count")
    plt.title("Histogram of memory area lifetime: " + title)
    #plt.axis([100, 0, 0, len(bins)*0.75])
    plt.axis('tight')
    plt.grid(True)
    #plt.show()
    fname = "%s-histogram-%d-%d.pdf" % (filename, old_lower, old_upper)
    print "Saving histogram:", fname
    plt.savefig(fname)

def plot_histogram2(xs, title, xlabel, lower, upper, ymax=None): #ymax=1000):
    old_lower, old_upper = lower, upper
    lower *= LOWER_UPPER_SCALE
    upper *= LOWER_UPPER_SCALE

    if len(xs) == 0:
        print "No bins for current histogram interval %d - %d" % (lower, upper)
        return
    bincount = 600
    matching_xs = [x for x in xs if x >= lower and x < upper]
    if len(matching_xs) < bincount:
        bincount=len(matching_xs)
    if len(matching_xs) == 0:
        return

    bincount=75

    n, bins, patches = plt.hist(matching_xs, bins=bincount, histtype='stepfilled', facecolor='g', alpha=0.75)
    #n, bins, patches = plt.hist(xs, bins=600, histtype='stepfilled')

    plt.xlabel(xlabel)
    plt.ylabel("Handle count")
    plt.title("Histogram of memory area lifetime: " + title)
    #plt.axis([100, 0, 0, len(bins)*0.75])
    plt.axis([lower, upper, 0, ymax])
    #plt.axis('tight')
    plt.grid(True)
    #plt.show()
    filename = "%s-histogram-%d-%d.pdf" % (title, old_lower, old_upper)
    print "Saving histogram:", filename
    plt.savefig(filename)

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


    g_ops_filename = fname + "-ops"
    g_ops_file = open(g_ops_filename, "rt")

    handle_count = 0

    lifetime_ops = {}
    dead_ops = {}
    ops_counter = 0
    print >> sys.stderr, "reading ops from file", g_ops_filename
    i = 0
    bytes = 0
    skipped = 0
    chars = "-\|/"
    chari = 0
    lh, lo, la, ls = -1, 'x', -1, -1
    for line in g_ops_file.xreadlines():
        if i % 100000 == 0:
            chari += 1
            print >> sys.stderr,  " ->", chars[chari % 4], "%.2f MB skipped %lu K duplicate ops (handle count %d, dead ops %d, alive ops %d)" % (bytes / 1048576.0, skipped/1000, handle_count, len(dead_ops.keys()), len(lifetime_ops.keys())),"\r",

        i += 1

        bytes += len(line)

        ph, po, pa, ps = lh, lo, la, ls

        try:
            lh, lo, la, ls = line.split()
        except:
            continue
        lh, lo, la, ls = int(lh), lo, int(la), int(ls)

        # skip duplicates, even though it's not entirely correct when measuring lifetime
        # however, it takes too long time to care about duplicates.
        #if False: # ph == lh and lo in ['L', 'S', 'M'] and po in ['L', 'S', 'M']:
        #    skipped += 1
        #    continue
        #else:
        if True:

            # g_ops.append((mid, op, size, address))
            # == op[0] == handle == lh
            # == op[1] == operation == lo
            # == op[2] == size == ls
            # == op[3] == address == la
            op = (lh, lo, ls, la)
            #print "+", op
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
                    ops_counter += 1
                #print "lifetime op other", lifetime_ops[key]
                except:
                    print "Handle", op[0], "has no previous new associated:", op
                    continue


            """
            op = (lh, lo, la, ls)
            if op[1] == 'N':
                # own count, current total ops
                lifetime_ops[op[0]] = [0, ops_counter]
            elif op[1] == 'F':
                dead_ops[lh] = lifetime_ops[op[0]]
                # ops_counter - own count - ops counter at creation = correct number of others ops
                dead_ops[lh][1] = ops_counter - dead_ops[lh][0] - dead_ops[lh][1]
                del lifetime_ops[lh]
            #elif op[1] in ['L', 'S', 'M']:
            else:
                lifetime_ops[lh][0] += 1

            ops_counter += 1
            """


        if handle_count < lh:
            handle_count = lh


    print  >> sys.stderr, " ->", chars[chari % 4], "%.2f MB skipped %lu K duplicate ops (handle count %d, dead ops %d, alive ops %d)" % (bytes / 1048576.0, skipped/1000, handle_count, len(dead_ops.keys()), len(lifetime_ops.keys())),"\r",

    print >> sys.stderr, "\n\nmoving remaining alive ops (%d of them) to dead area" % len(lifetime_ops.keys())

    """
    # move remaining alive ops to dead, to get a correct value of "other"
    for key in lifetime_ops.keys():
        dead_ops[key] = lifetime_ops[key]
        # ops_counter - own count - ops counter at creation = correct number of others ops
        dead_ops[key][1] = ops_counter - dead_ops[key][0] - dead_ops[key][1]

    g_ops_file.close()

    print >> sys.stderr, "processing."

    lifetime_ops = dead_ops
    del dead_ops
    """

    if True:
        st = open(fname + "-statistics", "wt")

        handles = {}

        stats = []

        """
        for key in lifetime_ops.keys():
            try:
                own = lifetime_ops[key][0]
                other = lifetime_ops[key][1]
                micro_inside = own+other
                if micro_inside == 0:
                    micro_inside = 1
                micro_lifetime = float(own)/float(micro_inside)
                macro_lifetime = float(other+own)/float(ops_counter)
                stats.append((key, micro_lifetime, macro_lifetime, own, other))
                if macro_lifetime >= MACRO_LIFETIME_THRESHOLD:
                    handles[key] = macro_lifetime
            except:
                continue
        """

        total_count = ops_counter
        for key in lifetime_ops.keys():
            own = lifetime_ops[key][1]
            other = lifetime_ops[key][2]
            micro_lifetime = float(other)/float(own)
            macro_lifetime = float(other+own)/float(total_count)
            #print "+ ", macro_lifetime
            stats.append((key, micro_lifetime, macro_lifetime, own, other))


        print >> st, "\nOps per handle (sorted by micro lifetime):"
        stats.sort(key=lambda x: x[1])
        micro_xs = []
        for handle, micro_lifetime, macro_lifetime, own, other in stats:
            print >> st, "# %d: %.2f (own = %d, other = %d)" % (handle, micro_lifetime, own, other)
            lt = int(micro_lifetime*100000.0)
            micro_xs.append(lt)

        print >> st, "\nOps per handle (sorted by macro lifetime):"
        stats.sort(key=lambda x: x[2])
        xs = []
        for handle, micro_lifetime, macro_lifetime, own, other in stats:
            print >> st, "# %d: %d%% (own = %d, other = %d)" % (handle, macro_lifetime*100.0, own, other)
            #xs.append(int(macro_lifetime*100.0))

            # XXX: Why scale by such a huge factor?  Does it make sense for eg. Opera?  Scale by an appropriate amount
            # in that case!
            #lt = int(macro_lifetime*100000.0)
            lt = int(macro_lifetime*100.0) # into percentage
            xs.append(lt)

        print >> st, "\nTotal number of ops:", ops_counter

        print >> st, "\nOut of bounds ops (%d ops):" % len(g_out_of_bounds)
        print >> st, g_out_of_bounds

        st.close()

        print "Macro histograms."
        # X axis is lifetime
        # xs contains lifetimes. we only want to show the ones with the longest lifetime,
        # not arbitrary. sorting and counting lifetime, only displaying the ones above a certain threshold.

        plot_title = os.path.basename(fname)

        # XXX: Net needed, it seems.
        xs.sort(reverse=True)
        for lower, upper in [(0, 1), (10, 15), (75, 100), (0, 100)]:
            plot_histogram(xs, fname+"-macro", plot_title+": Macro", "(other+own ops within lifetime of handle) / total ops => overall lifetime (%)",
                           lower, upper)

        # X axis is lifetime
        # xs contains lifetimes. we only want to show the ones with the longest lifetime,
        # not arbitrary. sorting and counting lifetime, only displaying the ones above a certain threshold.
        print "Micro histograms."
        micro_xs.sort(reverse=True)
        for lower, upper in [(0, 1), (10, 15), (75, 100), (0, 100)]:
            try:
                plot_histogram(micro_xs, fname+"-micro", plot_title+": Micro", "other ops / own ops => activity of handle within its lifetime (%)",
                               lower, upper)
            except:
                print "Couldn't plot micro_xs for lower", lower, "to upper", upper

        #for lower, upper in [(0, 3), (10, 15), (75, 100)]: plot_histogram(xs, fname, lower, upper)

    # generate new ops-lock file
    ops_filename = fname + "-ops"
    ops_lock_filename = fname + "-ops-lock"
    ops_file = open(ops_filename, "rt")
    ops_lock_file = open(ops_lock_filename, "wt")

    lifetime_ops = {}
    dead_ops = {}
    ops_counter = 0
    print >> sys.stderr, "\n\nGenerating locks file.\nReading ops from file", g_ops_filename
    i = 0
    bytes = 0
    skipped = 0
    chars = "-\|/"
    chari = 0
    lh, lo, la, ls = -1, 'x', -1, -1
    for line in ops_file.xreadlines():
        if i % 100000 == 0:
            chari += 1
            print >> sys.stderr,  " ->", chars[chari % 4], "%.2f MB skipped %lu K duplicate ops (handle count %d, dead ops %d, alive ops %d)" % (bytes / 1048576.0, skipped/1000, handle_count, len(dead_ops.keys()), len(lifetime_ops.keys())),"\r",

        i += 1

        bytes += len(line)

        ph, po, pa, ps = lh, lo, la, ls

        try:
            lh, lo, la, ls = line.split()
        except:
            continue
        lh, lo, la, ls = int(lh), lo, int(la), int(ls)

        # skip duplicates, even though it's not entirely correct when measuring lifetime
        # however, it takes too long time to care about duplicates.
        if ph == lh and lo in ['L', 'S', 'M'] and po in ['L', 'S', 'M']:
            skipped += 1
            continue
        else:
            if handles.has_key(lh):
                if lo == 'N':
                    # lock (= Open)
                    print >> ops_lock_file, "%d %s %d %d" % (lh, lo, la, ls)
                    print >> ops_lock_file, "%d O %d %d" % (lh, la, ls)
                elif lo == 'F':
                    # unlock (= Close)
                    print >> ops_lock_file, "%d C %d %d" % (lh, la, ls)
                    print >> ops_lock_file, "%d %s %d %d" % (lh, lo, la, ls)
                # else, we have a load/store, and since they're locked during the
                # entire lifetime, there's no extra store op.
            else:
                print >> ops_lock_file, "%d %s %d %d" % (lh, lo, la, ls)

    print >> sys.stderr
    ops_file.close()
    ops_lock_file.close()

main()

