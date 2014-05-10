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
import os.path

# reference value color first
COLORS = ["b-", "r-", "g-", "k-", "m-", "y-", "c-"]
#COLORS = ["k-", "k:", "k--", "k_", "k", "k", "k-."]
#COLORS = ["k--", "g", "r-.", "y_", "k ", "k", "k-."]
#COLORS = ["k--", "g", "r-.", "y_", "k ", "k", "k-."]
g_figure = None
g_figure_nr = 1

g_multiple_figure = None
g_multiple_figure_nr = 1

def log10_(x):
    from math import log10
    if x == 0:
        return 0
    else:
        return log10(x)

def subplot_multiple(t, xl, yl):
    global g_multiple_figure_nr

    ax = g_multiple_figure.add_subplot(2, 1, g_multiple_figure_nr, xlabel=xl, ylabel=yl, title=t)
    g_multiple_figure_nr += 1

    return ax

def plot_allocstats_multiple(app, allocstats_multiple):
    # figure out longest running plot and make all others as long.
    longest_length = 0
    for i in range(len(allocstats_multiple)):
        allocstats = allocstats_multiple[i]
        opstats = allocstats['alloc_stats']
        if allocstats['opmode'] != 'allocstats':
            continue

        opstats.sort(key=lambda x: x['op_index'])
        allocstats_multiple[i]['alloc_stats'] = opstats

        count_before_zero_maxmem = 0
        for j in range(len(opstats)):
            count_before_zero_maxmem += 1
            if opstats[j]['maxmem'] == 0:
                break

        opstats = opstats[:count_before_zero_maxmem]
        allocstats_multiple[i]['alloc_stats'] = opstats

        print "Driver %s has %d items before zero maxmem" % (allocstats['driver'], len(opstats))
        if len(opstats) > longest_length:
            longest_length = len(opstats)

    # Title of the graph, top title.
    #g_multiple_figure.suptitle(app) # disabled for now

    #ax = subplot_multiple(title, "Time (op)", pretty)
    accumulated_times = []
    ax = subplot_multiple("Time per operation (nanoseconds), logarithmic scale", "Op number", "")
    plots = []
    color_counter = 0
    for i in range(len(allocstats_multiple)):
        allocstats = allocstats_multiple[i]
        plot_color = COLORS[i]
        color_counter += 1

        opstats = allocstats['alloc_stats']
        if allocstats['opmode'] != 'allocstats':
            continue

        opmode = 'allocstats'
        pretty = "Largest block size (bytes)"

        driver = allocstats['driver']
        opsfile = allocstats['opsfile']
        heap_size = allocstats['heap_size']
        theoretical_heap_size = allocstats['theoretical_heap_size']
        title = "%s: %s (heap size: %d bytes (%d KiB))\nMax allocatable block size at each point, plus time per operation" % (driver, opsfile, heap_size, heap_size/1024)

        def ratio(op):
            #return op[opmode] / op['free']
            #return (op['free'] - op['maxmem']) / 1024.0
            #return op[opmode]
            #return (op['free'] - op['used'] - op['maxmem']) / 1024
            return (op['free'] - op['maxmem']) / 1024

        def ratiopercent(op):
            return (op['free'] - op['maxmem']) / op['free'] * 100.0
            #return op[opmode] / op['free'] * 100.0

        freediff = [ratio(op) for op in opstats]
        freediffpercent = [ratiopercent(op) for op in opstats]
        maxmem = [op['maxmem'] for op in opstats]
        free = [op['free'] for op in opstats]
        used = [op['used'] for op in opstats]
        overhead = [op['overhead'] for op in opstats]

        #ax.plot(frag, 'b-', label=pretty)

        #
        # Make use of size!
        #
        # diff(heap_size, [sum('size') until this point], actual allocated) => "difference between theoretically available and actual available"
        #

        accumulated = False
        logarithmic = True
        op = opstats[0]
        print "calculating time."
        accumulated_time_nonlog = [op['current_op_time'] + op['oom_time']]
        accumulated_time = [op['current_op_time'] + op['oom_time']]

        if logarithmic:
            accumulated_time[0] = log10_(accumulated_time[0])

        for i in range(1, len(opstats)):
            op = opstats[i]

            current = op['current_op_time'] + op['oom_time']

            previous = accumulated_time_nonlog[i-1]
            thetime = 0
            if accumulated:
                thetime = previous + current
            else:
                if op['op'] == 'N':
                    thetime = current
                else:
                    thetime = previous

            accumulated_time_nonlog.append(thetime)
            if logarithmic:
                bef = thetime
                thetime = log10_(thetime)

            accumulated_time.append(thetime)

        if len(accumulated_time) < longest_length:
            accumulated_time.extend([0] * (longest_length - len(accumulated_time)))


        #p1a, = ax.plot(map(lambda x: x/1024, free), 'g-', label='Theoretical usable space')
        #p1b, = ax.plot(map(lambda x: x/1024, maxmem), 'r-', label='Allocatable')
        #p1, = ax.plot(freediff, 'y-', label='diff Allocatable vs Theoretical')

        """
        ax2 = ax.twinx()
        ax2.yaxis.tick_left()
        ax2.yaxis.set_label_position("left")
        ax2.set_ylabel('Time (us)')

        ax.yaxis.tick_right()
        ax.yaxis.set_label_position('right')
        ax.set_ylabel('KiB')
        """
        ax.set_ylabel('Time (ns, log10-scale)')

        #plot_time, = ax2.plot(accumulated_time, plot_color, label='Time: %s %s' % (driver, opsfile))
        plot_time, = ax.plot(accumulated_time, plot_color, label='%s' % (driver))
        accumulated_times.append(accumulated_time)

        plots.append(plot_time)

    plt.legend(plots, [plot.get_label() for plot in plots], loc=2) # loc= 2=upper left, 4=lower right, 3=lower left

    accumulated_spaces = []
    foo = []
    plots = []
    ax = subplot_multiple("Maximum allocatable memory after each operation from original allocation list", "Op number", "")
    for i in range(len(allocstats_multiple)):
        allocstats = allocstats_multiple[i]
        plot_color = COLORS[i]
        color_counter += 1

        opstats = allocstats['alloc_stats']
        if allocstats['opmode'] != 'allocstats':
            continue

        # sort opstats by 'op_index'
        opstats.sort(key=lambda x: x['op_index'])

        opmode = 'allocstats'
        pretty = "Largest block size (bytes)"

        driver = allocstats['driver']
        opsfile = allocstats['opsfile']
        heap_size = allocstats['heap_size']
        theoretical_heap_size = allocstats['theoretical_heap_size']
        title = "%s: %s (heap size: %d bytes (%d KiB))\nMax allocatable block size at each point, plus time per operation" % (driver, opsfile, heap_size, heap_size/1024)

        def ratio(op):
            #return op[opmode] / op['free']
            #return (op['free'] - op['maxmem']) / 1024.0
            #return op[opmode]
            #return (op['free'] - op['used'] - op['maxmem']) / 1024
            return (op['free'] - op['maxmem']) / 1024

        def ratiopercent(op):
            return (op['free'] - op['maxmem']) / op['free'] * 100.0
            #return op[opmode] / op['free'] * 100.0

        freediff = [ratio(op) for op in opstats]
        freediffpercent = [ratiopercent(op) for op in opstats]
        maxmem = [op['maxmem'] for op in opstats]
        free = [op['free'] for op in opstats]
        used = [op['used'] for op in opstats]
        overhead = [op['overhead'] for op in opstats]


        maxmem = map(lambda x: x/1024, maxmem)

        if len(maxmem) < longest_length:
            maxmem.extend([0] * (longest_length - len(maxmem)))

        #p1a, = ax.plot(map(lambda x: x/1024, free), 'g-', label='Theoretical usable space')
        sz = map(lambda x: x/1024, maxmem)
        p1b, = ax.plot(sz, plot_color, label='%s' % driver)
        #p1, = ax.plot(freediff, 'y-', label='diff Allocatable vs Theoretical')

        accumulated_spaces.append(sz)

        foo = maxmem[:]

        """
        ax2 = ax.twinx()
        ax2.yaxis.tick_left()
        ax2.yaxis.set_label_position("left")
        ax2.set_ylabel('Time (us)')

        ax.yaxis.tick_right()
        ax.yaxis.set_label_position('right')
        """
        ax.set_ylabel('KiB')

        #plot_time, = ax2.plot(accumulated_time, plot_color, label='Time: %s %s' % (driver, opsfile))
        #plot, = ax2.plot(accumulated_time, plot_color, label='Time: %s' % (driver))

        plots.append(p1b)

    plt.legend(plots, [plot.get_label() for plot in plots], loc=1) # loc= 2=upper left, 4=lower right, 3=lower left














    #################################################
    import operator, os.path

    tablelabelbase = os.path.basename(app)
    i = tablelabelbase.find('.')
    if i > 0:
        tablelabelbase = tablelabelbase[:i]

    def dostuffwith(foos, title, rev=False):
        if rev:
            bestfunc, worstfunc = operator.gt, operator.lt
        else:
            bestfunc, worstfunc = operator.lt, operator.gt
        # keep track of which allocator performed best at every point by increasing its index
        bestest = [0] * len(foos)
        worstest = [0] * len(foos)
        penalty = [0] * len(foos)
        goodness = [0] * len(foos)

        maxpen = float(len(foos[0]) * len(foos))
        for i in range(len(foos[0])): # for each point
            best_j = 0
            best = 0
            worst_j = 0
            worst = 0
            times = []
            for j in range(len(foos)): # for each allocator
                times.append(foos[j][i])
                if bestfunc(foos[j][i], best):
                    best = foos[j][i]
                    best_j = j
                if worstfunc(foos[j][i], worst):
                    worst = foos[j][i]
                    worst_j = j

            bestest[best_j] += 1
            worstest[worst_j] += 1

            # ascending order, i.e least time = lowest index
            timesindex = sorted((e, index) for index, e in enumerate(times))
            if rev:
                timesindex.reverse()

            best_goodness = timesindex[0][0]
            for j in range(len(timesindex)):
                foo, index = timesindex[j] # note, foo unused
                diff = float(abs(foo - best_goodness)) / float(best_goodness)

                #penalty[timesindex[j][1]] += j # quick = less penalty
                #penalty[index] += j # quick = less penalty

                #penalty[index] += j # quick = less penalty
                #goodness[index] += diff # j*diff

                penalty[index] += j # quick = less penalty
                goodness[index] += (j*diff) # j*diff


        print
        print ".. raw:: latex\n"
        print "   \\begin{table}"
        print "   \\begin{tabular}{r | l c c c}"
        print "   \\hline"
        print "   \\multicolumn{5}{c}{\\bf %s} \\\\" % title
        print "   \\hline"
        print "   {\\bf Driver} & {\\bf Penalty (count)} & {\\bf Penalty (weighted)} & {\\bf Best} & {\\bf Worst} \\\\"
        print "   \\hline"
        stats = []
        for i in range(len(allocstats_multiple)):
            pen = float(penalty[i])/maxpen * 100.0
            good = float(goodness[i])/maxpen * 100.0
            b = float(bestest[i])/float(len(foos[0]))*100.0
            w = float(worstest[i])/float(len(foos[0]))*100.0  #float(worstest[i])/float(len(foos))))
            stats.append((pen,
                          allocstats_multiple[i]['driver'],
                          good,
                          b,
                          w))

        stats.sort()
        for stat in stats:
            goodness = stat[2] - stat[3]
            print "   %s & %d\\%% & %d\\%% & %d\\%% & %d\\%% \\\\" % (stat[1], int(stat[0]), int(stat[2]), int(stat[3]), int(stat[4]))
            #print "%s % 15d % 15d % 15d" % (stat[1].ljust(32), stat[0], stat[2], stat[3])
        print "   \\hline"
        print "   \\end{tabular}"
        print "   \\caption{%s-%s}" % (tablelabelbase, title.lower())
        print "   \\label{table:%s-%s}" % (tablelabelbase, title.lower())
        print "   \\end{table}"
        print

    #################################################
    # T I M E
    #################################################

    # TODO: Vikta placeringen mot skillnaden!!!

    #ts = [time for times in accumulated_times for time in times]
    #median = sorted(ts)[len(ts)/2]
    dostuffwith(accumulated_times, "Speed")

    #################################################
    # S P A C E 
    #################################################

    dostuffwith(accumulated_spaces, "Space", rev=True)




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


