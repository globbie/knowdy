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

static int traverse_marshall(struct kndSharedSetElemIdx *parent_idx, elem_marshall_cb cb,
                             struct kndSharedSetDir **result_dir, struct kndTask *task);

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

    if (DEBUG_SHARED_SET_LEVEL_3)
        knd_log("== set idx to save ID remainder: \"%.*s\"", id_size, id);

    assert(parent_idx != NULL);

    idx_pos = obj_id_base[(unsigned int)*id];
    if (id_size > 1) {
        do {
            orig_idx = atomic_load_explicit(&parent_idx->idxs[idx_pos], memory_order_relaxed);
            if (orig_idx) {
                idx = orig_idx;
                break;
            }
            err = knd_shared_set_elem_idx_new(self->mempool, &idx);
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
        if (DEBUG_SHARED_SET_LEVEL_2)
            knd_log("NB: -- set has no root idx");
        return knd_OK;
    }
    err = traverse_idx(self->idx, cb, obj, &count);
    if (err) return err;
    return knd_OK;
}

static int build_elems_footer(struct kndSharedSetDir *dir, bool use_keys, size_t cell_size, struct kndTask *task)
{
    unsigned char buf[KND_NAME_SIZE];
    size_t numval;
    struct kndSharedSetDirEntry *entry;
    struct kndOutput *out = task->out;
    int err;

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        entry = &dir->entries[i];
        if (use_keys) {
            if (!entry->payload_size) continue;
            err = out->writec(out, obj_id_seq[i]);
            KND_TASK_ERR("output failure");
        }
        numval = entry->payload_size;
        switch (cell_size) {
        case 4:
            knd_pack_u32(buf, numval);
            break;
        case 3:
            knd_pack_u24(buf, numval);
            break;
        case 2:
            knd_pack_u16(buf, numval);
            break;
        default:
            *buf = numval;
            break;
        }
        err = out->write(out, (const char*)buf, cell_size);
        KND_TASK_ERR("set dir output failed");
    }
    err = out->writec(out, (char)cell_size);
    KND_TASK_ERR("output failure");
    err = out->writec(out, (char)use_keys);
    KND_TASK_ERR("output failure");
    return knd_OK;
}

static int build_subdirs_footer(struct kndSharedSetDir *dir, bool use_keys, size_t cell_size, struct kndTask *task)
{
    unsigned char buf[KND_NAME_SIZE];
    size_t numval;
    struct kndSharedSetDirEntry *entry;
    struct kndSharedSetDir *subdir;
    struct kndOutput *out = task->out;
    int err;

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        entry = &dir->entries[i];
        subdir = entry->subdir;
        if (use_keys) {
            if (!subdir->total_size) continue;
            err = out->writec(out, obj_id_seq[i]);
            KND_TASK_ERR("subdir output failure");
        }
        numval = subdir->total_size;
        switch (cell_size) {
        case 4:
            knd_pack_u32(buf, numval);
            break;
        case 3:
            knd_pack_u24(buf, numval);
            break;
        case 2:
            knd_pack_u16(buf, numval);
            break;
        default:
            *buf = numval;
            break;
        }
        err = out->write(out, (const char*)buf, cell_size);
        KND_TASK_ERR("set dir output failed");
    }
    err = out->writec(out, (char)cell_size);
    KND_TASK_ERR("output failure");
    err = out->writec(out, (char)use_keys);
    KND_TASK_ERR("output failure");
    
    return knd_OK;
}

static int marshall_elems(struct kndSharedSetElemIdx *parent_idx, struct kndSharedSetDir *dir,
                          elem_marshall_cb cb, bool use_keys, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    void *elem;
    struct kndSharedSetDirEntry *entry;
    int err;

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        if (parent_idx->idxs[i]) dir->num_subdirs++;

        elem = parent_idx->elems[i];
        if (!elem) continue;
        entry = &dir->entries[i];
        entry->elem = elem;

        if (use_keys) {
            if (dir->num_elems) {
                err = out->writec(out, (char)'{');
                KND_TASK_ERR("output failure");
            }
            err = out->writec(out, obj_id_seq[i]);
            KND_TASK_ERR("output failure");
        }

        err = cb(elem, &entry->payload_size, task);
        if (err) return err;

        if (entry->payload_size > dir->cell_max_val)
            dir->cell_max_val = entry->payload_size;

        dir->num_elems++;
    }
    return knd_OK;
}

