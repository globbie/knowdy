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
#include "knd_mempool.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_text.h"
#include "knd_dict.h"
#include "knd_repo.h"
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
    struct kndTranslation *tr;
    struct kndProcArg *arg;
    struct kndProcCall *call;
    struct kndProcVar *base;

    knd_log("%*s{proc %.*s  {_id %.*s}",
            depth * KND_OFFSET_SIZE, "",
            self->name_size, self->name,
            self->entry->id_size, self->entry->id);

    for (tr = self->tr; tr; tr = tr->next) {
        knd_log("%*s  {%.*s %.*s}", (depth + 1) * KND_OFFSET_SIZE, "",
                tr->locale_size, tr->locale, tr->val_size, tr->val);
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

static gsl_err_t run_get_proc(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndRepo *repo = ctx->repo;
    struct kndProc *proc;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    ctx->proc = NULL;

    err = knd_get_proc(repo, name, name_size, &proc, ctx->task);
    if (err) return make_gsl_err_external(err);
    ctx->proc = proc;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t present_proc_selection(void *obj,
                                        const char *unused_var(val),
                                        size_t unused_var(val_size))
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndProc *proc = ctx->proc;
    knd_format format = task->ctx->format;
    struct kndOutput *out = task->out;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. presenting proc selection..");

    if (!proc) return make_gsl_err(gsl_FAIL);

    out->reset(out);
    
    /* export BODY */
    err = knd_proc_export(proc, format, task, out);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t remove_proc(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndProc *proc = ctx->proc;
    struct kndTask *task = ctx->task;
    int err;

    if (DEBUG_PROC_LEVEL_TMP)
        knd_log(".. removing proc: %.*s", name_size, name);

    if (!proc) {
        knd_log("-- remove operation: no proc selected");
        /*repo->log->reset(repo->log);
        err = repo->log->write(repo->log, name, name_size);
        if (err) return make_gsl_err_external(err);
        err = repo->log->write(repo->log, " class name not specified",
                               strlen(" class name not specified"));
                               if (err) return make_gsl_err_external(err);*/
        return make_gsl_err(gsl_NO_MATCH);
    }

    if (DEBUG_PROC_LEVEL_TMP)
        knd_log("== proc to remove: \"%.*s\"\n",
                proc->name_size, proc->name);

    task->type = KND_COMMIT_STATE;
    if (!task->ctx->commit) {
        err = knd_commit_new(task->mempool, &task->ctx->commit);
        if (err) return make_gsl_err_external(err);

        task->ctx->commit->orig_state_id = atomic_load_explicit(&task->repo->num_commits,
                                                                memory_order_relaxed);
    }

    err = knd_proc_commit_state(proc, KND_REMOVED, task);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t
parse_proc_inst_import(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    if (!ctx->proc) {
        knd_log("-- proc not selected");
        int err = ctx->task->log->writef(ctx->task->log, "no proc selected");
        if (err) return *total_size = 0, make_gsl_err_external(err);
        return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }
    return knd_proc_inst_parse_import(ctx->proc, ctx->repo, rec, total_size, ctx->task);
}

gsl_err_t knd_proc_select(struct kndRepo *repo,
                          const char *rec,
                          size_t *total_size,
                          struct kndTask *task)
{
    struct LocalContext ctx = {
        .task = task,
        .repo = repo
    };
    gsl_err_t parser_err;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. proc selection: \"%.*s\"", 16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_proc,
          .obj = &ctx
        },
        { .type = GSL_SET_STATE,
          .name = "_rm",
          .name_size = strlen("_rm"),
          .run = remove_proc,
          .obj = &ctx
        },
        { .type = GSL_SET_STATE,
          .name = "inst",
          .name_size = strlen("inst"),
          .parse = parse_proc_inst_import,
          .obj = &ctx
        },
        { .is_default = true,
          .run = present_proc_selection,
          .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs,
                                sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        /*knd_log("-- proc parse error: \"%.*s\"",
                repo->log->buf_size, repo->log->buf);
        if (!repo->log->buf_size) {
            e = repo->log->write(repo->log, "proc parse failure",
                                 strlen("proc parse failure"));
            if (e) return make_gsl_err_external(e);
            }*/
        return parser_err;
    }

    if (!ctx.proc)
        return make_gsl_err(gsl_FAIL);

    knd_state_phase phase;

    /* any commits happened? */
    switch (task->type) {
    case KND_COMMIT_STATE:
        phase = KND_UPDATED;
        if (task->phase == KND_REMOVED)
            phase = KND_REMOVED;
        err = knd_proc_commit_state(ctx.proc, phase, task);
        if (err) return make_gsl_err_external(err);
        break;
    default:
        break;
    }

    return make_gsl_err(gsl_OK);
}

int knd_proc_export(struct kndProc *self,
                    knd_format format,
                    struct kndTask *task,
                    struct kndOutput *out)
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

int knd_proc_get_arg(struct kndProc *self,
                     const char *name, size_t name_size,
                     struct kndProcArgRef **result)
{
    struct kndProcArgRef *ref;
    struct kndProcArg *arg = NULL;
    struct kndSharedDict *arg_name_idx = self->entry->repo->proc_arg_name_idx;
    struct kndSet *arg_idx = self->arg_idx;
    int err;

    if (DEBUG_PROC_LEVEL_2) {
        knd_log("\n.. \"%.*s\" proc (repo: %.*s) to select arg \"%.*s\" arg_idx:%p",
                self->name_size, self->name,
                self->entry->repo->name_size, self->entry->repo->name,
                name_size, name, arg_idx);
    }

    ref = knd_shared_dict_get(arg_name_idx, name, name_size);
    if (!ref) {
        /* if (self->entry->repo->base) {
            arg_name_idx = self->entry->repo->base->arg_name_idx;
            ref = arg_name_idx->get(arg_name_idx, name, name_size);
        }*/
        if (!ref) {
            knd_log("-- no such proc arg: \"%.*s\"", name_size, name);
            return knd_NO_MATCH;
        }
    }

    /* iterating over synonymous attrs */
    for (; ref; ref = ref->next) {
        arg = ref->arg;
        if (DEBUG_PROC_LEVEL_2) {
            knd_log("== arg %.*s is used in proc: %.*s",
                    name_size, name,
                    ref->proc->name_size,
                    ref->proc->name);
        }

        if (ref->proc == self) break;

        err = knd_proc_is_base(ref->proc, self);
        if (!err) break;
    }
    if (!arg) return knd_NO_MATCH;
    
    err = arg_idx->get(arg_idx,
                       arg->id, arg->id_size, (void**)&ref);            RET_ERR();
    if (err) return knd_NO_MATCH;

    *result = ref;
    return knd_OK;
}

int knd_proc_is_base(struct kndProc *self,
                     struct kndProc *child)
{
    struct kndProcEntry *entry = child->entry;
    struct kndProcRef *ref;
    struct kndProc *proc;
    size_t count = 0;

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
             knd_log("  => is %zu): %.*s (repo:%.*s)  base resolved:%d",
                     count,
                     proc->name_size, proc->name,
                     proc->entry->repo->name_size, proc->entry->repo->name,
                     proc->base_is_resolved);
         }
         count++;

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

int knd_get_proc(struct kndRepo *repo,
                 const char *name, size_t name_size,
                 struct kndProc **result,
                 struct kndTask *task)
{
    struct kndProcEntry *entry;
    struct kndProc *proc;
    struct kndOutput *log = task->log;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. \"%.*s\" repo to get proc: \"%.*s\"..",
                repo->name_size, repo->name, name_size, name);

    entry = knd_shared_dict_get(repo->proc_name_idx,
                                name, name_size);
    if (!entry) {
        if (repo->base) {
            err = knd_get_proc(repo->base, name, name_size, result, task);
            if (err) return err;
            return knd_OK;
        }

        knd_log("-- no such proc: \"%.*s\"", name_size, name);

        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return err;
        err = log->write(log, " proc name not found",
                         strlen(" proc name not found"));
        if (err) return err;
        return knd_NO_MATCH;
    }

    if (entry->phase == KND_REMOVED) {
        knd_log("-- \"%s\" proc was removed", name);
        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return err;
        err = log->write(log, " proc was removed",
                         strlen(" proc was removed"));
        if (err) return err;
        task->ctx->http_code = HTTP_GONE;
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
    if (err) {
        knd_log("-- proc state alloc failed");
        return err;
    }
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

int knd_proc_commit_state(struct kndProc *self,
                          knd_state_phase phase,
                          struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndStateRef *state_ref;
    struct kndCommit *commit = task->ctx->commit;
    struct kndState *state = NULL;
    int err;

    if (DEBUG_PROC_LEVEL_TMP) {
        knd_log(".. \"%.*s\" proc (repo:%.*s) to commit its state (phase:%d)",
                self->name_size, self->name,
                self->entry->repo->name_size, self->entry->repo->name,
                phase);
    }

    err = commit_state(self, NULL, phase, &state, task);                          RET_ERR();

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


int knd_proc_var_new(struct kndMemPool *mempool,
                            struct kndProcVar **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndProcVar), &page);          RET_ERR();
    *result = page;
    return knd_OK;
}

int knd_proc_arg_var_new(struct kndMemPool *mempool,
                                struct kndProcArgVar **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndProcArgVar), &page);                 RET_ERR();
    *result = page;
    return knd_OK;
}


int knd_proc_entry_new(struct kndMemPool *mempool,
                       struct kndProcEntry **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                            sizeof(struct kndProcEntry), &page);                  RET_ERR();
    *result = page;
    return knd_OK;
}

int knd_proc_ref_new(struct kndMemPool *mempool,
                     struct kndProcRef **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY, sizeof(struct kndProcRef), &page);
    if (err) return err;
    *result = page;
    return knd_OK;
}

int knd_proc_new(struct kndMemPool *mempool,
                 struct kndProc **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                            sizeof(struct kndProc), &page);                       RET_ERR();
    *result = page;
    return knd_OK;
}
