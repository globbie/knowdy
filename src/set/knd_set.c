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

int knd_set_intersect(struct kndSet **sets, size_t num_sets,
                      struct kndSetRange *unused_var(range), struct kndSet **unused_var(result),
                      struct kndTask *unused_var(task))
{
    //struct kndSetDir *base_dir, *dir;
    struct kndSetDir *dirs[KND_MAX_CLAUSES];

    assert (num_sets >= 2 && sets != NULL);

    size_t num_dirs = num_sets - 1;
    //int err;

    if (num_sets == 1) {
        return knd_FAIL;
    }

    /* sort sets by size */
    qsort(sets, num_sets, sizeof(struct kndSet*), compare_set_by_size_ascend);

    /* the smallest set is taken as a base */
    // base_dir = sets[0]->dir;
    //sets++;

    for (size_t i = 0; i < num_dirs; i++) {
        dirs[i] = sets[i]->dir;
    }

    //err = knd_set_dir_new(&dir, task->mempool);
    //KND_TASK_ERR("failed to alloc a set elem dir");

    //err = traverse(base_dir, dirs, num_dirs, dir, task);
    //KND_TASK_ERR("failed to traverse set dirs");

    // TODO return kndSet

    return knd_OK;
}

static int save_list_elem(struct kndSet *self, struct kndSetDir *parent_dir,
                          int dir_pos, void *val)
{
    struct kndSetElem *ref, *prev;
    int err;

    err = knd_set_elem_new(&ref, self->mempool);
    if (err) {
        knd_log("-- set elem mempool limit reached");
        return err;
    }
    ref->val = val;

    if (parent_dir->elems[dir_pos]) {
        prev = parent_dir->elems[dir_pos];
        ref->next = prev;
        ref->numval = prev->numval + 1;
    }

    parent_dir->elems[dir_pos] = ref;
    return knd_OK;
}

static int save_elem(struct kndSet *self, struct kndSetDir *parent_dir,
                     void *elem, const char *id, size_t id_size, struct kndTask *task)
{
    struct kndSetDir *dir;
    int dir_pos;
    int err;

    if (DEBUG_SET_LEVEL_2) {
        knd_log("== set dir to save {id-remainder %.*s}", id_size, id);
    }

    dir_pos = obj_id_base[(unsigned char)*id];
    if (id_size > 1) {
        dir = parent_dir->subdirs[dir_pos];
        if (!dir) {
            err = knd_set_dir_new(&dir, parent_dir->id, parent_dir->id_size, id, self->mempool);
            KND_TASK_ERR("failed to alloc a set dir");
            
            parent_dir->subdirs[dir_pos] = dir;
        }
        err = save_elem(self, dir, elem, id + 1, id_size - 1, task);
        if (err) return err;
        return knd_OK;
    }

    /* assign elem */
    switch (self->type) {
    case KND_SET_UNIQUE_VALUES:
        if (parent_dir->elems[dir_pos] != NULL) return knd_CONFLICT;
        parent_dir->elems[dir_pos] = elem;
        self->num_elems++;
        return knd_OK;
    case KND_SET_MULTIPLE_VALUES:
        err = save_list_elem(self, parent_dir, dir_pos, elem);
        if (err) return err;
        self->num_elems++;
        return knd_OK;
    default:
        break;
    }
    return knd_FAIL;
}

static int get_elem(struct kndSet *self, struct kndSetDir *parent_dir,
                    void **result, const char *id, size_t id_size)
{
    struct kndSetDir *dir;
    void *elem;
    int dir_pos;
    int err;

    dir_pos = obj_id_base[(unsigned char)*id];

    if (DEBUG_SET_LEVEL_2) {
        knd_log(".. get elem by ID, {id-remainder %.*s} {dir-pos %d}",
                id_size, id, dir_pos);
    }
    if (id_size > 1) {
        dir = parent_dir->subdirs[dir_pos];
        if (!dir) return knd_NO_MATCH;

        err = get_elem(self, dir, result, id + 1, id_size - 1);
        if (err) return err;

        return knd_OK;
    }

    elem = parent_dir->elems[dir_pos];
    if (!elem) {
        return knd_NO_MATCH;
    }

    *result = elem;
    return knd_OK;
}

int knd_set_add(struct kndSet *self, const char *key, size_t key_size, void *elem, struct kndTask *task)
{
    int err;
    assert(key_size != 0);
    assert(key != NULL);
    assert(elem != NULL);

    err = save_elem(self, self->dir, elem, key, key_size, task);
    KND_TASK_ERR("failed to add an elem to a set");

    return knd_OK;
}

int knd_set_get(struct kndSet *self, const char *key, size_t key_size, void **elem,
                struct kndTask *unused_var(task))
{
    int err;
    if (!self->dir) return knd_FAIL;
    err = get_elem(self, self->dir, elem, key, key_size);
    if (err) return err;
    return knd_OK;
}

