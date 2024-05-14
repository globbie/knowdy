#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "knd_class.h"
#include "knd_utils.h"
#include "knd_mempool.h"
#include "knd_shared_set.h"
#include "knd_repo.h"
#include "knd_task.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#define DEBUG_SHARED_SET_GSP_LEVEL_0 0
#define DEBUG_SHARED_SET_GSP_LEVEL_1 0
#define DEBUG_SHARED_SET_GSP_LEVEL_2 0
#define DEBUG_SHARED_SET_GSP_LEVEL_3 0
#define DEBUG_SHARED_SET_GSP_LEVEL_4 0
#define DEBUG_SHARED_SET_GSP_LEVEL_TMP 1

static int traverse_marshall(struct kndSharedSetElemIdx *parent_idx,
                             struct kndStorageLeaf *leaf,
                             char *idbuf, size_t idbuf_size,
                             elem_marshall_cb cb, struct kndSharedSetDir **result_dir,
                             struct kndTask *task);

static int build_elems_footer(struct kndSharedSetDir *dir, bool use_keys,
                              size_t cell_size, struct kndTask *task)
{
    unsigned char buf[KND_NAME_SIZE];
    struct kndOutput *out = task->out;
    size_t block_size;

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        block_size = dir->elem_block_sizes[i];
        if (use_keys) {
            if (!block_size) continue;
            OUTC(obj_id_seq[i]);
        }
        knd_pack_int(buf, block_size, cell_size);
        OUT((const char*)buf, cell_size);
    }
    if (use_keys) {
        OUTC((int)dir->num_term_elems);
    }
    OUTC((int)cell_size);
    OUTC((char)use_keys);
    return knd_OK;
}

static int build_subdirs_footer(struct kndSharedSetDir *dir, char *idbuf, size_t idbuf_size,
                                bool use_keys, size_t cell_size, struct kndTask *task)
{
    unsigned char buf[KND_NAME_SIZE];
    struct kndSharedSetDir *subdir;
    struct kndOutput *out = task->out;
    size_t subdir_block_size;

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        subdir = dir->subdirs[i];

        if (!subdir)
            subdir_block_size = 0;
        else
            subdir_block_size = subdir->total_size;

        if (use_keys) {
            if (!subdir_block_size) continue;
            OUTC(obj_id_seq[i]);
        }

        if (DEBUG_SHARED_SET_GSP_LEVEL_3) {
            idbuf[idbuf_size] = obj_id_seq[i];
            knd_log(">> \"%.*s\" subdir size: %zu", idbuf_size + 1, idbuf, subdir_block_size);
        }

        knd_pack_int(buf, subdir_block_size, cell_size);
        OUT((const char*)buf, cell_size);
    }
    if (use_keys) {
        OUTC((char)dir->num_subdirs);
    }
    OUTC((char)cell_size);
    OUTC((char)use_keys);
    return knd_OK;
}

static int marshall_elems(struct kndSharedSetElemIdx *parent_idx, struct kndSharedSetDir *dir,
                          const char *idbuf, size_t idbuf_size, elem_marshall_cb cb,
                          bool use_keys, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    void *elem;
    size_t block_size;
    int err;

    if (DEBUG_SHARED_SET_GSP_LEVEL_2) {
        knd_log(".. marshall elems of dir \"%.*s\"", idbuf_size, idbuf);
    }

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        if (parent_idx->idxs[i]) dir->num_subdirs++;
        elem = parent_idx->elems[i];
        if (!elem) continue;

        if (use_keys) {
            if (dir->num_term_elems) {
                /* rec separator */
                OUTC((char)'\0');
            }
            OUTC(obj_id_seq[i]);
        }
        if (DEBUG_SHARED_SET_GSP_LEVEL_2)
            knd_log(">> elem %.*s%c", idbuf_size, idbuf, obj_id_seq[i]);

        err = cb(elem, &block_size, task);
        if (err) return err;

        if (block_size > dir->cell_max_val)
            dir->cell_max_val = block_size;

        dir->elem_block_sizes[i] = block_size;
        dir->num_term_elems++;
    }

    if (DEBUG_SHARED_SET_GSP_LEVEL_3) {
        if (!idbuf_size) {
            knd_log("{root-dir {num-subdirs %zu} {num-term-elems %zu {total-elems %zu}}",
                    dir->num_subdirs, dir->num_term_elems, dir->total_elems);
        } else {
            knd_log("{dir %.*s} {num-subdirs %zu} {num-term-elems %zu {total-elems %zu}}",
                    idbuf_size, idbuf, dir->num_subdirs,
                    dir->num_term_elems, dir->total_elems);
        }
    }
    return knd_OK;
}

