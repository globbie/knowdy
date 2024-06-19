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

    /*err = marshall_idx(c->inst_idx, task->filepath, task->filepath_size,
                       buf, buf_size, knd_class_inst_marshall, snapshot, task);
    KND_TASK_ERR("failed to build the class inst GSP storage");
    */
    return knd_OK;
}


int knd_repo_snapshot(struct kndRepo *repo, struct kndTask *task)
{
    struct kndRepoSnapshot *curr_snapshot, *snapshot;
    struct kndStorageLeaf *leaf;
    size_t numid = 0;
    size_t last_commit_id = 0;
    int err;

    curr_snapshot = atomic_load_explicit(&repo->snapshot, memory_order_relaxed);
    last_commit_id = atomic_load_explicit(&snapshot->num_commits, memory_order_relaxed);

    switch (curr_snapshot->state) {
    case KND_SNAPSHOT_INIT:
        snapshot = curr_snapshot;
        break;
    default:
        numid = curr_snapshot->numid + 1;
        err = knd_repo_snapshot_new(&snapshot, numid, last_commit_id, repo, task);
        KND_TASK_ERR("failed to create a repo snapshot");
        break;
    }

    if (DEBUG_REPO_GSP_LEVEL_TMP) {
        knd_log(".. building a GSP snapshot #%zu of {repo %.*s {last-commit %zu}}",
                numid, repo->name_size, repo->name, last_commit_id);
    }

    /* save class names */
    err = knd_shared_dict_marshall(task->idxs->class_name_idx, snapshot->path, snapshot->path_size,
                                   "class_names", strlen("class_names"),
                                   knd_class_names_marshall, snapshot->idxs.class_name_idx, task);
    KND_TASK_ERR("failed to build the class name idx");

    //err = knd_shared_set_marshall(task->idxs->class_idx, path, path_size, "class", strlen("class"),
    //                              knd_class_marshall, snapshot->idxs.class_idx, task);
    //KND_TASK_ERR("failed to build the class storage");

    /* class insts storage */
    /*memcpy(task->filepath, path, path_size);
    task->filepath_size = path_size;
    task->filepath[path_size] = '\0';

    err = knd_shared_set_map(task->idxs->class_idx, export_class_insts, (void*)task);
    KND_TASK_ERR("failed to build the class inst storage");
    */

    /* global string dict storage */
    /*err = marshall_idx(repo->str_idx, path, path_size,
                       "strings.gsp", strlen("strings.gsp"),
                       knd_charseq_marshall, snapshot, task);
    KND_TASK_ERR("failed to build the string idx");
    */

    repo->snapshot_temp = snapshot;
    return knd_OK;
}
