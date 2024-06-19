#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "knd_class.h"
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

static int traverse_marshall(struct kndSharedSetElemIdx *parent_idx,
                             struct kndStorageLeaf *leaf,
                             char *idbuf, size_t idbuf_size,
                             const char *range_from_id, size_t range_from_id_size,
                             elem_marshall_cb cb,
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
        knd_log(".. marshall elems of {dir %.*s}", idbuf_size, idbuf);
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
        if (DEBUG_SHARED_SET_GSP_LEVEL_2) {
            knd_log(">> elem %.*s%c", idbuf_size, idbuf, obj_id_seq[i]);
        }
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
                            const char *range_from_id, size_t range_from_id_size,
                            elem_marshall_cb cb, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndSharedSetElemIdx *idx;
    struct kndSharedSetDir *subdir;
    const char *next_dir_id = range_from_id;
    size_t next_dir_id_size = range_from_id_size;
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
                range_from_id_size, range_from_id);
    }
    
    if (range_from_id_size) {
        if (*range_from_id == '/') {
            next_dir_id = range_from_id + 1;
            next_dir_id_size = range_from_id_size - 1;
        } else {
            idx_pos = obj_id_base[(const int)*range_from_id];
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
            if (range_from_id_size) {
                next_dir_id = range_from_id + 1;
                next_dir_id_size = range_from_id_size - 1;
            }
        }

        err = traverse_marshall(idx, leaf, idbuf, idbuf_size + 1,
                                next_dir_id, next_dir_id_size, cb, &subdir, task);
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
        leaf->range_to_id[0] = '/';
        leaf->range_to_id_size = 1;
        return knd_OK;
    }
    memcpy(leaf->range_to_id, dir_id, dir_id_size);
    leaf->range_to_id_size = dir_id_size;
    return knd_OK;
}

