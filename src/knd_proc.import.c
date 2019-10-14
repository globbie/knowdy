#include <string.h>
#include <stdatomic.h>

#include <gsl-parser.h>

#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_proc_call.h"
#include "knd_utils.h"
#include "knd_task.h"
#include "knd_output.h"
#include "knd_mempool.h"
#include "knd_proc_arg.h"
#include "knd_text.h"
#include "knd_class.h"
#include "knd_repo.h"

#define DEBUG_PROC_IMPORT_LEVEL_0 0
#define DEBUG_PROC_IMPORT_LEVEL_1 0
#define DEBUG_PROC_IMPORT_LEVEL_2 0
#define DEBUG_PROC_IMPORT_LEVEL_3 0
#define DEBUG_PROC_IMPORT_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
    struct kndProc *proc;
    struct kndProcVar *proc_var;
};

static gsl_err_t parse_proc_arg_item(void *obj,
                                     const char *rec,
                                     size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndProc *self = ctx->proc;
    struct kndProcArg *arg;
    int err;
    gsl_err_t parser_err;

    err = knd_proc_arg_new(ctx->task->mempool, &arg);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    parser_err = knd_proc_arg_parse(arg, rec, total_size, ctx->task);
    if (parser_err.code) return parser_err;

    // append
    knd_proc_declare_arg(self, arg);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_proc_call_item(void *obj,
                                      const char *rec,
                                      size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndProc *self = ctx->proc;
    struct kndProcCall *call;
    int err;
    gsl_err_t parser_err;

    err = knd_proc_call_new(ctx->task->mempool, &call);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    parser_err = knd_proc_call_parse(call, rec, total_size, ctx->task);
    if (parser_err.code) return parser_err;

    // append
    knd_proc_declare_call(self, call);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_base_arg_classname(void *obj, const char *name, size_t name_size)
{
    struct kndProcArgVar *self = obj;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    self->name = name;
    self->name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_result_classname(void *obj, const char *name, size_t name_size)
{
    struct kndProc *self = obj;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    self->name = name;
    self->name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t validate_base_arg(void *obj,
                                   const char *name,
                                   size_t name_size,
                                   const char *rec,
                                   size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndProcVar *base = ctx->proc_var;

    if (name_size > sizeof ((struct kndProcArgVar *)NULL)->name)
        return *total_size = 0, make_gsl_err(gsl_LIMIT);

    struct kndProcArgVar *proc_arg_var;
    int err;

    err = knd_proc_arg_var_new(ctx->task->mempool, &proc_arg_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    proc_arg_var->name = name;
    proc_arg_var->name_size = name_size;

    struct gslTaskSpec specs[] = {
// FIXME(k15tfu): ?? switch to something like this
//        {
//            .is_implied = true,
//            .buf = base_arg->name,
//            .buf_size = &base_arg->name_size,
//            .max_buf_size = sizeof base_arg->name
//        },
        {
            .name = "c",
            .name_size = strlen("c"),
            .run = set_base_arg_classname,
            .obj = proc_arg_var
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        //free(base_arg);
        return parser_err;
    }

    knd_proc_var_declare_arg(base, proc_arg_var);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_proc_var_name(void *obj, const char *name, size_t name_size)
{
    struct kndProcVar *self = obj;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    self->name = name;
    self->name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_base(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndProc *self = ctx->proc;
    struct kndProcVar *proc_var;
    int err;

    err = knd_proc_var_new(ctx->task->mempool, &proc_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    proc_var->proc = self;
    ctx->proc_var = proc_var;

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .run = set_proc_var_name,
            .obj = proc_var
        },
        {   .type = GSL_SET_STATE,
            .validate = validate_base_arg,
            .obj = &ctx
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(specs[0]));
    if (parser_err.code) {
        //free(base);
        return parser_err;
    }

    declare_base(self, proc_var);

    return make_gsl_err(gsl_OK);
}


static gsl_err_t run_set_cost(void *obj, const char *val, size_t val_size)
{
    struct kndProc *self = obj;
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    long numval;
    int err;

    if (!val_size) return make_gsl_err_external(knd_FAIL);
    if (val_size >= KND_NAME_SIZE) return make_gsl_err_external(knd_LIMIT);

    memcpy(buf, val, val_size);
    buf_size = val_size;
    buf[buf_size] = '\0';

    err = knd_parse_num(buf, &numval);
    if (err) return make_gsl_err_external(err);
    
    self->estimate.cost = numval;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_estimate(void *obj,
                                const char *rec,
                                size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndProc *self = ctx->proc;

    if (DEBUG_PROC_IMPORT_LEVEL_2)
        knd_log(".. proc estimate parsing: \"%.*s\"..",
                32, rec);

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .run = run_set_cost,
            .obj = self
        },
        {   .name = "time",
            .name_size = strlen("time"),
            .parse = gsl_parse_size_t,
            .obj = &self->estimate.time
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t set_proc_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = ctx->repo;
    struct kndOutput *log = task->log;
    struct kndProc *self = ctx->proc, *proc;
    struct kndDict *proc_name_idx = task->proc_name_idx;
    struct kndProcEntry *entry;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    self->entry->name = name;
    self->entry->name_size = name_size;
    self->name = name;
    self->name_size = name_size;

    if (task->type != KND_LOAD_STATE) {
        knd_log(".. proc doublet checking..\n");
        err = knd_get_proc(repo, name, name_size, &proc, task);
        if (!err) goto doublet;
    }

    entry = knd_dict_get(proc_name_idx, name, name_size);
    if (!entry) {
        entry = self->entry;
        err = knd_dict_set(proc_name_idx,
                           name, name_size,
                           (void*)entry);
        if (err) return make_gsl_err_external(err);
        return make_gsl_err(gsl_OK);
    }

    return make_gsl_err(gsl_OK);

 doublet:
    knd_log("-- \"%.*s\" proc doublet found?", name_size, name);
    log->reset(log);
    err = log->write(log, name, name_size);
    if (err) return make_gsl_err_external(err);

    err = log->write(log,   " proc name already exists",
                     strlen(" proc name already exists"));
    if (err) return make_gsl_err_external(err);

    task->ctx->http_code = HTTP_CONFLICT;

    return make_gsl_err(gsl_FAIL);
}

int knd_inner_proc_import(struct kndProc *proc,
                          const char *rec,
                          size_t *total_size,
                          struct kndRepo *repo,
                          struct kndTask *task)
{
    if (DEBUG_PROC_IMPORT_LEVEL_2)
        knd_log(".. import an anonymous inner proc: %.*s..",
                64, rec);

    struct LocalContext ctx = {
        .task = task,
        .repo = repo,
        .proc = proc
    };

    struct gslTaskSpec proc_arg_spec = {
        .is_list_item = true,
        .parse = parse_proc_arg_item,
        .obj = &ctx
    }; 

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_proc_name,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = knd_parse_gloss_array,
          .obj = task
        },
        { .name = "is",
          .name_size = strlen("is"),
          .parse = parse_base,
          .obj = &ctx
        }, 
        {   .type = GSL_GET_ARRAY_STATE,
            .name = "arg",
            .name_size = strlen("arg"),
            .parse = gsl_parse_array,
            .obj = &proc_arg_spec
        },
        {
            .name = "result",
            .name_size = strlen("result"),
            .run = set_result_classname,
            .obj = proc
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- parser error: %d", parser_err.code);
        return parser_err.code;
    }

    if (task->ctx->tr) {
        proc->tr = task->ctx->tr;
        task->ctx->tr = NULL;
    }

    if (DEBUG_PROC_IMPORT_LEVEL_2)
        knd_proc_str(proc, 0);

    return knd_OK;
}

gsl_err_t knd_proc_inst_parse_import(struct kndProc *self,
                                     struct kndRepo *repo,
                                     const char *rec,
                                     size_t *total_size,
                                     struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndProcInst *inst;
    struct kndProcInstEntry *entry;
    // struct kndDict *name_idx;
    struct kndState *state;
    struct kndStateRef *state_ref;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_PROC_IMPORT_LEVEL_2) {
        knd_log(".. import \"%.*s\" inst.. (repo:%.*s)",
                128, rec,
                repo->name_size, repo->name);
    }

    err = knd_proc_inst_new(mempool, &inst);
    if (err) {
        knd_log("-- proc inst alloc failed");
        return *total_size = 0, make_gsl_err_external(err);
    }

    err = knd_state_new(mempool, &state);
    if (err) {
        knd_log("-- state alloc failed");
        return *total_size = 0, make_gsl_err_external(err);
    }
    err = knd_proc_inst_entry_new(mempool, &entry);
    if (err) return make_gsl_err_external(err);

    inst->entry = entry;
    entry->inst = inst;
    state->phase = KND_CREATED;
    state->numid = 1;
    inst->base = self;
    inst->states = state;
    inst->num_states = 1;

    parser_err = knd_proc_inst_import(inst, repo, rec, total_size, task);
    if (parser_err.code) return parser_err;

    err = knd_state_ref_new(mempool, &state_ref);
    if (err) {
        knd_log("-- state ref alloc for imported inst failed");
        return make_gsl_err_external(err);
    }
    state_ref->state = state;
    state_ref->type = KND_STATE_PROC_INST;
    state_ref->obj = (void*)entry;

    state_ref->next = task->ctx->proc_inst_state_refs;
    task->ctx->proc_inst_state_refs = state_ref;

    inst->entry->numid = atomic_fetch_add_explicit(&repo->num_proc_insts, 1,\
                                                   memory_order_relaxed);
    inst->entry->numid++;
    knd_uid_create(inst->entry->numid, inst->entry->id, &inst->entry->id_size);

    if (DEBUG_PROC_IMPORT_LEVEL_2)
        knd_log("++ \"%.*s\" (%.*s) proc inst parse OK!",
                inst->name_size, inst->name,
                inst->entry->id_size, inst->entry->id,
                self->name_size, self->name);

    /* automatic name assignment if no explicit name given */
    if (!inst->name_size) {
        inst->name = inst->entry->id;
        inst->name_size = inst->entry->id_size;
    }

    //name_idx = repo->proc_inst_name_idx;

    // TODO  lookup prev inst ref
    /*err = name_idx->set(name_idx,
                        inst->name, inst->name_size,
                        (void*)entry);
    if (err) return make_gsl_err_external(err);
    */

    // err = knd_register_proc_inst(self, entry, mempool);
    //if (err) return make_gsl_err_external(err);

    task->type = KND_COMMIT_STATE;

    if (!task->ctx->commit) {
        err = knd_commit_new(task->mempool, &task->ctx->commit);
        if (err) return make_gsl_err_external(err);

        task->ctx->commit->orig_state_id = atomic_load_explicit(&task->repo->num_commits,
                                                                memory_order_relaxed);
    }
    state->commit = task->ctx->commit;

    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_proc_import(struct kndRepo *repo,
                          const char *rec,
                          size_t *total_size,
                          struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndProcEntry *entry;
    struct kndProc *proc;
    int err;

    if (DEBUG_PROC_IMPORT_LEVEL_2)
        knd_log(".. import proc: \"%.*s\"..", 32, rec);

    err = knd_proc_entry_new(mempool, &entry);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    entry->name = "/";
    entry->name_size = 1;
    entry->repo = repo;

    err = knd_proc_new(mempool, &proc);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    proc->name = entry->name;
    proc->name_size = 1;
    entry->proc = proc;
    proc->entry = entry;

    struct LocalContext ctx = {
        .task = task,
        .repo = repo,
        .proc = proc
    };

    struct gslTaskSpec proc_arg_spec = {
        .is_list_item = true,
        .parse = parse_proc_arg_item,
        .obj = &ctx
    }; 

    struct gslTaskSpec proc_call_spec = {
        .is_list_item = true,
        .parse = parse_proc_call_item,
        .obj = &ctx
    }; 


    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_proc_name,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = knd_parse_gloss_array,
          .obj = task
        },
        { .name = "is",
          .name_size = strlen("is"),
          .parse = parse_base,
          .obj = &ctx
        }, 
        {   .type = GSL_GET_ARRAY_STATE,
            .name = "arg",
            .name_size = strlen("arg"),
            .parse = gsl_parse_array,
            .obj = &proc_arg_spec
        },
        {
            .name = "result",
            .name_size = strlen("result"),
            .run = set_result_classname,
            .obj = proc
        },
        {   .name = "estim",
            .name_size = strlen("estim"),
            .parse = parse_estimate,
            .obj = &ctx
        },
        {   .type = GSL_GET_ARRAY_STATE,
            .name = "do",
            .name_size = strlen("do"),
            .parse = gsl_parse_array,
            .obj = &proc_call_spec
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- parser error: %d  total size:%zu",
                parser_err.code, *total_size);

        const char *c = rec + *total_size;
        knd_log("== CONTEXT: %.*s", 32, c);
        knd_log("==          ^^^^");
        return parser_err;
    }

    if (task->ctx->tr) {
        proc->tr = task->ctx->tr;
        task->ctx->tr = NULL;
    }

    if (!proc->name_size)
        return make_gsl_err(gsl_FORMAT);

    if (DEBUG_PROC_IMPORT_LEVEL_2)
        knd_proc_str(proc, 0);

    if (task->type == KND_COMMIT_STATE) {
        err = knd_proc_commit_state(proc, KND_CREATED, task);
        if (err) return make_gsl_err_external(err);
    }

    return make_gsl_err(gsl_OK);
    // TODO free resources
    //return parser_err;
}
