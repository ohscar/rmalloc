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
    ssize_t size;
} memory_t;

/***************************************************************************/

memory_t *rmalloc(ssize_t size);
void *rmlock(memory_t *);
void unlock(memory_t *);
void free(memory_t *);

#ifdef __cplusplus
}
#endif
#endif
