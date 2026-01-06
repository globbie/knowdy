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
#include "knd_shared_dict.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_proc.h"
#include "knd_memblock.h"
#include "knd_mempool.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_REPO_LEVEL_0 0
#define DEBUG_REPO_LEVEL_1 0
#define DEBUG_REPO_LEVEL_2 0
#define DEBUG_REPO_LEVEL_3 0
#define DEBUG_REPO_LEVEL_TMP 1

void knd_repo_del(struct kndRepo *self)
{
    // TODO del snapshots
    free(self);
}

int knd_repo_index_proc_arg(struct kndRepo *repo, struct kndProc *proc,
                            struct kndProcArg *arg, struct kndTask *task)
{
    struct kndMemPool *mempool   = task->mempool;
    struct kndSet *arg_idx       = task->idxs.proc_arg_idx;
    struct kndDict *arg_name_idx = task->idxs.proc_arg_name_idx;
    struct kndProcArgRef *ref, *arg_ref, *next_arg_ref;
    int err;

    /* generate unique attr id */
    arg->numid = ++task->idxs.proc_arg_id_count;
    arg->numid++;
    knd_uid_create(arg->numid, arg->id, &arg->id_size);

    err = knd_proc_arg_ref_new(&arg_ref, mempool);
    if (err) {
        return err;
    }
    arg_ref->arg = arg;
    arg_ref->proc = proc;

    switch (task->type) {
    case KND_TASK_RESTORE:
        // fall through
    case KND_TASK_BULK_LOAD:

        // TODO
        err = knd_proc_get_arg(proc, arg->name, arg->name_size, &ref, task);

        err = knd_dict_get(arg_name_idx, arg->name, arg->name_size, (void**)&next_arg_ref, task);
        
        arg_ref->next = next_arg_ref;

        err = knd_dict_set(arg_name_idx, arg->name, arg->name_size, (void*)arg_ref, task);
        KND_TASK_ERR("failed to globally register {arg %.*s}", arg->name_size, arg->name);

        err = knd_set_add(arg_idx, arg->id, arg->id_size, (void*)arg_ref, task);
        KND_TASK_ERR("failed to globally register numid of {arg %.*s}", arg->name_size, arg->name);

        return knd_OK;
    default:
        break;
    }

    /* local task name idx */
    //err = knd_dict_set(task->idxs->proc_arg_name_idx, arg->name, arg->name_size, (void*)arg_ref);
    //KND_TASK_ERR("failed to register arg name %.*s", arg->name_size, arg->name);

    if (DEBUG_REPO_LEVEL_2) {
        knd_log("++ new primary {arg %.*s {id %.*s}} of {proc %.*s} {repo %.*s}",
                arg->name_size, arg->name, arg->id_size, arg->id,
                proc->name_size, proc->name, repo->name_size, repo->name);
    }
    return knd_OK;
}

int knd_conc_folder_new(struct kndConcFolder **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndConcFolder));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndConcFolder));
    *result = page;
    return knd_OK;
}

int knd_snapshot_build_path(struct kndRepoSnapshot *s, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndRepo *repo = s->repo;
    int err;

    out->reset(out);
    OUT(repo->path, repo->path_size);
    OUTF("snapshot_%zu/", s->numid);
    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("GSP path too long");
    }
    memcpy(s->path, out->buf, out->buf_size);
    s->path_size = out->buf_size;
    s->path[out->buf_size] = '\0';

    return knd_OK;
}

int knd_repo_snapshot_new(struct kndRepoSnapshot **result, size_t numid, size_t latest_commit_id,
                          struct kndRepo *repo, knd_agent_role_type role, struct kndTask *task)
{
    struct kndRepoSnapshot *s;
    struct kndMemPool *mempool = task->mempool;
    int err;

    s = calloc(1, sizeof(struct kndRepoSnapshot));
    if (!s) return knd_NOMEM;
    s->numid = numid;
    s->repo = repo;
    s->start_from_commit_id = latest_commit_id;
    s->role = role;

    err = knd_snapshot_build_path(s, task);
    KND_TASK_ERR("failed to build a default snapshot path");

    err = knd_set_new(&s->commit_idx, KND_SET_UNIQUE_VALUES, mempool);
    if (err) return err;
    s->max_journals = KND_MAX_JOURNALS;
    s->max_journal_size = KND_MAX_JOURNAL_SIZE;

    err = knd_set_new(&s->cache.str_idx, KND_SET_UNIQUE_VALUES, mempool);
    if (err) return err;
    err = knd_dict_new(&s->cache.str_dict, KND_MEDIUM_DICT_SIZE, mempool);
    if (err) return err;