static int marshall_subdirs(struct kndSharedSetElemIdx *parent_idx, struct kndSharedSetDir *dir,
                            elem_marshall_cb cb, struct kndTask *task)
{
    unsigned char buf[KND_NAME_SIZE];
    struct kndOutput *out = task->out;
    struct kndSharedSetElemIdx *idx;
    struct kndSharedSetDir *subdir;
    struct kndSharedSetDirEntry *entry;
    size_t byte_size, cell_size = 1;
    size_t subdirs_total_size = 0;
    size_t numval = 0;
    bool use_keys = false;
    int err;

    dir->cell_max_val = 0;

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        idx = parent_idx->idxs[i];
        if (!idx) continue;
        entry = &dir->entries[i];

        subdir = NULL;
        err = traverse_marshall(idx, cb, &subdir, task);
        KND_TASK_ERR("failed to traverse a subdir");

        entry->subdir = subdir;

        subdirs_total_size += entry->subdir->total_size;

        if (entry->subdir->total_size > dir->cell_max_val)
            dir->cell_max_val = entry->subdir->total_size;

        dir->total_elems += subdir->total_elems;
        dir->num_subdirs++;
    }

    knd_log("\ntotal subdirs size:%zu", subdirs_total_size);

    // build subdirs footer
    cell_size = knd_min_bytes(dir->cell_max_val);
    if (KND_RADIX_BASE - dir->num_subdirs > KND_RADIX_BASE / 2)
        use_keys = true;

    dir->total_size += subdirs_total_size;

    knd_log(">> subdir footer  max val size:%zu cell size:%zu  use keys:%d",
            dir->cell_max_val, cell_size, use_keys);

    err = build_subdirs_footer(dir, use_keys, cell_size, task);
    KND_TASK_ERR("failed to build set dir footer");

    // elems area size
    if (dir->payload_size) {
        if (DEBUG_SHARED_SET_LEVEL_2)
            knd_log("== PAYLOAD size:%zu", dir->payload_size);

        byte_size = knd_min_bytes(dir->payload_size);
        numval = dir->payload_size;
        switch (byte_size) {
        case 4:
            knd_pack_u32(buf, numval);
            break;
        case 3:
            knd_pack_u24(buf, numval);
            break;
        case 2:
            knd_pack_u16(buf, numval);
            break;
        default:
            *buf = numval;
            break;
        }
        err = out->write(out, (const char*)buf, byte_size);
        KND_TASK_ERR("elem area size output failed");
        err = out->writec(out, (char)byte_size);
        KND_TASK_ERR("output failure");
    } else {
        err = out->writec(out, (char)0);
        KND_TASK_ERR("output failure");
    }
    
    err = knd_append_file((const char*)task->filepath, out->buf, out->buf_size);
    KND_TASK_ERR("set idx write failure");
    return knd_OK;
}

static int traverse_marshall(struct kndSharedSetElemIdx *parent_idx, elem_marshall_cb cb,
                             struct kndSharedSetDir **result_dir, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndSharedSetDir *dir;
    size_t cell_size = 1;
    bool use_keys = false;
    int err;

    dir = calloc(1, sizeof(struct kndSharedSetDir));
    if (!dir) {
        err = knd_NOMEM;
        KND_TASK_ERR("failed to alloc kndSharedSetDir");
    }
    out->reset(out);
    err = marshall_elems(parent_idx, dir, cb, false, task);
    KND_TASK_ERR("failed to marshall elems");

    if (out->buf_size) {
        // calc cell size
        cell_size = knd_min_bytes(dir->cell_max_val);

        // calc footer overhead 
        if ((float)KND_SET_MIN_FOOTER_SIZE / (float)out->buf_size > KND_MAX_IDX_OVERHEAD) {
            out->reset(out);
            dir->num_elems = 0;
            err = marshall_elems(parent_idx, dir, cb, true, task);
            KND_TASK_ERR("failed to marshall elems");
            
            err = out->writec(out, (char)0);
            KND_TASK_ERR("output failure");
            err = out->writec(out, (char)use_keys);
            KND_TASK_ERR("output failure");
        }
        else {
            if (KND_RADIX_BASE - dir->num_elems > KND_RADIX_BASE / 2)
                use_keys = true;
            
            err = build_elems_footer(dir, use_keys, cell_size, task);
            KND_TASK_ERR("failed to build set dir footer");
        }
        dir->payload_size = out->buf_size;

        knd_log(">> elems block \"%.*s\" payload size:%zu",
                out->buf_size, out->buf, dir->payload_size);

        err = knd_append_file((const char*)task->filepath, out->buf, out->buf_size);
        KND_TASK_ERR("set idx write failure");

        dir->total_elems = dir->num_elems;
        dir->total_size = dir->payload_size;
    }

    // sync subdirs
    if (dir->num_subdirs) {
        out->reset(out);
        dir->num_subdirs = 0;
        err = marshall_subdirs(parent_idx, dir, cb, task);
        KND_TASK_ERR("failed to marshall subdirs");
    } else {
        // empty subdir footer
    }

    *result_dir = dir;
    return knd_OK;
}

int knd_shared_set_marshall(struct kndSharedSet *self, elem_marshall_cb cb, size_t *total_size, struct kndTask *task)
{
    struct kndSharedSetDir *root_dir;
    int err;

    err = traverse_marshall(self->idx, cb, &root_dir, task);
    if (err) return err;

    if (DEBUG_SHARED_SET_LEVEL_TMP)
        knd_log("== total exported set size:%zu", root_dir->total_size);

    *total_size = root_dir->total_size;
    return knd_OK;
}

int knd_shared_set_new(struct kndMemPool *mempool, struct kndSharedSet **result)
{
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
    assert(mempool->small_page_size >= sizeof(struct kndSharedSet));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, (void**)&set);
    if (err) return err;
    memset(set, 0, sizeof(struct kndSharedSet));

    err = knd_shared_set_elem_idx_new(mempool, &idx);
    if (err) return err;
    set->mempool = mempool;
    set->idx = idx;
    *result = set;
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
    assert(mempool->page_size >= sizeof(struct kndSharedSetElemIdx));
    err = knd_mempool_page(mempool, KND_MEMPAGE_BASE, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndSharedSetElemIdx));
    *result = page;
    return knd_OK;
}
