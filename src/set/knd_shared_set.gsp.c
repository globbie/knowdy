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

static int traverse_marshall(struct kndSharedSet *self, struct kndStorageLeaf *leaf,
                             struct kndSharedSetElemIdx *parent_idx,
                             char *idbuf, size_t idbuf_size,
                             const char *range_from_addr, size_t range_from_addr_size,
                             knd_set_elem_marshall_cb_t cb, void *cb_ctx,
                             struct kndSharedSetDir **result_dir,
                             struct kndTask *task);

static void append_leaf(struct kndStorageLeaf **leaves, struct kndStorageLeaf *leaf)
{
    struct kndStorageLeaf *tail_leaf;

    if (!(*leaves)) {
        *leaves = leaf;
        return;
    }

    tail_leaf = (*leaves)->tail;

    if (!tail_leaf) {
        (*leaves)->next = leaf;
        (*leaves)->tail = leaf;        
    } else {
        tail_leaf->next = leaf;
        (*leaves)->tail = leaf;
    }
}

static int build_elems_footer(struct kndSharedSetDir *dir, bool use_keys,
                              size_t cell_size, struct kndTask *task)
{
    unsigned char buf[KND_NAME_SIZE];
    struct kndOutput *out = task->out;
    size_t block_size;

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        block_size = dir->idx->elem_block_sizes[i];
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
        subdir = dir->idx->subdirs[i];

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
            knd_log(">> {subdir %.*s {size %zu}}", idbuf_size + 1, idbuf, subdir_block_size);
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
                          const char *idbuf, size_t idbuf_size,
                          knd_set_elem_marshall_cb_t cb, void *cb_ctx,
                          bool use_keys, struct kndStorageLeaf *leaf, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    void *elem;
    size_t block_size;
    int err;

    if (DEBUG_SHARED_SET_GSP_LEVEL_TMP) {
        if (!idbuf_size) {
            knd_log(".. marshall elems of {dir /}");
        } else {
            knd_log(".. marshall elems of {dir %.*s}", idbuf_size, idbuf);
        }
    }

    out->reset(out);

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

        if (DEBUG_SHARED_SET_GSP_LEVEL_3) {
            knd_log(">> elem %.*s%c", idbuf_size, idbuf, obj_id_seq[i]);
        }

        err = cb(elem, cb_ctx, leaf, &block_size, task);
        KND_TASK_ERR("failed marshalling {elem %.*s%c} {err %d}",
                     idbuf_size, idbuf, obj_id_seq[i], err);

        if (block_size > dir->cell_max_val)
            dir->cell_max_val = block_size;

        dir->idx->elem_block_sizes[i] = block_size;
        dir->num_term_elems++;
    }

    if (DEBUG_SHARED_SET_GSP_LEVEL_3) {
        if (!idbuf_size) {
            knd_log("{root-dir {num-subdirs %zu} {num-term-elems %zu {total-elems %zu}}",
                    dir->num_subdirs, dir->num_term_elems, dir->total_elems);
        } else {
            knd_log("{dir %.*s} {num-subdirs %zu} {num-term-elems %zu {total-elems %zu}}",
                    idbuf_size, idbuf, dir->num_subdirs, dir->num_term_elems, dir->total_elems);
        }
    }
    return knd_OK;
}

