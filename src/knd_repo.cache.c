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
    struct kndDict *class_name_idx = task->cache->class_name_idx;
    int err;

    knd_log(".. reindex %.*s", entry->name_size, entry->name);

    // TODO: copy class entry

    err = knd_dict_set(class_name_idx, entry->name, entry->name_size, (void*)entry);
    KND_TASK_ERR("failed to assign {class %.*s} to cache idx",
                 entry->name_size, entry->name);

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

    err = knd_shared_set_map(idxs->class_idx, reindex_class, (void*)task);
    KND_TASK_ERR("failed to reindex class idx in {repo %.*s}", repo->name_size, repo->name);

    return knd_OK;
}

int knd_repo_cache_new(struct kndMemPool *mempool, struct kndRepoCache **result)
{
    struct kndRepoCache *cache;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndRepoCache));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, (void**)&cache);
    if (err) return err;
    memset(cache, 0, sizeof(struct kndRepoCache));

    err = knd_set_new(&cache->class_idx, mempool);
    if (err) return err;

    err = knd_dict_new(&cache->class_name_idx, mempool, KND_MEDIUM_DICT_SIZE);
    if (err) return err;

    *result = cache;
    return knd_OK;
}