    err = knd_set_new(&s->cache.cls_idx, KND_SET_UNIQUE_VALUES, mempool);
    if (err) return err;
    err = knd_dict_new(&s->cache.cls_name_idx, KND_HUGE_DICT_SIZE, mempool);
    if (err) return err;

    err = knd_set_new(&s->cache.attr_idx, KND_SET_UNIQUE_VALUES, mempool);
    if (err) return err;
    err = knd_dict_new(&s->cache.attr_name_idx, KND_MEDIUM_DICT_SIZE, mempool);
    if (err) return err;

    /* shared idxs */
    err = knd_shared_set_new(&s->idxs.cls_idx, mempool);
    if (err) return err;
    err = knd_shared_dict_new(&s->idxs.cls_name_idx, KND_MEDIUM_DICT_SIZE, mempool, false);
    if (err) return err;


    err = knd_shared_set_new(&s->idxs.proc_idx, mempool);
    if (err) return err;
    err = knd_shared_dict_new(&s->idxs.proc_name_idx, KND_MEDIUM_DICT_SIZE, mempool, false);
    if (err) return err;


    
    *result = s;
    return knd_OK;
}

int knd_repo_snapshot_activate(struct kndRepo *repo, struct kndRepoSnapshot **result, struct kndTask *task)
{
    struct kndRepoSnapshot *snapshot;
    int err;

    assert (repo->snapshot_temp != NULL);

    /* it is now safe to transfer all interim commits to new memory */
    err = knd_repo_transfer_commits(repo, task);
    KND_TASK_ERR("failed to transfer sys repo commits");

    err = knd_repo_save_meta(repo->snapshot_temp, task);
    KND_TASK_ERR("failed to update persistent repo meta");

    /* switching the snapshots */
    snapshot = repo->snapshot_temp;    
    repo->snapshot_temp = repo->snapshot;
    repo->snapshot = snapshot;

    *result = snapshot;
    return knd_OK;
}

int knd_repo_snapshot_fetch_memblock(struct kndRepoSnapshot *self,
                                     size_t space_required,
                                     struct kndMemBlock **result,
                                     struct kndTask *task)
{
    struct kndMemBlock *block, *curr_block;
    int err;

    if (space_required >= KND_MEMBLOCK_BUF_SIZE) return knd_LIMIT;

    if (!self->blocks) {
        err = knd_memblock_new(&block, 0, KND_MEMBLOCK_BUF_SIZE);
        KND_TASK_ERR("failed to alloc a memblock");
        self->blocks = block;
        *result = block;
        return knd_OK;
    }

    curr_block = self->blocks;
    if ((curr_block->capacity - curr_block->buf_size) >= space_required) {
        *result = curr_block;
        return knd_OK;
    }

    err = knd_memblock_new(&block, 0, KND_MEMBLOCK_BUF_SIZE);
    KND_TASK_ERR("failed to alloc a memblock");
    block->next = curr_block;
    self->blocks = block;
    self->num_blocks++;
    *result = block;
    return knd_OK;
}

void knd_repo_snapshot_del(struct kndRepoSnapshot *snapshot)
{
    struct kndMemBlock *block, *next_block;

    for (block = snapshot->blocks; block; block = next_block) {
        next_block = block->next;
        if (block->buf)
            free(block->buf);
        free(block);
    }
    free(snapshot);
}

int knd_repo_new(struct kndRepo **repo, const char *name, size_t name_size,
                 const char *path, size_t path_size,
                 const char *schema_path, size_t schema_path_size)
{
    struct kndRepo *self;

    if (name_size >= (KND_NAME_SIZE - 1)) return knd_LIMIT;

    self = calloc(1, sizeof(struct kndRepo));
    if (!self) return knd_NOMEM;

    memcpy(self->name, name, name_size);
    self->name_size = name_size;

    if (path_size) {
        if (path_size >= (KND_PATH_SIZE - 1)) return knd_LIMIT;

        memcpy(self->path, path, path_size);
        self->path_size = path_size;
        if (path[path_size - 1] != '/') {
            self->path[path_size] = '/';
            self->path_size++;
        }
    }

    /* check special repo names */
    switch (self->name[0]) {
    case '/': // base repo
    case '~': // user repo
        break;
    default:
        if (self->path_size + name_size >= (KND_PATH_SIZE - 1)) return knd_LIMIT;

        memcpy(self->path + self->path_size, name, name_size);
        self->path_size += name_size;

        if (self->path[self->path_size - 1] != '/') {
            self->path[self->path_size] = '/';
            self->path_size++;
        }
    }
    self->schema_path = schema_path;
    self->schema_path_size = schema_path_size;
 
    *repo = self;
    return knd_OK;
}
