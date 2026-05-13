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

static int register_cls_name(void *elem, void *ctx, struct kndTask *task)
{
    struct kndClassEntry *entry = elem;
    struct kndDict *name_idx = ctx;
    int err;

    if (DEBUG_REPO_CACHE_LEVEL_TMP) {
        knd_log(".. register cls entry {name %.*s}", entry->name_size, entry->name);
    }

    err = knd_dict_set(name_idx, entry->name, entry->name_size, (void*)entry, task);
    KND_TASK_ERR("failed to register cls entry {name %.*s}", entry->name_size, entry->name);

    return knd_OK;
}

static int read_cls_cache(struct kndSet *cls_cache_idx, struct kndDict *name_idx, struct kndTask *task)
{
    struct kndStorageLeaf *leaf;
    int err;

    if (DEBUG_REPO_CACHE_LEVEL_TMP) {
        knd_log(".. unmarshalling cached cls entries {num-leaves %zu}", cls_cache_idx->num_leaves);
    }

    FOREACH (leaf, cls_cache_idx->leaves) {
        err = knd_set_read_leaf(cls_cache_idx, leaf, NULL, knd_class_entry_unmarshall, NULL, task);
        KND_TASK_ERR("failed to read a cls entry idx {leaf %zu}", leaf->numid);
    }

    err = knd_set_map(cls_cache_idx, NULL, NULL, NULL, register_cls_name, name_idx, task);
    KND_TASK_ERR("failed to index cls names");

    return knd_OK;
}

int knd_repo_read_cache(struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    int err;

    if (DEBUG_REPO_CACHE_LEVEL_TMP) {
        knd_log(".. update cache from {snapshot #%zu {path %.*s}}",
                snapshot->numid, snapshot->path_size, snapshot->path);
    }

    err = read_cls_cache(snapshot->cache.cls_cache_idx, snapshot->cache.cls_name_idx, task);
    KND_TASK_ERR("failed to read cls entries cache in {snapshot #%zu {path %.*s}}",
                 snapshot->numid, snapshot->path_size, snapshot->path);

    /*
    err = knd_dict_read(class_name_idx, knd_class_entry_unmarshall, NULL, task);
    KND_TASK_ERR("failed to read cls name idx in {snapshot #%zu {path %.*s}}",
                 snapshot->numid, snapshot->path_size, snapshot->path);
    */

    /*err = read_class_entries(class_idx, task);
    KND_TASK_ERR("failed to read class entries in {snapshot #%zu {path %.*s}}",
                 snapshot->numid, snapshot->path_size, snapshot->path);

    err = read_attr_name_idx(attr_name_idx, task);
    KND_TASK_ERR("failed to read attr name idx in {snapshot #%zu {path %.*s}}",
                 snapshot->numid, snapshot->path_size, snapshot->path);

    err = read_str_idx(str_idx, task);
    KND_TASK_ERR("failed to read strings idx in {snapshot #%zu {path %.*s}}",
                 snapshot->numid, snapshot->path_size, snapshot->path);
    */
    
    return knd_OK;
}