def plot_allocstats(allocstats):
    opstats = allocstats['alloc_stats']

    # sort opstats by 'op_index'
    opstats.sort(key=lambda x: x['op_index'])

    if allocstats['opmode'] == 'memplot':
        opmode = 'memplot'
        pretty = "Fragmentation (%)"
    elif allocstats['opmode'] == 'allocstats':
        opmode = 'allocstats'
        pretty = "Largest block size (bytes)"

    driver = allocstats['driver']
    opsfile = allocstats['opsfile']
    heap_size = allocstats['heap_size']
    theoretical_heap_size = allocstats['theoretical_heap_size']
    #title = "Driver '%s' running on input '%s' with heap size %d bytes (%d kb)" % (driver, opsfile, heap_size, heap_size/1024)
    #title = "Theofree vs actual free RAM over time [%s / %s]\nActual heap %d bytes / %d kb, heap theosize %d bytes / %d kb" % (driver, opsfile, heap_size, heap_size/1024, theoretical_heap_size, theoretical_heap_size/1024)
    #title = "%s: %s\nActual heap %d bytes (%d KiB), heap theosize %d bytes / %d kb" % (driver, opsfile, heap_size, heap_size/1024, theoretical_heap_size, theoretical_heap_size/1024)
    title = "%s: %s (heap size: %d bytes (%d KiB))\nMax allocatable block size at each point, plus time per operation" % (driver, opsfile, heap_size, heap_size/1024)

    ax = subplot(title, "Time (op)", pretty)

    if opmode == 'memplot':
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


        frag = map(log10_, frag)
        free = map(log10_, free)
        used = map(log10_, used)
        overhead = map(log10_, overhead)

        p1, = ax.plot(frag, 'b-', label=pretty)
        p2, = ax.plot(free, 'g-', label='Free')
        p3, = ax.plot(used, 'r-', label='Used')
        p4, = ax.plot(overhead, 'k-', label='Overhead')

        ax.yaxis.tick_right()
        ax.yaxis.set_label_position('right')

        lines=[p1, p2, p3, p4]
        plt.legend(lines, [l.get_label() for l in lines], loc=2) # upper left

    elif opmode == 'allocstats':
        max_size = 0
        for op in opstats:
            m = op['free'] + op['overhead'] + op['used']
            if m > max_size:
                max_size = m

        def ratio(op):
            #return op[opmode] / op['free']
            #return (op['free'] - op['maxmem']) / 1024.0
            #return op[opmode]
            #return (op['free'] - op['used'] - op['maxmem']) / 1024
            return (op['free'] - op['maxmem']) / 1024

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

        #
        # Make use of size!
        #
        # diff(heap_size, [sum('size') until this point], actual allocated) => "difference between theoretically available and actual available"
        #


        accumulated = False
        logarithmic = True
        op = opstats[0]
        print "calculating time."
        accumulated_time_nonlog = [op['current_op_time'] + op['oom_time']]
        accumulated_time = [op['current_op_time'] + op['oom_time']]

        if logarithmic:
            accumulated_time[0] = log10_(accumulated_time[0])

        for i in range(1, len(opstats)):
            op = opstats[i]

            current = op['current_op_time'] + op['oom_time']

            previous = accumulated_time_nonlog[i-1]
            thetime = 0
            if accumulated:
                thetime = previous + current
            else:
                if op['op'] == 'N':
                    thetime = current
                else:
                    thetime = previous

            accumulated_time_nonlog.append(thetime)
            if logarithmic:
                bef = thetime
                thetime = log10_(thetime)

            accumulated_time.append(thetime)


        free = map(lambda x: x/1024, free)
        maxmem = map(lambda x: x/1024, maxmem)


        """
        free = [10] * len(free)
        maxmem = [10] * len(maxmem)
        freediff = [10] * len(freediff)
        accumulated_time = [10] * len(accumulated_time)
        """

        p1a, = ax.plot(free, 'g-', label='Theoretical usable space')
        p1b, = ax.plot(maxmem, 'r-', label='Allocatable')
        p1, = ax.plot(freediff, 'y-', label='diff Allocatable vs Theoretical')

        ax2 = ax.twinx()
        ax2.yaxis.tick_left()
        ax2.yaxis.set_label_position("left")
        ax2.set_ylabel('Time (us)')

        ax.yaxis.tick_right()
        ax.yaxis.set_label_position('right')
        ax.set_ylabel('KiB')

        p2, = ax2.plot(accumulated_time, 'b-', label='Malloc time (no maxmem/free)')

        lines=[p1a, p1b, p1, p2]
        plt.legend(lines, [l.get_label() for l in lines]) # loc= 2=upper left, 4=lower right, 3=lower left

        """
        p1, = ax.plot(freediff, 'y-', label='Diff (kb)')
        p1a, = ax.plot(map(lambda x: x/1024, free), 'g-', label='Free: theory (kb)')
        p1b, = ax.plot(map(lambda x: x/1024, maxmem), 'r-', label='Free: actual (kb)')

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
        plt.legend(lines, [l.get_label() for l in lines], loc=3) # 2=upper left, 4=lower right, 3=lower left
        """

