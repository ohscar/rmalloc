/* compact.h
 *
 * rmalloc compacting allocator for interactive systems
 */
#ifndef __compact_h
#define __compact_h

#include <stddef.h>
#include <stdint.h>

/* memory layout:
 * - header list grows from top of stack and downwards
 * - objects grow from bottom of stack and upwards
 * - TODO: switch around, such that the stack always grows from bottom? that
 *   way, the heap could be grown.
 *
 * header:
 * - lock type: unlocked = 0, locked = 1, weak = 2
 * - pointer to memory block
 * - size of memory block
 *
 * free memory block:
 * - 4 bytes at N-4: header address, points back to this block
 * - 4 bytes at N-8: next free memory block.
 *
 * handle:
 * - alias of header, opaque type. means the header block cannot be compacted!
 *
 * header list:
 * - insert new at the first free location
 * - keep bitmap (64 bytes) to narrow down the search of first free block
 * - 64*8=512 bits, initially 8 headers per bit = 4096 entries.
 * - bitmap scaled to number of objects, when limit reached, double and scale
 *   down (b0=b0*b1, b2=b2*b3, ...).
 * - 
 */

typedef void *handle_t;

void cinit(void *heap, uint32_t size);

handle_t *cmalloc(int size);
void cfree(handle_t *);
void *clock(handle_t *);
void *cweaklock(handle_t *);
void cunlock(handle_t *);

/* internal */

/*
header_t *header_find_free();
header_t *header_new();
void header_free(header_t *h);
header_t *header_set_unused(header_t *header);

free_memory_block_t *block_from_header(header_t *header);
header_t *block_new(int size);
header_t *block_free(header_t *header);

uint32_t stat_total_free_list();
*/

#endif // __compact_h
