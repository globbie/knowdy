#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_proc.h"
#include "knd_class.h"
#include "knd_mempool.h"
#include "knd_attr.h"
#include "knd_attr_inst.h"
#include "knd_repo.h"

#include "knd_user.h"
#include "knd_state.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_PROC_INST_LEVEL_1 0
#define DEBUG_PROC_INST_LEVEL_2 0
#define DEBUG_PROC_INST_LEVEL_3 0
#define DEBUG_PROC_INST_LEVEL_4 0
#define DEBUG_PROC_INST_LEVEL_TMP 1

struct LocalContext {
    struct kndProcInst *proc_inst;
    struct kndRepo *repo;
    struct kndTask *task;
};

static gsl_err_t run_set_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndProcInst *self = ctx->proc_inst;
    //struct kndProcEntry *proc_entry;
    struct kndProcInstEntry *entry;
    struct kndRepo *repo = ctx->repo;
    //struct kndDict *proc_name_idx = repo->proc_name_idx;
    struct kndDict *name_idx = repo->proc_inst_name_idx;
    struct kndOutput *log = ctx->task->log;
    struct kndTask *task = ctx->task;
    int err;

    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    entry = knd_dict_get(name_idx, name, name_size);
    if (entry) {
        if (entry->inst && entry->inst->states->phase == KND_REMOVED) {
            knd_log("-- this proc instance has been removed lately: %.*s",
                    name_size, name);
            goto assign_name;
        }
        knd_log("-- proc instance name doublet found: %.*s",
                name_size, name);
        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return make_gsl_err_external(err);
        err = log->write(log,   " proc inst name already exists",
                         strlen(" proc inst name already exists"));
        if (err) return make_gsl_err_external(err);
        task->http_code = HTTP_CONFLICT;
        return make_gsl_err(gsl_EXISTS);
    }
    assign_name:
    self->name = name;
    self->name_size = name_size;
    if (DEBUG_PROC_INST_LEVEL_TMP)
        knd_log("++ proc inst name: \"%.*s\"",
                self->name_size, self->name);
    return make_gsl_err(gsl_OK);
}

static int validate_arg(struct kndProcInst *self,
                        const char *name,
                        size_t name_size,
                        struct kndProcArg **result,
                        struct kndProcArgInst **result_arg)
{
    struct kndProc *conc;
    struct kndProcArgRef *arg_ref;
    struct kndProcArg *proc_arg;
    struct kndProcArgInst *arg = NULL;
    //struct kndOutput *log;
    int err;

    if (DEBUG_PROC_INST_LEVEL_2)
        knd_log(".. \"%.*s\" (base proc: %.*s) to validate arg: \"%.*s\"",
                self->name_size, self->name,
                self->base->name_size, self->base->name,
                name_size, name);

    /* check existing args */
    for (arg = self->args; arg; arg = arg->next) {
        if (!memcmp(arg->arg->name, name, name_size)) {
            if (DEBUG_PROC_INST_LEVEL_2)
                knd_log("++ ARG \"%.*s\" is already set!", name_size, name);
            *result_arg = arg;
            return knd_OK;
        }
    }

    conc = self->base;
    err = knd_proc_get_arg(conc, name, name_size, &arg_ref);
    if (err) {
        knd_log("  -- \"%.*s\" proc arg not approved", name_size, name);
        /*log->reset(log);
        e = log->write(log, name, name_size);
        if (e) return e;
        e = log->write(log, " arg not confirmed",
                       strlen(" arg not confirmed"));
                       if (e) return e;*/
        return err;
    }

    proc_arg = arg_ref->arg;
    *result = proc_arg;
    return knd_OK;
}

static gsl_err_t parse_import_arg(void *obj,
                                       const char *name, size_t name_size,
                                       const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndProcInst *self = ctx->proc_inst;
    struct kndProcArgInst *arg = NULL;
    struct kndProcArg *proc_arg = NULL;
    struct kndMemPool *mempool;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_PROC_INST_LEVEL_2)
        knd_log(".. parsing arg import REC: %.*s", 128, rec);

    err = validate_arg(self, name, name_size, &proc_arg, &arg);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    mempool = ctx->task->mempool;
    err = knd_proc_arg_inst_new(mempool, &arg);
    if (err) {
        knd_log("-- arg alloc failed :(");
        return *total_size = 0, make_gsl_err_external(err);
    }
    arg->parent = self;
    arg->arg = proc_arg;

    parser_err = knd_arg_inst_import(arg, rec, total_size, ctx->task);
    if (parser_err.code) goto final;

    if (!self->tail) {
        self->tail = arg;
        self->args = arg;
    }
    else {
        self->tail->next = arg;
        self->tail = arg;
    }
    self->num_args++;

    if (DEBUG_PROC_INST_LEVEL_2)
        knd_log("++ arg %.*s parsing OK!",
                arg->arg->name_size, arg->arg->name);

    return make_gsl_err(gsl_OK);

    final:

    knd_log("-- validation of \"%.*s\" arg failed :(", name_size, name);
    // TODO arg->del(arg);

    return parser_err;
}

gsl_err_t knd_proc_inst_import(struct kndProcInst *self,
                               struct kndRepo *repo,
                               const char *rec, size_t *total_size,
                               struct kndTask *task)
{
    if (DEBUG_PROC_INST_LEVEL_TMP)
        knd_log(".. proc inst import REC: %.*s", 128, rec);

    struct LocalContext ctx = {
        .proc_inst = self,
        .repo = repo,
        .task = task
    };
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = &ctx
        },
        { .type = GSL_SET_STATE,
          .validate = parse_import_arg,
          .obj = &ctx
        },
        { .validate = parse_import_arg,
          .obj = &ctx
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t run_set_state_id(void *obj, const char *name, size_t name_size)
{
    struct kndProcInst *self = obj;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    if (DEBUG_PROC_INST_LEVEL_2)
        knd_log("++ proc inst state: \"%.*s\" inst:%.*s",
                name_size, name, self->name_size, self->name);

    return make_gsl_err(gsl_OK);
}

gsl_err_t kndProcInst_read_state(struct kndProcInst *self,
                                  const char *rec, size_t *total_size,
                                  struct kndTask *task)
{
    struct LocalContext ctx = {
        .proc_inst = self,
        .task = task
    };
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_state_id,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .validate = parse_import_arg,
          .obj = &ctx
        },
        { .validate = parse_import_arg,
          .obj = &ctx
        }
    };

    if (DEBUG_PROC_INST_LEVEL_2)
        knd_log(".. reading proc inst state: %.*s", 128, rec);

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}
