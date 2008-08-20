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

#include <stdint.h>
#include <sys/types.h>

//typedef unsigned long size_t;

typedef uint32_t pointer_size_t;

#define RM_OK                   1
#define RM_ERROR                0
#define RM_INSUFFICIENT_MEMORY -1

/***************************************************************************/

typedef struct {
    uint8_t locks;
} memory_t;

typedef uint8_t status_t;

/***************************************************************************/

status_t rmalloc_init(void);
status_t rmalloc_destroy(void);
void rmalloc_dump(void);
void *rmalloc_ram_end(void);
void *rmalloc_ram_top(void);
void rmalloc_print(memory_t *);
void rmalloc_compact(memory_t *);

status_t rmalloc(memory_t **, size_t);
status_t rmrealloc(memory_t **, memory_t **, size_t);
status_t rmlock(memory_t *, void **);
status_t rmunlock(memory_t *);
status_t rmfree(memory_t *);
const char *rmstatus(status_t);

/***************************************************************************/
#ifdef __cplusplus
}
#endif
#endif // __rmalloc_h
