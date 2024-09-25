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

int knd_repo_snapshot_create(struct kndRepo *repo, struct kndTask *task)
{
    struct kndRepoSnapshot *curr_snapshot, *s;
    size_t numid;
    size_t last_commit_id;    
    int err;

    curr_snapshot = atomic_load_explicit(&repo->snapshot, memory_order_relaxed);
    last_commit_id = atomic_load_explicit(&curr_snapshot->num_commits, memory_order_relaxed);

    switch (curr_snapshot->state) {
    case KND_SNAPSHOT_INIT:
        numid = 0;
        break;
    default:
        numid = curr_snapshot->numid + 1;
    }

    err = knd_repo_snapshot_new(&s, numid, last_commit_id, repo, task);
    KND_TASK_ERR("failed to create a repo snapshot");

    err = knd_mkpath((const char*)s->path, s->path_size, 0755, false);
    KND_TASK_ERR("mkpath %.*s failed", s->path_size, s->path);

    if (DEBUG_REPO_GSP_LEVEL_TMP) {
        knd_log(".. building a GSP snapshot #%zu of {repo %.*s {last-commit %zu}}",
                numid, repo->name_size, repo->name, last_commit_id);
    }

    err = knd_shared_dict_marshall(task->idxs->class_name_idx, s->path, s->path_size,
                                   "class-name-idx", strlen("class-name-idx"),
                                   knd_class_names_marshall, s->idxs.class_name_idx, task);
    KND_TASK_ERR("failed to build a class name idx");

    err = knd_shared_dict_marshall(task->idxs->attr_name_idx, s->path, s->path_size,
                                   "attr-name-idx", strlen("attr-name-idx"),
                                   knd_attr_names_marshall, s->idxs.attr_name_idx, task);
    KND_TASK_ERR("failed to build an attr name idx");

    /* save class content */
    err = knd_shared_set_marshall(task->idxs->class_idx, s->path, s->path_size,
                                  "classes", strlen("classes"),
                                  knd_class_marshall, s->idxs.class_idx, task);
    KND_TASK_ERR("failed to build a class idx");

    /* global string dict storage 
       NB: shoud be exported last */
    err = knd_shared_set_marshall(task->idxs->str_idx, s->path, s->path_size,
                                  "strings", strlen("strings"),
                                  knd_charseq_marshall, s->idxs.str_idx, task);
    KND_TASK_ERR("failed to build a string idx");
    
    repo->snapshot_temp = s;
    return knd_OK;
}
