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

static int traverse_marshall(struct kndSharedSetElemIdx *parent_idx, char *idbuf, size_t idbuf_size,
                             const char *filename, size_t filename_size,
                             elem_marshall_cb cb, struct kndSharedSetDir **result_dir, struct kndTask *task);

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
    struct kndOutput *out = task->out;
    size_t block_size;
    int err;

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        block_size = dir->elem_block_sizes[i];
        if (use_keys) {
            if (!block_size) continue;
            err = out->writec(out, obj_id_seq[i]);
            KND_TASK_ERR("output failure");
        }
        knd_pack_int(buf, block_size, cell_size);
        err = out->write(out, (const char*)buf, cell_size);
        KND_TASK_ERR("set dir output failed");
    }
    err = out->writec(out, (char)cell_size);
    KND_TASK_ERR("output failure");
    err = out->writec(out, (int)use_keys);
    KND_TASK_ERR("output failure");
    return knd_OK;
}

static int build_subdirs_footer(struct kndSharedSetDir *dir, char *idbuf, size_t idbuf_size,
                                bool use_keys, size_t cell_size, struct kndTask *task)
{
    unsigned char buf[KND_NAME_SIZE];
    struct kndSharedSetDir *subdir;
    struct kndOutput *out = task->out;
    size_t subdir_block_size;
    int err;

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        subdir = dir->subdirs[i];

        if (!subdir)
            subdir_block_size = 0;
        else
            subdir_block_size = subdir->total_size;

        if (use_keys) {
            if (!subdir_block_size) continue;
            err = out->writec(out, obj_id_seq[i]);
            KND_TASK_ERR("subdir output failure");
        }

        if (DEBUG_SHARED_SET_LEVEL_3) {
            idbuf[idbuf_size] = obj_id_seq[i];
            knd_log(">> \"%.*s\" subdir size: %zu", idbuf_size + 1, idbuf, subdir_block_size);
        }

        knd_pack_int(buf, subdir_block_size, cell_size);
        err = out->write(out, (const char*)buf, cell_size);
        KND_TASK_ERR("set dir output failed");
    }
    if (use_keys) {
        err = out->writec(out, (char)dir->num_subdirs);
        KND_TASK_ERR("output failure");
    }
    err = out->writec(out, (char)cell_size);
    KND_TASK_ERR("output failure");
    err = out->writec(out, (char)use_keys);
    KND_TASK_ERR("output failure");
    return knd_OK;
}

static int marshall_elems(struct kndSharedSetElemIdx *parent_idx, struct kndSharedSetDir *dir,
                          const char *idbuf, size_t idbuf_size,
                          elem_marshall_cb cb, bool use_keys, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    void *elem;
    size_t block_size;
    int err;

    knd_log(".. marshall elems..");

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        if (parent_idx->idxs[i]) dir->num_subdirs++;
        elem = parent_idx->elems[i];
        if (!elem) continue;

        if (use_keys) {
            if (dir->num_elems) {
                // add rec separator
                err = out->writec(out, (char)'\0');
                KND_TASK_ERR("output failure");
            }
            err = out->writec(out, obj_id_seq[i]);
            KND_TASK_ERR("output failure");
        }

        if (DEBUG_SHARED_SET_LEVEL_TMP)
            knd_log(">> marshall elem %.*s", idbuf_size, idbuf);

        err = cb(elem, &block_size, task);
        if (err) return err;

        if (block_size > dir->cell_max_val)
            dir->cell_max_val = block_size;

        dir->elem_block_sizes[i] = block_size;
        dir->num_elems++;
    }
    return knd_OK;
}

static int marshall_subdirs(struct kndSharedSetElemIdx *parent_idx, struct kndSharedSetDir *dir,
                            char *idbuf, size_t idbuf_size, const char *filename, size_t filename_size,
                            elem_marshall_cb cb, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndSharedSetElemIdx *idx;
    struct kndSharedSetDir *subdir;
    size_t cell_size = 1;
    bool use_keys = false;
    int err;

    dir->cell_max_val = 0;

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        idx = parent_idx->idxs[i];
        if (!idx) continue;

        idbuf[idbuf_size] = obj_id_seq[i];
        subdir = NULL;
        err = traverse_marshall(idx, idbuf, idbuf_size + 1, filename, filename_size, cb, &subdir, task);
        KND_TASK_ERR("failed to traverse a subdir");

        dir->subdirs[i] = subdir;

        if (subdir->total_size > dir->cell_max_val)
            dir->cell_max_val = subdir->total_size;

        dir->total_elems += subdir->total_elems;
        dir->subdir_block_size += subdir->total_size;
        dir->num_subdirs++;
    }

    // build subdirs footer
    cell_size = knd_min_bytes(dir->cell_max_val);
    if (KND_RADIX_BASE - dir->num_subdirs > KND_RADIX_BASE / 2)
        use_keys = true;

    out->reset(out);
    err = build_subdirs_footer(dir, idbuf, idbuf_size, use_keys, cell_size, task);
    KND_TASK_ERR("failed to build set dir footer");

    err = knd_append_file((const char*)filename, out->buf, out->buf_size);
    KND_TASK_ERR("set idx write failure");

    dir->subdir_block_size += out->buf_size;
    dir->total_size += dir->subdir_block_size;

    if (DEBUG_SHARED_SET_LEVEL_2)
        knd_log(">> \"%.*s\" total subdir block size:%zu", idbuf_size, idbuf, dir->subdir_block_size);

    return knd_OK;
}

