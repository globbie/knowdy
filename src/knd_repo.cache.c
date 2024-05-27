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

#define DEBUG_REPO_CACHE_LEVEL_0 0
#define DEBUG_REPO_CACHE_LEVEL_1 0
#define DEBUG_REPO_CACHE_LEVEL_2 0
#define DEBUG_REPO_CACHE_LEVEL_3 0
#define DEBUG_REPO_CACHE_LEVEL_TMP 1

static int reindex_class(void *obj, const char *unused_var(elem_id),
                         size_t unused_var(elem_id_size),
                         size_t unused_var(count), void *elem)
{
    struct kndTask *task = obj;
    struct kndClassEntry *entry = elem;
    struct kndDict *class_name_idx = task->cache_swap->class_name_idx;
    int err;

    knd_log(".. reindex %.*s", entry->name_size, entry->name);

    // TODO: copy class entry

    err = knd_dict_set(task->class_name_idx, entry->name, entry->name_size, (void*)entry);
    KND_TASK_ERR("failed to assign {class %.*s} to cache idx",
                 entry->name_size, entry->name);

    return knd_OK;
}

int knd_repo_rebuild_cache(struct kndRepo *repo, struct kndTask *task)
{
    struct kndMemPool *mempool = task->cache_mempool;
    struct kndRepoCache *cache;
    struct kndDict *class_name_idx;
    int err;

    if (DEBUG_REPO_CACHE_LEVEL_TMP) {
        knd_log(".. rebuilding cache for {repo %.*s}",
                repo->name_size, repo->name);
    }

    err = knd_repo_cache_new(mempool, &cache);
    KND_TASK_ERR("failed to alloc a repo cache");

    err = knd_dict_new(&class_name_idx, mempool, KND_SMALL_DICT_SIZE);
    KND_TASK_ERR("failed to alloc a class name idx");
    cache->class_name_idx = class_name_idx;
    task->cache_swap = cache;

    // TODO sort entries by usage

    // filter out the least recently used entries
    
    err = knd_shared_dict_map(repo->idxs.class_name_idx, reindex_class, (void*)task);
    KND_TASK_ERR("failed to reindex class name idx {repo %.*s}",
                 repo->name_size, repo->name);
        
    return knd_OK;
}
