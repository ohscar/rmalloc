#!/usr/bin/env python
"""
grapher.py - draw curve of data in the following format:

HS<heap_size>
#<counter> MD<memory_delta> FB<free_bytes> LAB<largest_allocatable_block> FP<fragmentation_percent> T<time_in_usec> OOM<caused_oom>
...
#<counter> MD<memory_delta> FB<free_bytes> LAB<largest_allocatable_block> FP<fragmentation_percent> T<time_in_usec> OOM<caused_oom>

e.g.

HS2147483648
#796128 MD-36 FB2146626120 LAB2146626120 FP0 T1 OOM4
"""

import sys
import random
import numpy as np
import matplotlib.mlab as mlab
import matplotlib.pyplot as plt

# reference value color first
COLORS = ["b-", "r-", "g-", "b-", "y-", "m-", "c-"]
g_figure = plt.figure(figsize=(19.2,12.0), dpi=300)
g_figure_nr = 1

def subplot2(t, xl, yl):
    global g_figure_nr

    ax = g_figure.add_subplot(2, 1, g_figure_nr, xlabel=xl, ylabel=yl, title=t)
    g_figure_nr += 1

    return ax

def subplot(t, xl, yl):
    global g_figure_nr

    ax = g_figure.add_subplot(1, 1, g_figure_nr, xlabel=xl, ylabel=yl, title=t)
    g_figure_nr += 1

    return ax

# allocsys is a list of (allocator_name, ys)
# first is the ref alloc.
def plot_memory(allocsys, hs):
    ax = subplot("Heap usage", "", "Memory usage (kb)")

    for i in range(len(allocsys)):
        name, ys = allocsys[i]
        ax.plot(ys, COLORS[i])

    #plt.axis([100, 0, 0, len(bins)*0.75])
    #plt.axis([0, len(ys), 0, hs/1024.0])
    #plt.axis('tight')
    #plt.grid(True)
    #plt.show()

def plot_speed(allocsys, hs):
    ax = subplot("Speed", "Time", "Time (usec)")

    for i in range(len(allocsys)):
        name, ys = allocsys[i]
        rs = []
        v = 15
        for j in range(len(ys)):
            rs.append(v)
            v += random.randint(-1, 1)
            if v > 100:
                v = 100
            elif v < 0:
                v = 0
        ax.plot(rs, COLORS[i])

def plot_allocstats(allocstats):
    opstats = allocstats['alloc_stats']

    # sort opstats by 'op_index'
    opstats.sort(key=lambda x: x['op_index'])

    if allocstats['opmode'] == 'fragmentation':
        opmode = 'fragmentation'
        pretty = "Fragmentation (%)"
    elif allocstats['opmode'] == 'maxmem':
        opmode = 'maxmem'
        pretty = "Largest block size (bytes)"

    driver = allocstats['driver']
    opsfile = allocstats['opsfile']
    heap_size = allocstats['heap_size']
    theoretical_heap_size = allocstats['theoretical_heap_size']
    #title = "Driver '%s' running on input '%s' with heap size %d bytes (%d kb)" % (driver, opsfile, heap_size, heap_size/1024)
    title = "Theofree vs actual free RAM over time [%s / %s]\nActual heap %d bytes / %d kb, heap theosize %d bytes / %d kb" % (driver, opsfile, heap_size, heap_size/1024, theoretical_heap_size, theoretical_heap_size/1024)
    ax = subplot(title, "Time (op)", pretty)

    if opmode == 'fragmentation':
        max_size = 0
        for op in opstats:
            m = op['free'] + op['overhead'] + op['used']
            if m > max_size:
                max_size = m

        def s(n):
            return float(n)/max_size * 100.0

        frag = [op[opmode] for op in opstats]
        free = [s(op['free']) for op in opstats]
        used = [s(op['used']) for op in opstats]
        overhead = [s(op['overhead']) for op in opstats]

        def log10_(x):
            from math import log10
            if x == 0:
                return 0
            else:
                return log10(x)

        frag = map(log10_, frag)
        free = map(log10_, free)
        used = map(log10_, used)
        overhead = map(log10_, overhead)

        p1, = ax.plot(frag, 'b-', label=pretty)
        p2, = ax.plot(free, 'g-', label='Free')
        p3, = ax.plot(used, 'r-', label='Used')
        p4, = ax.pot(overhead, 'k-', label='Overhead')

        ax.yaxis.tick_right()
        ax.yaxis.set_label_position('right')

        lines=[p1, p2, p3, p4]
        plt.legend(lines, [l.get_label() for l in lines], loc=2) # upper left

    elif opmode == 'maxmem':
        max_size = 0
        for op in opstats:
            m = op['free'] + op['overhead'] + op['used']
            if m > max_size:
                max_size = m

        def ratio(op):
            #return op[opmode] / op['free']
            return (op['free'] - op['maxmem']) / 1024.0
            #return op[opmode]

        def ratiopercent(op):
            return (op['free'] - op['maxmem']) / op['free'] * 100.0
            #return op[opmode] / op['free'] * 100.0

        def s(n):
            return float(n)/max_size * 100.0

        freediff = [ratio(op) for op in opstats]
        freediffpercent = [ratiopercent(op) for op in opstats]
        maxmem = [op['maxmem'] for op in opstats]
        free = [op['free'] for op in opstats]
        used = [op['used'] for op in opstats]
        overhead = [op['overhead'] for op in opstats]

        #ax.plot(frag, 'b-', label=pretty)
        p1, = ax.plot(freediff, 'r-', label='Diff (kb)')
        p1a, = ax.plot(map(lambda x: x/1024, free), 'y-', label='Free: theory (kb)')
        p1b, = ax.plot(map(lambda x: x/1024, maxmem), 'g-', label='Free: actual (kb)')

        ax2 = ax.twinx()
        ax2.yaxis.tick_left()
        ax2.yaxis.set_label_position("left")
        p2, = ax2.plot(freediffpercent, 'b-', label='Diff (%)')
        ax.set_ylabel('Diff actual vs theoretical')

        ax.yaxis.tick_right()
        ax.yaxis.set_label_position('right')
        ax2.set_ylabel('Diff (%) in usable memory vs total theoretical memory')
        ax.set_ylabel('Diff (kbytes)')

        lines=[p1, p2, p1a, p1b]
        #lines=[p2]
        plt.legend(lines, [l.get_label() for l in lines], loc=3) # 2=upper left, 4=lower right, 3=lower left

