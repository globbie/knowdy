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

static void proc_call_arg_str(struct kndProcCallArg *self,
                              size_t depth)
{
    const char *arg_type = "";
    size_t arg_type_size = 0;
    struct kndClassVar *cvar;

    if (self->arg) {
        arg_type = self->arg->classname;
        arg_type_size = self->arg->classname_size;
    }

    knd_log("%*s  {%.*s %.*s", depth * KND_OFFSET_SIZE, "",
            self->name_size, self->name,
            self->val_size, self->val);

    if (self->val_size) {
        knd_log("%*s  {_c %.*s}", depth * KND_OFFSET_SIZE, "",
                arg_type_size, arg_type);
    }

    if (self->class_var) {
        cvar = self->class_var;
        knd_log("%*s    {", depth * KND_OFFSET_SIZE, "");
        if (cvar->attrs) {
            str_attr_vars(cvar->attrs, depth + 1);
        }
        knd_log("%*s    }", depth * KND_OFFSET_SIZE, "");
    }

    knd_log("%*s  }", depth * KND_OFFSET_SIZE, "");
}

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
    struct kndProcCallArg *call_arg;
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

    if (self->proc_call) {
        knd_log("%*s    {do %.*s", depth * KND_OFFSET_SIZE, "",
                self->proc_call->name_size, self->proc_call->name);
        for (call_arg = self->proc_call->args; call_arg; call_arg = call_arg->next) {
            proc_call_arg_str(call_arg, depth + 1);
        }
        knd_log("%*s    }", depth * KND_OFFSET_SIZE, "");
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

    err = knd_get_proc(repo, name, name_size, &proc);
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
    knd_format format = task->format;
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
    //struct kndDict *proc_name_idx = ctx->repo->proc_name_idx;

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

    proc->entry->phase = KND_REMOVED;

    // TODO proc_name_idx->remove(proc_name_idx,
    //                      proc->name, proc->name_size);

    //repo->log->reset(repo->log);
    /*err = repo->log->write(repo->log, proc->name, proc->name_size);
    if (err) return make_gsl_err_external(err);
    err = repo->log->write(repo->log, " proc removed",
                           strlen(" proc removed"));
    if (err) return make_gsl_err_external(err);
    */
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

    if (DEBUG_PROC_LEVEL_TMP)
        knd_log(".. parsing Proc select: \"%.*s\"",
                16, rec);

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

    /* any updates happened? */
    /*if (self->curr_proc) {
        if (self->curr_proc->inbox_size || self->curr_proc->inst_inbox_size) {
            self->curr_proc->next = self->inbox;
            self->inbox = self->curr_proc;
            self->inbox_size++;
        }
    }*/

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
        err = knd_proc_export_JSON(self, task, out);
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
        break;
    }
    return knd_OK;
}

int knd_proc_get_arg(struct kndProc *self,
                     const char *name, size_t name_size,
                     struct kndProcArgRef **result)
{
    struct kndProcArgRef *ref;
    struct kndDict *arg_name_idx = self->entry->repo->proc_arg_name_idx;
    struct kndSet *arg_idx = self->arg_idx;
    int err;

    if (DEBUG_PROC_LEVEL_TMP) {
        knd_log("\n.. \"%.*s\" proc (repo: %.*s) to select arg \"%.*s\" arg_idx:%p",
                self->name_size, self->name,
                self->entry->repo->name_size, self->entry->repo->name,
                name_size, name, arg_idx);
    }

    ref = knd_dict_get(arg_name_idx, name, name_size);
    if (!ref) {
        /*if (self->entry->repo->base) {
            arg_name_idx = self->entry->repo->base->arg_name_idx;
            ref = arg_name_idx->get(arg_name_idx, name, name_size);
            }*/
        if (!ref) {
            knd_log("-- no such proc arg: \"%.*s\"", name_size, name);
            return knd_NO_MATCH;
        }
    }

    err = arg_idx->get(arg_idx,
                       ref->arg->id, ref->arg->id_size, (void**)&ref);      RET_ERR();
    if (err) return knd_NO_MATCH;

    *result = ref;
    return knd_OK;
}


int knd_get_proc(struct kndRepo *repo,
                 const char *name, size_t name_size,
                 struct kndProc **result)
{
    struct kndProcEntry *entry;
    struct kndProc *proc;
    int err;

    if (DEBUG_PROC_LEVEL_TMP)
        knd_log(".. repo %.*s to get proc: \"%.*s\"..",
                repo->name_size, repo->name, name_size, name);

    entry = knd_dict_get(repo->proc_name_idx,
                         name, name_size);
    if (!entry) {
        if (repo->base) {
            err = knd_get_proc(repo->base, name, name_size, result);
            if (err) return err;
            return knd_OK;
        }

        knd_log("-- no such proc: \"%.*s\"", name_size, name);

        /*repo->log->reset(repo->log);
        err = repo->log->write(repo->log, name, name_size);
        if (err) return err;
        err = repo->log->write(repo->log, " Proc name not found",
                               strlen(" Proc name not found"));
                               if (err) return err;*/
        return knd_NO_MATCH;
    }

    if (entry->phase == KND_REMOVED) {
        knd_log("-- \"%s\" proc was removed", name);
        /*repo->log->reset(repo->log);
        err = repo->log->write(repo->log, name, name_size);
        if (err) return err;
        err = repo->log->write(repo->log, " proc was removed",
                               strlen(" proc was removed"));
        if (err) return err;
        */
        //repo->root_proc->task->http_code = HTTP_GONE;
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

int knd_proc_call_arg_new(struct kndMemPool *mempool,
                          struct kndProcCallArg **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                            sizeof(struct kndProcCallArg), &page);  RET_ERR();
    *result = page;
    return knd_OK;
}

int knd_proc_call_new(struct kndMemPool *mempool,
                      struct kndProcCall **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndProcCall), &page);  RET_ERR();
    *result = page;
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

int knd_proc_inst_entry_new(struct kndMemPool *mempool,
                            struct kndProcInstEntry **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndProcInstEntry), &page);                  RET_ERR();
    *result = page;
    return knd_OK;
}

int knd_proc_inst_new(struct kndMemPool *mempool,
                      struct kndProcInst **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                            sizeof(struct kndProcInst), &page);                       RET_ERR();
    *result = page;
    return knd_OK;
}

int knd_proc_entry_new(struct kndMemPool *mempool,
                       struct kndProcEntry **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                            sizeof(struct kndProcEntry), &page);                  RET_ERR();
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
