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

int knd_repo_update_cache(struct kndRepoSnapshot *snapshot, struct kndTask *unused_var(task))
{
    struct kndDict *class_name_idx = snapshot->cache.class_name_idx;

    //struct kndSet *class_idx = snapshot->idxs.class_idx;
    //struct kndDict *attr_name_idx = snapshot->idxs.attr_name_idx;
    //struct kndSet *str_idx = snapshot->idxs.str_idx;
    //int err;

    if (DEBUG_REPO_CACHE_LEVEL_TMP) {
        knd_log(".. update cache from {snapshot %.*s}", snapshot->path_size, snapshot->path);
        knd_log(">> {cls-name-dict %zu}", class_name_idx->num_items);
    }

    // TODO pass a set of cached entries as cb_ctx

    /*err = knd_dict_read(class_name_idx, knd_class_entry_unmarshall, NULL, task);
    KND_TASK_ERR("failed to read class name idx in {snapshot #%zu {path %.*s}}",
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

    //err = knd_repo_cache_update(snapshot, task);
    //KND_TASK_ERR("failed to update a repo cache");
    
    return knd_OK;
}
