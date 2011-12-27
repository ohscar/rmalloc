/* rmalloc
 *
 * buddy allocator, simplest possible implementation.
 */
#ifndef __buddy_h
#define __buddy_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void binit(uint32_t heap_size);
void bdestroy();
void *balloc(uint32_t n);
void bfree(void *);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __buddy_h
