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
#include "knd_dict.h"
#include "knd_shared_set.h"
#include "knd_user.h"
#include "knd_query.h"
#include "knd_task.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_proc.h"
#include "knd_mempool.h"
#include "knd_cache.h"
#include "knd_state.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_REPO_GSP_LEVEL_0 0
#define DEBUG_REPO_GSP_LEVEL_1 0
#define DEBUG_REPO_GSP_LEVEL_2 0
#define DEBUG_REPO_GSP_LEVEL_3 0
#define DEBUG_REPO_GSP_LEVEL_TMP 1

#if 0
static int attr_facet_marshall(void *elem, void *ctx, struct kndStorageLeaf *leaf,
                              size_t *output_size, struct kndTask *task)
{
    struct kndAttrRef *ref = elem;
    struct kndAttr *attr = ref->attr;
    struct kndFacet *facet = attr->facet;
    struct kndSetRange *range = ctx;
    int err;

    if (DEBUG_REPO_GSP_LEVEL_TMP) {
        knd_log(">> building GSP for {cls %.*s {attr %.*s}} {leaf %zu}",
                attr->owner->name_size, attr->owner->name,
                attr->name_size, attr->name, leaf->numid);
    }

    if (!facet) {
        knd_log("-- no facet, no GSP payload");
        return knd_OK;
    }

    err = knd_facet_leaf_marshall(facet, attr->type, leaf, range, output_size, task);
    KND_TASK_ERR("failed to marshall attr facet");

    return knd_OK;
}
#endif

static int marshall_attr_idx(struct kndRepoSnapshot *s, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    //int err;

    assert (s->path_size > 0);

    out->reset(out);
    OUT(s->path, s->path_size);
    if (s->path[s->path_size - 1] != '/') {
        OUT("/", 1);
    }
    OUT("attrs", strlen("attrs"));
    if (out->buf_size >= KND_PATH_SIZE) return knd_LIMIT;

    //err = knd_set_marshall(task->idxs->attr_idx, NULL, out->buf, out->buf_size,
    //                              attr_facet_marshall, NULL, &leaf, &num_leaves, task);
    //KND_TASK_ERR("failed to build attr facet idx");

    return knd_OK;
}

static int marshall_cls_names(struct kndDict *name_idx, const char *path, size_t path_size,
                              struct kndTask *task)
{
    struct kndOutput *out = task->out;
    char buf[KND_PATH_SIZE + 1];
    size_t buf_size;
    int err;

    assert (path_size != 0);

    if (DEBUG_REPO_GSP_LEVEL_2) {
        knd_log(">> marshalling class names in {path %.*s} {num-items %zu}",
                path_size, path, name_idx->num_items);
    }

    out->reset(out);
    OUT(path, path_size);
    if (path[path_size - 1] != '/') {
        OUT("/", 1);
    }

    /* agent specific folder */
    OUTF("agent_%d", task->id);
    OUT("/", 1);
    OUT("cls-names/", strlen("cls-names/"));
    if (out->buf_size >= KND_PATH_SIZE) return knd_LIMIT;

    memcpy(buf, out->buf, out->buf_size);
    buf_size = out->buf_size;

    err = knd_dict_marshall(name_idx, NULL, buf, buf_size, knd_class_name_marshall, NULL, task);
    KND_TASK_ERR("failed to marhall cls names dict");

    return knd_OK;
}

static int marshall_attr_names(const char *path, size_t path_size, struct kndTask *task)
{
    //struct kndStorageLeaf *leaf;
    //size_t num_leaves = 0;
    struct kndOutput *out = task->out;
    //int err;

    assert (path_size != 0);

    out->reset(out);
    OUT(path, path_size);
    if (path[path_size - 1] != '/') {
        OUT("/", 1);
    }
    /* agent specific folder */
    OUTF("agent_%d", task->id);
    OUT("/", 1);
    OUT("attr-name-idx/", strlen("attr-name-idx/"));
    if (out->buf_size >= KND_PATH_SIZE) return knd_LIMIT;

    //memcpy(task->idxs->attr_name_idx_path, out->buf, out->buf_size);
    //task->idxs.attr_name_idx_path_size = out->buf_size;

    //err = knd_shared_set_marshall(task->idxs->attr_idx, NULL, out->buf, out->buf_size,
    //                              knd_attr_name_marshall, NULL, &leaf, &num_leaves, task);
    //KND_TASK_ERR("failed to marshall an attr name idx");
    return knd_OK;
}

static int marshall_name_mappings(const char *path, size_t path_size,
                                  struct kndTask *main_task, struct kndTask *task)
{
    int err;

    switch (main_task->type) {
    case KND_TASK_BULK_LOAD:
        err = marshall_cls_names(main_task->idxs.cls_name_idx, path, path_size, task);
        KND_TASK_ERR("failed to marshall cls names");

        //err = marshall_attr_names(path, path_size, task);
        //KND_TASK_ERR("failed to marshall attr names");
        break;
    default:
        break;
    }

    return knd_OK;
}

static int marshall_content(const char *path, size_t path_size,
                            struct kndTask *main_task, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    char buf[KND_PATH_SIZE + 1];
    size_t buf_size;
    int err;

    assert (path_size > 0);

    out->reset(out);
    OUT(path, path_size);
    if (path[path_size - 1] != '/') {
        OUT("/", 1);
    }

    /* agent specific folder */
    OUTF("agent_%d", task->id);
    OUT("/", 1);
    OUT("cls-content/", strlen("cls-content/"));
    if (out->buf_size >= KND_PATH_SIZE) return knd_LIMIT;

    memcpy(buf, out->buf, out->buf_size);
    buf_size = out->buf_size;

    switch (main_task->type) {
    case KND_TASK_BULK_LOAD:
        err = knd_set_marshall(main_task->idxs.cls_idx, NULL, buf, buf_size, knd_class_marshall, NULL, task);
        KND_TASK_ERR("failed to marshall a class idx");
        break;
    default:
        break;
    }

    //memcpy(task->idxs.cls_idx_path, path, path_size);
    //task->idxs.cls_idx_path_size = path_size;

    return knd_OK;
}

