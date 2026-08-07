#include <gsl-parser.h>
#include <string.h>

#include "knd_task.h"
#include "knd_set.h"
#include "knd_mempool.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_proc_call.h"
#include "knd_class.h"
#include "knd_repo.h"

#define DEBUG_PROC_RESOLVE_LEVEL_0 0
#define DEBUG_PROC_RESOLVE_LEVEL_1 0
#define DEBUG_PROC_RESOLVE_LEVEL_2 0
#define DEBUG_PROC_RESOLVE_LEVEL_3 0
#define DEBUG_PROC_RESOLVE_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepoSnapshot *snapshot;
    struct kndProc *proc;
    struct kndProc *base;
};

int knd_repo_index_proc_arg(struct kndRepoSnapshot *unused_var(snapshot), struct kndProc *proc,
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

    return knd_OK;
}

static int inherit_arg(void *elem, void *ctx_obj, struct kndTask *task)
{
    struct kndProcArgRef *src_ref = elem;
    struct LocalContext  *ctx = ctx_obj;
    struct kndProc       *self = ctx->proc;
    struct kndProcArg    *arg    = src_ref->arg;
    struct kndProc       *base = arg->parent;
    struct kndProcArgRef *ref = NULL;
    int err;

    err = knd_set_get(self->arg_idx, arg->id, arg->id_size, (void**)&ref, task);
    if (!err) {
        if (DEBUG_PROC_RESOLVE_LEVEL_2) {
            knd_log("== \"%.*s\" (id:%.*s) arg is already registered in \"%.*s\"",
                    arg->name_size, arg->name, arg->id_size, arg->id, self->name_size, self->name);
        }
        if (src_ref->var)
            ref->var = src_ref->var;
        return knd_OK;
    }

    err = knd_proc_arg_ref_new(&ref, task->mempool);
    KND_TASK_ERR("failed to alloc a proc arg ref");
    ref->arg = arg;
    ref->var = src_ref->var;
    ref->proc = src_ref->proc;

    err = knd_set_add(self->arg_idx, arg->id, arg->id_size, (void*)ref, task);
    KND_TASK_ERR("failed to idx a proc arg ref");

    if (DEBUG_PROC_RESOLVE_LEVEL_3) {
        knd_log("..  \"%.*s\" of \"%.*s\" inherited by {proc %.*s}",
                arg->name_size, arg->name,
                base->name_size, base->name, self->name_size, self->name);
    }
    return knd_OK;
}

static int inherit_args(struct kndProc *self, struct kndProc *base,
                        struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    int err;

    if (!base->is_resolved) {
        err = knd_proc_resolve(base, snapshot, task);
        KND_TASK_ERR("failed to resolve base proc");
    }

    if (DEBUG_PROC_RESOLVE_LEVEL_2) {
        knd_log(".. {proc %.*s} to inherit args from {proc %.*s}",
                self->entry->name_size, self->entry->name, base->name_size, base->name);
    }
    struct LocalContext ctx = {
        .task = task,
        .proc = self,
        .base = base
    };

    err = knd_set_map(base->arg_idx, NULL, NULL, NULL,
                      inherit_arg, (void*)&ctx, task);
    KND_TASK_ERR("failed to inherit proc args");
    return knd_OK;
}

int knd_resolve_proc_ref(const char *name, size_t name_size,
                         struct kndProc *unused_var(base),
                         struct kndProcEntry **result, struct kndTask *task)
{
    struct kndProcEntry *entry;
    int err;

    if (DEBUG_PROC_RESOLVE_LEVEL_2)
        knd_log(".. resolving proc ref:  %.*s", name_size, name);

    err = knd_dict_get(task->idxs.proc_name_idx, name, name_size, (void**)&entry, task);
    if (err) {
        /*if (repo->base) {
            err = knd_get_proc(repo->base, name, name_size, result, task);
            KND_TASK_ERR("no such proc: \"%.*s\"", name_size, name);
            return knd_OK;
            }*/
        err = knd_NO_MATCH;
        KND_TASK_ERR("no such {proc %.*s}", name_size, name);
    }
    *result = entry;
    return knd_OK;
}

static int link_ancestor(struct kndProc *self, struct kndProcEntry *base_entry, struct kndTask *task)
{
    struct kndProcEntry *entry = self->entry;
    struct kndMemPool *mempool = task->mempool;
    struct kndProc *base;
    struct kndProcRef *ref;
    int err;

    base = base_entry->proc;

    /* check doublets */
    for (ref = entry->ancestors; ref; ref = ref->next) {
        if (ref->proc == base) return knd_OK;
    }

    if (DEBUG_PROC_RESOLVE_LEVEL_2)
        knd_log(".. %.*s proc to link an ancestor: \"%.*s\"",
                self->name_size, self->name, base->name_size, base->name);

    /*if (base_entry->repo != entry->repo) {
        prev_entry = knd_dict_get(proc_name_idx, base_entry->name, base_entry->name_size);
        if (prev_entry) {
            base = prev_entry->proc;
        } else {
            knd_log("-- proc \"%.*s\" not found in repo \"%.*s\"",
                    base_entry->name_size, base_entry->name,
                    self->entry->repo->name_size, self->entry->repo->name);

            err = knd_proc_clone(base_entry->proc, self->entry->repo, &base, task);
            KND_TASK_ERR("failed to clone a proc");
        }
    }
    */

    /* add an ancestor */
    err = knd_proc_ref_new(&ref, mempool);                                       RET_ERR();
    ref->proc = base;
    ref->entry = base->entry;
    ref->next = entry->ancestors;
    entry->ancestors = ref;
    entry->num_ancestors++;

    return knd_OK;
}

