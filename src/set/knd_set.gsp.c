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

static int marshall_dir(struct kndSetDir *dir, struct kndSetRange *range,
                        knd_set_elem_marshall_cb_t cb, void *cb_ctx, knd_set_type set_type,
                        struct kndStorageLeaf *leaf, struct kndTask *task);

static void append_leaf(struct kndSet *s, struct kndStorageLeaf *leaf)
{
    if (!s->leaves) {
        s->leaves = leaf;
        s->leaf_tail = leaf;
        s->num_leaves++;        
        return;
    }

    s->leaf_tail->next = leaf;
    s->leaf_tail = leaf;
    s->num_leaves++;        
}

static bool has_more_elems(struct kndSet *unused_var(s), struct kndSetRange *range,
                           struct kndStorageLeaf *leaf)
{
    size_t curr_to_id = 0;
    size_t range_to_id = KND_RADIX_BASE;

    if (!leaf->range_to_addr_size) return false;

    knd_calc_num_id(leaf->range_to_addr, leaf->range_to_addr_size, &curr_to_id);

    if (range) {
        knd_calc_num_id(range->to_id, range->to_id_size, &range_to_id);
    }

    return (range_to_id > curr_to_id);
}

static int apply_cb(void *obj, knd_set_elem_marshall_cb_t cb, void *cb_ctx, knd_set_type set_type,
                    size_t *result, struct kndStorageLeaf *leaf, struct kndTask *task)
{
    struct kndSetElem *elems, *elem;
    int err;

    switch (set_type) {
    case KND_SET_UNIQUE_VALUES:
        err = cb(obj, cb_ctx, leaf, result, task);
        if (err) return err;
        leaf->num_elems++;
        return knd_OK;
    case KND_SET_MULTIPLE_VALUES:
        elems = obj;
        FOREACH (elem, elems) {
            err = cb(elem->val, cb_ctx, leaf, result, task);
            if (err) return err;
            leaf->num_elems++;
        }
        return knd_OK;
    default:
        break;
    }
    return knd_FAIL;
}

static int create_dir_block(struct kndSetDir *dir, struct kndSetRange *unused_var(range),
                            struct kndSetDirBlock **result, struct kndTask *task)
{
    struct kndSetDirBlock *block;
    int err;

    err = knd_set_dir_block_new(&block, task->mempool);
    KND_TASK_ERR("failed to alloc a set dir block");

    block->next = dir->blocks;
    dir->num_blocks++;
    dir->blocks = block;

    // TODO set range
    
    block->to_dir = KND_RADIX_BASE;
    block->to_elem = KND_RADIX_BASE;

    //if (range) {
    //}
    
    *result = block;
    return knd_OK;
}

static int build_elems_footer(struct kndSetDir *dir, struct kndSetDirBlock *block,
                              struct kndStorageLeaf *leaf, struct kndTask *task)
{
    unsigned char buf[KND_NAME_SIZE];
    struct kndOutput *out = task->out;
    struct kndSetElem *elem;
    size_t idx_val_size = 0;
    size_t byte_size;
    unsigned int i;
    int err;

    out->reset(out);

    idx_val_size = knd_min_bytes(block->max_elem_size);

    for (i = block->from_elem; i < block->to_elem; i++) {
        elem = dir->elems[i];

        if (block->use_elem_keys) {
            if (!elem->size) continue;
            OUTC(obj_id_seq[i]);
        }
        knd_pack_int(buf, elem->size, idx_val_size);
        OUT((const char*)buf, idx_val_size);
    }

    /* idx of variable length = num_elems * idx_val_size  */
    if (block->use_elem_keys) {
        OUTC((int)block->num_elems);
    }
    OUTC((int)idx_val_size);
    OUTC((char)block->use_elem_keys);

    if (block->elems_rec_size) {
        byte_size = knd_min_bytes(block->elems_rec_size);
        knd_pack_int(buf, block->elems_rec_size, byte_size);
        OUT((const char*)buf, byte_size);
        OUTC((char)byte_size);
    } else {
        OUTC('\0');
    }

    switch (task->mode) {
    case KND_TASK_TRACE_MODE:
        break;
    default:
        err = knd_append_file(leaf->filepath, out->buf, out->buf_size);
        KND_TASK_ERR("failed to append write to {file %.*s}", leaf->filepath_size, leaf->filepath);
        leaf->curr_size += out->buf_size;
        break;
    }

