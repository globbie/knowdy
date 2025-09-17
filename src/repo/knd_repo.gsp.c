#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <stdatomic.h>

#include "knd_repo.h"
#include "knd_attr.h"
#include "knd_facet.h"
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

static int attr_facet_marshall(void *elem, void *ctx, struct kndStorageLeaf *leaf,
                              size_t *output_size, struct kndTask *task)
{
    struct kndAttrRef *ref = elem;
    struct kndAttr *attr = ref->attr;
    struct kndFacet *facet = attr->facet;
    struct kndSetRange *range = ctx;
    int err;

    if (DEBUG_REPO_GSP_LEVEL_2) {
        knd_log(">> building GSP for {cls %.*s {attr %.*s}} {leaf %zu}",
                attr->owner->name_size, attr->owner->name,
                attr->name_size, attr->name, leaf->numid);
    }

    if (!facet) return knd_NO_MATCH;

    err = knd_facet_leaf_marshall(facet, attr->type, leaf, range, output_size, task);
    KND_TASK_ERR("failed to marshall attr facet");

    return knd_OK;
}

static int marshall_attr_idx(struct kndRepoSnapshot *s, struct kndTask *task)
{
    struct kndStorageLeaf *leaf;
    size_t num_leaves = 0;
    struct kndOutput *out = task->out;
    int err;

    out->reset(out);
    OUT(s->path, s->path_size);
    OUT("/", 1);
    OUT("attrs", strlen("attrs"));
    if (out->buf_size >= KND_PATH_SIZE) return knd_LIMIT;

    err = knd_shared_set_marshall(task->idxs->attr_idx, NULL, out->buf, out->buf_size,
                                  attr_facet_marshall, NULL, &leaf, &num_leaves, task);
    KND_TASK_ERR("failed to build attr facet idx");

    return knd_OK;
}

static int marshall_name_mappings(struct kndRepoSnapshot *s, struct kndTask *task)
{
    int err;

    err = knd_shared_dict_marshall(task->idxs->class_name_idx,
                                  s->path, s->path_size, "class-name-idx", strlen("class-name-idx"),
                                  knd_class_name_marshall, NULL, task);
    KND_TASK_ERR("failed to marhall a class name idx");

    err = knd_shared_dict_marshall(task->idxs->attr_name_idx,
                                  s->path, s->path_size, "attr-name-idx", strlen("attr-name-idx"),
                                  knd_attr_name_marshall, NULL, task);
    KND_TASK_ERR("failed to marshall an attr name idx");

    return knd_OK;
}

static int marshall_content(struct kndRepoSnapshot *s, struct kndTask *task)
{
    struct kndStorageLeaf *leaf;
    size_t num_leaves = 0;
    struct kndOutput *out = task->out;
    int err;

    out->reset(out);
    OUT(s->path, s->path_size);
    OUT("/", 1);
    OUT("classes", strlen("classes"));
    if (out->buf_size >= KND_PATH_SIZE) return knd_LIMIT;

    err = knd_shared_set_marshall(task->idxs->class_idx, NULL, out->buf, out->buf_size,
                                  knd_class_marshall, NULL, &leaf, &num_leaves, task);
    KND_TASK_ERR("failed to build a class idx");

    return knd_OK;
}

static int marshall_strings(struct kndRepoSnapshot *s, struct kndTask *task)
{
    struct kndStorageLeaf *leaf;
    size_t num_leaves = 0;
    struct kndOutput *out = task->out;
    int err;

    out->reset(out);
    OUT(s->path, s->path_size);
    OUT("/", 1);
    OUT("strings", strlen("strings"));
    if (out->buf_size >= KND_PATH_SIZE) return knd_LIMIT;

    err = knd_shared_set_marshall(task->idxs->str_idx, NULL, out->buf, out->buf_size,
                                  knd_charseq_marshall, NULL, &leaf, &num_leaves, task);
    KND_TASK_ERR("failed to build a string idx");

    return knd_OK;
}

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
    KND_TASK_ERR("failed to make {path %.*s}", s->path_size, s->path);

    if (DEBUG_REPO_GSP_LEVEL_TMP) {
        knd_log(".. building a GSP {snapshot #%zu} of {repo %.*s {last-commit %zu}}",
                numid, repo->name_size, repo->name, last_commit_id);
    }

    err = marshall_name_mappings(s, task);
    KND_TASK_ERR("failed to marshall name mappings in {path %.*s}", s->path_size, s->path);

    err = marshall_content(s, task);
    KND_TASK_ERR("failed to marshall main content in {path %.*s}", s->path_size, s->path);

    err = marshall_attr_idx(s, task);
    KND_TASK_ERR("failed to marshall attr idx in {path %.*s}", s->path_size, s->path);

    /* global string dict storage 
       NB: shoud be exported last */
    err = marshall_strings(s, task);
    KND_TASK_ERR("failed to marshall strings in {path %.*s}", s->path_size, s->path);

    repo->snapshot_temp = s;
    return knd_OK;
}
