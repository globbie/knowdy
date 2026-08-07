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
                        knd_set_elem_marshall_cb_t cb, void *cb_ctx, knd_set_cardinal_t catrdinal_type,
                        struct kndStorageLeaf *leaf, struct kndTask *task);

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

static int apply_cb(struct kndSetElem *elems, knd_set_elem_marshall_cb_t cb, void *cb_ctx,
                    knd_set_cardinal_t cardinal_t,
                    size_t *result, struct kndStorageLeaf *leaf, struct kndTask *task)
{
    struct kndSetElem *elem;
    int err;

    assert (elems->val != NULL);

    switch (cardinal_t) {
    case KND_SET_UNIQUE_VALUES:
        err = cb(elems->val, cb_ctx, leaf, result, task);
        if (err) return err;
        leaf->num_elems++;
        return knd_OK;
    case KND_SET_MULTIPLE_VALUES:
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
    //dir->num_blocks++;
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
    size_t idx_val_size = knd_min_bytes(block->max_elem_size);
    size_t byte_size;
    unsigned int i;
    int err;

    out->reset(out);
    if (!block->elems_rec_size) {
        OUTC('\0');
        goto final;
    }

    for (i = block->from_elem; i < block->to_elem; i++) {
        elem = dir->elems[i];

        if (block->use_elem_keys) {
            if (!elem) continue;
            OUTC(obj_id_seq[i]);
        }

        if (elem) {
            knd_pack_int(buf, elem->size, idx_val_size);
            OUT((const char*)buf, idx_val_size);

            if (DEBUG_SET_GSP_LEVEL_3) {
                knd_log(">>   {elem %d {rec-size %zu}} {cell-bytes %zu {block-max-elem-size %zu}}",
                        i, elem->size, idx_val_size, block->max_elem_size);
            }
        } else {
            knd_pack_int(buf, 0, idx_val_size);
            OUT((const char*)buf, idx_val_size);
        }
    }

    /* idx meta */
    OUTC((int)block->num_elems);
    OUTC((int)idx_val_size);
    OUTC((char)block->use_elem_keys);

    /* payload size */
    byte_size = knd_min_bytes(block->elems_rec_size);
    knd_pack_int(buf, block->elems_rec_size, byte_size);
    OUT((const char*)buf, byte_size);
    OUTC((char)byte_size);

    /* footer size */
    byte_size = knd_min_bytes(out->buf_size);
    knd_pack_int(buf, out->buf_size, byte_size);
    OUT((const char*)buf, byte_size);
    OUTC((char)byte_size);

final:

    switch (task->mode) {
    case KND_TASK_TRACE_MODE:
        break;
    default:
        err = knd_append_file(leaf->filename, out->buf, out->buf_size);
        KND_TASK_ERR("failed to append write to {file %.*s}", leaf->filename_size, leaf->filename);
        leaf->curr_size += out->buf_size;
        break;
    }

    block->elems_footer_size = out->buf_size;
    block->size += out->buf_size;

    if (DEBUG_SET_GSP_LEVEL_3) {
        knd_log("  == ELEMS {elems-payload-size %zu} {elems-footer-size %zu} {elems-block-size %zu}",
                block->elems_rec_size, block->elems_footer_size,
                block->elems_rec_size + block->elems_footer_size);
    }

    return knd_OK;
}

static int marshall_elems(struct kndSetDir *dir, struct kndSetDirBlock *block,
                          struct kndSetRange *unused_var(range),
                          knd_set_elem_marshall_cb_t cb, void *cb_ctx, knd_set_cardinal_t cardinal_t,
                          struct kndStorageLeaf *leaf, struct kndTask *task)
{
    struct kndSetElem *elem;
    size_t curr_size = 0;
    size_t num_empty_entries = 0;
    unsigned int i;
    int err;

    if (DEBUG_SET_GSP_LEVEL_2) {
        knd_log("marshall elems {from %zu} {to %zu}", block->from_elem, block->to_elem);
    }

    for (i = block->from_elem; i < block->to_elem; i++) {
        elem = dir->elems[i];
        if (!elem) {
            num_empty_entries++;
            continue;
        }
        curr_size = 0;

        err = apply_cb(elem, cb, cb_ctx, cardinal_t, &curr_size, leaf, task);
        KND_TASK_ERR("failed calling a cb on {elem %zu}", i);

        elem->size = curr_size;
        block->elems_rec_size += curr_size;
        block->num_elems++;

        /* calculate cell size */
        if (curr_size > block->max_elem_size) 
            block->max_elem_size = curr_size;

        /* leaf limit reached? */
        // TODO:  mark last elem
        if (leaf->curr_size > leaf->max_size) break; 
    }

    block->size += block->elems_rec_size;

    if (num_empty_entries > KND_RADIX_BASE / 2) block->use_elem_keys = true;

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
    size_t byte_size = 0;
    size_t tail_size = 0;
    int err;

    out->reset(out);
    /* no subdirs */
    if (!block->subdirs_rec_size) {
        OUTC('\0');
        tail_size = 1;
        goto final;
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

    OUTC((char)block->num_subdirs);
    OUTC((char)idx_val_size);
    OUTC((char)block->use_dir_keys);

    /* subdirs rec size */
    byte_size = knd_min_bytes(block->subdirs_rec_size);
    knd_pack_int(buf, block->subdirs_rec_size, byte_size);
    OUT((const char*)buf, byte_size);
    OUTC((char)byte_size);
    tail_size += byte_size + 1;

    /* subdirs footer size */
    byte_size = knd_min_bytes(out->buf_size);
    knd_pack_int(buf, out->buf_size, byte_size);
    OUT((const char*)buf, byte_size);
    OUTC((char)byte_size);
    tail_size += byte_size + 1;

 final:
    switch (task->mode) {
    case KND_TASK_TRACE_MODE:
        break;
    default:
        err = knd_append_file(leaf->filename, out->buf, out->buf_size);
        KND_TASK_ERR("failed to append write to {file %.*s}", leaf->filename_size, leaf->filename);
        leaf->curr_size += out->buf_size;
        break;
    }

    /* final footer size */
    block->subdirs_footer_size = out->buf_size;
    block->size += out->buf_size;

    if (DEBUG_SET_GSP_LEVEL_3) {
        const char *dir_id = dir->id_size ? dir->id : "/";
        size_t dir_id_size = dir->id_size ? dir->id_size : 1;

        knd_log("== {DIR %.*s {elems-block-size %zu} "
                "{subdirs-rec-size %zu} {subdirs-footer-size %zu} "
                "{block-size %zu}",
                dir_id_size, dir_id,
                block->elems_rec_size + block->elems_footer_size,
                block->subdirs_rec_size, block->subdirs_footer_size,
                block->size);
    }
    return knd_OK;
}

static int marshall_subdirs(struct kndSetDir *dir, struct kndSetDirBlock *block,
                            struct kndSetRange *range,
                            knd_set_elem_marshall_cb_t cb, void *cb_ctx, knd_set_cardinal_t cardinal_t,
                            struct kndStorageLeaf *leaf, struct kndTask *task)
{
    struct kndSetDir *subdir;
    unsigned int i;
    size_t max_subdir_size = 0;
    int err;

    if (block->num_subdirs < KND_RADIX_BASE / 2) {
        block->use_dir_keys = true;
    }

    for (i = block->from_dir; i < block->to_dir; i++) {
        subdir = dir->subdirs[i];
        if (!subdir) continue;

        err = marshall_dir(subdir, range, cb, cb_ctx, cardinal_t, leaf, task);
        KND_TASK_ERR("failed to marshall {subdir %.*s}", subdir->id_size, subdir->id);

        if (subdir->blocks->size > max_subdir_size) max_subdir_size = subdir->blocks->size;

        block->subdirs_rec_size += subdir->blocks->size;
        block->num_subdirs++;
    }

    block->size += block->subdirs_rec_size;
    block->max_subdir_size = max_subdir_size;

    err = build_subdirs_footer(dir, block, leaf, task);
    KND_TASK_ERR("failed to build set subdirs footer");

    return knd_OK;
}

static int marshall_dir(struct kndSetDir *dir, struct kndSetRange *range,
                        knd_set_elem_marshall_cb_t cb, void *cb_ctx, knd_set_cardinal_t cardinal_t,
                        struct kndStorageLeaf *leaf, struct kndTask *task)
{
    struct kndSetDirBlock *block;
    int err;

    err = create_dir_block(dir, range, &block, task);
    KND_TASK_ERR("failed to create a dir block");

    err = marshall_elems(dir, block, range, cb, cb_ctx, cardinal_t, leaf, task);
    KND_TASK_ERR("failed to marshall set elems");

    err = marshall_subdirs(dir, block, range, cb, cb_ctx, cardinal_t, leaf, task);
    KND_TASK_ERR("failed to marshall set subdirs");

    return knd_OK;
}

int knd_set_leaf_marshall(struct kndSet *s, struct kndSetRange *range,
                          struct kndStorageLeaf *leaf,
                          knd_set_elem_marshall_cb_t cb, void *cb_ctx,
                          struct kndTask *task)
{
    int err;

    if (DEBUG_SET_GSP_LEVEL_2) {
        knd_log("\n>> marshalling {leaf %.*s}", leaf->filename_size, leaf->filename);
    }

    /* GSP header */
    switch (task->mode) {
    case KND_TASK_TRACE_MODE:
        break;
    default:
        err = knd_append_file(leaf->filename, "GSP", strlen("GSP"));
        KND_TASK_ERR("failed to append write to {file %.*s}",
                     leaf->filename_size, leaf->filename);
        leaf->curr_size = strlen("GSP");
        break;
    }

    err = marshall_dir(s->dir, range, cb, cb_ctx, s->cardinal_t, leaf, task);
    KND_TASK_ERR("failed to marshall a set");

    if (!leaf->num_elems) {
        err = knd_FAIL;
        KND_TASK_ERR("no elems marshalled");
    }
    if (!s->dir->blocks->size) {
        err = knd_FAIL;
        KND_TASK_ERR("no payload written");
    }

    // TODO check file size against dir block size

    return knd_OK;
}

int knd_set_marshall(struct kndSet *s, struct kndSetRange *range,
                     const char *path, size_t path_size,
                     knd_set_elem_marshall_cb_t cb, void *cb_ctx,
                     struct kndStorage *store,
                     struct kndStorageLeaf **leaves, size_t *num_leaves, struct kndTask *task)
{
    assert (s->num_elems > 0);

    struct kndStorageLeaf *leaf;
    size_t leaf_count = 0;
    size_t min_leaf_size = store->leaf_min_size;
    size_t max_leaf_size = store->leaf_max_size;
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
        if (leaf_count >= KND_MAX_STORAGE_LEAVES) {
            err = knd_LIMIT;
            KND_TASK_ERR("max limit reached {max-storage-leaves %zu}", leaf_count);
        }

        err = knd_storage_leaf_new(&leaf, leaf_count, path, path_size,
                                   min_leaf_size, max_leaf_size,
                                   KND_STORAGE_MODE_READ_WRITE);
        KND_TASK_ERR("failed to alloc a storage leaf");

        err = knd_set_leaf_marshall(s, range, leaf, cb, cb_ctx, task);
        KND_TASK_ERR("failed to marshall a set storage leaf");

        assert (leaf->curr_size > 0);

        if (DEBUG_SET_GSP_LEVEL_3) {
            knd_log("{leaf %zu {size %zu}} {total-items %zu}",
                    leaf_count, leaf->curr_size, s->num_elems);
        }

        leaves[leaf_count] = leaf;
        leaf_count++;
    } while (has_more_elems(s, range, leaf));

    if (DEBUG_SET_GSP_LEVEL_3) {
        knd_log("++ set GSP complete {path %.*s} {num-leaves %zu}", path_size, path, s->store->num_leaves);
    }

    *num_leaves = leaf_count;
    return knd_OK;
}
