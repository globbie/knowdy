#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>

#include "knd_config.h"
#include "knd_cache.h"
#include "knd_utils.h"

#define DEBUG_CACHE_LEVEL_0 0
#define DEBUG_CACHE_LEVEL_1 0
#define DEBUG_CACHE_LEVEL_2 0
#define DEBUG_CACHE_LEVEL_3 0
#define DEBUG_CACHE_LEVEL_TMP 1

int knd_cache_set(struct kndCache *self, void *data, size_t *cell_num)
{
    struct kndCacheCell *cell = NULL;
    int num_readers = 0;
    size_t state, curr_state;
    void *prev_data;
    size_t start_cell_num = rand() % (int)self->num_cells - 1;
    size_t final_cell_num = self->num_cells;
    assert(start_cell_num >= 0 && start_cell_num < self->num_cells);

    curr_state = atomic_load_explicit(&self->state, memory_order_relaxed);

    for (size_t i = start_cell_num; i < final_cell_num; i++) {
        // explore the initial part of the array
        if (i == final_cell_num) {
            i = 0;
            final_cell_num = start_cell_num;
            continue;
        }
        cell = &self->cells[i];
        num_readers = atomic_load_explicit(&cell->num_readers, memory_order_relaxed);

        if (DEBUG_CACHE_LEVEL_TMP)
            knd_log("cell %zu) num readers:%zu", i, num_readers);

        // the cell is being used by readers
        if (num_readers > 0) continue;

        state = atomic_load_explicit(&cell->state, memory_order_relaxed);
        //if (curr_state - state < KND_CACHE_STATE_INTERVAL) {
            // recent activity detected
        //    continue;
        //}

        // looks like no one is reading this cell,
        // let's try grabbing it
        if (!atomic_compare_exchange_weak(&cell->num_readers, &num_readers, -1)) {
            // oops - not my lucky day :(
            continue;
        }

        // OK, now the readers will ignore this cell.
        // It's time to compete with concurrent writers only
        atomic_store_explicit(&cell->state, curr_state, memory_order_relaxed);

        prev_data = atomic_load_explicit(&cell->data, memory_order_relaxed);
        if (!atomic_compare_exchange_weak(&cell->data, &prev_data, data)) {
            continue;
        }
        // allow reader access
        atomic_store_explicit(&cell->num_readers, 0, memory_order_relaxed);

        if (DEBUG_CACHE_LEVEL_TMP)
            knd_log("++ set cell %zu) data:%p", i, cell->data);
        *cell_num = i;

        // eviction: free prev resources
        if (prev_data)
            self->cb(prev_data);
        break;
    }
    if (!cell) return knd_FAIL;

    return knd_OK;
}

int knd_cache_get(struct kndCache *self, size_t cell_num, void **result)
{
    struct kndCacheCell *cell;
    int num_readers;
    void *data;

    cell = &self->cells[cell_num];
    do {
        num_readers = atomic_load_explicit(&cell->num_readers, memory_order_relaxed);
        // knd_log(">> curr num readers: %d", num_readers);
        if (num_readers == -1) return knd_CONFLICT;
    } while (!atomic_compare_exchange_weak(&cell->num_readers, &num_readers, num_readers + 1));

    atomic_fetch_add_explicit(&self->state, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&cell->state, 1, memory_order_relaxed);

    // wild wild west..
    cell->num_hits++;

    data = atomic_load_explicit(&cell->data, memory_order_relaxed);

    // knd_log(">> got cell %zu: data: %p", cell_num, cell->data);
    *result = data;
    return knd_OK;
}

int knd_cache_release(struct kndCache *self, size_t cell_num, void *data)
{
    struct kndCacheCell *cell;
    int num_readers;
    void *prev_data;

    cell = &self->cells[cell_num];
    prev_data = atomic_load_explicit(&cell->data, memory_order_relaxed);
    if (prev_data != data) {
        knd_log("data payload mismatch in cache cell %zu (%p)", cell_num, prev_data);
        return knd_CONFLICT;
    }
    do {
        num_readers = atomic_load_explicit(&cell->num_readers, memory_order_relaxed);
        if (num_readers == -1 || num_readers == 0) return knd_CONFLICT;
    } while (!atomic_compare_exchange_weak(&cell->num_readers, &num_readers, num_readers - 1));
    return knd_OK;
}

int knd_cache_new(struct kndCache **result, size_t num_cells, size_t max_mem_size, cell_free_cb cb)
{
    struct kndCache *self;
    assert(num_cells > 0);
    self = calloc(1, sizeof(struct kndCache));
    if (!self) return knd_NOMEM;
    self->num_cells = num_cells;
    self->max_mem_size = max_mem_size;

    self->cells = calloc(num_cells, sizeof(struct kndCacheCell));
    if (!self->cells) return knd_NOMEM;
    self->cb = cb;

    *result = self;
    return knd_OK;
}
