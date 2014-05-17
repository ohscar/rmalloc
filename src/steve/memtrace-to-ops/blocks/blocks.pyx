"""
g_blocks_starts = {}
g_blocks_ends = {}
g_largest_block = 0
g_lowest_addr = 0xFFFFFFFE
g_highest_addr = 0
g_handle_counter = 0

# add start and end address to hash
def append(start, end):
    global g_handle_counter, g_lowest_addr, g_highest_addr, g_largest_block

    if end - start > g_largest_block:
        g_largest_block = end - start

    if g_lowest_addr > start:
        g_lowest_addr = start
    if g_highest_addr < end:
        g_highest_addr = end

    if g_blocks_starts.has_key(start):
        g_blocks_starts[start].append(g_handle_counter)
    else:
        g_blocks_starts[start] = [g_handle_counter]

    if g_blocks_ends.has_key(end):
        g_blocks_ends[end].append(g_handle_counter)
    else:
        g_blocks_ends[end] = [g_handle_counter]

    g_handle_counter += 1

def last_memory_handle():
    return g_handle_counter-1

def find_block(address):
    addr_lower = address
    addr_upper = address

    while address - addr_lower <= g_largest_block and addr_lower >= g_lowest_addr and not g_blocks_starts.has_key(addr_lower):
        addr_lower -= 1

    while addr_upper - address <= g_largest_block and addr_upper <= g_highest_addr and not g_blocks_ends.has_key(addr_upper):
        addr_upper += 1

    if g_blocks_starts.has_key(addr_lower) and g_blocks_ends.has_key(addr_upper):
        #print "found block:", g_blocks_starts[addr_lower], g_blocks_ends[addr_upper]
        return g_blocks_starts[addr_lower][-1]
    return -1

def length():
    print len(g_blocks_ends.keys())


"""

"""
g_memoryid_counter = 0

# (memory_id, start, end) pairs to map memory areas to id:s.
# scan the list from the back, since new allocations can occupy the same area.
g_blocks = []

# map start address to id
g_address_id = {}

# id maps to index in g_blocks
def find_block(address):
    for index in xrange(len(g_blocks)-1, -1, -1):
        memory_id, start, end = g_blocks[index]
        if address >= start and address < end:
            return memory_id

    return -1

def append(start, end):
    global g_memoryid_counter
    g_blocks.append((g_memoryid_counter, start, end))
    where = len(g_blocks)-1
    #g_address_id[start] = (g_memoryid_counter, where, end)
    g_address_id[start] = where
    g_memoryid_counter += 1

def length():
    return len(g_blocks)

def get(mid):
    return g_blocks[mid]

def remove(where, add):
    try:
        g_blocks.pop(where)
        del g_address_id[add]
    except:
        return
    for key in g_address_id.keys():
        if g_address_id[key] > where:
            g_address_id[key] -= 1
"""

#import redis
#g_redis = redis.Redis()

g_memoryid_counter = 0

# map all bytes of an allocated area to the corresponding memory handle
g_blocks = {}

g_ops = []
# memory_id OP address size
g_ops_file = None
g_ops_filename = None

# id maps to index in g_blocks
def find_block(address):
    global g_blocks
    try:
        mid = g_blocks[address]
        #mid = int(g_redis[address])
    except:
        return -1

    return mid

def length():
    global g_blocks
    s = {}
    for v in g_blocks.values():
        s[v] = 1
    return len(s.keys())

def append(start, end):
    global g_blocks
    #if start == 0x41C4AE8:
        #    print  >> sys.stderr,"append:", hex(start), hex(end)
    global g_memoryid_counter
    for address in range(start, end):
        g_blocks[address] = g_memoryid_counter
        #g_redis[address] = g_memoryid_counter
        #if start == 0x41C4AE8:
            #    print >> sys.stderr, "append at address", hex(address), "is", g_blocks[address]

    memory_id = g_memoryid_counter
    g_memoryid_counter += 1
    return memory_id

import sys

def remove(add):
    global g_blocks
    try:
        memory_id = g_blocks[add]
        #memory_id = int(g_redis[add])
        #if add == 0x41C4AE8:
            #    print  >> sys.stderr,"remove() 1", hex(next), "handle", g_blocks[next]
        del g_blocks[add]
        #del g_redis[add]
    except:
        #print "remove(0x%X): Address not in blocks" % add
        """
        s = {}
        for v in g_blocks.values():
            s[v] = 1
        v = []
        for k in s.keys():
            v.append(k)
        print "blocks:", [hex(k).lower() for k in g_blocks.keys()]
        sys.exit(1)
        """
        return

    next = add+1
    while True:
        try:
            if g_blocks[next] == memory_id:
            #if int(g_redis[next]) == memory_id:
                #if add == 0x41C4AE8:
                    #    print  >> sys.stderr,"remove() loop", hex(next), "handle", g_blocks[next]
                del g_blocks[next]
                #del g_redis[next]
            else:
                break
        except:
            break
        next = next + 1

def add_op(mid, op, address, size):
    global g_ops
    #print le, "%lu %s %lu %lu" % (int(mid), op, int(address), int(size))
    print >> g_ops_file, "%lu %s %lu %lu" % (int(mid), op, int(address), int(size))
    g_ops.append((mid, op, size, address))

OFFSET_OP = 1
OFFSET_ADDR = 2
OFFSET_SIZE = 3

def process_line(line):
    if line[0:4] != ">>> ":
        return

    t = line.split()
    if len(t) != 4:
        return

    size = int(t[OFFSET_SIZE])
    address = int(t[OFFSET_ADDR])

    if t[1] == "N":
        #>>> N 69586984 352
        memory_id = append(address, address+size);

        add_op(memory_id, 'N', address, size)
    elif t[1] == "F":
        # >>> F 69587384 0
        # remove the pointer from the list since it's no longer alive
        memory_id = find_block(address)
        if memory_id >= 0:
            add_op(memory_id, 'F', address, 0)
        remove(address)
    elif t[1] == "S":
        # >>> S 69586984 4
        memory_id = find_block(address)
        if memory_id >= 0:
            add_op(memory_id, 'S', address, size)
    elif t[1] == "L":
        # >>> L 69586984 4
        memory_id = find_block(address)
        if memory_id >= 0:
            add_op(memory_id, 'L', address, size)
    elif t[1] == "M":
        # >>> M 69586984 4
        memory_id = find_block(address)
        if memory_id >= 0:
            add_op(memory_id, 'M', address, size)