static int traverse_marshall(struct kndSharedSetElemIdx *parent_idx, char *idbuf, size_t idbuf_size,
                             const char *filename, size_t filename_size,
                             elem_marshall_cb cb, struct kndSharedSetDir **result_dir, struct kndTask *task)
{
    unsigned char buf[KND_NAME_SIZE];
    struct kndOutput *out = task->out;
    struct kndSharedSetDir *dir;
    size_t byte_size, cell_size = 1;
    bool use_keys = false;
    int err;

    dir = calloc(1, sizeof(struct kndSharedSetDir));
    if (!dir) {
        err = knd_NOMEM;
        KND_TASK_ERR("failed to alloc kndSharedSetDir");
    }

    out->reset(out);
    err = marshall_elems(parent_idx, dir, idbuf, idbuf_size, cb, false, task);
    KND_TASK_ERR("failed to marshall elems");

    if (out->buf_size) {
        // calc cell size
        cell_size = knd_min_bytes(dir->cell_max_val);
        // calc footer overhead
        if ((float)KND_SET_MIN_FOOTER_SIZE / (float)out->buf_size > KND_MAX_IDX_OVERHEAD) {

            if (DEBUG_SHARED_SET_LEVEL_TMP)
                knd_log("NB: another run to optimize elem packing (use explicit field keys)");

            out->reset(out);
            dir->num_elems = 0;
            err = marshall_elems(parent_idx, dir, idbuf, idbuf_size, cb, true, task);
            KND_TASK_ERR("failed to marshall elems");
            err = out->writec(out, (char)0);
            KND_TASK_ERR("output failure");
            err = out->writec(out, (char)1);
            KND_TASK_ERR("output failure");
        }
        else {
            if (KND_RADIX_BASE - dir->num_elems > KND_RADIX_BASE / 2)
                use_keys = true;
            
            err = build_elems_footer(dir, use_keys, cell_size, task);
            KND_TASK_ERR("failed to build set dir footer");
        }

        dir->payload_block_size = out->buf_size;
        dir->total_elems = dir->num_elems;
        dir->total_size = dir->payload_block_size;

        if (DEBUG_SHARED_SET_LEVEL_2)
            knd_log(">> \"%.*s\" payload block \"%.*s\" payload size:%zu",
                    idbuf_size, idbuf, out->buf_size, out->buf, dir->payload_block_size);

        err = knd_append_file((const char*)filename, out->buf, out->buf_size);
        KND_TASK_ERR("set idx write failure");
    }

    // sync subdirs
    if (dir->num_subdirs) {
        out->reset(out);
        dir->num_subdirs = 0;
        err = marshall_subdirs(parent_idx, dir, idbuf, idbuf_size, filename, filename_size, cb, task);
        KND_TASK_ERR("failed to marshall subdirs");
    }

    // payload block size
    out->reset(out);
    if (DEBUG_SHARED_SET_LEVEL_2)
        knd_log("== \"%.*s\" payload size:%zu  subdir block size:%zu  total:%zu",
                idbuf_size, idbuf, dir->payload_block_size, dir->subdir_block_size, dir->total_size);

    if (dir->payload_block_size) {
        byte_size = knd_min_bytes(dir->payload_block_size);
        knd_pack_int(buf, dir->payload_block_size, byte_size);
        err = out->write(out, (const char*)buf, byte_size);
        KND_TASK_ERR("elem block size output failed");
        err = out->writec(out, (char)byte_size);
        KND_TASK_ERR("output failure");
        dir->total_size += byte_size + 1;
    } else {
        err = out->writec(out, '\0');
        KND_TASK_ERR("output failure");
        dir->total_size++;
    }
    err = knd_append_file((const char*)filename, out->buf, out->buf_size);
    KND_TASK_ERR("set idx write failure");

    *result_dir = dir;
    return knd_OK;
}

int knd_shared_set_marshall(struct kndSharedSet *self, const char *filename, size_t filename_size,
                            elem_marshall_cb cb, size_t *total_size, struct kndTask *task)
{
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size = 0;
    struct kndSharedSetDir *root_dir;
    int err;

    err = traverse_marshall(self->idx, idbuf, idbuf_size, filename, filename_size, cb, &root_dir, task);
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

int knd_shared_set_dir_new(struct kndMemPool *mempool, struct kndSharedSetDir **result)
{
    void *page;
    int err;
    if (!mempool) {
        *result = calloc(1, sizeof(struct kndSharedSetDir));
        return *result ? 0 : knd_NOMEM;
    }
    assert(mempool->small_page_size >= sizeof(struct kndSharedSetDir));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndSharedSetDir));
    *result = page;
    return knd_OK;
}
