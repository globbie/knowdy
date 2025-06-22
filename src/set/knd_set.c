#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_class.h"
#include "knd_utils.h"
#include "knd_repo.h"
#include "knd_mempool.h"
#include "knd_set.h"
#include "knd_task.h"

#include <gsl-parser.h>

#define DEBUG_SET_LEVEL_0 0
#define DEBUG_SET_LEVEL_1 0
#define DEBUG_SET_LEVEL_2 0
#define DEBUG_SET_LEVEL_3 0
#define DEBUG_SET_LEVEL_4 0
#define DEBUG_SET_LEVEL_TMP 1

static int compare_set_by_size_ascend(const void *a,
                                      const void *b)
{
    struct kndSet **obj1, **obj2;

    obj1 = (struct kndSet**)a;
    obj2 = (struct kndSet**)b;

    if ((*obj1)->num_elems == (*obj2)->num_elems) return 0;
    if ((*obj1)->num_elems > (*obj2)->num_elems) return 1;

    return -1;
}

static int traverse(struct kndSet *self,
                    struct kndSetElemIdx *base_idx,
                    struct kndSetElemIdx **idxs,
                    size_t num_idxs,
                    struct kndSetElemIdx *result_idx)
{
    struct kndSetElemIdx *nested_idxs[KND_MAX_CLAUSES];
    struct kndSetElemIdx *idx, *sub_idx, *nested_idx;
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

        err = knd_set_elem_idx_new(&sub_idx, self->mempool);
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

int knd_set_intersect(struct kndSet *self, struct kndSet **sets, size_t num_sets)
{
    struct kndSetElemIdx *base_idx;
    struct kndSetElemIdx *idxs[KND_MAX_CLAUSES];

    assert (num_sets >= 2 && sets != NULL);

    size_t num_idxs = num_sets - 1;
    int err;

    if (num_sets == 1) {
        return knd_FAIL;
    }

    /* sort sets by size */
    qsort(sets, num_sets, sizeof(struct kndSet*), compare_set_by_size_ascend);

    /* the smallest set is taken as a base */
    base_idx = sets[0]->idx;
    sets++;

    for (size_t i = 0; i < num_idxs; i++) {
        idxs[i] = sets[i]->idx;
    }

    err = traverse(self, base_idx, idxs, num_idxs, self->idx);
    if (err) return err;

    return knd_OK;
}

static int save_elem(struct kndSet *self, struct kndSetElemIdx *parent_idx,
                     void *elem, const char *id, size_t id_size)
{
    struct kndSetElemIdx *idx;
    int idx_pos;
    int err;

    if (DEBUG_SET_LEVEL_2)
        knd_log("== set idx to save ID remainder: \"%.*s\"", id_size, id);

    idx_pos = obj_id_base[(unsigned char)*id];
    if (id_size > 1) {
        idx = parent_idx->idxs[idx_pos];
        if (!idx) {
            err = knd_set_elem_idx_new(&idx, self->mempool);
            if (err) {
                knd_log("-- set elem idx mempool limit reached");
                return err;
            }
            parent_idx->idxs[idx_pos] = idx;
        }
        err = save_elem(self, idx, elem, id + 1, id_size - 1);
        if (err) return err;
        return knd_OK;
    }

    /* assign elem */
    switch (self->type) {
    case KND_SET_UNIQUE_VALUES:
        if (parent_idx->elems[idx_pos] != NULL) return knd_CONFLICT;
        parent_idx->elems[idx_pos] = elem;
        break;
    case KND_SET_MULTIPLE_VALUES:
        break;
    default:
        break;
    }
    self->num_elems++;
    return knd_OK;
}

static int get_elem(struct kndSet *self, struct kndSetElemIdx *parent_idx,
                    void **result, const char *id, size_t id_size)
{
    struct kndSetElemIdx *idx;
    void *elem;
    int idx_pos;
    int err;

    idx_pos = obj_id_base[(unsigned char)*id];

    if (DEBUG_SET_LEVEL_2)
        knd_log(".. get elem by ID, remainder \"%.*s\" POS:%d", id_size, id, idx_pos);

    if (id_size > 1) {
        idx = parent_idx->idxs[idx_pos];
        if (!idx) return knd_NO_MATCH;

        err = get_elem(self, idx, result, id + 1, id_size - 1);
        if (err) return err;

        return knd_OK;
    }

    elem = parent_idx->elems[idx_pos];
    if (!elem) {
        return knd_NO_MATCH;
    }

    *result = elem;
    return knd_OK;
}

int knd_set_add(struct kndSet *self, const char *key, size_t key_size, void *elem)
{
    int err;
    assert(key_size != 0);
    assert(key != NULL);
    err = save_elem(self, self->idx, elem, key, key_size);
    if (err) return err;
    return knd_OK;
}

int knd_set_get(struct kndSet *self, const char *key, size_t key_size, void **elem)
{
    int err;
    if (!self->idx) return knd_FAIL;
    err = get_elem(self, self->idx, elem, key, key_size);
    if (err) return err;
    return knd_OK;
}

static int traverse_idx(struct kndSetElemIdx *parent_idx, map_cb_func cb, void *ctx)
{
    struct kndSetElemIdx *idx;
    void *elem;
    int err;

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        elem = parent_idx->elems[i];
        if (!elem) continue;
        err = cb(elem, ctx);
        if (err) return err;
    }

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        idx = parent_idx->idxs[i];
        if (!idx) continue;

        err = traverse_idx(idx, cb, ctx);
        if (err) return err;
    }
    return knd_OK;
}

int knd_set_map(struct kndSet *self, map_cb_func cb, void *ctx)
{
    int err;

    if (!self->idx) {
        if (DEBUG_SET_LEVEL_2)
            knd_log("NB: -- set has no root idx");
        return knd_OK;
    }
    err = traverse_idx(self->idx, cb, ctx);
    if (err) return err;

    return knd_OK;
}

static int build_dir_footer(struct kndSetDir *dir,
                                bool use_positional_indexing,
                                struct kndTask *task)
{
    unsigned char buf[KND_NAME_SIZE];
    size_t buf_size = KND_UINT_SIZE;
    unsigned int numval;
    struct kndSetDirEntry *entry;
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
                knd_pack_u32(buf, 0);
                err = out->write(out, (const char*)buf, buf_size);
                KND_TASK_ERR("set output failed");
            }
        } else {
            numval = entry->payload_size;
            knd_pack_u32(buf, numval);
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


int knd_set_new(struct kndSet **result, knd_set_type type, struct kndMemPool *mempool)
{
    void *page;
    struct kndSetElemIdx *idx;
    int err;

    assert(mempool->small_page_size >= sizeof(struct kndSet));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndSet));

    err = knd_set_elem_idx_new(&idx, mempool);
    if (err) return err;
    
    *result = page;
    (*result)->type = type;
    (*result)->mempool = mempool;
    (*result)->idx = idx;
    return knd_OK;
}

int knd_set_elem_idx_new(struct kndSetElemIdx **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->base_page_size >= sizeof(struct kndSetElemIdx));
    err = knd_mempool_page(mempool, KND_MEMPAGE_BASE, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndSetElemIdx));
    *result = page;
    return knd_OK;
}

int knd_set_elem_new(struct kndSetElem **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndSetElem));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndSetElem));
    *result = page;
    return knd_OK;
}
