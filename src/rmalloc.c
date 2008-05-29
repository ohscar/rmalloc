/*
 * rmmalloc.c
 *
 * Relocatable Memory Allocator
 *
 * Mikael Jansson <mail@mikael.jansson.be>
 */
#include "rmalloc.h"
#include <stdlib.h>

#define RMALLOC_INIT_RAM_SIZE ((1024*1024)*40)
#define RM_OK 1
#define RM_ERROR 0

typedef unsigned char uint8_t;

/****************************************************************************/

static uint8_t *g_ram;
static uint8_t *g_top;
static size_t g_ram_size;

/* allocates memory from the global pool */
static void *rm_alloc(size_t size) {
    if (g_top+size > g_ram+g_ram_size) 
        return NULL;

    g_top += size;
    return g_top;
}

/****************************************************************************/

typedef struct {
    void *ptr;
    size_t size;
    struct memory_block_t *previous;
    struct memory_block_t *next;
} memory_block_t;

static memory_block_t *g_root;
static memory_block_t *g_root_end;

static memory_block_t *mb_new(void);
static memory_block_t *mb_push(void *ptr, size_t size);
static memory_block_t *mb_remove(void *ptr);


memory_block_t *mb_new(void) {
    memory_block_t *mb;
    mb = (memory_block_t *)malloc(sizeof(memory_block_t));
    mb->ptr = 0;
    mb->size = NULL;
    mb->previous = NULL;
    mb->next = NULL;
}

/* push a pointer onto the root
 *
 * return: the newly allocated block.
 * modify: g_root_end
 * depend: -
 */
memory_block_t *mb_push(void *ptr, size_t size) {
    memory_block_t *mb = mb_new();
    if (mb) {
        g_root_end->next = mb;
        g_root_end->next->previous = g_root_end;
        g_root_end = g_root_end->next;
        
        g_root_end->ptr = ptr;
        g_root_end->size = size;
        g_root_end->next = NULL;
    }
    return mb;
}

/* uses the global root */
memory_block_t *mb_remove(void *ptr) {

}

/****************************************************************************/

status_t rmalloc_init(void) {
    g_ram_size = RMALLOC_INIT_RAM_SIZE;
    g_ram = (uint8_t *)malloc(g_ram_size);
    g_top = g_ram;

    g_root = (memory_block_t *)malloc(sizeof(memory_block_t));
    g_root_end = g_root;

    g_root->ptr = 0;
    g_root->size = NULL;
    g_root->previous = NULL;
    g_root->next = NULL;

    return RM_OK;
}

status_t rmalloc_destroy(void) {
     
}

status_t rmalloc(memory_t **memory, size_t size) {
     
}