static int apply_map_cb(struct kndSet *self, void *obj, map_cb_t cb, void *ctx, struct kndTask *task)
{
    struct kndSetElem *elems, *elem;
    int err;

    switch (self->type) {
    case KND_SET_UNIQUE_VALUES:
        err = cb(obj, ctx, task);
        if (err) return err;
        return knd_OK;
    case KND_SET_MULTIPLE_VALUES:
        elems = obj;
        FOREACH (elem, elems) {
            err = cb(elem->val, ctx, task);
            if (err) return err;
        }
        return knd_OK;
    default:
        break;
    }
    return knd_FAIL;
}

static int apply_filter_cb(struct kndSet *self, void *obj,
                           filter_cb_t filter_cb, void *filter_ctx,
                           map_cb_t map_cb, void *map_ctx, struct kndTask *task)
{
    struct kndSetElem *elems, *elem;
    int err;

    assert (filter_cb != NULL);
    assert (map_cb != NULL);

    switch (self->type) {
    case KND_SET_UNIQUE_VALUES:
        err = filter_cb(obj, filter_ctx);
        switch (err) {
        case knd_OK:
            err = map_cb(obj, map_ctx, task);
            if (err) return err;
            break;
        case knd_NO_MATCH:
            return knd_OK;
        default:
            return err;
        }
        return knd_OK;
    case KND_SET_MULTIPLE_VALUES:
        elems = obj;
        FOREACH (elem, elems) {
            err = filter_cb(elem->val, filter_ctx);
            switch (err) {
            case knd_OK:
                err = map_cb(elem->val, map_ctx, task);
                if (err) return err;
                break;
            case knd_NO_MATCH:
                continue;
            default:
                return err;
            }
        }
        return knd_OK;
    default:
        break;
    }
    return knd_FAIL;
}

static int traverse_dir(struct kndSet *self, struct kndSetDir *parent_dir,
                        struct kndSetRange *range,
                        filter_cb_t filter_cb, void *filter_ctx,
                        map_cb_t map_cb, void *map_ctx, struct kndTask *task)
{
    struct kndSetDir *dir;
    void *obj;
    int err;

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        obj = parent_dir->elems[i];
        if (!obj) continue;

        // TODO: apply range

        if (!filter_cb) {
            err = apply_map_cb(self, obj, map_cb, map_ctx, task);
            if (err) return err;
            continue;
        }

        err = apply_filter_cb(self, obj, filter_cb, filter_ctx, map_cb, map_ctx, task);
        if (err) return err;
    }

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        dir = parent_dir->subdirs[i];
        if (!dir) continue;

        // TODO: apply range

        err = traverse_dir(self, dir, range, filter_cb, filter_ctx, map_cb, map_ctx, task);
        if (err) return err;
    }
    return knd_OK;
}

int knd_set_map(struct kndSet *self, struct kndSetRange *range,
                filter_cb_t filter_cb, void *filter_ctx,
                map_cb_t map_cb, void *map_ctx, struct kndTask *task)
{
    int err;

    if (!self->dir) {
        if (DEBUG_SET_LEVEL_3)
            knd_log("NB: -- set has no root dir");
        return knd_OK;
    }

    err = traverse_dir(self, self->dir, range, filter_cb, filter_ctx, map_cb, map_ctx, task);
    if (err) return err;

    return knd_OK;
}

int knd_set_new(struct kndSet **result, knd_set_type type, struct kndMemPool *mempool)
{
    void *page;
    struct kndSet *s;
    int err;

    assert(mempool->small_page_size >= sizeof(struct kndSet));

    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndSet));

    s = page;
    s->type = type;
    s->mempool = mempool;

    err = knd_set_dir_new(&s->dir, "", 0, "", mempool);
    if (err) return err;

    *result = s;
    return knd_OK;
}

int knd_set_dir_new(struct kndSetDir **result, const char *parent_id, size_t parent_id_size,
                    const char *curr_id, struct kndMemPool *mempool)
{
    struct kndSetDir *dir;
    void *page;
    int err;
    assert(mempool->base_page_size >= sizeof(struct kndSetDir));
    err = knd_mempool_page(mempool, KND_MEMPAGE_BASE, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndSetDir));
    dir = page;

    if (parent_id_size) {
        memcpy(dir->id, parent_id, parent_id_size);
    }

    if (*curr_id) {
        dir->id[parent_id_size] = *curr_id;
        dir->id_size = parent_id_size + 1;
    }

    *result = dir;
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

int knd_set_dir_block_new(struct kndSetDirBlock **result, struct kndMemPool *mempool)
{
    struct kndSetDirBlock *b;
    void *page;
    int err;

    assert(mempool->base_page_size >= sizeof(struct kndSetDirBlock));
    err = knd_mempool_page(mempool, KND_MEMPAGE_BASE, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndSetDirBlock));
    b = page;

    *result = b;
    return knd_OK;
}

int knd_set_range_new(struct kndSetRange **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndSetRange));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndSetRange));
    *result = page;
    return knd_OK;
}