static int build_elems_block(struct kndSharedSetElemIdx *parent_idx,
                             struct kndStorageLeaf *leaf,
                             struct kndSharedSetDir *dir,
                             char *idbuf, size_t idbuf_size,
                             elem_marshall_cb cb, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    size_t cell_size = 1;
    bool use_keys = false;
    size_t footer_size;
    int err;

    out->reset(out);

    err = marshall_elems(parent_idx, dir, idbuf, idbuf_size, cb, false, task);
    KND_TASK_ERR("failed to marshall elems");

    if (out->buf_size) {
        // calc cell size
        cell_size = knd_min_bytes(dir->cell_max_val);
        // calc footer overhead
        if ((float)KND_SET_MIN_FOOTER_SIZE / (float)out->buf_size > KND_MAX_IDX_OVERHEAD) {

            if (DEBUG_SHARED_SET_GSP_LEVEL_3) {
                knd_log("!! NB: another run is needed to optimize elem packing "
                        " (use explicit field keys)");
            }
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
            //knd_log(".. write elems payload {size %zu} to file %.*s",
            //        out->buf_size, leaf->filepath_size, leaf->filepath);
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

        if (DEBUG_SHARED_SET_GSP_LEVEL_3) {
            knd_log("== {dir %.*s {payload-size %zu} {num-term-elems %zu}} {leaf {num-elems %zu}",
                    idbuf_size, idbuf, dir->payload_block_size, dir->num_term_elems,
                    leaf->num_elems);
        }
    }

    /* check leaf size limit overflow */
    if (leaf->file_size > leaf->max_leaf_size) {
        err = set_leaf_range_end(leaf, idbuf, idbuf_size);
        KND_TASK_ERR("failed to set leaf range end");

        err = write_payload_block_size(dir, leaf->filepath, &footer_size, task);
        KND_TASK_ERR("failed to write payload block size");
        dir->total_size += footer_size;
        leaf->file_size += footer_size;

        if (DEBUG_SHARED_SET_GSP_LEVEL_2) {
            knd_log("!! max snapshot leaf size reached "
                    "{leaf {size %zu {num-elems %zu}} at {dir %.*s}",
                    leaf->file_size, leaf->num_elems, leaf->range_to_id_size, leaf->range_to_id);
        }
        return knd_OK;
    }
    return knd_OK;
}

static int traverse_marshall(struct kndSharedSetElemIdx *parent_idx,
                             struct kndStorageLeaf *leaf,
                             char *idbuf, size_t idbuf_size,
                             const char *range_from_id, size_t range_from_id_size,
                             elem_marshall_cb cb, struct kndSharedSetDir **result_dir,
                             struct kndTask *task)
{
    struct kndSharedSetDir *dir;
    size_t footer_size;
    int err;

    if (DEBUG_SHARED_SET_GSP_LEVEL_2) {
        knd_log(">> {curr-dir %.*s} {from-dir-remainder %.*s}",
                idbuf_size, idbuf, range_from_id_size, range_from_id);
    }

    dir = calloc(1, sizeof(struct kndSharedSetDir));
    if (!dir) {
        err = knd_NOMEM;
        KND_TASK_ERR("failed to alloc kndSharedSetDir");
    }

    if (!range_from_id_size) {
        err = build_elems_block(parent_idx, leaf, dir, idbuf, idbuf_size, cb, task);
        KND_TASK_ERR("failed to build elems block of dir %.*s", idbuf_size, idbuf);
        if (leaf->range_to_id_size) {
            *result_dir = dir;
            return knd_OK;
        }
    }

    err = marshall_subdirs(parent_idx, leaf, dir, idbuf, idbuf_size,
                           range_from_id, range_from_id_size, cb, task);
    KND_TASK_ERR("failed to marshall subdirs");

    err = write_payload_block_size(dir, leaf->filepath, &footer_size, task);
    KND_TASK_ERR("failed to write payload block size");
    dir->total_size += footer_size;
    leaf->file_size += footer_size;

    *result_dir = dir;
    return knd_OK;
}

static int build_leaf_temp_filename(struct kndStorageLeaf *leaf,
                                    const char *path, size_t path_size, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    out->reset(out);
    OUT(path, path_size);
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

static int create_leaf(struct kndStorageLeaf **result,
                       const char *path, size_t path_size,
                       const char *range_from_id, size_t range_from_id_size,
                       struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndStorageLeaf *leaf;
    const char *header = KND_GSP_FILE_HEADER_NAME;
    size_t header_size = strlen(header);    
    int err;

    err = knd_storage_leaf_new(&leaf);
    KND_TASK_ERR("failed to alloc a storage leaf");
    leaf->min_leaf_size = KND_SNAPSHOT_LEAF_MIN_THRESHOLD;
    leaf->max_leaf_size = KND_SNAPSHOT_LEAF_MAX_THRESHOLD;

    if (range_from_id_size) {
        memcpy(leaf->range_from_id, range_from_id, range_from_id_size);
        leaf->range_from_id_size = range_from_id_size;
    }

    err = build_leaf_temp_filename(leaf, path, path_size, task);
    KND_TASK_ERR("failed to build a temp filename");

    out->reset(out);
    OUT(header, header_size);

    switch (task->mode) {
    case KND_TASK_TRACE_MODE:
        if (range_from_id_size) {
            knd_log("\n.. create new leaf file %.*s {leaf {from %.*s}}",
                    leaf->filepath_size, leaf->filepath,
                    leaf->range_from_id_size, leaf->range_from_id);
        } else {
            knd_log("\n.. create new leaf file %.*s (start from scratch)",
                    leaf->filepath_size, leaf->filepath);
        }
        break;
    default:
        err = knd_write_file((const char*)leaf->filepath, out->buf, out->buf_size);
        KND_TASK_ERR("failed writing to {file %.*s}", leaf->filepath_size, leaf->filepath);
        break;
    }
    leaf->file_size = out->buf_size;

    *result = leaf;
    return knd_OK;
}

int knd_storage_leaf_new(struct kndStorageLeaf **result)
{
    struct kndStorageLeaf *leaf;
    leaf = calloc(1, sizeof(struct kndStorageLeaf));
    if (!leaf) return knd_NOMEM;

    *result = leaf;
    return knd_OK;
}

static int finalize_leaf(struct kndStorageLeaf *leaf, const char *path, size_t path_size, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    char buf[KND_PATH_SIZE + 1];
    size_t buf_size;
    int err;

    out->reset(out);
    OUT(path, path_size);

    /* root dir special name */
    if (*leaf->range_from_id == '/') {
        OUT("0", 1);
    } else {
        OUT(leaf->range_from_id, leaf->range_from_id_size);
    }

    OUT("_", 1);

    if (*leaf->range_to_id == '/') {

    } else {
        OUT(leaf->range_to_id, leaf->range_to_id_size);
    }
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
        knd_log("\n.. renaming leaf file from %.*s to %.*s",
                leaf->filepath_size, leaf->filepath, out->buf_size, out->buf);
        break;
    default:
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

static int marshall_leaf(struct kndSharedSet *self, struct kndStorageLeaf *leaf,
                         elem_marshall_cb cb, struct kndTask *task)
{
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size = 0;
    struct kndSharedSetDir *dir;
    int err;

    if (DEBUG_SHARED_SET_GSP_LEVEL_2) {
        if (!leaf->range_from_id_size) {
            knd_log(">> marshalling set from scratch");
        } else {
            knd_log(">> marshalling set {from-dir %.*s}",
                    leaf->range_from_id_size, leaf->range_from_id);
        }
    }

    err = traverse_marshall(self->idx, leaf, idbuf, idbuf_size,
                            leaf->range_from_id, leaf->range_from_id_size, cb, &dir, task);
    KND_TASK_ERR("failed to marshall set idx");

    return knd_OK;
}

static int build_idx_path(struct kndSharedSet *idx,
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

    err = knd_mkpath((const char*)idx->path, idx->path_size, 0755, false);
    KND_TASK_ERR("mkpath %.*s failed", idx->path_size, idx->path);

    return knd_OK;
}

int knd_shared_set_marshall(struct kndSharedSet *idx,
                            const char *snapshot_path, size_t snapshot_path_size,
                            const char *pref, size_t pref_size,
                            elem_marshall_cb cb, struct kndSharedSet *result_idx,
                            struct kndTask *task)
{
    struct kndStorageLeaf *leaves = NULL, *leaf;

    /* starting values
       TODO: get range limits as parameters */
    const char *range_from_id = "";
    size_t range_from_id_size = 0;
    size_t total_elems = 0;
    int err;

    err = build_idx_path(idx, snapshot_path, snapshot_path_size, pref, pref_size, task);
    KND_TASK_ERR("failed to build a path for %.*s idx", pref_size, pref);

    /* split a set into a batch of leaves of max size */
    while (1) {
        err = create_leaf(&leaf, idx->path, idx->path_size, range_from_id, range_from_id_size, task);
        KND_TASK_ERR("failed to create a new storage leaf");

        err = marshall_leaf(idx, leaf, cb, task);
        KND_TASK_ERR("failed to marshall str idx");

        err = finalize_leaf(leaf, idx->path, idx->path_size, task);
        KND_TASK_ERR("failed to finalize a storage leaf");

        append_leaf(&leaves, leaf);

        /* set next range offset */
        range_from_id = leaf->range_to_id;
        range_from_id_size = leaf->range_to_id_size;
        total_elems += leaf->num_elems;

        if (DEBUG_SHARED_SET_GSP_LEVEL_TMP) {
            knd_log("++ {leaf {from %.*s} {to %.*s} {num-elems %zu {size %zu}} {total-elems %zu}",
                    leaf->range_from_id_size, leaf->range_from_id,
                    leaf->range_to_id_size, leaf->range_to_id,
                    leaf->num_elems,
                    leaf->file_size, total_elems);
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

    result_idx->leaves = leaves;
    return knd_OK;
}
