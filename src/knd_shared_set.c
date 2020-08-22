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

#define DEBUG_SET_LEVEL_0 0
#define DEBUG_SET_LEVEL_1 0
#define DEBUG_SET_LEVEL_2 0
#define DEBUG_SET_LEVEL_3 0
#define DEBUG_SET_LEVEL_4 0
#define DEBUG_SET_LEVEL_TMP 1

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

        err = knd_shared_set_elem_idx_new(self->mempool, &sub_idx);
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

extern int knd_shared_set_intersect(struct kndSharedSet *self, struct kndSharedSet **sets, size_t num_sets)
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
    struct kndSharedSetElemIdx *idx = NULL;
    struct kndSharedSetElemIdx *new_idx = NULL;
    void *prev_elem = NULL;
    int idx_pos;
    int err;

    if (DEBUG_SET_LEVEL_TMP)
        knd_log("== set idx to save ID remainder: \"%.*s\"", id_size, id);

    assert(parent_idx != NULL);

    idx_pos = obj_id_base[(unsigned int)*id];
    if (id_size > 1) {
        do {
            idx = atomic_load_explicit(&parent_idx->idxs[idx_pos], memory_order_relaxed);
            if (idx) break;

            err = knd_shared_set_elem_idx_new(self->mempool, &new_idx);
            if (err) {
                knd_log("-- set elem idx mempool limit reached");
                return err;
            }
        } while (!atomic_compare_exchange_weak(&parent_idx->idxs[idx_pos], &idx, new_idx));

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

    if (DEBUG_SET_LEVEL_2)
        knd_log(".. get elem by ID, remainder \"%.*s\"", id_size, id);

    assert(parent_idx != NULL);

    idx_pos = obj_id_base[(unsigned int)*id];
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
        if (DEBUG_SET_LEVEL_2)
            knd_log("NB: -- set has no root idx");
        return knd_OK;
    }

    err = traverse_idx(self->idx, cb, obj, &count);
    if (err) return err;

    return knd_OK;
}

static int build_dir_footer(struct kndSharedSetDir *dir,
                                bool use_positional_indexing,
                                struct kndTask *task)
{
    unsigned char buf[KND_NAME_SIZE];
    size_t buf_size = KND_UINT_SIZE;
    unsigned int numval;
    struct kndSharedSetDirEntry *entry;
    struct kndOutput *out = task->out;
    int err;

    out->reset(out);
    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        entry = &dir->entries[i];
        /* explicit field naming */
        if (!use_positional_indexing) {
            if (entry->payload_size || entry->subdir) {
                err = out->writec(out, (char)i);
                KND_TASK_ERR("output failure");
            }
        }

        if (!entry->payload_size) {
            if (use_positional_indexing) {
                knd_pack_int(buf, 0);
                err = out->write(out, (const char*)buf, buf_size);
                KND_TASK_ERR("set output failed");
            }
        } else {
            numval = entry->payload_size;
            knd_pack_int(buf, numval);
            err = out->write(out, (const char*)buf, buf_size);
            KND_TASK_ERR("set output failed");
        }

        if (!entry->subdir) {
            if (use_positional_indexing) {
                err = out->write(out, "|0000", 5);
                KND_TASK_ERR("set output failed");
            }
        } else {
            err = out->writef(out, "|%zu", entry->subdir->total_size);
            KND_TASK_ERR("set output failed");
        }
    }

    knd_log("SUBDIRS:%zu ELEMS:%zu DIR SIZE:%zu DIR:%.*s",
            dir->num_subdirs, dir->num_elems, dir->total_size, out->buf_size, out->buf);

    //knd_pack_int(buf, numval);
    
    return knd_OK;
}

static int traverse_sync(struct kndSharedSetElemIdx *parent_idx,
                         map_cb_func cb, void *obj, struct kndSharedSetDir **result_dir)
{
    char buf[KND_ID_SIZE];
    size_t buf_size = 0;
    struct kndTask *task = obj;
    struct kndSharedSetElemIdx *idx;
    struct kndSharedSetDir *dir, *subdir;
    struct kndSharedSetDirEntry *entry;
    size_t num_empty_entries = 0;
    bool use_positional_indexing = true;
    void *elem;
    int err;

    dir = calloc(1, sizeof(struct kndSharedSetDir));
    if (!dir) {
        err = knd_NOMEM;
        KND_TASK_ERR("failed to alloc kndSharedSetDir");
    }

    /* sync subdirs */
    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        idx = parent_idx->idxs[i];
        if (!idx) continue;

        entry = &dir->entries[i];

        subdir = NULL;
        err = traverse_sync(idx, cb, obj, &subdir);
        if (err) return err;

        entry->subdir = subdir;

        dir->total_size += entry->subdir->total_size;
        dir->total_elems += subdir->total_elems;
        dir->num_subdirs++;
    }

    /* sync elem bodies */
    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        elem = parent_idx->elems[i];
        if (!elem) {
            if (!parent_idx->idxs[i])
                num_empty_entries++;
            continue;
        }

        buf_size = 0;
        buf[buf_size] = obj_id_seq[i];
        buf_size = 1;

        err = cb(obj, buf, buf_size, 0, elem);
        if (err) return err;

        entry = &dir->entries[i];
        entry->payload_size = task->out->buf_size;
        dir->total_size += entry->payload_size;

        dir->num_elems++;

        // TODO: sync entry payload to file
    }

    /* build footer */
    if (num_empty_entries > KND_RADIX_BASE / 2)
        use_positional_indexing = false;

    err = build_dir_footer(dir, use_positional_indexing, task);
    KND_TASK_ERR("failed to build set dir footer");

    *result_dir = dir;

    return knd_OK;
}

int knd_shared_set_sync(struct kndSharedSet *self, map_cb_func cb, size_t *total_size, struct kndTask *task)
{
    struct kndSharedSetDir *root_dir;
    int err;

    err = traverse_sync(self->idx, cb, task, &root_dir);
    if (err) return err;

    knd_log("== total exported set size:%zu", root_dir->total_size);
    *total_size = root_dir->total_size;
    return knd_OK;
}

int knd_shared_set_new(struct kndMemPool *mempool, struct kndSharedSet **result)
{
    void *page;
    struct kndSharedSet *set;
    struct kndSharedSetElemIdx *idx;
    int err;

    if (!mempool) {
        set = calloc(1, sizeof(struct kndSharedSet));
        if (!set) return knd_NOMEM;

        err = knd_shared_set_elem_idx_new(mempool, &idx);
        if (err) return err;
        set->idx = idx;
        *result = set;
        return knd_OK;
    }

    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL, sizeof(struct kndSharedSet), &page);
        if (err) return err;

        err = knd_shared_set_elem_idx_new(mempool, &idx);
        if (err) return err;
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_SMALL, sizeof(struct kndSharedSet), &page);
        if (err) return err;
        err = knd_shared_set_elem_idx_new(mempool, &idx);
        if (err) return err;
    }
    
    *result = page;
    (*result)->mempool = mempool;
    (*result)->idx = idx;
    return knd_OK;
}

int knd_shared_set_elem_idx_new(struct kndMemPool *mempool, struct kndSharedSetElemIdx **result)
{
    void *page;
    int err;

    if (!mempool) {
        *result = calloc(1, sizeof(struct kndSharedSetElemIdx));
        return *result ? 0 : knd_NOMEM;
    }

    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_BASE, sizeof(struct kndSharedSetElemIdx), &page);
        if (err) return err;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_BASE, sizeof(struct kndSharedSetElemIdx), &page);
        if (err) return err;
        break;
    }
    *result = page;
    return knd_OK;
}