static int marshall_subdirs(struct kndSharedSet *self, struct kndStorageLeaf *leaf,
                            struct kndSharedSetElemIdx *parent_idx,
                            struct kndSharedSetDir *dir,
                            char *idbuf, size_t idbuf_size,
                            const char *range_from_addr, size_t range_from_addr_size,
                            knd_set_elem_marshall_cb_t cb, void *cb_ctx, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndSharedSetElemIdx *idx;
    struct kndSharedSetDir *subdir;
    const char *next_dir_id = range_from_addr;
    size_t next_dir_id_size = range_from_addr_size;
    int idx_pos;
    size_t cell_size = 1;
    size_t offset = 0;
    bool use_keys = false;
    int err;

    out->reset(out);
    dir->num_subdirs = 0;
    dir->cell_max_val = 0;

    if (DEBUG_SHARED_SET_GSP_LEVEL_2) {
        knd_log(".. marshall subdirs..  {range-from %.*s}",
                range_from_addr_size, range_from_addr);
    }
    
    if (range_from_addr_size) {
        if (*range_from_addr == '/') {
            next_dir_id = range_from_addr + 1;
            next_dir_id_size = range_from_addr_size - 1;
        } else {
            idx_pos = obj_id_base[(const int)*range_from_addr];
            if (idx_pos == -1) {
                err = knd_FORMAT;
                KND_TASK_ERR("invalid dir id");
            }
            offset = idx_pos;
        }
    }
    
    for (size_t i = offset; i < KND_RADIX_BASE; i++) {
        idx = parent_idx->idxs[i];
        if (!idx) continue;
        idbuf[idbuf_size] = obj_id_seq[i];

        if (i != offset) {
            if (range_from_addr_size) {
                next_dir_id = range_from_addr + 1;
                next_dir_id_size = range_from_addr_size - 1;
            }
        }

        err = traverse_marshall(self, leaf, idx, idbuf, idbuf_size + 1,
                                next_dir_id, next_dir_id_size, cb, cb_ctx, &subdir, task);
        switch (err) {
        case knd_OK:
            break;
        case knd_LIMIT:
            return err;
        default:
            KND_TASK_ERR("failed to traverse a subdir");
        }

        dir->idx->subdirs[i] = subdir;

        if (subdir->total_size > dir->cell_max_val)
            dir->cell_max_val = subdir->total_size;

        dir->total_elems += subdir->total_elems;
        dir->subdir_block_size += subdir->total_size;
        dir->num_subdirs++;
    }

    cell_size = knd_min_bytes(dir->cell_max_val);
    if (KND_RADIX_BASE - dir->num_subdirs > KND_RADIX_BASE / 2)
        use_keys = true;

    out->reset(out);
    err = build_subdirs_footer(dir, idbuf, idbuf_size, use_keys, cell_size, task);
    KND_TASK_ERR("failed to build set dir footer");

    switch (task->mode) {
    case KND_TASK_TRACE_MODE:
        // knd_log(".. write subdirs footer to file %.*s", leaf->filepath_size, leaf->filepath);
        break;
    default:
        err = knd_append_file((const char*)leaf->filepath, out->buf, out->buf_size);
        KND_TASK_ERR("set idx write failure");
        break;
    }

    dir->subdir_block_size += out->buf_size;
    dir->total_size += dir->subdir_block_size;

    leaf->curr_size += out->buf_size;

    if (DEBUG_SHARED_SET_GSP_LEVEL_2) {
        knd_log(">> {dir %.*s} total {block-size %zu}",
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
        //knd_log(".. appending payload block size %zu to file %s",
        //        dir->payload_block_size, filename);
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
    /* use special name for the root dir */

    if (!dir_id_size) {
        leaf->range_to_addr[0] = '/';
        leaf->range_to_addr_size = 1;
        return knd_OK;
    }
    memcpy(leaf->range_to_addr, dir_id, dir_id_size);
    leaf->range_to_addr_size = dir_id_size;
    return knd_OK;
}

static int build_elems_block(struct kndSharedSetElemIdx *parent_idx,
                             struct kndStorageLeaf *leaf, struct kndSharedSetDir *dir,
                             char *idbuf, size_t idbuf_size,
                             knd_set_elem_marshall_cb_t cb, void *cb_ctx,
                             struct kndTask *task)
{
    struct kndOutput *out = task->out;
    size_t cell_size = 1;
    bool use_keys = false;
    size_t footer_size;
    int err;

    if (DEBUG_SHARED_SET_GSP_LEVEL_2) {
        knd_log(".. build elem payload block {dir %.*s {id %.*s}}",
                dir->id_size, dir->id, idbuf_size, idbuf);
    }

    err = marshall_elems(parent_idx, dir, idbuf, idbuf_size, cb, cb_ctx, false, leaf, task);
    KND_TASK_ERR("failed to marshall elems");

    if (out->buf_size) {
        // TODO keys
    }

    cell_size = knd_min_bytes(dir->cell_max_val);

    /* calc footer overhead */
    if ((float)KND_SET_MIN_FOOTER_SIZE / (float)out->buf_size > KND_MAX_IDX_OVERHEAD) {
        if (DEBUG_SHARED_SET_GSP_LEVEL_3) {
            knd_log("!! NB: another run is needed to optimize elem packing "
                    " (use explicit field keys)");
        }
        out->reset(out);
        dir->num_term_elems = 0;
        err = marshall_elems(parent_idx, dir, idbuf, idbuf_size, cb, cb_ctx, true, leaf, task);
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
    
    leaf->curr_size += dir->payload_block_size;
    leaf->num_elems += dir->num_term_elems;
    
    if (DEBUG_SHARED_SET_GSP_LEVEL_3) {
        knd_log("== {dir %.*s {payload-size %zu} {num-term-elems %zu}} {leaf {num-elems %zu}",
                idbuf_size, idbuf, dir->payload_block_size, dir->num_term_elems,
                leaf->num_elems);
    }

    /* check leaf size limit overflow */
    if (leaf->curr_size > leaf->max_size) {
        err = set_leaf_range_end(leaf, idbuf, idbuf_size);
        KND_TASK_ERR("failed to set leaf range end");

        err = write_payload_block_size(dir, leaf->filepath, &footer_size, task);
        KND_TASK_ERR("failed to write payload block size");
        dir->total_size += footer_size;
        leaf->curr_size += footer_size;

        if (DEBUG_SHARED_SET_GSP_LEVEL_2) {
            knd_log("!! max snapshot leaf size reached "
                    "{leaf {size %zu {num-elems %zu}} at {dir %.*s}",
                    leaf->curr_size, leaf->num_elems,
                    leaf->range_to_addr_size, leaf->range_to_addr);
        }
        return knd_OK;
    }
    return knd_OK;
}

static int traverse_marshall(struct kndSharedSet *self, struct kndStorageLeaf *leaf,
                             struct kndSharedSetElemIdx *parent_idx,
                             char *idbuf, size_t idbuf_size,
                             const char *range_from_addr, size_t range_from_addr_size,
                             knd_set_elem_marshall_cb_t cb, void *cb_ctx,
                             struct kndSharedSetDir **result_dir, struct kndTask *task)
{
    struct kndSharedSetDir *dir;
    size_t footer_size;
    int err;

    if (DEBUG_SHARED_SET_GSP_LEVEL_TMP) {
        if (!idbuf_size) {
            knd_log(">> {curr-dir /} {from-dir-remainder %.*s} {num-term-elems %zu}",
                    range_from_addr_size, range_from_addr, parent_idx->num_term_elems);
        } else {
            knd_log(">> {curr-dir %.*s} {from-dir-remainder %.*s}",
                    idbuf_size, idbuf, range_from_addr_size, range_from_addr);
        }
    }

    err = knd_shared_set_dir_new(&dir, self->mempool);
    KND_TASK_ERR("failed to alloc a shared set dir");

    memcpy(dir->id, idbuf, idbuf_size);
    dir->id_size = idbuf_size;

    if (!range_from_addr_size) {
        err = build_elems_block(parent_idx, leaf, dir, idbuf, idbuf_size, cb, cb_ctx, task);
        KND_TASK_ERR("failed to build elems block of {dir %.*s}", idbuf_size, idbuf);
        if (leaf->range_to_addr_size) {
            *result_dir = dir;
            return knd_OK;
        }
    }

    err = marshall_subdirs(self, leaf, parent_idx, dir, idbuf, idbuf_size,
                           range_from_addr, range_from_addr_size, cb, cb_ctx, task);
    KND_TASK_ERR("failed to marshall subdirs");

    err = write_payload_block_size(dir, leaf->filepath, &footer_size, task);
    KND_TASK_ERR("failed to write payload block size");

    dir->total_size += footer_size;
    leaf->curr_size += footer_size;

    *result_dir = dir;
    return knd_OK;
}

static int build_leaf_temp_filename(struct kndStorageLeaf *leaf,
                                    const char *path, size_t path_size, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    assert (path_size > 0);

    out->reset(out);
    OUT(path, path_size);
    if (path[path_size - 1] != '/') {
        OUT("/", 1);
    }

    OUT("output", strlen("output"));
    OUT(KND_GSP_FILE_TMP_EXT_NAME, strlen(KND_GSP_FILE_TMP_EXT_NAME));
    
    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("GSP path too long");
    }
    memcpy(leaf->filepath, out->buf, out->buf_size);
    leaf->filepath[out->buf_size] = '\0';
    leaf->filepath_size = out->buf_size;
    return knd_OK;
}

static int storage_leaf_create(struct kndStorageLeaf **result, size_t numid,
                               struct kndSharedSet *idx, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndStorageLeaf *leaf;
    const char *header = KND_GSP_FILE_HEADER_NAME;
    size_t header_size = strlen(header);
    const char *path = idx->path;
    size_t path_size = idx->path_size;
    int err;

    //err = knd_storage_leaf_new(&leaf, numid);
    //KND_TASK_ERR("failed to alloc a storage leaf");

    err = build_leaf_temp_filename(leaf, path, path_size, task);
    KND_TASK_ERR("failed to build a temp filename");

    out->reset(out);
    OUT(header, header_size);

    switch (task->mode) {
    case KND_TASK_TRACE_MODE:
        knd_log("\n.. create {leaf {path %.*s}}",
                leaf->filepath_size, leaf->filepath);
        break;
    default:
        err = knd_write_file((const char*)leaf->filepath, out->buf, out->buf_size);
        KND_TASK_ERR("failed writing to {file %.*s}", leaf->filepath_size, leaf->filepath);
        break;
    }
    leaf->curr_size = out->buf_size;

    *result = leaf;
    return knd_OK;
}

static int finalize_leaf(struct kndStorageLeaf *leaf, const char *path, size_t path_size,
                         struct kndTask *task)
{
    struct kndOutput *out = task->out;
    char buf[KND_PATH_SIZE + 1];
    size_t buf_size;
    int err;

    if (DEBUG_SHARED_SET_GSP_LEVEL_2) {
        knd_log(".. finalize {leaf %zu {from %.*s} {to %.*s} {size %zu}}",
                leaf->numid, leaf->range_from_addr_size, leaf->range_from_addr,
                leaf->range_to_addr_size, leaf->range_to_addr, leaf->curr_size);
    }

    out->reset(out);

    /* make sure the file name is unique,
       some filesystems are case-insensitive */
    OUTF("%zu_", leaf->numid);

    /* root dir special name */
    if (*leaf->range_from_addr == '/') {
        OUT("0", 1);
    } else {
        OUT(leaf->range_from_addr, leaf->range_from_addr_size);
    }

    OUT("_", 1);

    if (*leaf->range_to_addr == '/') {
        //
    } else {
        OUT(leaf->range_to_addr, leaf->range_to_addr_size);
    }

    if (out->buf_size >= KND_SHORT_NAME_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("GSP filename too long");
    }
    memcpy(leaf->name, out->buf, out->buf_size);
    leaf->name[out->buf_size] = '\0';
    leaf->name_size = out->buf_size;

    out->reset(out);
    OUT(path, path_size);
    OUT(leaf->name, leaf->name_size);
    OUT(KND_GSP_FILE_EXT_NAME, strlen(KND_GSP_FILE_EXT_NAME));

    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("GSP path too long");
    }

    memcpy(buf, out->buf, out->buf_size);
    buf[out->buf_size] = '\0';
    buf_size = out->buf_size;

    /* rename leaf file */
    switch (task->mode) {
    case KND_TASK_TRACE_MODE:
        if (DEBUG_SHARED_SET_GSP_LEVEL_2) {
            knd_log("\n.. renaming leaf file from %.*s to %.*s",
                    leaf->filepath_size, leaf->filepath, out->buf_size, out->buf);
        }
        break;
    default:
        if (DEBUG_SHARED_SET_GSP_LEVEL_2) {
            knd_log(".. renaming {file %.*s} to {file %.*s}",
                    leaf->filepath_size, leaf->filepath, buf_size, buf);
        }
        err = rename((const char*)leaf->filepath, (const char*)buf);
        KND_TASK_ERR("failed renaming {file %.*s} to {file %.*s}",
                     leaf->filepath_size, leaf->filepath, out->buf_size, out->buf);
        break;
    }
    memcpy(leaf->filepath, buf, buf_size);
    leaf->filepath[buf_size] = '\0';
    leaf->filepath_size = buf_size;
    return knd_OK;
}

int knd_shared_set_leaf_marshall(struct kndSharedSet *self, struct kndStorageLeaf *leaf,
                                 struct kndSetRange *range,
                                 knd_set_elem_marshall_cb_t cb, void *cb_ctx,
                                 struct kndTask *task)
{
    struct kndSharedSetDir *dir;
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size = 0;
    int err;

    if (DEBUG_SHARED_SET_GSP_LEVEL_2) {
        if (!range->from_id_size) {
            knd_log(">> marshalling set from scratch");
        } else {
            knd_log(">> marshalling set {from-dir %.*s}",
                    range->from_id_size, range->from_id);
        }
    }

    err = traverse_marshall(self, leaf, self->idx, idbuf, idbuf_size,
                            range->from_id, range->from_id_size, cb, cb_ctx, &dir, task);
    KND_TASK_ERR("failed to marshall set idx");

    return knd_OK;
}

int knd_shared_set_build_path(struct kndSharedSet *idx,
                              const char *snapshot_path, size_t snapshot_path_size,
                              const char *pref, size_t pref_size, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    out->reset(out);
    OUT(snapshot_path, snapshot_path_size);
    OUT(pref, pref_size);
    OUT("/", 1);
    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("GSP path too long");
    }
    memcpy(idx->path, out->buf, out->buf_size);
    idx->path_size = out->buf_size;
    idx->path[out->buf_size] = '\0';

    return knd_OK;
}

