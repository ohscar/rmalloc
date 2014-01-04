#ifndef __compact_internal_h
#define __compact_internal_h

#define  __STDC_LIMIT_MACROS

#include <stdint.h>

/* header, see compact.h
 */

#define JEFF_MAX_RAM_VS_SLOWER_MALLOC 1

#if __x86_64__
typedef uint64_t ptr_t;
#define PTR_T_MAX UINT64_MAX
#else
typedef uint32_t ptr_t;
#define PTR_T_MAX UINT32_MAX
#endif

#define HEADER_FREE_BLOCK   0
#define HEADER_UNLOCKED     (1<<0)
#define HEADER_LOCKED       (1<<1)
#define HEADER_WEAK_LOCKED  (1<<2)

typedef struct header_t {
    void *memory;
    uint32_t size;
    uint8_t flags;

    struct header_t *next;
#if JEFF_MAX_RAM_VS_SLOWER_MALLOC == 0
    struct header_t *next_unused;
#endif
} header_t;

/* free memory block, see compact.h
 */
typedef struct free_memory_block_t {
    header_t *header;
    struct free_memory_block_t *next; // null if no next block.
} free_memory_block_t;

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef ptr_t (*compare_cb)(void *a, void *b);
header_t *header__sort(header_t *list,  int is_circular, int is_double, compare_cb cmp);
ptr_t header__cmp(void *a, void *b);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // __compact_internal_h

