#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <stdatomic.h>

#include "knd_repo.h"
#include "knd_shard.h"
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

static int marshall_idx(struct kndSharedSet *idx, const char *path, size_t path_size,
                        const char *filename, size_t filename_size,
                        elem_marshall_cb cb, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    char buf[KND_PATH_SIZE + 1];
    size_t buf_size;
    size_t total_size;
    int err;

    out->reset(out);
    OUT(path, path_size);
    OUT(filename, filename_size);
    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("GSP path too long");
    }
    memcpy(buf, out->buf, out->buf_size);
    buf_size = out->buf_size;
    buf[buf_size] = '\0';

    out->reset(out);
    OUT("GSP", strlen("GSP"));

    err = knd_write_file((const char*)buf, out->buf, out->buf_size);
    KND_TASK_ERR("failed writing to file \"%.*s\"", buf_size, buf);

    if (DEBUG_REPO_GSP_LEVEL_2) {
        knd_log(".. marshall {idx %.*s} {num-elems %zu}",
                filename_size, filename, idx->num_elems);
    }
    err = knd_shared_set_marshall(idx, buf, buf_size, cb, &total_size, task);
    KND_TASK_ERR("failed to marshall str idx");

    if (DEBUG_REPO_GSP_LEVEL_TMP) {
        KND_TASK_LOG("{idx-snapshot {filepath %.*s {size %zu}}}", buf_size, buf, total_size);
    }
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
                       buf, buf_size, knd_class_inst_marshall, task);
    KND_TASK_ERR("failed to build the class inst GSP storage");
    return knd_OK;
}

int knd_repo_snapshot(struct kndRepo *repo, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndRepoSnapshot *snapshot = atomic_load_explicit(&repo->snapshots,
                                                            memory_order_relaxed);
    size_t latest_commit_id = atomic_load_explicit(&repo->snapshots->num_commits,
                                                   memory_order_relaxed);
    struct kndStorageLeaf *leaf;
    const char *path;
    size_t path_size;
    int err;

    if (DEBUG_REPO_GSP_LEVEL_TMP) {
        knd_log(".. building a GSP snapshot of {repo %.*s {latest-commit %zu}}",
                repo->name_size, repo->name, latest_commit_id);
    }

    err = knd_storage_leaf_new(&leaf);
    KND_TASK_ERR("failed to alloc a storage leaf");
    
    out->reset(out);
    OUT(repo->path, repo->path_size);
    OUTF("snapshot_%zu/", repo->snapshots->numid);
    OUTF("agent_%d/", task->id);
    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("GSP path too long");
    }
    path = out->buf;
    path_size = out->buf_size;

    err = knd_mkpath(path, path_size, 0755, false);
    KND_TASK_ERR("mkpath %.*s failed", path_size, path);

    /* class storage */
    err = marshall_idx(repo->class_idx, path, path_size,
                       "classes.gsp", strlen("classes.gsp"),
                       knd_class_marshall, task);
    KND_TASK_ERR("failed to build the class storage");

    /* class insts */
    memcpy(task->filepath, path, path_size);
    task->filepath_size = path_size;
    task->filepath[path_size] = '\0';

    err = knd_shared_set_map(repo->class_idx, export_class_insts, (void*)task);
    KND_TASK_ERR("failed to build the class inst storage");

    /* global string dict */
    err = marshall_idx(repo->str_idx, path, path_size,
                       "strings.gsp", strlen("strings.gsp"),
                       knd_charseq_marshall, task);
    KND_TASK_ERR("failed to build the string idx");

    snapshot->leaf = leaf;

    return knd_OK;
}