int knd_shared_set_marshall(struct kndSharedSet *idx, struct kndSetRange *range,
                            const char *path, size_t path_size,
                            knd_set_elem_marshall_cb_t cb, void *cb_ctx,
                            struct kndStorageLeaf **result, size_t *total_leaves,
                            struct kndTask *task)
{
    struct kndStorageLeaf *leaves = NULL, *leaf;
    struct kndSetRange local_range = { 0 };
    size_t leaf_count = 0;
    size_t total_elems = 0;
    int err;

    if (!range) {
        range = &local_range;
    }

    if (path_size >= KND_PATH_SIZE) return knd_LIMIT;
    memcpy(idx->path, path, path_size);
    idx->path_size = path_size;
    idx->path[path_size] = '\0';

    err = knd_mkpath((const char*)idx->path, idx->path_size, 0755, false);
    KND_TASK_ERR("failed to make {path %.*s}", idx->path_size, idx->path);

    if (DEBUG_SHARED_SET_GSP_LEVEL_TMP) {
        knd_log(".. saving {idx {path %.*s}}", idx->path_size, idx->path);
    }

    /* split a set into a batch of leaves of max size */
    while (1) {
        leaf_count++;

        //err = knd_storage_leaf_new(&leaf, leaf_count);
        //KND_TASK_ERR("failed to alloc a storage leaf");

        err = knd_shared_set_leaf_marshall(idx, leaf, range, cb, cb_ctx, task);
        KND_TASK_ERR("failed to marshall a leaf of a shared set");

        err = finalize_leaf(leaf, idx->path, idx->path_size, task);
        KND_TASK_ERR("failed to finalize a storage leaf");

        if (DEBUG_SHARED_SET_GSP_LEVEL_TMP) {
            knd_log("++ {leaf {from %.*s} {to %.*s} {num-elems %zu {size %zu}} {total-elems %zu}",
                    leaf->range_from_addr_size, leaf->range_from_addr,
                    leaf->range_to_addr_size, leaf->range_to_addr,
                    leaf->num_elems, leaf->curr_size, range->num_elems);
        }

        append_leaf(&leaves, leaf);

        /* set next range offset */
        memcpy(local_range.from_id, leaf->range_to_addr, leaf->range_to_addr_size);
        local_range.from_id_size = leaf->range_to_addr_size;
        total_elems += leaf->num_elems;


        if (leaf_count > KND_MAX_STORAGE_LEAVES) {
            err = knd_LIMIT;
            KND_TASK_ERR("max limit reached {max-storage-leaves %zu}", leaf_count);
        }

        /* more leafs needed?
           TODO: when the task is performed by N workers,
                 add constraints on a worker's segment range */
        if (idx->num_elems > total_elems) continue;

        break;
    }

    if (total_elems != idx->num_elems) {
        err = knd_FAIL;
        KND_TASK_ERR("total elems mismatch after marshalling: %zu vs original %zu",
                     total_elems, idx->num_elems);
    }

    *result = leaves;
    *total_leaves = leaf_count;

    return knd_OK;
}