    block->elems_footer_size = out->buf_size;
    block->size += out->buf_size;
    return knd_OK;
}

static int marshall_elems(struct kndSetDir *dir, struct kndSetDirBlock *block,
                          struct kndSetRange *unused_var(range),
                          knd_set_elem_marshall_cb_t cb, void *cb_ctx, knd_set_type set_type,
                          struct kndStorageLeaf *leaf, struct kndTask *task)
{
    struct kndSetElem *elem;
    size_t curr_size = 0;
    size_t num_empty_entries = 0;
    unsigned int i;
    int err;

    for (i = block->from_elem; i < block->to_elem; i++) {
        elem = dir->elems[i];
        if (!elem) {
            num_empty_entries++;
            continue;
        }
        curr_size = 0;

        err = apply_cb(elem, cb, cb_ctx, set_type, &curr_size, leaf, task);
        KND_TASK_ERR("failed calling a cb on {elem %zu}", i);

        elem->size = curr_size;
        block->elems_rec_size += curr_size;
        block->num_elems++;

        /* leaf limit reached? */
        // TODO:  mark last elem
        if (leaf->curr_size > leaf->max_size) break; 
    }

    block->size += block->elems_rec_size;

    err = build_elems_footer(dir, block, leaf, task);
    KND_TASK_ERR("failed to build GSP elems footer");

    return knd_OK;
}

static int build_subdirs_footer(struct kndSetDir *dir, struct kndSetDirBlock *block,
                                struct kndStorageLeaf *leaf, struct kndTask *task)
{
    unsigned char buf[KND_NAME_SIZE];
    struct kndSetDir *subdir;
    struct kndOutput *out = task->out;
    size_t subdir_block_size;
    size_t idx_val_size;
    int err;

    out->reset(out);

    /* no subdirs */
    if (!block->subdirs_rec_size) {
        OUTC('\0');
        switch (task->mode) {
        case KND_TASK_TRACE_MODE:
            break;
        default:
            err = knd_append_file(leaf->filepath, out->buf, out->buf_size);
            KND_TASK_ERR("failed to append write to {file %.*s}", leaf->filepath_size, leaf->filepath);
            leaf->curr_size += out->buf_size;
            break;
        }

        block->subdirs_footer_size = out->buf_size;
        block->size += out->buf_size;
        return knd_OK;
    }

    idx_val_size = knd_min_bytes(block->max_subdir_size);

    for (size_t i = block->from_dir; i < block->to_dir; i++) {
        subdir = dir->subdirs[i];

        if (!subdir)
            subdir_block_size = 0;
        else
            subdir_block_size = subdir->blocks->size;

        if (block->use_dir_keys) {
            if (!subdir_block_size) continue;
            OUTC(obj_id_seq[i]);
        }

        knd_pack_int(buf, subdir_block_size, idx_val_size);
        OUT((const char*)buf, idx_val_size);
    }

    /* subdirs index has variable length */
    if (block->use_dir_keys) {
        OUTC((char)block->num_subdirs);
    }
    OUTC((char)idx_val_size);
    OUTC((char)block->use_dir_keys);

    switch (task->mode) {
    case KND_TASK_TRACE_MODE:
        break;
    default:
        err = knd_append_file(leaf->filepath, out->buf, out->buf_size);
        KND_TASK_ERR("failed to append write to {file %.*s}", leaf->filepath_size, leaf->filepath);
        leaf->curr_size += out->buf_size;
        break;
    }

    block->subdirs_footer_size = out->buf_size;
    block->size += out->buf_size;

    return knd_OK;
}

static int marshall_subdirs(struct kndSetDir *dir, struct kndSetDirBlock *block,
                            struct kndSetRange *range,
                            knd_set_elem_marshall_cb_t cb, void *cb_ctx, knd_set_type set_type,
                            struct kndStorageLeaf *leaf, struct kndTask *task)
{
    struct kndSetDir *subdir;
    unsigned int i;
    int err;

    for (i = block->from_dir; i < block->to_dir; i++) {
        subdir = dir->subdirs[i];
        if (!subdir) continue;

        err = marshall_dir(subdir, range, cb, cb_ctx, set_type, leaf, task);
        KND_TASK_ERR("failed to marshall {subdir %.*s}", subdir->id_size, subdir->id);

        block->subdirs_rec_size += subdir->blocks->size;
    }