def plot_init():
    global g_figure
    g_figure = plt.figure(figsize=(19.2,12.0), dpi=300)
    #plt.axis('tight')
    #plt.grid(True)

def plot_save(fname):
    print "Saving plot to", fname
    plt.savefig(fname)

def plot_init_multiple():
    global g_multiple_figure
    #g_multiple_figure = plt.figure(figsize=(19.2,12.0), dpi=300)
    #g_multiple_figure = plt.figure(figsize=(12,19.2), dpi=300)
    g_multiple_figure = plt.figure(figsize=(12,16), dpi=300)
    #g_multiple_figure = plt.figure(figsize=(12,28.8), dpi=300)
    #plt.axis('tight')
    #plt.grid(True)

def plot_save_multiple(app):
    fname = app + ".png"
    print "Saving plot to", fname
    plt.savefig(fname)

def main():
    if len(sys.argv) < 2:
        print "usage: %s plotdatafile" % sys.argv[0]
        print "<plotsdatafile> is the name of a file of Python data generated from plot_<driver>."
        sys.exit(1)

    if len(sys.argv) == 3:
        print "usage: %s app plotdatafile1 plotdatafile2 ..." % sys.argv[0]
        print "<plotsdatafile> is the name of a file of Python data generated from plot_<driver>."
        sys.exit(1)

    if len(sys.argv) > 3:
        app = sys.argv[1]
        plotfiles = sys.argv[2:]
        print plotfiles
        allocstats = []
        for plotfile in plotfiles:
            if os.path.exists(plotfile):
                stats = {}
                execfile(plotfile, {}, stats)
                allocstats.append(stats)

        print "init"
        plot_init_multiple()
        print "plot"
        plot_allocstats_multiple(app, allocstats)
        print "save"
        plot_save_multiple(app)

    elif len(sys.argv) == 2:
        print "yes?"
        plotfilename = sys.argv[1]
        allocstats = {}
        execfile(plotfilename, {}, allocstats)

        print "Reading data:", plotfilename

        plot_init()

        plot_allocstats(allocstats)

        figurefile = "plot-%s-%s.png" % (allocstats['driver'], os.path.basename(allocstats['opsfile']))
        plot_save(figurefile)

        #plt.show()

if __name__ == '__main__':
    main()


"""
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
                # ""# 230090: 78% (own = 13, other = 164322011)""
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
"""


