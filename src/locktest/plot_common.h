#ifndef PLOT_COMMON_H
#define PLOT_COMMON_H

void print_after_malloc_stats(memory_t *, int address, int size);
void print_after_free_stats(int address, int size);

bool user_init(unsigned int heap_size);
void user_destroy();
void user_oom(int size);
memory_t *user_malloc(int size);
void user_free(memory_t *);
void user_lock(memory_t *);
void user_unlock(memory_t *);

#endif // PLOT_COMMON_H
