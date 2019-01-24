#include <gsl-parser.h>
#include <string.h>

#include "knd_task.h"
#include "knd_set.h"
#include "knd_mempool.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_class.h"
#include "knd_repo.h"

#define DEBUG_PROC_RESOLVE_LEVEL_0 0
#define DEBUG_PROC_RESOLVE_LEVEL_1 0
#define DEBUG_PROC_RESOLVE_LEVEL_2 0
#define DEBUG_PROC_RESOLVE_LEVEL_3 0
#define DEBUG_PROC_RESOLVE_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
    struct kndProc *proc;
};

static int inherit_arg(void *obj,
                        const char *unused_var(elem_id),
                        size_t unused_var(elem_id_size),
                        size_t unused_var(count),
                        void *elem)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    struct kndProc *self = ctx->proc;
    struct kndSet     *arg_idx = self->arg_idx;
    struct kndProcArgRef *src_ref = elem;
    struct kndProcArg    *arg    = src_ref->arg;
    struct kndProcArgRef *ref;
    int err;

    if (DEBUG_PROC_RESOLVE_LEVEL_TMP) 
        knd_log("..  \"%.*s\" proc arg inherited by %.*s..",
                arg->name_size, arg->name,
                self->name_size, self->name);

    err = knd_proc_arg_ref_new(mempool, &ref);                                        RET_ERR();
    ref->arg = arg;
    //ref->arg_var = src_ref->arg_var;
    ref->proc = src_ref->proc;

    err = arg_idx->add(arg_idx,
                       arg->id, arg->id_size,
                       (void*)ref);                                              RET_ERR();
    return knd_OK;
}

static int inherit_args(struct kndProc *self,
                        struct kndProc *base,
                        struct kndRepo *unused_var(repo),
                        struct kndTask *task)
{
    int err;

    if (!base->is_resolved) {
        err = knd_proc_resolve(base, task);                                      RET_ERR();
    }

    if (DEBUG_PROC_RESOLVE_LEVEL_TMP) {
        knd_log(".. \"%.*s\" proc to inherit args from \"%.*s\"..",
                self->entry->name_size, self->entry->name,
                base->name_size, base->name);
    }

    struct LocalContext ctx = {
        .task = task,
        .proc = self
    };

    err = base->arg_idx->map(base->arg_idx,
                              inherit_arg,
                              (void*)&ctx);                                        RET_ERR();
    return knd_OK;
}

static int resolve_parents(struct kndProc *self,
                           struct kndRepo *repo,
                           struct kndTask *task)
{
    struct kndProcVar *base;
    struct kndProc *proc;
    //struct kndProcArg *arg;
    //struct kndProcArgEntry *arg_entry;
    //struct kndProcArgVar *arg_item;
    int err;

    if (DEBUG_PROC_RESOLVE_LEVEL_TMP)
        knd_log(".. resolve parent procs of \"%.*s\"..",
                self->name_size, self->name);

    /* resolve refs  */
    for (base = self->bases; base; base = base->next) {
        if (DEBUG_PROC_RESOLVE_LEVEL_TMP)
            knd_log("\n.. \"%.*s\" proc to get its parent: \"%.*s\"..",
                    self->name_size, self->name,
                    base->name_size, base->name);

        err = knd_get_proc(repo,
                           base->name, base->name_size, &proc);                 RET_ERR();
        if (proc == self) {
            knd_log("-- self reference detected in \"%.*s\" :(",
                    base->name_size, base->name);
            return knd_FAIL;
        }
        base->proc = proc;

        err = inherit_args(self, base->proc, repo, task);                                     RET_ERR();

    }
    return knd_OK;
}

int knd_resolve_proc_ref(struct kndClass *self,
                         const char *name, size_t name_size,
                         struct kndProc *unused_var(base),
                         struct kndProc **result,
                         struct kndTask *unused_var(task))
{
    struct kndProc *proc;
    int err;

    if (DEBUG_PROC_RESOLVE_LEVEL_2)
        knd_log(".. resolving proc ref:  %.*s", name_size, name);

    err = knd_get_proc(self->entry->repo,
                       name, name_size, &proc);                                   RET_ERR();

    /*c = dir->conc;
    if (!c->is_resolved) {
        err = knd_class_resolve(c);                                                RET_ERR();
    }

    if (base) {
        err = is_base(base, c);                                                   RET_ERR();
    }
    */

    *result = proc;

    return knd_OK;
}

int knd_proc_resolve(struct kndProc *self,
                     struct kndTask *task)
{
    struct kndRepo *repo = self->entry->repo;
    struct kndProcArg *arg = NULL;
    struct kndProcArgRef *arg_ref;
    int err;

    if (DEBUG_PROC_RESOLVE_LEVEL_TMP)
        knd_log(".. resolving PROC: %.*s",
                self->name_size, self->name);

    if (!self->arg_idx) {
        err = knd_set_new(task->mempool, &self->arg_idx);                         RET_ERR();
    }

    for (arg = self->args; arg; arg = arg->next) {
        err = knd_proc_arg_resolve(arg, repo);                                    RET_ERR();

        /* no conflicts detected, register a new arg in repo */
        err = knd_repo_index_proc_arg(repo, self, arg, task);                     RET_ERR();

        /* local index */
        err = knd_proc_arg_ref_new(task->mempool, &arg_ref);                      RET_ERR();
        arg_ref->arg = arg;
        arg_ref->proc = self;

        err = self->arg_idx->add(self->arg_idx,
                                 arg->id, arg->id_size,
                                 (void*)arg_ref);                                 RET_ERR();
    }

    if (self->bases) {
        err = resolve_parents(self, repo, task);                                  RET_ERR();
    }

    //   if (self->proc_call) {
    //     err = resolve_proc_call(self);                                            RET_ERR();
    //}
    
    self->is_resolved = true;

    return knd_OK;
}
