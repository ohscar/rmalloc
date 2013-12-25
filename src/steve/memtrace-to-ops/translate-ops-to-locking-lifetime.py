#!/usr/bin/env python
"""
translate-ops-to-locking-lifetime.py

Read in ops file, fork off jobs for each handle that processes lifetime.

It uses the naive solution.
"""

import numpy as np
import matplotlib as mpl
import matplotlib.mlab as mlab
import matplotlib.pyplot as plt
import json

import copy

import sys
import os.path
import os
import time

g_ops_filename = None
g_ops_file = None

MACRO_LIFETIME_THRESHOLD = 0.9

# largest address + size
g_heap_top = 0

# how much of the heap is used, not counting alignment, structures and
# similar.
g_perfect_bytes_used = 0

#LOWER_UPPER_SCALE = 1000
LOWER_UPPER_SCALE = 1

MIN_RANGE = 75 # items on X axis.

#from drange import drange

import multiprocessing
import ctypes

# has to be huge to fit stuff
OPS_LIST_SIZE = 256 * (1024**2)

g_ops_list = multiprocessing.Array(ctypes.c_uint32, OPS_LIST_SIZE)
g_starts = []
g_stops = {}
fname = None
ops_counter = 0
g_op_stop_limit = 0 # 0.99*ops_counter

FLOAT_SPEED = 1.0
SINK_SPEED = 0.5

def generate_outfile_name(fname, handle):
    return "%s-microlife_part_%d" % (fname, handle)

def calculator(start, opslist=g_ops_list):
    # do processing
    handle = opslist[start]

    outfile = generate_outfile_name(fname, handle)
    lifetime = []

    life = 0

    if g_stops.has_key(handle):
        stop = g_stops[handle]
        """
        if stop > g_op_stop_limit:
            print "!", handle, "> 95% lifetime, no go."
            #open(outfile, "wt").write(json.dumps((handle, start, stop, [])))
            return
        """

    else:
        print "!", handle, "has no stop; unknown lifetime."
        #open(outfile, "wt").write(json.dumps((handle, start, None, [])))
        return


    #print "->", handle, "from", start, "to", stop, "=", (stop-start)/(1000.0*1000.0), "M ops"
    print "->", handle, "=", (stop-start)/(1000.0*1000.0), "M ops"

    last_lock_status = 0
    lock_status = -1
    i = start+1
    counter = 0
    moreops = []
    while i < stop:
        h = opslist[i]
        if h == handle:
            life += FLOAT_SPEED
        else:
            if life >= SINK_SPEED:
                life -= SINK_SPEED

        if life > 0:
            lock_status = 1
        else:
            lock_status = 0

        if handle == 174:
            moreops.append((i, lock_status))


        # normal op data
        #op = {i : {handle, ' '} # 'N' or 'F' here!
        #op = (i, h, ' ')
        #lifetime.append(op)

        # locking data
        if lock_status != last_lock_status:
            last_lock_status = lock_status

            lock = 'L' if lock_status == 1 else 'U'
            #op = {i : (handle, lock)}
            op = (i, handle, lock)
            lifetime.append(op)

        i += 1

    """
    v = (handle, start, stop, lifetime)
    print v
    del lifetime
    del v
    """
    #data = (handle, start, stop, lifetime)
    data = lifetime
    open(outfile, "wt").write(json.dumps(data))


    """
    if handle == 174:
        for i, lock_status in moreops:
            print "handle 174: %10d %d" % (i, lock_status)
    """

    del lifetime

