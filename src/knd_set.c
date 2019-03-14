#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_class.h"
#include "knd_utils.h"
#include "knd_repo.h"
#include "knd_mempool.h"
#include "knd_set.h"
#include "knd_attr.h"
#include "knd_elem.h"
#include "knd_task.h"

#include <gsl-parser.h>

#define DEBUG_SET_LEVEL_0 0
#define DEBUG_SET_LEVEL_1 0
#define DEBUG_SET_LEVEL_2 0
#define DEBUG_SET_LEVEL_3 0
#define DEBUG_SET_LEVEL_4 0
#define DEBUG_SET_LEVEL_TMP 1

static int 
knd_compare_set_by_size_ascend(const void *a,
                                   const void *b)
{
    struct kndSet **obj1, **obj2;

    obj1 = (struct kndSet**)a;
    obj2 = (struct kndSet**)b;

    if ((*obj1)->num_elems == (*obj2)->num_elems) return 0;
    if ((*obj1)->num_elems > (*obj2)->num_elems) return 1;

    return -1;
}

static int kndSet_traverse(struct kndSet *self,
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

    if (DEBUG_SET_LEVEL_2)
        knd_log(".. traverse %.*s, total elems: %zu",
                self->base->name_size, self->base->name, self->num_elems);

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

        err = knd_set_elem_idx_new(self->mempool, &sub_idx);
        if (err) {
            knd_log("-- set elem idx mempool limit reached :(");
            return err;
        }

        result_idx->idxs[i] = sub_idx;

        err = kndSet_traverse(self,
                              idx,
                              nested_idxs, num_idxs,
                              sub_idx);
        if (err) return err;

    }
    
    return knd_OK;
}

extern int knd_set_intersect(struct kndSet *self,
                             struct kndSet **sets,
                             size_t num_sets)
{
    struct kndSetElemIdx *base_idx;
    struct kndSetElemIdx *idxs[KND_MAX_CLAUSES];
    size_t num_idxs = num_sets - 1;
    int err;

    if (num_sets == 1) {
        return knd_FAIL;
    }

    if (!self->idx) {
        err = knd_set_elem_idx_new(self->mempool, &self->idx);
        if (err) {
            knd_log("-- set elem idx mempool limit reached :(");
            return err;
        }
    }
    
    if (DEBUG_SET_LEVEL_2)
        knd_log(" .. intersection by Set \"%.*s\".. total sets:%zu",
                self->base->name_size, self->base->name, num_sets);

    /* sort sets by size */
    qsort(sets,
          num_sets,
          sizeof(struct kndSet*),
          knd_compare_set_by_size_ascend);

    /* the smallest set is taken as a base */
    base_idx = sets[0]->idx;
    sets++;

    for (size_t i = 0; i < num_idxs; i++)
        idxs[i] = sets[i]->idx;

    err = kndSet_traverse(self,
                          base_idx,
                          idxs, num_idxs,
                          self->idx);
    if (err) return err;

    return knd_OK;
}

static int save_elem(struct kndSet *self,
                     struct kndSetElemIdx *parent_idx,
                     void *elem,
                     const char *id,
                     size_t id_size)
{
    struct kndSetElemIdx *idx;
    int idx_pos;
    int err;

    if (DEBUG_SET_LEVEL_2) {
        knd_log("== set idx to save ID remainder: \"%.*s\" elem:%p",
                id_size, id, elem);
    }

    idx_pos = obj_id_base[(unsigned int)*id];
    if (id_size > 1) {
        idx = parent_idx->idxs[idx_pos];
        if (!idx) {
            err = knd_set_elem_idx_new(self->mempool, &idx);
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
    parent_idx->elems[idx_pos] = elem;
    self->num_elems++;
    self->num_valid_elems++;

    return knd_OK;
}

static int get_elem(struct kndSet *self,
                    struct kndSetElemIdx *parent_idx,
                    void **result,
                    const char *id,
                    size_t id_size)
{
    struct kndSetElemIdx *idx;
    void *elem;
    int idx_pos;
    int err;

    idx_pos = obj_id_base[(unsigned int)*id];

    if (DEBUG_SET_LEVEL_2)
        knd_log(".. get elem by ID, remainder \"%.*s\" POS:%d  idx:%p",
                id_size, id, idx_pos, parent_idx);

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

static int kndSet_add_elem(struct kndSet *self,
                           const char *key,
                           size_t key_size,
                           void *elem)
{
    int err;
    assert(key_size != 0);
    assert(key != NULL);

    if (!self->idx) {
        err = knd_set_elem_idx_new(self->mempool, &self->idx);
        if (err) {
            knd_log("-- set elem idx mempool limit reached :(");
            return err;
        }
    }
    err = save_elem(self, self->idx, elem, key, key_size);
    if (err) return err;
    return knd_OK;
}

static int kndSet_get_elem(struct kndSet *self,
                           const char *key,
                           size_t key_size,
                           void **elem)
{
    int err;

    if (!self->idx) return knd_FAIL;

    err = get_elem(self, self->idx, elem, key, key_size);
    if (err) return err;

    return knd_OK;
}

static int kndSet_traverse_idx(struct kndSetElemIdx *parent_idx,
                               map_cb_func cb,
                               void *obj,
                               size_t *count)
{
    char buf[KND_ID_SIZE];
    size_t buf_size = 0;
    struct kndSetElemIdx *idx;
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

        err = kndSet_traverse_idx(idx, cb, obj, count);
        if (err) return err;
    }

    return knd_OK;
}

static int kndSet_map(struct kndSet *self,
                      map_cb_func cb,
                      void *obj)
{
    size_t count = 0;
    int err;

    if (!self->idx) {
        if (DEBUG_SET_LEVEL_2)
            knd_log("NB: -- set has no root idx");
        return knd_OK;
    }

    err = kndSet_traverse_idx(self->idx, cb, obj, &count);
    if (err) return err;

    return knd_OK;
}

extern int kndSet_init(struct kndSet *self)
{
    self->add = kndSet_add_elem;
    self->get = kndSet_get_elem;
    self->map = kndSet_map;
    return knd_OK;
}

extern int knd_set_new(struct kndMemPool *mempool,
                       struct kndSet **result)
{
    void *page;
    int err;

    //knd_log("..set new [size:%zu]", sizeof(struct kndSet));
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                            sizeof(struct kndSet), &page);                    RET_ERR();
    *result = page;
    (*result)->mempool = mempool;
    kndSet_init(*result);

    return knd_OK;
}

extern int knd_set_elem_idx_new(struct kndMemPool *mempool,
                                struct kndSetElemIdx **result)
{
    void *page;
    int err;

    //knd_log("..set elem idx new [size:%zu] num pages:%zu",
    //       sizeof(struct kndSetElemIdx),
    //        mempool->pages_used);

    err = knd_mempool_alloc(mempool, KND_MEMPAGE_BASE,
                            sizeof(struct kndSetElemIdx), &page);                      RET_ERR();
    *result = page;
    return knd_OK;
}
