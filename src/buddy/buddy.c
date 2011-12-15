/* rmalloc
 *
 * buddy allocator, simplest possible implementation.
 */
#include "buddy.h"

#include <stdint.h>
#include <stdlib.h>

static void *heap = NULL;
static void *end = NULL;

void binit(size_t heap_size)
{
    heap = (uint8_t *)malloc(heap_size);
    end = heap + heap_size;
}
void bdestroy()
{
    if (heap)
        free(heap);
}

void *balloc(size_t n)
{
    /*
    if (heap + n > end)
        return NULL;

    heap += n;
    return (heap - n);
    */

    return malloc(n);
}

void bfree(void *p)
{
    free(p);
}

