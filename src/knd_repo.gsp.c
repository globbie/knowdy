#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <stdatomic.h>

#include "knd_repo.h"
#include "knd_attr.h"
#include "knd_set.h"
#include "knd_shared_set.h"
#include "knd_user.h"
#include "knd_query.h"
#include "knd_task.h"
#include "knd_dict.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_proc.h"
#include "knd_mempool.h"
#include "knd_state.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_REPO_GSP_LEVEL_0 0
#define DEBUG_REPO_GSP_LEVEL_1 0
#define DEBUG_REPO_GSP_LEVEL_2 0
#define DEBUG_REPO_GSP_LEVEL_3 0
#define DEBUG_REPO_GSP_LEVEL_TMP 1

/*static int present_class_entry(void *obj, const char *unused_var(elem_id),
                               size_t unused_var(elem_id_size),
                               size_t unused_var(count), void *elem)
{
    struct kndTask *task = obj;
    struct kndClassEntry *entry = elem;
    knd_log("{class %.*s {id %.*s}}",
            entry->name_size, entry->name, entry->id_size, entry->id);
    return knd_OK;
    }*/

static int build_leaf_filename(struct kndStorageLeaf *leaf,
                               const char *path, size_t path_size,
                               const char *pref, size_t pref_size,
                               struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;
    out->reset(out);
    OUT(path, path_size);
    OUT(pref, pref_size);

    // TODO add range

    if (leaf->file_size) {
        OUT(KND_GSP_FILE_EXT_NAME, strlen(KND_GSP_FILE_EXT_NAME));
    } else {
        OUT(KND_GSP_FILE_TMP_EXT_NAME, strlen(KND_GSP_FILE_TMP_EXT_NAME));
    }
    
    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("GSP path too long");
    }
    memcpy(leaf->filepath, out->buf, out->buf_size);
    leaf->filepath[out->buf_size] = '\0';
    leaf->filepath_size = out->buf_size;
    return knd_OK;
}

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