static int marshall_cache(const char *path, size_t path_size,
                          struct kndTask *main_task, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndSet *idx;
    struct kndTaskCache *cache = &main_task->cache;
    //struct kndRepoCache *cache = &main_task->repo->snapshot->cache;
    struct kndCacheItem *item;
    struct kndClassEntry *entry;
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size;
    char buf[KND_PATH_SIZE + 1];
    size_t buf_size;
    size_t count = 0;
    int err;

    assert (path_size > 0);

    if (!cache->num_cls_entries) return knd_OK;

    idx = task->cache.cls_idx;

    FOREACH (item, cache->cls_entries) {
        count++;
        entry = item->data;
        knd_uid_create(count, idbuf, &idbuf_size);

        err = knd_set_add(idx, idbuf, idbuf_size, (void*)entry, task);
        KND_TASK_ERR("failed to add a cached cls entry to a set idx");

        if (DEBUG_REPO_GSP_LEVEL_TMP) {
            knd_log(">> {cache-item %zu {num-hits %zu} {cls %.*s {id %.*s}}}",
                    count, item->num_hits, entry->name_size, entry->name,
                    entry->id_size, entry->id);
        }        
    }

    out->reset(out);
    OUT(path, path_size);
    if (path[path_size - 1] != '/') {
        OUT("/", 1);
    }

    OUTF("agent_%d", task->id);
    OUT("/", 1);
    OUT("cls-cache/", strlen("cls-cache/"));
    if (out->buf_size >= KND_PATH_SIZE) return knd_LIMIT;

    memcpy(buf, out->buf, out->buf_size);
    buf_size = out->buf_size;

    switch (main_task->type) {
    case KND_TASK_BULK_LOAD:
        err = knd_set_marshall(idx, NULL, buf, buf_size, knd_class_name_marshall, NULL, task);
        KND_TASK_ERR("failed to marshall a cls cache idx");
        break;
    default:
        break;
    }
    return knd_OK;
}

#if 0
static int marshall_strings(struct kndRepoSnapshot *s, struct kndTask *task)
{
    char path[KND_PATH_SIZE + 1];
    size_t path_size;
    struct kndOutput *out = task->out;
    int err;

    assert (s->path_size > 0);

    out->reset(out);
    OUT(s->path, s->path_size);
    if (s->path[s->path_size - 1] != '/') {
        OUT("/", 1);
    }
    /* agent specific folder */
    OUTF("agent_%d", task->id);
    OUT("/", 1);
    OUT("strings/", strlen("strings/"));
    if (out->buf_size >= KND_PATH_SIZE) return knd_LIMIT;

    memcpy(path, out->buf, out->buf_size);
    path_size = out->buf_size;

    if (DEBUG_REPO_GSP_LEVEL_2) {
        knd_log(".. marshall strings {num-elems %zu}", task->idxs->str_idx->num_elems);
    }

    err = knd_set_marshall(task->idxs->str_idx, NULL, path, path_size,
                           knd_charseq_marshall, NULL, task);
    KND_TASK_ERR("failed to marshall a string idx");

    memcpy(task->idxs->str_idx_path, path, path_size);
    task->idxs->str_idx_path_size = path_size;

    return knd_OK;
}
#endif

int knd_repo_build_snapshot(struct kndRepo *repo, struct kndTask *main_task, struct kndTask *task)
{
    struct kndRepoSnapshot *curr_snapshot, *s;
    size_t numid;
    size_t last_commit_id;    
    int err;

    curr_snapshot = repo->snapshot;
    last_commit_id = atomic_load_explicit(&curr_snapshot->num_commits, memory_order_relaxed);

    switch (curr_snapshot->state) {
    case KND_SNAPSHOT_INIT:
        numid = 0;
        break;
    default:
        numid = curr_snapshot->numid + 1;
    }

    if (DEBUG_REPO_GSP_LEVEL_TMP) {
        knd_log(".. building a GSP {snapshot #%zu} of {repo %.*s}",
                numid, repo->name_size, repo->name);
    }

    err = knd_repo_snapshot_new(&s, numid, last_commit_id, repo, task->role, task);
    KND_TASK_ERR("failed to create a repo snapshot");

    err = knd_mkpath((const char*)s->path, s->path_size, 0755, false);
    KND_TASK_ERR("failed to make {path %.*s}", s->path_size, s->path);

    err = marshall_name_mappings(s->path, s->path_size, main_task, task);
    KND_TASK_ERR("failed to marshall name mappings in {path %.*s}", s->path_size, s->path);

    err = marshall_content(s->path, s->path_size, main_task, task);
    KND_TASK_ERR("failed to marshall main content in {path %.*s}", s->path_size, s->path);

    err = marshall_cache(s->path, s->path_size, main_task, task);
    KND_TASK_ERR("failed to marshall cache in {path %.*s}", s->path_size, s->path);

    //err = marshall_attr_idx(s, task);
    //KND_TASK_ERR("failed to marshall attr idx in {path %.*s}", s->path_size, s->path);

    //err = marshall_strings(s, task);
    //KND_TASK_ERR("failed to marshall strings in {path %.*s}", s->path_size, s->path);

    repo->snapshot_temp = s;
    return knd_OK;
}