static int link_base(struct kndProc *self, struct kndProc *base, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndProcRef *ref, *baseref;
    struct kndProcEntry *entry = self->entry;
    bool parent_linked = false;
    int err;

    FOREACH (baseref, base->entry->ancestors) {
        err = link_ancestor(self, baseref->entry, task);                          RET_ERR();
    }

    if (!parent_linked) {
        /* register a parent */
        err = knd_proc_ref_new(&ref, mempool);
        KND_TASK_ERR("mempool failed to alloc kndProcRef");
        ref->proc = base;
        ref->entry = base->entry;
        ref->next = entry->ancestors;
        entry->ancestors = ref;
        entry->num_ancestors++;
    }
    return knd_OK;
}

static int resolve_bases(struct kndProc *self, struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndProcEntry *entry = self->entry;
    struct kndProcVar *base;
    int err;

    assert(!self->base_is_resolved);
    if (self->base_resolving_in_progress) {
        err = knd_FAIL;
        KND_TASK_ERR("vicious circle detected while resolving the bases of \"%.*s\"", entry->name_size, entry->name);
    }
    self->base_resolving_in_progress = true;

    FOREACH (base, self->bases) {
        if (!base->proc) {
            err = knd_get_proc(snapshot, base->name, base->name_size, &base->proc, task);
            KND_TASK_ERR("failed to resolve base proc");
        }
        if (!base->proc->is_resolved) {
            err = knd_proc_resolve(base->proc, snapshot, task);
            KND_TASK_ERR("failed to resolve proc \"%.*s\"", base->proc->name_size, base->proc->name);
        }
        err = link_base(self, base->proc, task);
        KND_TASK_ERR("failed to link base proc");
    }
    self->base_is_resolved = true;
    return knd_OK;
}

int knd_proc_resolve(struct kndProc *self, struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndProcArg *arg = NULL;
    struct kndProcArgVar *var = NULL;
    struct kndProcArgRef *arg_ref;
    struct kndProcVar *base;
    int err;

    if (DEBUG_PROC_RESOLVE_LEVEL_2)
        knd_log(".. resolving proc \"%.*s\"", self->name_size, self->name);

    if (self->resolving_in_progress) {
        knd_log("-- vicious circle detected in \"%.*s\"", self->name_size, self->name);
        return knd_FAIL;
    }
    self->resolving_in_progress = true;

    if (!self->arg_idx) {
        err = knd_set_new(&self->arg_idx, KND_SET_STORE_MEMONLY, task->mempool);
        RET_ERR();
    }

    FOREACH (arg, self->args) {
        err = knd_proc_arg_resolve(arg, snapshot, task);
        KND_TASK_ERR("failed to resolve a proc arg");

        err = knd_repo_index_proc_arg(snapshot, self, arg, task);
        KND_TASK_ERR("failed to register a proc arg");

        // local index 
        err = knd_proc_arg_ref_new(&arg_ref, task->mempool);
        KND_TASK_ERR("failed to alloc an arg ref");
        arg_ref->arg = arg;
        arg_ref->proc = self;
        err = knd_set_add(self->arg_idx, arg->id, arg->id_size, (void*)arg_ref, task);
        KND_TASK_ERR("failed to idx an arg ref");
    }

    if (!self->base_is_resolved) {
        err = resolve_bases(self, snapshot, task);
        KND_TASK_ERR("failed to resolve base procs");
    }

    /* arg inheritance */
    for (base = self->bases; base; base = base->next) {
        err = inherit_args(self, base->proc, snapshot, task);
        KND_TASK_ERR("failed to inherit args");

        for (var = base->args; var; var = var->next) {
            err = knd_resolve_proc_arg_var(self, var, snapshot, task);
            KND_TASK_ERR("failed to resolve proc arg var \"%.*s\"", var->name_size, var->name);
        }
    }
    if (self->result_classname_size) {
        err = knd_get_cls_entry_by_name(snapshot, self->result_classname, self->result_classname_size,
                                        &self->result, task);
        KND_TASK_ERR("no such {cls %.*s}", self->result_classname_size, self->result_classname);
        //knd_log("EFFECT: %.*s", self->result_classname_size, self->result_classname);
    }
    //   if (self->proc_call) {
    //     err = resolve_proc_call(self);                                            RET_ERR();
    //}
    
    self->is_resolved = true;
    return knd_OK;
}

