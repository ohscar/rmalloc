#include "../compact/compact.h"
#include "../compact/compact.c"

typedef handle_t memory_t;

#include "plot_common.h"
#include "plot_data.c"

#include <stdio.h>

void print_after_malloc_stats(memory_t *handle, int address, int size) {
    printf("after alloc size %d to address %p / handle %p\n", size, address, handle);
}

void print_after_free_stats(int address, int size);
    printf("after free %p of size %d\n", address, size);
}

memory_t *user_malloc(int size) {
    return rmmalloc(size);
}

void user_free(memory_t *h) {
    rmfree(h);
}

void user_lock(memory_t *h) {
    rmlock(h);
}

void user_unlock(memory_t *h) {
    rmunlock(h);
}

bool user_init(unsigned int heap_size) {
    rminit(malloc(heap_size), heap_size);
}
void user_destroy() {
    rmdestroy();
}

void user_oom(int size) {

}

