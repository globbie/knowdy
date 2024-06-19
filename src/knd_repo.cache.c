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
#include "knd_shared_dict.h"
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

static bool detect_if_cacheable(struct kndClassEntry *entry)
{
    size_t num_requests = atomic_load_explicit(&entry->num_requests, memory_order_relaxed);
    if (num_requests) {
        //knd_log("{class %.*s {num-requests %zu}}",
        //        entry->name_size, entry->name, num_requests);
        // TODO
        return true;
    }
    return false;
}

static int reindex_class(void *obj, const char *unused_var(elem_id),
                         size_t unused_var(elem_id_size),
                         size_t unused_var(count), void *elem)
{
    struct kndTask *task = obj;
    struct kndClassEntry *orig_entry = elem;
    struct kndClassEntry *entry;
    struct kndRepoSnapshot *snapshot = task->repo->snapshot_temp;
    struct kndMemPool *mempool = task->mempool;
    assert (snapshot != NULL);

    struct kndSharedDict *class_name_idx = snapshot->idxs.class_name_idx;
    struct kndSharedSet  *class_idx = snapshot->idxs.class_idx;
    int err;

    if (DEBUG_REPO_CACHE_LEVEL_3) {
        knd_log(".. reindex %.*s", orig_entry->name_size, orig_entry->name);
    }

    err = knd_class_entry_copy(orig_entry, &entry, mempool, task);
    KND_TASK_ERR("failed to make a class entry copy");

    err = knd_shared_dict_set(class_name_idx, entry->name, entry->name_size, (void*)entry, NULL, false);
    KND_TASK_ERR("failed to assign {class %.*s} to cache class name idx", entry->name_size, entry->name);

    err = knd_shared_set_add(class_idx, entry->id, entry->id_size, (void*)entry);
    KND_TASK_ERR("failed to assign {class %.*s} to cache class idx", entry->name_size, entry->name);

    return knd_OK;
}

static int build_cache_item(void *obj, const char *unused_var(elem_id),
                            size_t unused_var(elem_id_size),
                            size_t unused_var(count), void *elem)
{
    struct kndTask *task = obj;
    struct kndClassEntry *orig_entry = elem;
    struct kndClassEntry *entry;
    struct kndRepoSnapshot *snapshot = task->repo->snapshot_temp;
    struct kndMemPool *cache_mempool = task->cache_mempool;
    struct kndClass *c, *c_copy = NULL;

    assert (snapshot != NULL);
    struct kndSharedSet *class_idx = snapshot->idxs.class_idx;
    int err;

    /* no caching */
    if (!detect_if_cacheable(orig_entry)) return knd_OK;

    if (DEBUG_REPO_CACHE_LEVEL_2) {
        knd_log(".. cache copy of {class %.*s}", orig_entry->name_size, orig_entry->name);
    }

    err = knd_class_acquire(orig_entry, &c, task);
    KND_TASK_ERR("failed to acquire class %.*s", orig_entry->name_size, orig_entry->name);

    /* NB: using cache mempool */
    err = knd_class_copy(c, &c_copy, cache_mempool, task);
    KND_TASK_ERR("failed to make a class copy");

    err = knd_shared_set_get(class_idx, orig_entry->id, orig_entry->id_size, (void**)&entry);
    KND_TASK_ERR("no such entry {class %.*s}", orig_entry->name_size, orig_entry->name);
    c_copy->entry = entry;
    entry->cached_version = c_copy;

    return knd_OK;
}

int knd_repo_cache_update(struct kndRepo *repo, struct kndTask *task)
{
    struct kndRepoIndices *idxs = task->idxs;
    int err;

    if (DEBUG_REPO_CACHE_LEVEL_TMP) {
        knd_log(".. rebuilding cache for {repo %.*s}", repo->name_size, repo->name);
    }

    // TODO sort entries by usage

    // filter out the least recently used entries

    task->repo = repo;

    err = knd_shared_set_map(idxs->class_idx, reindex_class, (void*)task);
    KND_TASK_ERR("failed to reindex class idx in {repo %.*s}", repo->name_size, repo->name);

    err = knd_shared_set_map(idxs->class_idx, build_cache_item, (void*)task);
    KND_TASK_ERR("failed to build class cache for {repo %.*s}", repo->name_size, repo->name);

    return knd_OK;
}