def main():
    global g_ops_list
    global g_starts
    global fname
    global ops_counter
    global g_op_stop_limit

    print "Perform a _complete_ locking calculation. Very slow!"

    if len(sys.argv) == 2:
        fname = sys.argv[1]
    else:
        print "usage: %s opsfile\nspits out opsfile-microlife_part_N files." % sys.argv[0]
        sys.exit(1)

    g_ops_filename = fname + "-ops"
    g_ops_file = open(g_ops_filename, "rt")

    handle_count = 0
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
            print >> sys.stderr,  "*", chars[chari % 4], "%.2f MB skipped %lu K duplicate ops (handle count %d, ops_counter %d)" % (bytes / 1048576.0, skipped/1000, handle_count, ops_counter),"\r",

        i += 1

        bytes += len(line)

        #ph, po, pa, ps = lh, lo, la, ls
        try:
            lh, lo, la, ls = line.split()
        except:
            continue
        lh, lo, la, ls = int(lh), lo, int(la), int(ls)

        if handle_count < lh:
            handle_count = lh

        g_ops_list[ops_counter] = lh

        if lo == 'N':
            #print "Create %d at %d" % (lh, ops_counter)
            g_starts.append(ops_counter)
        elif lo == 'F':
            #print "Kill %d at %d" % (lh, ops_counter)
            g_stops[lh] = ops_counter

        ops_counter += 1


    g_op_stop_limit = int(float(0.95*float(ops_counter)))

    print >> sys.stderr,  " ->", chars[chari % 4], "%.2f MB skipped %lu K duplicate ops (handle count %d, ops_counter %d)" % (bytes / 1048576.0, skipped/1000, handle_count, ops_counter)
    g_ops_file.close()

    pool = multiprocessing.Pool(processes=multiprocessing.cpu_count() * 2)
    chunksize = 4
    mapresult = pool.map_async(calculator, g_starts, chunksize)
    print "Crunching", len(g_starts), "tasks."
    remaining = mapresult._number_left
    old_remaining = remaining + 1
    while True:
        if mapresult.ready():
            break
        remaining = mapresult._number_left
        if remaining < old_remaining:
            old_remaining = remaining
            print "                                                                           Waiting for", remaining*chunksize, "tasks to complete..."
        time.sleep(60)

    pool.close()
    pool.join()

    ##############################################################################################

    del g_ops_list
    ops = []

    # and now, read everything back in!
    for i in range(0, handle_count + 1):
        # try opening the file, read in as data, blah blah
        outfilename = generate_outfile_name(fname, i)
        try:
            f = open(outfilename, "r")
        except:
            print "no data for handle %d, next." % i
            continue

        try:
            data = json.loads(f.read())
            f.close()
        except:
            print "couldn't parse/read json data from %s; next." % outfilename
            continue

        ops.extend(data)

        os.remove(outfilename)

    opsdict = {i: (handle, op) for (i, handle, op) in ops}

    #####################################################################
    g_ops_file = open(g_ops_filename, "rt")

    handle_count = 0
    print >> sys.stderr, "Reading ops from file into opslist:", g_ops_filename
    i = 0
    bytes = 0
    skipped = 0
    chars = "-\|/"
    chari = 0
    lh, lo, la, ls = -1, 'x', -1, -1
    ops = []
    for line in g_ops_file.xreadlines():
        if i % 500000 == 0:
            chari += 1
            print >> sys.stderr,  "*", chars[chari % 4], "%.2f MB skipped %lu K duplicate ops (handle count %d, ops_counter %d)" % (bytes / 1048576.0, skipped/1000, handle_count, ops_counter),"\r",

        i += 1

        bytes += len(line)

        #ph, po, pa, ps = lh, lo, la, ls
        try:
            lh, lo, la, ls = line.split()
        except:
            continue
        lh, lo, la, ls = int(lh), lo, int(la), int(ls)

        if handle_count < lh:
            handle_count = lh

        if lo == 'N' or lo == 'F':
            op = lh, lo, la, ls
            ops.append(op)

        if opsdict.has_key(i):
            op = (opsdict[i][0], opsdict[i][1], 0, 0) # handle, op, address, size
            ops.append(op)

    print >> sys.stderr,  " ->", chars[chari % 4], "%.2f MB skipped %lu K duplicate ops (handle count %d, ops_counter %d)" % (bytes / 1048576.0, skipped/1000, handle_count, ops_counter)
    g_ops_file.close()
    #####################################################################

    lockopsfilename = fname + "-lockopsfull"
    lockopsfile = open(lockopsfilename, "wt")
    for op in ops:
        print >> lockopsfile, "%d %s %d %d" % (op[0], op[1], op[2], op[3])

    lockopsfile.close()

    print "Locking ops data written to", lockopsfilename

    """
    print "-------------"

    apa = []
    for index, opdata in opsdict.items():
        apa.append((index, opdata[0], opdata[1]))
    apa.sort(key=lambda x: x[0])
    for a in apa:
        print a
    """

if __name__ == '__main__':
    main()


