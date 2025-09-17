#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "knd_class.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_memblock.h"
#include "knd_mempool.h"
#include "knd_repo.h"
#include "knd_task.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#define DEBUG_SET_GSP_LEVEL_0 0
#define DEBUG_SET_GSP_LEVEL_1 0
#define DEBUG_SET_GSP_LEVEL_2 0
#define DEBUG_SET_GSP_LEVEL_3 0
#define DEBUG_SET_GSP_LEVEL_4 0
#define DEBUG_SET_GSP_LEVEL_TMP 1

static int apply_cb(void *obj, knd_set_elem_marshall_cb_t cb, void *unused_var(ctx),
                    knd_set_type set_type,
                    size_t *result, struct kndStorageLeaf *leaf, struct kndTask *task)
{
    struct kndSetElem *elems, *elem;
    int err;

    switch (set_type) {
    case KND_SET_UNIQUE_VALUES:
        err = cb(obj, NULL, leaf, result, task);
        if (err) return err;
        return knd_OK;
    case KND_SET_MULTIPLE_VALUES:
        elems = obj;
        FOREACH (elem, elems) {
            err = cb(elem->val, NULL, leaf, result, task);
            if (err) return err;
        }
        return knd_OK;
    default:
        break;
    }
    return knd_FAIL;
}

static int traverse(struct kndSetDir *parent_dir,
                    knd_set_elem_marshall_cb_t cb, void *cb_ctx, knd_set_type set_type,
                    struct kndStorageLeaf *leaf, struct kndTask *task)
{
    struct kndSetDirBlock *block;
    struct kndSetDir *subdir;
    size_t num_empty_entries = 0;
    bool use_positional_indexing = true;
    void *elem;
    size_t numval = 0;
    int err;

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        subdir = parent_dir->subdirs[i];
        if (!subdir) continue;

        //entry = &dir->entries[i];
        
        err = traverse(subdir, cb, cb_ctx, set_type, leaf, task);
        if (err) return err;

        /*entry->subdir = subdir;          
          dir->total_size += entry->subdir->total_size;
          dir->total_elems += subdir->total_elems;
          dir->num_subdirs++;
        */
    }

    /* sync elem bodies */
    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        elem = parent_dir->elems[i];
        if (!elem) {
            if (!parent_dir->subdirs[i])
                num_empty_entries++;
            continue;
        }
        
        err = apply_cb(elem, cb, NULL, set_type, &numval, leaf, task);
        if (err) return err;
        
        /* entry = &dir->entries[i];
           entry->payload_size = task->out->buf_size;
           dir->total_size += entry->payload_size;
           dir->num_elems++;
        */
        // TODO: sync entry payload to file
    }

    /* build footer */
    if (num_empty_entries > KND_RADIX_BASE / 2) {
        use_positional_indexing = false;
    }

    //err = build_dir_footer(dir, use_positional_indexing, task);
    //KND_TASK_ERR("failed to build set dir footer");

    //*result_dir = dir;
    return knd_OK;
}

int knd_set_leaf_marshall(struct kndSet *s, struct kndStorageLeaf *leaf,
                          struct kndSetRange *range,
                          knd_set_elem_marshall_cb_t cb, void *cb_ctx,
                          size_t *total_size, struct kndTask *task)
{
    struct kndSetDir *dir = s->dir;
    int err;

    assert (dir != NULL);

    if (DEBUG_SET_GSP_LEVEL_TMP) {
        knd_log(">> marshalling {leaf %.*s {from %.*s} {to %.*s}}",
                leaf->filepath_size, leaf->filepath,
                leaf->range_from_addr_size, leaf->range_from_addr,
                leaf->range_to_addr_size, leaf->range_to_addr);
    }

    err = traverse(s->dir, cb, cb_ctx, s->type, leaf, task);
    KND_TASK_ERR("failed to marshall a set");

    // TODO
    *total_size = 0;
    return knd_OK;
}

int knd_set_marshall(struct kndSet *s, struct kndSetRange *range,
                     knd_set_elem_marshall_cb_t cb, void *cb_ctx,
                     const char *path, size_t path_size,
                     size_t *total_size, struct kndTask *task)
{
    struct kndStorageLeaf *leaf;
    int err;

    if (DEBUG_SET_GSP_LEVEL_TMP) {
        knd_log(">> set marshalling in progress..");
    }

    return knd_OK;
}