    block->size += block->subdirs_rec_size;

    err = build_subdirs_footer(dir, block, leaf, task);
    KND_TASK_ERR("failed to build set subdirs footer");

    return knd_OK;
}

static int marshall_dir(struct kndSetDir *dir, struct kndSetRange *range,
                        knd_set_elem_marshall_cb_t cb, void *cb_ctx, knd_set_type set_type,
                        struct kndStorageLeaf *leaf, struct kndTask *task)
{
    struct kndSetDirBlock *block;
    int err;

    err = create_dir_block(dir, range, &block, task);
    KND_TASK_ERR("failed to create a dir block");

    err = marshall_elems(dir, block, range, cb, cb_ctx, set_type, leaf, task);
    KND_TASK_ERR("failed to marshall set elems");

    err = marshall_subdirs(dir, block, range, cb, cb_ctx, set_type, leaf, task);
    KND_TASK_ERR("failed to marshall set subdirs");

    return knd_OK;
}

int knd_set_leaf_marshall(struct kndSet *s, struct kndSetRange *range,
                          struct kndStorageLeaf *leaf,
                          knd_set_elem_marshall_cb_t cb, void *cb_ctx,
                          struct kndTask *task)
{
    int err;

    if (DEBUG_SET_GSP_LEVEL_TMP) {
        knd_log(">> marshalling {leaf %.*s}", leaf->filepath_size, leaf->filepath);
    }

    err = marshall_dir(s->dir, range, cb, cb_ctx, s->type, leaf, task);
    KND_TASK_ERR("failed to marshall a set");

    if (!leaf->num_elems) {
        err = knd_FAIL;
        KND_TASK_ERR("no elems marshalled");
    }
    if (!s->dir->blocks->size) {
        err = knd_FAIL;
        KND_TASK_ERR("no payload written");
    }
    return knd_OK;
}

int knd_set_marshall(struct kndSet *s, struct kndSetRange *range,
                     const char *path, size_t path_size,
                     knd_set_elem_marshall_cb_t cb, void *cb_ctx, struct kndTask *task)
{
    assert (s->num_elems > 0);

    struct kndStorageLeaf *leaf;
    size_t leaf_count = 0;
    size_t min_leaf_size = task->storage_conf->leaf_min_size;
    size_t max_leaf_size = task->storage_conf->leaf_max_size;
    int err;

    if (DEBUG_SET_GSP_LEVEL_2) {
        knd_log(">> set marshalling in progress..");
    }

    if (path_size >= KND_PATH_SIZE) return knd_LIMIT;
    s->path = path;
    s->path_size = path_size;

    err = knd_mkpath((const char*)path, path_size, 0755, false);
    KND_TASK_ERR("failed to make {path %.*s}", path_size, path);

    /* split a set into a batch of leaves of max size */
    do {
        err = knd_storage_leaf_new(&leaf, ++leaf_count, path, path_size, min_leaf_size, max_leaf_size);
        KND_TASK_ERR("failed to alloc a storage leaf");

        err = knd_set_leaf_marshall(s, range, leaf, cb, cb_ctx, task);
        KND_TASK_ERR("failed to marshall a set storage leaf");

        assert (leaf->curr_size > 0);

        if (DEBUG_SET_GSP_LEVEL_3) {
            knd_log("{leaf %zu {size %zu}} {total-items %zu}",
                    leaf_count, leaf->curr_size, s->num_elems);
        }

        /* empty leaf?
        if (!leaf->curr_size) {
            knd_log("-- empty leaf?");
            knd_storage_leaf_del(leaf);
            break;
            }*/

        append_leaf(s, leaf);

        if (leaf_count >= KND_MAX_STORAGE_LEAVES) {
            err = knd_LIMIT;
            KND_TASK_ERR("max limit reached {max-storage-leaves %zu}", leaf_count);
        }

    } while (has_more_elems(s, range, leaf));

    if (DEBUG_SET_GSP_LEVEL_3) {
        knd_log("++ set GSP complete {path %.*s} {num-leaves %zu}", path_size, path, s->num_leaves);
    }
    return knd_OK;
}
