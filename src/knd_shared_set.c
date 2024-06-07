#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_class.h"
#include "knd_utils.h"
#include "knd_repo.h"
#include "knd_mempool.h"
#include "knd_shared_set.h"
#include "knd_task.h"

#include <gsl-parser.h>

#define DEBUG_SHARED_SET_LEVEL_0 0
#define DEBUG_SHARED_SET_LEVEL_1 0
#define DEBUG_SHARED_SET_LEVEL_2 0
#define DEBUG_SHARED_SET_LEVEL_3 0
#define DEBUG_SHARED_SET_LEVEL_4 0
#define DEBUG_SHARED_SET_LEVEL_TMP 1

static int compare_set_by_size_ascend(const void *a, const void *b)
{
    struct kndSharedSet **obj1, **obj2;

    obj1 = (struct kndSharedSet**)a;
    obj2 = (struct kndSharedSet**)b;

    if ((*obj1)->num_elems == (*obj2)->num_elems) return 0;
    if ((*obj1)->num_elems > (*obj2)->num_elems) return 1;

    return -1;
}

static int traverse(struct kndSharedSet *self, struct kndSharedSetElemIdx *base_idx,
                    struct kndSharedSetElemIdx **idxs, size_t num_idxs,
                    struct kndSharedSetElemIdx *result_idx)
{
    struct kndSharedSetElemIdx *nested_idxs[KND_MAX_CLAUSES];
    struct kndSharedSetElemIdx *idx, *sub_idx, *nested_idx;
    void *elem;
    bool gotcha = false;
    int err;

    /* iterate over terminal elems */
    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        elem = base_idx->elems[i];
        if (!elem) continue;

        gotcha = true;
        for (size_t j = 0; j < num_idxs; j++) {
            idx = idxs[j];
            if (!idx->elems[i]) {
                gotcha = false;
                break;
            }
        }
        if (!gotcha) continue;

        /* the elem is present in _all_ sets,
           save the result */
        result_idx->elems[i] = elem;
        self->num_valid_elems++;
        self->num_elems++;
    }

    /* iterate over subfolders */
    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        idx = base_idx->idxs[i];
        if (!idx) continue;

        gotcha = true;
        for (size_t j = 0; j < num_idxs; j++) {
            nested_idx = idxs[j];

            if (!nested_idx->idxs[i]) {
                gotcha = false;
                break;
            }
            nested_idxs[j] = nested_idx->idxs[i];
        }
        if (!gotcha) continue;

        err = knd_shared_set_elem_idx_new(self, &sub_idx);
        if (err) {
            knd_log("-- set elem idx mempool limit reached :(");
            return err;
        }
        result_idx->idxs[i] = sub_idx;

        err = traverse(self, idx, nested_idxs, num_idxs, sub_idx);
        if (err) return err;
    }
    return knd_OK;
}

int knd_shared_set_intersect(struct kndSharedSet *self, struct kndSharedSet **sets, size_t num_sets)
{
    struct kndSharedSetElemIdx *base_idx;
    struct kndSharedSetElemIdx *idxs[KND_MAX_CLAUSES];
    size_t num_idxs = num_sets - 1;
    int err;

    assert(num_sets > 1);

    /* sort sets by size */
    qsort(sets, num_sets, sizeof(struct kndSharedSet*), compare_set_by_size_ascend);

    /* the smallest set is taken as a base */
    base_idx = sets[0]->idx;
    sets++;

    for (size_t i = 0; i < num_idxs; i++)
        idxs[i] = sets[i]->idx;

    err = traverse(self, base_idx, idxs, num_idxs, self->idx);
    if (err) return err;

    return knd_OK;
}

static int save_elem(struct kndSharedSet *self, struct kndSharedSetElemIdx *parent_idx,
                     void *elem, const char *id, size_t id_size)
{
    struct kndSharedSetElemIdx *orig_idx, *idx = NULL;
    void *prev_elem = NULL;
    int idx_pos;
    int err;

    if (DEBUG_SHARED_SET_LEVEL_2)
        knd_log("== set idx to save ID remainder: \"%.*s\"", id_size, id);

    assert(parent_idx != NULL);

    idx_pos = obj_id_base[(unsigned char)*id];
    if (idx_pos == -1) {
        knd_log("-- invalid elem id");
        return knd_FORMAT;
    }

    /* subdir needed */
    if (id_size > 1) {
        do {
            orig_idx = atomic_load_explicit(&parent_idx->idxs[idx_pos], memory_order_relaxed);
            if (orig_idx) {
                idx = orig_idx;
                break;
            }
            err = knd_shared_set_elem_idx_new(self, &idx);
            if (err) {
                knd_log("-- set elem idx mempool limit reached");
                return err;
            }
        } while (!atomic_compare_exchange_weak(&parent_idx->idxs[idx_pos], &orig_idx, idx));

        err = save_elem(self, idx, elem, id + 1, id_size - 1);
        if (err) return err;
        return knd_OK;
    }

    /* assign payload elem */
    do {
       prev_elem = atomic_load_explicit(&parent_idx->elems[idx_pos], memory_order_relaxed);
       if (prev_elem && !self->allow_overwrite) {
           knd_log("set elem already exists");
           return knd_CONFLICT;
       }
    } while (!atomic_compare_exchange_weak(&parent_idx->elems[idx_pos], &prev_elem, elem));

    atomic_fetch_add_explicit(&parent_idx->num_term_elems, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&parent_idx->total_elems, 1, memory_order_relaxed);

    atomic_fetch_add_explicit(&self->num_elems, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&self->num_valid_elems, 1, memory_order_relaxed);
    return knd_OK;
}

