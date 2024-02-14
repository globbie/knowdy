#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

#include <gsl-parser.h>

#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_proc_call.h"
#include "knd_class.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_mempool.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_text.h"
#include "knd_dict.h"
#include "knd_repo.h"
#include "knd_shard.h"
#include "knd_user.h"
#include "knd_output.h"

#define DEBUG_PROC_LEVEL_0 0
#define DEBUG_PROC_LEVEL_1 0
#define DEBUG_PROC_LEVEL_2 0
#define DEBUG_PROC_LEVEL_3 0
#define DEBUG_PROC_LEVEL_TMP 1

struct LocalContext {
    struct kndRepo *repo;
    struct kndTask *task;
    struct kndProc *proc;
};

static void proc_base_str(struct kndProcVar *self,
                          size_t depth)
{
    knd_log("%*s  {is %.*s}", depth * KND_OFFSET_SIZE, "",
            self->name_size, self->name);
}

void knd_proc_str(struct kndProc *self, size_t depth)
{
    struct kndText *tr;
    struct kndProcArg *arg;
    struct kndProcCall *call;
    struct kndProcVar *base;

    knd_log("%*s{proc %.*s  {_id %.*s}",
            depth * KND_OFFSET_SIZE, "",
            self->name_size, self->name,
            self->entry->id_size, self->entry->id);

    for (tr = self->tr; tr; tr = tr->next) {
        knd_log("%*s  {%.*s %.*s}", (depth + 1) * KND_OFFSET_SIZE, "",
                tr->locale_size, tr->locale, tr->seq->val_size, tr->seq->val);
    }

    for (base = self->bases; base; base = base->next) {
        proc_base_str(base, depth + 1);
    }

    if (self->result_classname_size) {
        knd_log("%*s    {result class:%.*s}", depth * KND_OFFSET_SIZE, "",
                self->result_classname_size, self->result_classname);
    }

    if (self->args) {
        knd_log("%*s    [arg", depth * KND_OFFSET_SIZE, "");
        for (arg = self->args; arg; arg = arg->next) {
            knd_proc_arg_str(arg, depth + 2);
        }
        knd_log("%*s    ]", depth * KND_OFFSET_SIZE, "");
    }

    if (self->calls) {
        knd_log("%*s    [call", depth * KND_OFFSET_SIZE, "");
        for (call = self->calls; call; call = call->next) {
            knd_proc_call_str(call, depth + 2);
        }
        knd_log("%*s    ]", depth * KND_OFFSET_SIZE, "");
    }

    knd_log("%*s}",
            depth * KND_OFFSET_SIZE, "");
}

int knd_proc_export(struct kndProc *self, knd_format format, struct kndTask *task, struct kndOutput *out)
{
    int err;

    switch (format) {
    case KND_FORMAT_JSON:
        err = knd_proc_export_JSON(self, task, out, 0);
        if (err) return err;
        break;
        /*case KND_FORMAT_GSP:
        err = knd_proc_export_GSP(self, task, out);
        if (err) return err;
        break;*/
    case KND_FORMAT_SVG:
        err = knd_proc_export_SVG(self, task, out);
        if (err) return err;
        break;
    default:
        err = knd_proc_export_GSL(self, task, false, 0);
        if (err) return err;
        break;
    }
    return knd_OK;
}

int knd_proc_get_arg(struct kndProc *self, const char *name, size_t name_size, struct kndProcArgRef **result)
{
    struct kndProcArgRef *ref;
    struct kndProcArg *arg = NULL;

    assert(self->entry != NULL);

    struct kndSharedDict *arg_name_idx = self->entry->repo->proc_arg_name_idx;
    struct kndSet *arg_idx = self->arg_idx;
    int err;

    if (DEBUG_PROC_LEVEL_2) {
        knd_log(".. \"%.*s\" proc (repo: %.*s) to select arg \"%.*s\" ",
                self->name_size, self->name,
                self->entry->repo->name_size, self->entry->repo->name, name_size, name);
    }

    ref = knd_shared_dict_get(arg_name_idx, name, name_size);
    if (!ref) {
        if (self->entry->repo->base) {
            arg_name_idx = self->entry->repo->base->proc_arg_name_idx;
            ref = knd_shared_dict_get(arg_name_idx, name, name_size);
        }
        if (!ref) {
            if (DEBUG_PROC_LEVEL_2)
                knd_log("-- no such proc arg: \"%.*s\"", name_size, name);
            return knd_NO_MATCH;
        }
    }

    /* iterating over synonymous attrs */
    for (; ref; ref = ref->next) {
        arg = ref->arg;
        if (ref->proc == self) break;

        err = knd_proc_is_base(ref->proc, self);
        if (!err) break;
    }
    if (!arg) return knd_NO_MATCH;

    err = arg_idx->get(arg_idx, arg->id, arg->id_size, (void**)&ref);
    if (err) return knd_NO_MATCH;

    *result = ref;
    return knd_OK;
}

