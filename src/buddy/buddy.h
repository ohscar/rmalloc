/* rmalloc
 *
 * buddy allocator, simplest possible implementation.
 */
#ifndef __buddy_h
#define __buddy_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void binit(size_t heap_size);
void bdestroy();
void *balloc(size_t n);
void bfree(void *);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __buddy_h
