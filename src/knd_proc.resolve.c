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
    struct kndRepo *repo;
    struct kndProc *proc;
    struct kndProc *base;
};

static int inherit_arg(void *obj,
                       const char *unused_var(elem_id),
                       size_t unused_var(elem_id_size),
                       size_t unused_var(count),
                       void *elem)
{
    struct LocalContext  *ctx = obj;
    struct kndTask       *task = ctx->task;
    struct kndMemPool    *mempool = task->mempool;
    struct kndProc       *self = ctx->proc;
    struct kndSet        *arg_idx = self->arg_idx;
    struct kndProcArgRef *src_ref = elem;
    struct kndProcArg    *arg    = src_ref->arg;
    struct kndProc       *base = arg->parent;
    struct kndProcArgRef *ref;
    int err;

    if (DEBUG_PROC_RESOLVE_LEVEL_TMP) 
        knd_log("..  \"%.*s\" of \"%.*s\" inherited by \"%.*s\"",
                arg->name_size, arg->name, base->name_size, base->name, self->name_size, self->name);

    err = knd_proc_arg_ref_new(mempool, &ref);                                    RET_ERR();
    ref->arg = arg;
    //ref->arg_var = src_ref->arg_var;
    ref->proc = src_ref->proc;

    err = arg_idx->add(arg_idx, arg->id, arg->id_size, (void*)ref);                    RET_ERR();
    return knd_OK;
}

static int inherit_args(struct kndProc *self, struct kndProc *base, struct kndRepo *unused_var(repo), struct kndTask *task)
{
    int err;

    if (!base->is_resolved) {
        err = knd_proc_resolve(base, task);
        KND_TASK_ERR("failed to resolve base proc");
    }

    if (DEBUG_PROC_RESOLVE_LEVEL_2)
        knd_log(".. \"%.*s\" proc to inherit args from \"%.*s\"..",
                self->entry->name_size, self->entry->name, base->name_size, base->name);

    struct LocalContext ctx = {
        .task = task,
        .proc = self,
        .base = base
    };

    err = base->arg_idx->map(base->arg_idx, inherit_arg, (void*)&ctx);             RET_ERR();
    return knd_OK;
}

int knd_resolve_proc_ref(struct kndClass *self, const char *name, size_t name_size,
                         struct kndProc *unused_var(base),
                         struct kndProc **result, struct kndTask *task)
{
    struct kndProc *proc;
    int err;

    if (DEBUG_PROC_RESOLVE_LEVEL_2)
        knd_log(".. resolving proc ref:  %.*s", name_size, name);

    err = knd_get_proc(self->entry->repo, name, name_size, &proc, task);                             RET_ERR();

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
    err = knd_proc_ref_new(mempool, &ref);                                       RET_ERR();
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

    if (DEBUG_PROC_RESOLVE_LEVEL_2)
        knd_log(".. \"%.*s\" (%.*s) links to base => \"%.*s\" (%.*s)",
                entry->name_size, entry->name,
                entry->repo->name_size, entry->repo->name,
                base->entry->name_size, base->entry->name,
                base->entry->repo->name_size, base->entry->repo->name);

    /*if (base->entry->repo != repo) {
        err = knd_proc_clone(base, repo, &base_copy, task);                   RET_ERR();
        base = base_copy;
        err = link_ancestor(self, base->entry, task);                             RET_ERR();
        parent_linked = true;
        }*/

    /* copy the ancestors */
    for (baseref = base->entry->ancestors; baseref; baseref = baseref->next) {
        err = link_ancestor(self, baseref->entry, task);                          RET_ERR();
    }

    if (!parent_linked) {
        /* register a parent */
        err = knd_proc_ref_new(mempool, &ref);
        KND_TASK_ERR("mempool failed to alloc kndProcRef");
        ref->proc = base;
        ref->entry = base->entry;
        ref->next = entry->ancestors;
        entry->ancestors = ref;
        entry->num_ancestors++;
    }
    return knd_OK;
}

static int resolve_bases(struct kndProc *self, struct kndTask *task)
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

    for (base = self->bases; base; base = base->next) {
        if (!base->proc) {
            err = knd_get_proc(entry->repo, base->name, base->name_size, &base->proc, task);
            KND_TASK_ERR("failed to resolve base proc");
        }
        if (!base->proc->is_resolved) {
            err = knd_proc_resolve(base->proc, task);
            KND_TASK_ERR("failed to resolve proc \"%.*s\"", base->proc->name_size, base->proc->name);
        }
        err = link_base(self, base->proc, task);
        KND_TASK_ERR("failed to link base proc");
    }
    self->base_is_resolved = true;
    return knd_OK;
}

int knd_proc_resolve(struct kndProc *self, struct kndTask *task)
{
    struct kndRepo *repo = self->entry->repo;
    struct kndProcArg *arg = NULL;
    struct kndProcArgRef *arg_ref;
    struct kndProcVar *base;
    int err;

    if (DEBUG_PROC_RESOLVE_LEVEL_2)
        knd_log(".. resolving proc \"%.*s\"",  self->name_size, self->name);

    if (self->resolving_in_progress) {
        knd_log("-- vicious circle detected in \"%.*s\"", self->name_size, self->name);
        return knd_FAIL;
    }
    self->resolving_in_progress = true;

    if (!self->arg_idx) {
        err = knd_set_new(task->mempool, &self->arg_idx);                         RET_ERR();
    }

    for (arg = self->args; arg; arg = arg->next) {
        err = knd_proc_arg_resolve(arg, repo);                                    RET_ERR();

        // no conflicts detected, register a new arg in repo
        err = knd_repo_index_proc_arg(repo, self, arg, task);                     RET_ERR();

        // local index 
        err = knd_proc_arg_ref_new(task->mempool, &arg_ref);                      RET_ERR();
        arg_ref->arg = arg;
        arg_ref->proc = self;
        err = self->arg_idx->add(self->arg_idx, arg->id, arg->id_size, (void*)arg_ref);      RET_ERR();
    }

    if (!self->base_is_resolved) {
        err = resolve_bases(self, task);
        KND_TASK_ERR("failed to resolve base procs");
    }

    /* arg inheritance */
    for (base = self->bases; base; base = base->next) {
        if (DEBUG_PROC_RESOLVE_LEVEL_3)
            knd_log("\n.. \"%.*s\" proc to inherit args from its parent: \"%.*s\"",
                    self->name_size, self->name, base->name_size, base->name);

        err = inherit_args(self, base->proc, repo, task);
        KND_TASK_ERR("failed to inherit args");
    }

    if (self->result_classname_size) {
        err = knd_get_class_entry(repo, self->result_classname, self->result_classname_size, &self->result, task);
        KND_TASK_ERR("no such class: %.*s", self->result_classname_size, self->result_classname);
        //knd_log("EFFECT: %.*s", self->result_classname_size, self->result_classname);
    }

    //   if (self->proc_call) {
    //     err = resolve_proc_call(self);                                            RET_ERR();
    //}
    
    self->is_resolved = true;

    return knd_OK;
}