int knd_proc_is_base(struct kndProc *self, struct kndProc *child)
{
    struct kndProcEntry *entry = child->entry;
    struct kndProcRef *ref;
    struct kndProc *proc;

    if (DEBUG_PROC_LEVEL_2) {
        knd_log(".. check inheritance: %.*s (repo:%.*s) [resolved: %d] => "
                " %.*s (repo:%.*s) num ancestors:%zu [base resolved:%d  resolved:%d]",
                child->name_size, child->name,
                child->entry->repo->name_size, child->entry->repo->name,
                child->is_resolved,
                self->entry->name_size, self->entry->name,
                self->entry->repo->name_size, self->entry->repo->name,
                self->entry->num_ancestors,
                self->base_is_resolved, self->is_resolved);
    }

    for (ref = entry->ancestors; ref; ref = ref->next) {
         proc = ref->proc;
         if (DEBUG_PROC_LEVEL_2) {
             knd_log("  => is: %.*s (repo:%.*s)  base resolved:%d",
                     proc->name_size, proc->name,
                     proc->entry->repo->name_size, proc->entry->repo->name,
                     proc->base_is_resolved);
         }
         if (proc == self) {
             return knd_OK;
         }
         if (self->entry->orig) {
             if (self->entry->orig->proc == proc)
                 return knd_OK;
         }
    }

    if (DEBUG_PROC_LEVEL_2)
        knd_log("-- no inheritance from  \"%.*s\" to \"%.*s\" :(",
                self->entry->name_size, self->entry->name,
                child->name_size, child->name);
    return knd_FAIL;
}

int knd_get_proc(struct kndRepo *repo, const char *name, size_t name_size,
                 struct kndProc **result, struct kndTask *task)
{
    struct kndProcEntry *entry;
    struct kndProc *proc;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. \"%.*s\" repo to get proc: \"%.*s\"..",
                repo->name_size, repo->name, name_size, name);

    entry = knd_shared_dict_get(repo->proc_name_idx, name, name_size);
    if (!entry) {
        if (repo->base) {
            err = knd_get_proc(repo->base, name, name_size, result, task);
            KND_TASK_ERR("no such proc: \"%.*s\"", name_size, name);
            return knd_OK;
        }
        err = knd_NO_MATCH;
        KND_TASK_ERR("no such proc: \"%.*s\"", name_size, name);
    }

    if (entry->phase == KND_REMOVED) {
        KND_TASK_LOG("\"%s\" proc was removed", name);
        return knd_NO_MATCH;
    }

    if (entry->proc) {
        proc = entry->proc;
        entry->phase = KND_SELECTED;
        *result = proc;
        return knd_OK;
    }

    // TODO: defreeze
    return knd_FAIL;
}

int knd_get_proc_entry(struct kndRepo *repo, const char *name, size_t name_size,
                       struct kndProcEntry **result, struct kndTask *task)
{
    struct kndProcEntry *entry;
    struct kndSharedDict *proc_name_idx = repo->proc_name_idx;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. \"%.*s\" repo to get proc entry: \"%.*s\"", repo->name_size, repo->name, name_size, name);

    entry = knd_shared_dict_get(proc_name_idx, name, name_size);
    if (!entry) {
        if (DEBUG_PROC_LEVEL_2)
            knd_log("-- no local proc \"%.*s\" found in repo %.*s",
                    name_size, name, repo->name_size, repo->name);
        /* check base repo */
        if (repo->base) {
            err = knd_get_proc_entry(repo->base, name, name_size, result, task);
            if (err) return err;
            return knd_OK;
        }
        return knd_NO_MATCH;
    }

    *result = entry;
    return knd_OK;
}

