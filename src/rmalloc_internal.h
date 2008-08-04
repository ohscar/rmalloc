/*
 * rmalloc_internal.h
 *
 * Relocatable Memory Allocator
 * Internals
 *
 * Mikael Jansson <mail@mikael.jansson.be>
 */
#ifndef __rmalloc_internal_h
#define __rmalloc_internal_h
#ifdef __cplusplus
extern "C" {
#endif 


#define RMALLOC_INIT_RAM_SIZE (8 * 1024*1024)

#define RM_ASSERT(x) {uint8_t status = x; if (!status) {fprintf(stderr, "[%s:%d] RM_ASSERT(%s) failed!\n", __FILE__, __LINE__, #x);}}
#define RM_ASSERT_RET(x, err) {uint8_t status = (x); if (!status) {\
    fprintf(stderr, "[%s:%d] RM_ASSERT_RET(%s) failed!\n", __FILE__, __LINE__, #x); return err;}}

#define RM_MERGED        0
#define RM_NOT_MERGED    1
#define RM_CANNOT_SHRINK 2

#define MEMORY_AS_BLOCK(x) ((memory_block_t *)(x))
#define BLOCK_AS_MEMORY(x) ((memory_t *)(x))

#define RM_OK                   1
#define RM_ERROR                0
#define RM_INSUFFICIENT_MEMORY -1

/***************************************************************************/

typedef struct memory_block_t {
    memory_t memory;
    void *ptr;
    uint8_t used : 1;
    size_t size;
    struct memory_block_t *previous;
    struct memory_block_t *next;
} memory_block_t;

/***************************************************************************/


#ifdef __cplusplus
}
#endif
#endif // __rmalloc_internal_h