def main():
    if len(sys.argv) < 2:
        print "usage: %s plotdatafile" % sys.argv[0]
        print "<plotsdatafile> is the name of a file of Python data generated from plot_<driver>."
        sys.exit(1)

    plotfilename = sys.argv[1]
    allocstats = {}
    execfile(plotfilename, {}, allocstats)

    print "Reading data:", plotfilename

    plot_init()

    plot_allocstats(allocstats)

    figurefile = "plot-%s-%s.png" % (allocstats['driver'], allocstats['opsfile'])
    plot_save(figurefile)

    #plt.show()

#
# App
#
def main2():
    if len(sys.argv) < 3:
        print "usage: %s: lifetime refplot plot1 [...]" % sys.argv[0]
        print "all arguments are files."
        print "lifetime is calculated lifetime values for current alloc data."
        print "plot1 [...plotN] are data files generated by different allocators, for current alloc data."
        sys.exit(1)

    allocs = []

    lifetime = []
    try:
        lifetime_file = open(sys.argv[1])
    except:
        lifetime_file = None
    if lifetime_file:
        found = False
        for line in lifetime_file.xreadlines():
            if not found and line.find("macro") > 0:
                found = True
            if found:
                # """# 230090: 78% (own = 13, other = 164322011)"""
                data = line.split()
                if data[0] != '#':
                    break

                try:
                    handle = int(data[1][:-1])
                    percent = int(data[2][:-1])
                except:
                    continue

                if percent >= 90:
                    lifetime[handle] = percent

    # extract all data
    for fname in sys.argv[2:]:
        f = open(fname, "r")
        if not f:
            sys.exit(1)

        line = f.readline()
        heap_size = int(line[2:])
        print "Heap size:", heap_size
        data = []
        for line in f.xreadlines():
            #796128 MD-36 FB2146626120 LAB2146626120 FP0 T1 OOM4
            count, delta, free_bytes, lab, fragmentation, optime, caused_oom = line.split()

            count = int(count[1:])
            delta = int(delta[2:])
            free_bytes = int(free_bytes[2:])
            lab = int(lab[3:])
            fragmentation = int(fragmentation[2:])
            optime = int(optime[1:])
            caused_oom = int(caused_oom[3:])

            data.append((count, delta, free_bytes, lab, fragmentation, optime, caused_oom))

        f.close()

        print "appending allocs"
        allocs.append((fname, data))

    plot_init()

    # plot memory usage
    allocsys = []
    for fname, data in allocs:
        ys = []
        for count, delta, free_bytes, lab, fragmentation, optime, caused_oom in data:
            # do awesome calculations
            ys.append((heap_size - free_bytes)/1024.0)
            #ys.append(free_bytes)
        allocsys.append((fname, ys))

    print "plotting memory"
    plot_memory(allocsys, heap_size)

    # plot speed
    allocsys = []
    for fname, data in allocs:
        ys = []
        for count, delta, free_bytes, lab, fragmentation, optime, caused_oom in data:
            # do awesome calculations
            ys.append(optime)
            #ys.append(free_bytes)
        allocsys.append((fname, ys))

    plot_speed(allocsys, heap_size)

    plot_save()

def plot_init():
    pass
    #plt.axis('tight')
    #plt.grid(True)

def plot_save(fname):
    print "Saving plot to", fname
    plt.savefig(fname)

if __name__ == '__main__':
    main()


