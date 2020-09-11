/* lock-free fixed size LRU cache */

#pragma once

#include <stdatomic.h>
#include "knd_config.h"

typedef void (*cell_free_cb)(void *obj);

struct kndCacheCell
{
    atomic_int num_readers;
    atomic_size_t data_mem_size;
    atomic_size_t state;
    size_t num_hits;
    void * _Atomic data;
};

struct kndCache
{
    struct kndCacheCell *cells;
    size_t num_cells;
    size_t max_mem_size;
    atomic_size_t state;
    cell_free_cb cb;
};

int knd_cache_new(struct kndCache **self, size_t num_cells, size_t max_mem_size, cell_free_cb cb);
void knd_cache_del(struct kndCache *self);

int knd_cache_set(struct kndCache *self, void *data, size_t *cell_num);
int knd_cache_get(struct kndCache *self, size_t cell_num, void **result);
int knd_cache_release(struct kndCache *self, size_t cell_num, void *data);