static int init_leaf(struct kndRepoSnapshot *snapshot,
                     const char *path, size_t path_size,
                     const char *pref, size_t pref_size,
                     const char *range_from_id, size_t range_from_id_size,
                     struct kndStorageLeaf **result, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndStorageLeaf *leaf;
    const char *header = KND_GSP_FILE_HEADER_NAME;
    size_t header_size = strlen(header);    
    int err;

    err = knd_storage_leaf_new(&leaf, snapshot);
    KND_TASK_ERR("failed to alloc a storage leaf");
    if (range_from_id_size) {
        memcpy(leaf->range_from_id, range_from_id, range_from_id_size);
        leaf->range_from_id_size = range_from_id_size;
    }
    err = build_leaf_filename(leaf, path, path_size, pref, pref_size, task);
    KND_TASK_ERR("failed to build a leaf filename");

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

static int finalize_leaf(struct kndStorageLeaf *leaf,
                         const char *path, size_t path_size,
                         const char *pref, size_t pref_size,
                         struct kndTask *task)
{
    struct kndOutput *out = task->out;
    char buf[KND_PATH_SIZE + 1];
    int err;

    out->reset(out);
    OUT(path, path_size);
    OUT(pref, pref_size);
    OUT("_", 1);

    /* root dir special name */
    if (*leaf->range_from_id == '/') {
    } else {
        OUT(leaf->range_from_id, leaf->range_from_id_size);
    }
    OUT("_to_", strlen("_to_"));

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

    memcpy(leaf->filepath, out->buf, out->buf_size);
    leaf->filepath[out->buf_size] = '\0';
    leaf->filepath_size = out->buf_size;
    return knd_OK;
}

static int marshall_idx(struct kndSharedSet *idx, const char *path, size_t path_size,
                        const char *pref, size_t pref_size,
                        elem_marshall_cb cb, struct kndRepoSnapshot *snapshot,
                        struct kndStorageLeaf **result,
                        struct kndTask *task)
{
    struct kndStorageLeaf *leaves = NULL, *leaf;

    /* starting values
       TODO: get range limits as parameters */
    const char *range_from_id = "";
    size_t range_from_id_size = 0;
    size_t total_elems = 0;
    int err;

    /* split a set into a batch of leaves of max size */
    while (1) {
        err = init_leaf(snapshot, path, path_size, pref, pref_size,
                        range_from_id, range_from_id_size, &leaf, task);
        KND_TASK_ERR("failed to create a new snapshot leaf");

        err = knd_shared_set_marshall(idx, leaf, cb, task);
        KND_TASK_ERR("failed to marshall str idx");

        err = finalize_leaf(leaf, path, path_size, pref, pref_size, task);
        KND_TASK_ERR("failed to finalize a snapshot leaf");

        append_leaf(&leaves, leaf);

        /* set next range offset */
        range_from_id = leaf->range_to_id;
        range_from_id_size = leaf->range_to_id_size;
        total_elems += leaf->num_elems;

        if (DEBUG_REPO_GSP_LEVEL_TMP) {
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

    *result = leaves;
    return knd_OK;
}

static int export_class_insts(void *obj, const char *unused_var(elem_id),
                              size_t unused_var(elem_id_size),
                              size_t unused_var(count), void *elem)
{
    char buf[KND_NAME_SIZE + 1];
    size_t buf_size;
    struct kndTask *task = obj;
    struct kndClassEntry *entry = elem;
    struct kndClass *c;
    struct kndOutput *out = task->out;
    struct kndStorageLeaf *leaf;
    struct kndRepoSnapshot *snapshot = atomic_load_explicit(&task->repo->snapshot, memory_order_relaxed);
    int err;

    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire class %.*s", entry->name_size, entry->name);
    if (!c->inst_idx) return knd_OK;

    if (DEBUG_REPO_GSP_LEVEL_2) {
        knd_log("\n== class \"%.*s\" total insts:%zu",
                c->name_size, c->name, c->inst_idx->num_elems);
        knd_log(">> path \"%.*s\"", task->filepath_size, task->filepath);
    }
    out->reset(out);
    OUT("inst_", strlen("inst_"));
    OUT(entry->id, entry->id_size);
    OUT(".gsp", strlen(".gsp"));
    memcpy(buf, out->buf, out->buf_size);
    buf_size = out->buf_size;

    err = marshall_idx(c->inst_idx, task->filepath, task->filepath_size,
                       buf, buf_size, knd_class_inst_marshall, snapshot, &leaf, task);
    KND_TASK_ERR("failed to build the class inst GSP storage");
    snapshot->class_inst_db = leaf;
    return knd_OK;
}

static int build_snapshot_path(struct kndRepo *repo,
                               char *path, size_t *path_size,
                               struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    out->reset(out);
    OUT(repo->path, repo->path_size);
    OUTF("snapshot_%zu/", repo->snapshot->numid);
    OUTF("agent_%d/", task->id);
    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("GSP path too long");
    }
    memcpy(path, out->buf, out->buf_size);
    *path_size = out->buf_size;
    path[out->buf_size] = '\0';
    return knd_OK;
}

int knd_repo_snapshot(struct kndRepo *repo, size_t last_commit_id, struct kndTask *task)
{
    struct kndRepoSnapshot *snapshot;
    struct kndStorageLeaf *leaf;
    char path[KND_PATH_SIZE + 1];
    size_t path_size;
    int err;

    if (DEBUG_REPO_GSP_LEVEL_TMP) {
        knd_log(".. building a GSP snapshot of {repo %.*s {last-commit %zu}}",
                repo->name_size, repo->name, last_commit_id);
    }

    err = knd_repo_snapshot_new(&snapshot, task->cache_mempool, task->mempool);
    KND_TASK_ERR("failed to alloc a repo snapshot");
    snapshot->min_leaf_size = KND_SNAPSHOT_LEAF_MIN_THRESHOLD;
    snapshot->max_leaf_size = KND_SNAPSHOT_LEAF_MAX_THRESHOLD;
  
    err = build_snapshot_path(repo, path, &path_size, task);
    KND_TASK_ERR("failed to build a file path");

    err = knd_mkpath((const char*)path, path_size, 0755, false);
    KND_TASK_ERR("mkpath %.*s failed", path_size, path);

    /* class storage */
    err = marshall_idx(task->idxs->class_idx, path, path_size, "class", strlen("class"),
                       knd_class_marshall, snapshot, &leaf, task);
    KND_TASK_ERR("failed to build the class storage");
    snapshot->class_db = leaf;

    /* class insts storage */
    memcpy(task->filepath, path, path_size);
    task->filepath_size = path_size;
    task->filepath[path_size] = '\0';

    err = knd_shared_set_map(task->idxs->class_idx, export_class_insts, (void*)task);
    KND_TASK_ERR("failed to build the class inst storage");

    /* global string dict storage */
    err = knd_storage_leaf_new(&leaf, snapshot);
    KND_TASK_ERR("failed to alloc a storage leaf");

    /*err = marshall_idx(repo->str_idx, path, path_size,
                       "strings.gsp", strlen("strings.gsp"),
                       knd_charseq_marshall, snapshot, &leaf, task);
    KND_TASK_ERR("failed to build the string idx");
    snapshot->string_db = leaf;
    */

    repo->snapshot_temp = snapshot;
    return knd_OK;
}
