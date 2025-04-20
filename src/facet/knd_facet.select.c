#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_facet.h"
#include "knd_class.h"
#include "knd_task.h"
#include "knd_mempool.h"
#include "knd_output.h"
#include "knd_utils.h"
#include "knd_config.h"

#define DEBUG_FACET_SELECT_LEVEL_1 0
#define DEBUG_FACET_SELECT_LEVEL_2 0
#define DEBUG_FACET_SELECT_LEVEL_3 0
#define DEBUG_FACET_SELECT_LEVEL_4 0
#define DEBUG_FACET_SELECT_LEVEL_5 0
#define DEBUG_FACET_SELECT_LEVEL_TMP 1

#if 0
static int compare_set_by_size_ascend(const void *a, const void *b)
{
    struct kndFacet **obj1, **obj2;

    obj1 = (struct kndFacet**)a;
    obj2 = (struct kndFacet**)b;

    if ((*obj1)->num_elems == (*obj2)->num_elems) return 0;
    if ((*obj1)->num_elems > (*obj2)->num_elems) return 1;

    return -1;
}

int knd_shared_set_intersect(struct kndFacet *self, struct kndFacet **sets, size_t num_sets)
{
    struct kndFacetElemIdx *base_idx;
    struct kndFacetElemIdx *idxs[KND_MAX_CLAUSES];
    size_t num_idxs = num_sets - 1;
    int err;

    assert(num_sets > 1);

    /* sort sets by size */
    qsort(sets, num_sets, sizeof(struct kndFacet*), compare_set_by_size_ascend);

    /* the smallest set is taken as a base */
    base_idx = sets[0]->idx;
    sets++;

    for (size_t i = 0; i < num_idxs; i++)
        idxs[i] = sets[i]->idx;

    err = traverse(self, base_idx, idxs, num_idxs, self->idx);
    if (err) return err;

    return knd_OK;
}

int knd_shared_set_get(struct kndFacet *self, const char *key, size_t key_size, void **elem)
{
    int err;
    if (!self->idx) return knd_FAIL;

    err = get_elem(self, self->idx, elem, key, key_size);
    if (err) return err;

    return knd_OK;
}

static int traverse_idx(struct kndFacetElemIdx *parent_idx, map_cb_func cb, void *obj, size_t *count)
{
    char buf[KND_ID_SIZE];
    size_t buf_size = 0;
    struct kndFacetElemIdx *idx;
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

int knd_shared_set_map(struct kndFacet *self, map_cb_func cb, void *obj)
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

static int get_elem(struct kndFacet *self, struct kndFacetElemIdx *parent_idx,
                    void **result, const char *id, size_t id_size)
{
    struct kndFacetElemIdx *idx;
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
#endif

int knd_facet_map(struct kndFacet *facet, void *val,
                  const char *range_from, size_t range_from_size,
                  const char *range_to, size_t range_to_size,
                  knd_facet_map_fn cb, struct kndTask *task)
{
    int err;

    if (facet->cache_size) {
        for (size_t i = 0; i < facet->cache_size; i++) {
            err = cb(facet->cache[i], task);
            KND_TASK_ERR("failed to call a facet cb func");
        }
    }

    for (size_t i = 0; i < facet->num_children; i++) {
        //
    }

    return knd_OK;
}