static int marshall_subdirs(struct kndSharedSetElemIdx *parent_idx,
                            struct kndStorageLeaf *leaf, struct kndSharedSetDir *dir,
                            char *idbuf, size_t idbuf_size,
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

        err = traverse_marshall(idx, leaf, idbuf, idbuf_size + 1, cb, &subdir, task);
        KND_TASK_ERR("failed to traverse a subdir");

        dir->subdirs[i] = subdir;

        if (subdir->total_size > dir->cell_max_val)
            dir->cell_max_val = subdir->total_size;

        dir->total_elems += subdir->total_elems;
        dir->subdir_block_size += subdir->total_size;
        dir->num_subdirs++;

        /* leaf max size reached */
        if (leaf->range_to_id_size) break;
    }

    // build subdirs footer
    cell_size = knd_min_bytes(dir->cell_max_val);
    if (KND_RADIX_BASE - dir->num_subdirs > KND_RADIX_BASE / 2)
        use_keys = true;

    out->reset(out);
    err = build_subdirs_footer(dir, idbuf, idbuf_size, use_keys, cell_size, task);
    KND_TASK_ERR("failed to build set dir footer");

    switch (task->mode) {
    case KND_TASK_TRACE_MODE:
        knd_log(".. write subdirs footer to file %.*s", leaf->filepath_size, leaf->filepath);
        break;
    default:
        err = knd_append_file((const char*)leaf->filepath, out->buf, out->buf_size);
        KND_TASK_ERR("set idx write failure");
        break;
    }

    dir->subdir_block_size += out->buf_size;
    dir->total_size += dir->subdir_block_size;

    leaf->file_size += out->buf_size;

    if (DEBUG_SHARED_SET_GSP_LEVEL_2) {
        knd_log(">> \"%.*s\" total subdir block size:%zu",
                idbuf_size, idbuf, dir->subdir_block_size);
    }
    return knd_OK;
}

static int write_payload_block_size(struct kndSharedSetDir *dir,
                                    const char *filename, size_t *output_size,
                                    struct kndTask *task)
{
    struct kndOutput *out = task->out;
    unsigned char buf[KND_NAME_SIZE];
    size_t byte_size;
    int err;

    out->reset(out);
    if (dir->payload_block_size) {
        byte_size = knd_min_bytes(dir->payload_block_size);
        knd_pack_int(buf, dir->payload_block_size, byte_size);
        OUT((const char*)buf, byte_size);
        OUTC((char)byte_size);
    } else {
        OUTC('\0');
    }

    switch (task->mode) {
    case KND_TASK_TRACE_MODE:
        knd_log(".. appending payload block size %zu to file %s",
                dir->payload_block_size, filename);
        break;
    default:
        err = knd_append_file((const char*)filename, out->buf, out->buf_size);
        KND_TASK_ERR("failed to append write to file %s", filename);
        break;
    }
    *output_size = out->buf_size;
    return knd_OK;
}

static int set_leaf_range_end(struct kndStorageLeaf *leaf, const char *dir_id, size_t dir_id_size)
{
    /* root dir? */
    if (!dir_id_size) {
        leaf->range_to_id[0] = '0';
        leaf->range_to_id_size = 1;
        return knd_OK;
    }
    memcpy(leaf->range_to_id, dir_id, dir_id_size);
    leaf->range_to_id_size = dir_id_size;
    return knd_OK;
}