static int get_elem(struct kndSharedSet *self, struct kndSharedSetElemIdx *parent_idx,
                    void **result, const char *id, size_t id_size)
{
    struct kndSharedSetElemIdx *idx;
    void *elem;
    int idx_pos;
    int err;

    if (DEBUG_SHARED_SET_LEVEL_2)
        knd_log(".. get elem by ID, remainder \"%.*s\"", id_size, id);

    assert(parent_idx != NULL);

    idx_pos = obj_id_base[(unsigned char)*id];
    if (idx_pos == -1) {
        knd_log("-- invalid elem id");
        return knd_FORMAT;
    }
    if (id_size > 1) {
        idx = atomic_load_explicit(&parent_idx->idxs[idx_pos], memory_order_relaxed);
        if (!idx) return knd_NO_MATCH;

        err = get_elem(self, idx, result, id + 1, id_size - 1);
        if (err) return err;
        return knd_OK;
    }
    elem = atomic_load_explicit(&parent_idx->elems[idx_pos], memory_order_relaxed);
    if (!elem) return knd_NO_MATCH;

    *result = elem;
    return knd_OK;
}

int knd_shared_set_add(struct kndSharedSet *self, const char *key, size_t key_size, void *elem)
{
    int err;
    assert(key_size != 0);
    assert(key != NULL);
    err = save_elem(self, self->idx, elem, key, key_size);
    if (err) return err;
    return knd_OK;
}

int knd_shared_set_get(struct kndSharedSet *self, const char *key, size_t key_size, void **elem)
{
    int err;
    if (!self->idx) return knd_FAIL;

    err = get_elem(self, self->idx, elem, key, key_size);
    if (err) return err;

    return knd_OK;
}

static int traverse_idx(struct kndSharedSetElemIdx *parent_idx, map_cb_func cb, void *obj, size_t *count)
{
    char buf[KND_ID_SIZE];
    size_t buf_size = 0;
    struct kndSharedSetElemIdx *idx;
    void *elem;
    int err;

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        elem = parent_idx->elems[i];
        if (!elem) continue;
        buf_size = 0;
        buf[buf_size] = obj_id_seq[i];
        buf_size = 1;

        err = cb(obj, buf, buf_size, *count, elem);
        if (err) return err;
        (*count)++;
    }

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        idx = parent_idx->idxs[i];
        if (!idx) continue;

        err = traverse_idx(idx, cb, obj, count);
        if (err) return err;
    }

    return knd_OK;
}

int knd_shared_set_map(struct kndSharedSet *self, map_cb_func cb, void *obj)
{
    size_t count = 0;
    int err;

    if (!self->idx) {
        if (DEBUG_SHARED_SET_LEVEL_2)
            knd_log("NB: -- set has no root idx");
        return knd_OK;
    }
    err = traverse_idx(self->idx, cb, obj, &count);
    if (err) return err;
    return knd_OK;
}

int knd_shared_set_new(struct kndSharedSet **result, struct kndMemPool *mempool)
{
    struct kndSharedSet *set;
    struct kndSharedSetElemIdx *idx;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndSharedSet));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, (void**)&set);
    if (err) return err;
    memset(set, 0, sizeof(struct kndSharedSet));
    set->mempool = mempool;

    err = knd_shared_set_elem_idx_new(set, &idx);
    if (err) return err;
    set->idx = idx;
    *result = set;
    return knd_OK;
}

int knd_shared_set_elem_idx_new(struct kndSharedSet *self, struct kndSharedSetElemIdx **result)
{
    struct kndMemPool *mempool = self->mempool;
    void *page;
    int err;
    assert(mempool->page_size >= sizeof(struct kndSharedSetElemIdx));
    err = knd_mempool_page(mempool, KND_MEMPAGE_BASE, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndSharedSetElemIdx));
    *result = page;
    return knd_OK;
}

int knd_shared_set_dir_new(struct kndSharedSet *self, struct kndSharedSetDir **result)
{
    struct kndMemPool *mempool = self->mempool;
    void *page;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndSharedSetDir));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndSharedSetDir));
    *result = page;
    return knd_OK;
}
