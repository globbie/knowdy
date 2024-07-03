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
    if (num_requests > 5) {
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

    err = knd_shared_dict_set(class_name_idx, entry->name, entry->name_size, (void*)entry);
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
    struct kndClassEntry *entry = elem;
    struct kndRepoSnapshot *snapshot = task->snapshot;
    struct kndMemPool *cache_mempool = task->mempool;
    struct kndClass *c, *c_copy = NULL;
    struct kndSharedSet *class_idx = snapshot->idxs.class_idx;
    struct kndStorageLeaf *leaf = NULL;
    int err;

    if (!detect_if_cacheable(entry)) return knd_OK;

    if (DEBUG_REPO_CACHE_LEVEL_TMP) {
        knd_log(".. make a cache copy of {class %.*s {id %.*s}}",
                entry->name_size, entry->name, entry->id_size, entry->id);
    }

    err = knd_shared_set_find_leaf(class_idx, entry->id, entry->id_size, &leaf, task);
    KND_TASK_ERR("no storage leaf found for unmarshalling class entry %.*s",
                 entry->id_size, entry->id);

    task->payload = (void*)entry;
    err = knd_storage_leaf_read_elem(leaf, entry->id, entry->id_size,
                                     knd_class_unmarshall, (void**)&c, task);
    KND_TASK_ERR("failed to unmarshall {class %.*s}", entry->name_size, entry->name);

    c->entry = entry;
    c->name = entry->name;
    c->name_size = entry->name_size;
    entry->cached_version = c;

    return knd_OK;
}

int knd_repo_cache_update(struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndSharedDict *class_name_idx = snapshot->idxs.class_name_idx;
    struct kndRepo *repo = snapshot->repo;
    int err;

    if (DEBUG_REPO_CACHE_LEVEL_TMP) {
        knd_log("\n.. rebuilding cache for {repo %.*s}", repo->name_size, repo->name);
    }

    // TODO sort entries by usage

    err = knd_shared_dict_map(class_name_idx, build_cache_item, (void*)task);
    KND_TASK_ERR("failed to build class cache for {repo %.*s}", repo->name_size, repo->name);

    return knd_OK;
}
