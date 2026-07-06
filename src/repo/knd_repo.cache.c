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
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_proc.h"
#include "knd_mempool.h"
#include "knd_state.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_REPO_CACHE_LEVEL_0 0
#define DEBUG_REPO_CACHE_LEVEL_1 0
#define DEBUG_REPO_CACHE_LEVEL_2 0
#define DEBUG_REPO_CACHE_LEVEL_3 0
#define DEBUG_REPO_CACHE_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepoSnapshot *snapshot;
    const char *id;
    size_t id_size;
    const char *name;
    size_t name_size;
    struct kndClassEntry *entry;
};

#if 0
static bool detect_if_cacheable(struct kndClassEntry *unused_var(entry))
{
    //size_t num_requests = atomic_load_explicit(&entry->num_requests, memory_order_relaxed);
    //if (num_requests > 3) {
        //knd_log("{cls %.*s {num-requests %zu}}",
        //        entry->name_size, entry->name, num_requests);
        // TODO
    //    return true;
    //}
    return true;
}

static int build_cls_cache_item(void *elem, void *ctx)
{
    struct kndClassEntry *entry = elem;
    struct kndTask *task = ctx;
    struct kndClass *c;
    int err;

    if (!detect_if_cacheable(entry)) return knd_OK;

    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire {cls %.*s}", entry->name_size, entry->name);

    if (DEBUG_REPO_CACHE_LEVEL_3) {
        knd_log("made a cache copy of {cls %.*s {id %.*s}}",
                c->name_size, c->name, entry->id_size, entry->id);
    }
    return knd_OK;
}
#endif

static int index_cls_entry(void *elem, void *ctx_obj, struct kndTask *task)
{
    struct kndClassEntry *entry = elem;
    struct LocalContext *ctx = ctx_obj;
    struct kndRepoSnapshot *snapshot = ctx->snapshot;
    struct kndDict *name_idx = snapshot->cache.cls_name_idx;
    struct kndSet *cls_idx = snapshot->cache.cls_idx;
    int err;

    if (DEBUG_REPO_CACHE_LEVEL_2) {
        knd_log(".. indexing cls {entry {id %.*s}}", entry->id_size, entry->id);
    }

    err = knd_cls_entry_decode(entry, snapshot, task);
    KND_TASK_ERR("failed to decode {cls-entry %.*s}", entry->id_size, entry->id);

    entry->phase = KND_CLASS_CACHED;

    err = knd_dict_set(name_idx, entry->name, entry->name_size, (void*)entry, task);
    KND_TASK_ERR("failed to register cls entry {name %.*s} {err %d}",
                 entry->name_size, entry->name, err);

    err = knd_set_add(cls_idx, entry->id, entry->id_size, (void*)entry, task);
    KND_TASK_ERR("failed to update snapshot cls idx of {cls %.*s} {err %d}",
                 entry->name_size, entry->name, err);

    return knd_OK;
}

static int expand_cls_entry(void *elem, void *ctx_obj, struct kndTask *task)
{
    struct kndClassEntry *entry = elem;
    struct LocalContext *ctx = ctx_obj;
    struct kndRepoSnapshot *snapshot = ctx->snapshot;
    struct kndSet *cls_idx = snapshot->cache.cls_idx;
    struct kndClass *cls;
    int err;

    assert (entry != NULL);

    if (DEBUG_REPO_CACHE_LEVEL_2) {
        knd_log(".. expanding cls {entry %.*s {id %.*s}}",
                entry->name_size, entry->name, entry->id_size, entry->id);
    }

    ctx->entry = entry;
    ctx->id = entry->id;
    ctx->id_size = entry->id_size;

    /* read cs entry glosses */
    err = knd_set_fetch(cls_idx, entry->id, entry->id_size, knd_cls_entry_unmarshall,
                        ctx, (void**)&entry, task);
    switch (err) {
    case knd_OK:
        err = knd_cls_entry_decode(entry, snapshot, task);
        KND_TASK_ERR("failed to decode {cls-entry %.*s}", entry->id_size, entry->id);
        break;
    case knd_NO_MATCH:
        break;
    default:
        KND_TASK_ERR("failed to fetch a cls entry {cls {id %.*s}}", entry->id_size, entry->id);
    }

    err = knd_set_fetch(cls_idx, entry->id, entry->id_size, knd_cls_body_unmarshall, ctx, (void**)&cls, task);
    switch (err) {
    case knd_OK:
        err = knd_class_decode(cls, snapshot, task);
        KND_TASK_ERR("failed to decode {cls %.*s}", cls->name_size, cls->name);
        entry->cls = cls;
        break;
    case knd_NO_MATCH:
        return knd_NO_MATCH;
    default:
        KND_TASK_ERR("failed to fetch a {cls %.*s {id %.*s}} content {err %d}",
                     entry->name_size, entry->name, entry->id_size, entry->id, err);
        break;
    }

    if (DEBUG_REPO_CACHE_LEVEL_3) {
        knd_log("++ {cls-entry %.*s {id %.*s}} expanded",
                entry->name_size, entry->name, entry->id_size, entry->id);
    }
    return knd_OK;
}

static int read_cls_cache(struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndSet *cls_cache_idx = snapshot->cache.cls_cache_idx;
    struct kndSetStore *store = cls_cache_idx->store;
    struct kndStorageLeaf *leaf;
    size_t total_cache_items = 0;
    int err;

    assert (store != NULL);

    if (DEBUG_REPO_CACHE_LEVEL_2) {
        knd_log(".. unmarshalling cached cls entries {num-leaves %zu}", store->num_leaves);
    }

    for (size_t i = 0; i < store->num_leaves; i++) {
        leaf = store->leaves[i];

        err = knd_set_read_leaf(cls_cache_idx, leaf, NULL, knd_cls_entry_ref_unmarshall, NULL, task);
        KND_TASK_ERR("failed to read a cls entry idx {leaf %zu}", leaf->numid);

        total_cache_items += leaf->num_elems;
    }

    struct LocalContext ctx = {
        .task = task,
        .snapshot = snapshot
    };

    err = knd_set_map(cls_cache_idx, NULL, NULL, NULL, index_cls_entry, &ctx, task);
    KND_TASK_ERR("failed to index cached cls entries");

    err = knd_set_map(cls_cache_idx, NULL, NULL, NULL, expand_cls_entry, &ctx, task);
    KND_TASK_ERR("failed to expand cached cls entries");

    if (DEBUG_REPO_CACHE_LEVEL_3) {
        knd_log("++ cls cache initialized {total %zu}", total_cache_items);
    }
    return knd_OK;
}

int knd_repo_read_cache(struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    int err;

    if (DEBUG_REPO_CACHE_LEVEL_2) {
        knd_log(".. update cache from {snapshot #%zu {path %.*s}}",
                snapshot->numid, snapshot->path_size, snapshot->path);
    }

    task->type = KND_TASK_CACHE_UPDATE;

    err = read_cls_cache(snapshot, task);
    KND_TASK_ERR("failed to read cls entries cache in {snapshot #%zu {path %.*s}}",
                 snapshot->numid, snapshot->path_size, snapshot->path);

    /*
    err = read_str_idx(str_idx, task);
    KND_TASK_ERR("failed to read strings idx in {snapshot #%zu {path %.*s}}",
                 snapshot->numid, snapshot->path_size, snapshot->path);
    */
    
    return knd_OK;
}
