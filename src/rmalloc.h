/*
 * rmalloc.h
 *
 * Relocatable Memory Allocator
 *
 * Mikael Jansson <mail@mikael.jansson.be>
 */
#ifndef __rmalloc_h
#define __rmalloc_h
#ifdef __cplusplus
extern "C" {
#endif 

/***************************************************************************/

typedef struct {
    void *ptr;
    size_t size;
} memory_t;

typedef int status_t;

/***************************************************************************/

status_t rmalloc(memory_t **, size_t);
status_t rmlock(memory_t *, void **);
status_t rmunlock(memory_t *);
status_t rmfree(memory_t *);

/***************************************************************************/

#ifdef __cplusplus
}
#endif
#endif