static int commit_state(struct kndProc *self,
                        struct kndStateRef *children,
                        knd_state_phase phase,
                        struct kndState **result,
                        struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndState *head, *state;
    int err;

    err = knd_state_new(mempool, &state);
    KND_TASK_ERR("proc state alloc failed");
    state->phase = phase;
    state->commit = task->ctx->commit;
    state->children = children;

    do {
       head = atomic_load_explicit(&self->states,
                                   memory_order_relaxed);
       state->next = head;
    } while (!atomic_compare_exchange_weak(&self->states, &head, state));
 
    // TODO inform your ancestors 

    *result = state;
    return knd_OK;
}

int knd_proc_entry_clone(struct kndProcEntry *self, struct kndRepo *repo, struct kndProcEntry **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    if (task->user_ctx)
        mempool = task->shard->user->mempool;
    struct kndProcEntry *entry;
    struct kndSharedDict *name_idx = repo->proc_name_idx;
    struct kndSharedDictItem *item = NULL;
    //struct kndProcRef *ref, *tail_ref, *r;
    // struct kndSet *proc_idx = repo->proc_idx;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. cloning proc entry %.*s (%.*s) to repo \"%.*s\"",
                self->name_size, self->name, self->repo->name_size, self->repo->name,
                repo->name_size, repo->name);

    err = knd_proc_entry_new(mempool, &entry);
    KND_TASK_ERR("failed to alloc a proc entry");

    entry->repo = repo;
    entry->orig = self;
    entry->proc = self->proc;
    entry->name = self->name;
    entry->name_size = self->name_size;
    entry->ancestors = self->ancestors;
    entry->num_ancestors = self->num_ancestors;
    entry->descendants = self->descendants;

    err = knd_shared_dict_set(name_idx, entry->name,  entry->name_size,
                              (void*)entry, mempool, task->ctx->commit, &item, false);
    KND_TASK_ERR("failed to register proc \"%.*s\"", entry->name_size, entry->name);
    entry->dict_item = item;

    *result = entry;
    return knd_OK;
}

int knd_proc_commit_state(struct kndProc *self,
                          knd_state_phase phase,
                          struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndStateRef *state_ref;
    struct kndCommit *commit = task->ctx->commit;
    struct kndState *state = NULL;
    int err;

    if (DEBUG_PROC_LEVEL_2) {
        knd_log(".. \"%.*s\" proc (repo:%.*s) to commit its state (phase:%d)",
                self->name_size, self->name,
                self->entry->repo->name_size, self->entry->repo->name, phase);
    }

    err = commit_state(self, NULL, phase, &state, task);
    RET_ERR();

    /*switch (phase) {
    case KND_CREATED:
    case KND_REMOVED:
        break;
    case KND_UPDATED:
        break;
    default:
        break;
        }*/

    /* register state */
    err = knd_state_ref_new(mempool, &state_ref);                                 RET_ERR();
    state_ref->state = state;
    state_ref->type = KND_STATE_PROC;
    state_ref->obj = self->entry;

    state_ref->next = commit->proc_state_refs;
    commit->proc_state_refs = state_ref;
    return knd_OK;
}

int knd_proc_var_new(struct kndMemPool *mempool, struct kndProcVar **result)
{
    void *page;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndProcVar));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndProcVar));
    *result = page;
    return knd_OK;
}

int knd_proc_idx_new(struct kndMemPool *mempool, struct kndProcIdx **result)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndProcIdx));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndProcIdx));
    *result = page;
    return knd_OK;
}

int knd_proc_entry_new(struct kndMemPool *mempool, struct kndProcEntry **result)
{
    void *page;
    int err;
    assert(mempool->small_x2_page_size >= sizeof(struct kndProcEntry));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X2, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndProcEntry));
    *result = page;
    return knd_OK;
}

int knd_proc_ref_new(struct kndMemPool *mempool, struct kndProcRef **result)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndProcRef));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndProcRef));
    *result = page;
    return knd_OK;
}

int knd_proc_new(struct kndMemPool *mempool, struct kndProc **result)
{
    void *page;
    int err;
    assert(mempool->small_x2_page_size >= sizeof(struct kndProc));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X2, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndProc));
    *result = page;
    return knd_OK;
}