static int traverse_marshall(struct kndSharedSetElemIdx *parent_idx,
                             struct kndStorageLeaf *leaf,
                             char *idbuf, size_t idbuf_size,
                             elem_marshall_cb cb, struct kndSharedSetDir **result_dir,
                             struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndSharedSetDir *dir;
    size_t cell_size = 1;
    bool use_keys = false;
    size_t footer_size;
    int err;

    if (DEBUG_SHARED_SET_GSP_LEVEL_TMP) {
        if (!idbuf_size) {
            knd_log(">> {root-dir}");
        } else {
            knd_log(">> {dir %.*s}", idbuf_size, idbuf);
        }
    }

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

            if (DEBUG_SHARED_SET_GSP_LEVEL_3)
                knd_log("NB: another run to optimize elem packing (use explicit field keys)");

            out->reset(out);
            dir->num_term_elems = 0;
            err = marshall_elems(parent_idx, dir, idbuf, idbuf_size, cb, true, task);
            KND_TASK_ERR("failed to marshall elems");
            OUTC((char)0);
            OUTC((char)1);
        }
        else {
            if (KND_RADIX_BASE - dir->num_term_elems > KND_RADIX_BASE / 2)
                use_keys = true;
            
            err = build_elems_footer(dir, use_keys, cell_size, task);
            KND_TASK_ERR("failed to build set dir footer");
        }

        switch (task->mode) {
        case KND_TASK_TRACE_MODE:
            knd_log(".. write elems payload {size %zu} to file %.*s",
                    out->buf_size, leaf->filepath_size, leaf->filepath);
            break;
        default:
            err = knd_append_file((const char*)leaf->filepath, out->buf, out->buf_size);
            KND_TASK_ERR("set idx write failure");
            break;
        }

        dir->payload_block_size = out->buf_size;
        dir->total_elems = dir->num_term_elems;
        dir->total_size  = dir->payload_block_size;

        leaf->file_size += dir->payload_block_size;

        leaf->num_elems += dir->num_term_elems;

        if (DEBUG_SHARED_SET_GSP_LEVEL_TMP) {
            knd_log(">>  {dir %.*s {payload-size %zu} {num-term-elems %zu}} {leaf-total-elems %zu}",
                    idbuf_size, idbuf, dir->payload_block_size, dir->num_term_elems,
                    leaf->num_elems);
        }
    }

    /* check leaf size limit overflow */
    if (leaf->file_size > leaf->snapshot->max_leaf_size) {
        err = set_leaf_range_end(leaf, idbuf, idbuf_size);
        KND_TASK_ERR("failed to set leaf range end");
        
        if (DEBUG_SHARED_SET_GSP_LEVEL_TMP) {
            knd_log("!! max snapshot leaf size reached {size %zu} at {dir %.*s}",
                    leaf->file_size, leaf->range_to_id_size, leaf->range_to_id);
        }
        err = write_payload_block_size(dir, leaf->filepath, &footer_size, task);
        KND_TASK_ERR("failed to write payload block size");
        dir->total_size += footer_size;
        leaf->file_size += footer_size;

        *result_dir = dir;
        return knd_OK;
    }

    if (dir->num_subdirs) {
        out->reset(out);
        dir->num_subdirs = 0;
        err = marshall_subdirs(parent_idx, leaf, dir, idbuf, idbuf_size, cb, task);
        KND_TASK_ERR("failed to marshall subdirs");
    }

    err = write_payload_block_size(dir, leaf->filepath, &footer_size, task);
    KND_TASK_ERR("failed to write payload block size");
    dir->total_size += footer_size;
    leaf->file_size += footer_size;

    *result_dir = dir;
    return knd_OK;
}

static int get_dir_idx(struct kndSharedSetElemIdx *idx, const char *id, size_t id_size,
                       struct kndSharedSetElemIdx **result, struct kndTask *task)
{
    int idx_pos;
    int err;

    idx_pos = obj_id_base[(const int)*id];
    if (idx_pos == -1) {
        err = knd_FORMAT;
        KND_TASK_ERR("invalid dir id");
    }
    if (id_size > 1) {
        return get_dir_idx(idx->idxs[idx_pos], id + 1, id_size - 1, result, task);
    }

    *result = idx->idxs[idx_pos];
    return knd_OK;
}

int knd_shared_set_marshall(struct kndSharedSet *self, struct kndStorageLeaf *leaf,
                            elem_marshall_cb cb, struct kndTask *task)
{
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size = 0;
    struct kndSharedSetElemIdx *idx = self->idx;
    struct kndSharedSetDir *dir;
    int err;

    if (DEBUG_SHARED_SET_GSP_LEVEL_TMP) {
        const char *from_id = leaf->range_from_id;
        size_t from_id_size = leaf->range_from_id_size;
        if (!leaf->range_from_id_size) {
            from_id = "/";
            from_id_size = 1;
        }
        knd_log(".. {set {num-elems %zu}} building {leaf %zu {from %.*s}}}",
                self->num_elems, leaf->numid, from_id_size, from_id,
                leaf->range_to_id_size, leaf->range_to_id);
    }

    /* starting from a range offset */
    if (leaf->range_from_id_size) {
        err = get_dir_idx(self->idx, leaf->range_from_id, leaf->range_from_id_size, &idx, task);
        KND_TASK_ERR("failed to find a dir idx %.*s",
                     leaf->range_from_id_size, leaf->range_from_id);

        dir = calloc(1, sizeof(struct kndSharedSetDir));
        if (!dir) {
            err = knd_NOMEM;
            KND_TASK_ERR("failed to alloc kndSharedSetDir");
        }
        err = marshall_subdirs(idx, leaf, dir, idbuf, idbuf_size, cb, task);
        KND_TASK_ERR("failed to marshall subdirs");

        // leaf->num_elems = dir->total_elems;
        return knd_OK;
    }

    /* starting from the root idx */
    err = traverse_marshall(idx, leaf, idbuf, idbuf_size, cb, &dir, task);
    KND_TASK_ERR("failed to marshall set idx");

    // leaf->num_elems = dir->total_elems;
    return knd_OK;
}
