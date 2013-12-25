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
import matplotlib as mpl
import matplotlib.mlab as mlab
import matplotlib.pyplot as plt
import json

import copy

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

MIN_RANGE = 75 # items on X axis.

from drange import drange

def formatter_x_to_percent(x, pos):
    return "{0}".format(round(x*100.0, 2))

def formatter_max_to_percent(y, pos, ymax):
    p = float(y)/ymax * 100.0
    return "{0}\n{1}%".format(y, round(p, 2))

def plot_histogram(xs, filename, title, xlabel,      lower, upper, normed=False):
    old_lower, old_upper = int(lower*1000), int(upper*1000)

    # lower/upper is in percentage.
    # in order to see what's appropriate, we need to figure out the scale.
    # like this?
    # http://stackoverflow.com/questions/15177203/how-is-the-pyplot-histogram-bins-interpreted

    matching_xs = [x for x in xs if x >= lower and x <= upper]
    if len(matching_xs) == 0:
        print "Nothing matching for", lower, upper
        return

    """
    if lower == 75:
        i = 0
        for x in matching_xs:
            print x,
            if i % 80 == 0:
                print
            i+=1
        #matching_xs = [99]*20 + [90]*10 + [80]*5
        pass
    """

    # at least MIN_RANGE steps.
    step = upper-lower
    if step < MIN_RANGE:
        step = float(step) / MIN_RANGE
    else:
        step = 1.0

    bins = [i for i in drange(lower, upper+0.000001, step)]
    #print "bins from lower to upper:", bins
    #n, bins, patches = plt.hist(matching_xs, bins=[0, 70,80,90,100], histtype='stepfilled')
    #n, bins, patches = plt.hist(xs, bins=100, range=(lower, upper), histtype='stepfilled')
    #n, bins, patches = plt.hist(xs, bins=100, histtype='stepfilled')
    #n, bins, patches = plt.hist(matching_xs, bins=100, histtype='stepfilled')
    n, bins, patches = plt.hist(matching_xs, bins=bins, normed=normed, histtype='stepfilled')
    #n, bins, patches = plt.hist(matching_xs, bins=bins, histtype='stepfilled')
    #print "n =", n
    print "lower, upper =", lower, upper
    #print "bins =", bins
    #print "patches =", patches


    plt.xlabel(xlabel)
    plt.ylabel("Handle count / % of max")
    plt.title("Histogram of memory area lifetime (%d - %d): %s" % (old_lower, old_upper, title))

    #plt.axis([100, 0, 0, len(bins)*0.75])

    plt.axis('tight')
    plt.grid(True)

    ax = plt.gca()
    ax.xaxis.set_major_formatter(mpl.ticker.FuncFormatter(formatter_x_to_percent))
    ymin, ymax = ax.get_ylim()
    ax.yaxis.set_major_formatter(mpl.ticker.FuncFormatter(lambda y, pos: formatter_max_to_percent(y, pos, ymax)))

    #plt.show()
    fname = "%s-histogram-%d-%d.pdf" % (filename, old_lower, old_upper)
    plt.savefig(fname)
    print "Saved histogram:", fname

    plt.clf()
    plt.cla()
    plt.close()

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

    g_opslist_filename = fname + "-json-ops"
    g_opslist_file = None

    ops_list = []
    ops_list_read = False
    live_objects = {}

    lifetime_ops = {}
    g_lifetime_filename = fname + "-lifetime"
    if os.path.exists(g_lifetime_filename):
        print "Cached lifetime exists, loading...",
        lifetime_data = json.loads(open(g_lifetime_filename, "rt").read())
        if type(lifetime_data) != dict or not lifetime_data.has_key('lifetime-ops'):
            lifetime_ops = {}
            print "invalid, regenerating"
        else:
            try:
                lifetime_ops = lifetime_data['lifetime-ops']
                ops_counter = lifetime_data['mod-op-count']
                print "OK. %d items, ops counter = %d\n" % (len(lifetime_ops.keys()), ops_counter)
            except:
                print "invalid, regenerating"
                lifetime_ops = {}

    if os.path.exists(g_opslist_filename):
        try:
            ops_list = json.loads(open(g_opslist_filename, "rt").read())
            if type(ops_list) != list:
                ops_list = []
            ops_list_read = True
        except:
            ops_list = []

    if not ops_list:
        g_ops_file = open(g_ops_filename, "rt")
        handle_count = 0
        ops_counter = 0
        print >> sys.stderr, "Reading ops from file into opslist:", g_ops_filename
        i = 0
        bytes = 0
        skipped = 0
        chars = "-\|/"
        chari = 0
        lh, lo, la, ls = -1, 'x', -1, -1
        for line in g_ops_file.xreadlines():
            if i % 500000 == 0:
                chari += 1
                print >> sys.stderr,  " ->", chars[chari % 4], "%.2f MB skipped %lu K duplicate ops (handle count %d)" % (bytes / 1048576.0, skipped/1000, handle_count),"\r",

            i += 1

            #bytes += len(line)

            #ph, po, pa, ps = lh, lo, la, ls
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
            op = (lh, lo, la, ls)
            ops_list.append(lh)

        print >> sys.stderr, "\n"
        #print >> sys.stderr, " ->", chars[chari % 4], "%.2f MB skipped %lu K duplicate ops (handle count %d, dead ops %d, alive ops %d)" % (bytes / 1048576.0, skipped/1000, handle_count),"\r",
        g_ops_file.close()

        print "Saving ops data:", g_opslist_filename
        g_opslist_file = open(g_opslist_filename, "wt")
        g_opslist_file.write(json.dumps(ops_list))
        g_opslist_file.close()
        del ops_list


    if not lifetime_ops:
        g_ops_file = open(g_ops_filename, "rt")
        g_lifetime_file = open(g_lifetime_filename, "wt")

        handle_count = 0
        lifetime_ops = {}

        dead_ops = {}
        ops_counter = 0
        print >> sys.stderr, "Reading ops from file into lifetime", g_ops_filename
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
                op = (lh, lo, la, ls)
                if lo == 'N':
                    # own count, current total ops
                    lifetime_ops[lh] = [0, ops_counter]
                    #live_objects[lh] = [i, 0]
                    #lo = copy.deepcopy(live_objects)
                    #live_objects_time.append(lo)
                elif op[1] == 'F':
                    dead_ops[lh] = copy.deepcopy(lifetime_ops[lh])
                    # ops_counter - own count - ops counter at creation = correct number of others ops
                    dead_ops[lh][1] = ops_counter - dead_ops[lh][0] - dead_ops[lh][1]
                    del lifetime_ops[lh]

                    #del live_objects[lh]
                    #lo = copy.deepcopy(live_objects)
                    #live_objects_time.append(lo)
                    #live_objects[lh][1] = i
                #elif op[1] in ['L', 'S', 'M']:
                else:
                    lifetime_ops[lh][0] += 1
                    ops_counter += 1

            if handle_count < lh:
                handle_count = lh

        print >> sys.stderr, " ->", chars[chari % 4], "%.2f MB skipped %lu K duplicate ops (handle count %d, dead ops %d, alive ops %d)" % (bytes / 1048576.0, skipped/1000, handle_count, len(dead_ops.keys()), len(lifetime_ops.keys())),"\r",


        print >> sys.stderr, "\n\nmoving remaining alive ops (%d of them) to dead area" % len(lifetime_ops.keys())
        # move remaining alive ops to dead, to get a correct value of "other"
        for key in lifetime_ops.keys():
            dead_ops[key] = lifetime_ops[key]
            # ops_counter - own count - ops counter at creation = correct number of others ops
            dead_ops[key][1] = ops_counter - dead_ops[key][0] - dead_ops[key][1]

        g_ops_file.close()

        print >> sys.stderr, "processing."

        lifetime_ops = copy.deepcopy(dead_ops)
        del dead_ops

        print "Saving lifetime data:", g_lifetime_filename
        lifetime_data = {'mod-op-count': ops_counter, 'lifetime-ops': lifetime_ops}
        g_lifetime_file.write(json.dumps(lifetime_data))
        g_lifetime_file.close()

    #
    # stats
    #
    st = open(fname + "-statistics", "wt")

    handles = {}

    stats = []

    """
    print >> st, "Live objects (count) over time"
    for objs in live_objects_time:
        l = len(objs.keys())
        print >> st, l
    """

    """
    print >> st, "Individual object lifetime"
    for key in live_objects.keys():
        se = live_objects[key]
        if se[1] > se[0]:
            start, end = se
            print >> st, "%d # %d - %d" % (end-start, start, end)
    """

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

    print "Calculating micro/macro lifetime."
    total_count = ops_counter
    for key in lifetime_ops.keys():
        own = lifetime_ops[key][0]
        other = lifetime_ops[key][1]
        # XXX: What is more correct - 1 or 0.00001?
        if own == 0:
            own = 1.0
        micro_lifetime = float(other)/float(own)
        macro_lifetime = float(other+own)/float(total_count)

        #print "+ ", macro_lifetime
        v = (int(key), micro_lifetime, macro_lifetime, own, other)
        #print "appending", v
        stats.append(v)

    """
    total_count = ops_counter
    for key in lifetime_ops.keys():
        own = lifetime_ops[key][1]
        other = lifetime_ops[key][2]
        micro_lifetime = float(other)/float(own)
        macro_lifetime = float(other+own)/float(total_count)
        #print "+ ", macro_lifetime
        stats.append((key, micro_lifetime, macro_lifetime, own, other))
    """


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

        #lt = int(macro_lifetime*100.0) # into percentage
        #lt = int(macro_lifetime*100.0) # to get more bins.
        lt = macro_lifetime # to get more bins.
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

    # Sort such that we can cut the data off where requested.
    xs.sort(reverse=True)
    for lower, upper in [(0, 0.001), (0.0001, 0.02), (0.01, 0.15), (0.75, 1.0), (0.01, 1.0), (0, 1.0)]:
        normed = False
        plot_histogram(xs, fname+"-macro", plot_title+": Macro", "(other+own ops within lifetime of handle) / total ops => overall lifetime (%)",
                       lower, upper, normed)

    # X axis is lifetime
    # xs contains lifetimes. we only want to show the ones with the longest lifetime,
    # not arbitrary. sorting and counting lifetime, only displaying the ones above a certain threshold.
    if False:
        print "Micro histograms."
        micro_xs.sort(reverse=True)
        for lower, upper in [(0, 0.01), (0.10, 0.15), (.75, 1.0), (0, 1.0)]:
            try:
                plot_histogram(micro_xs, fname+"-micro", plot_title+": Micro", "other ops / own ops => activity of handle within its lifetime (%)",
                               lower, upper)
            except:
                print "Couldn't plot micro_xs for lower", lower, "to upper", upper

    #for lower, upper in [(0, 3), (10, 15), (75, 100)]: plot_histogram(xs, fname, lower, upper)

    # XXX: TODO. this could be cached data. don't have to read it again.
    """
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
    """

if __name__ == '__main__':
    main()


