#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_proc.h"
#include "knd_class.h"
#include "knd_mempool.h"
#include "knd_attr.h"
#include "knd_shared_dict.h"
#include "knd_attr_inst.h"
#include "knd_repo.h"

#include "knd_user.h"
#include "knd_shard.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_PROC_INST_IMPORT_LEVEL_1 0
#define DEBUG_PROC_INST_IMPORT_LEVEL_2 0
#define DEBUG_PROC_INST_IMPORT_LEVEL_3 0
#define DEBUG_PROC_INST_IMPORT_LEVEL_4 0
#define DEBUG_PROC_INST_IMPORT_LEVEL_TMP 1

struct LocalContext {
    struct kndProcInstEntry *entry;
    struct kndProcInst *inst;
    struct kndRepo *repo;
    struct kndTask *task;
};

static gsl_err_t run_set_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndProcInst *self = ctx->inst;
    struct kndProcInstEntry *entry;
    struct kndRepo *repo = ctx->repo;
    struct kndSharedDict *name_idx = repo->proc_inst_name_idx;
    struct kndOutput *log = ctx->task->log;
    struct kndTask *task = ctx->task;
    int err;

    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    entry = knd_shared_dict_get(name_idx, name, name_size);
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
    if (DEBUG_PROC_INST_IMPORT_LEVEL_TMP)
        knd_log("++ proc inst name: \"%.*s\"",
                self->name_size, self->name);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_set_alias(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndProcInst *self = ctx->inst;
    self->alias = name;
    self->alias_size = name_size;
    return make_gsl_err(gsl_OK);
}

static int validate_arg(struct kndProcInst *self, const char *name, size_t name_size,
                        struct kndProcArg **result, struct kndProcArgInst **result_arg, struct kndTask *task)
{
    struct kndProcArgRef *arg_ref;
    struct kndProcArg *proc_arg;
    struct kndProcArgInst *arg = NULL;
    //struct kndOutput *log;
    int err;

    if (DEBUG_PROC_INST_IMPORT_LEVEL_2)
        knd_log(".. \"%.*s\" (blueprint proc: %.*s) to validate arg: \"%.*s\"",
                self->name_size, self->name,
                self->blueprint->name_size, self->blueprint->name,
                name_size, name);

    /* check existing args */
    for (arg = self->args; arg; arg = arg->next) {
        if (!memcmp(arg->arg->name, name, name_size)) {
            if (DEBUG_PROC_INST_IMPORT_LEVEL_2)
                knd_log("++ ARG \"%.*s\" is already set!", name_size, name);
            *result_arg = arg;
            return knd_OK;
        }
    }
    err = knd_proc_get_arg(self->blueprint->proc, name, name_size, &arg_ref);
    KND_TASK_ERR("\"%.*s\" proc arg not approved", name_size, name);

    proc_arg = arg_ref->arg;
    *result = proc_arg;
    return knd_OK;
}

static gsl_err_t parse_import_arg(void *obj,
                                       const char *name, size_t name_size,
                                       const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndProcInst *self = ctx->inst;
    struct kndProcArgInst *arg = NULL;
    struct kndProcArg *proc_arg = NULL;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_PROC_INST_IMPORT_LEVEL_2)
        knd_log(".. parsing arg import REC: %.*s", 128, rec);

    err = validate_arg(self, name, name_size, &proc_arg, &arg, task);
    if (err) {
        return *total_size = 0, make_gsl_err_external(err);
    }

    if (task->user_ctx)
        mempool = task->shard->user->mempool;

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

    if (DEBUG_PROC_INST_IMPORT_LEVEL_2)
        knd_log("++ arg %.*s parsing OK!",
                arg->arg->name_size, arg->arg->name);

    return make_gsl_err(gsl_OK);

    final:

    knd_log("-- validation of \"%.*s\" arg failed :(", name_size, name);
    // TODO arg->del(arg);

    return parser_err;
}


static gsl_err_t run_set_state_id(void *obj, const char *name, size_t name_size)
{
    struct kndProcInst *self = obj;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    if (DEBUG_PROC_INST_IMPORT_LEVEL_2)
        knd_log("++ proc inst state: \"%.*s\" inst:%.*s",
                name_size, name, self->name_size, self->name);

    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_proc_inst_read_state(struct kndProcInst *self, const char *rec, size_t *total_size, struct kndTask *task)
{
    struct LocalContext ctx = {
        .inst = self,
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

    if (DEBUG_PROC_INST_IMPORT_LEVEL_2)
        knd_log(".. reading proc inst state: %.*s", 128, rec);

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t import_proc_inst(struct kndProcInstEntry *entry, const char *rec, size_t *total_size, struct kndTask *task)
{
    struct kndRepo *repo = entry->repo;

    if (DEBUG_PROC_INST_IMPORT_LEVEL_2)
        knd_log(".. proc inst import REC: %.*s", 128, rec);

    struct LocalContext ctx = {
        .inst = entry->inst,
        .repo = repo,
        .task = task
    };
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = &ctx
        },
        { .name = "_as",
          .name_size = strlen("_as"),
          .run = run_set_alias,
          .obj = &ctx
        },
        { .validate = parse_import_arg,
          .obj = &ctx
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

int knd_import_proc_inst(struct kndProcEntry *self, const char *rec, size_t *total_size, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    if (task->user_ctx)
        mempool = task->shard->user->mempool;

    struct kndProcInst *inst;
    struct kndProcInstEntry *entry;
    // struct kndDict *name_idx;
    struct kndState *state;
    struct kndStateRef *state_ref;
    struct kndTaskContext *ctx = task->ctx;
    struct kndRepo *repo = self->repo;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_PROC_INST_IMPORT_LEVEL_TMP)
        knd_log(".. import \"%.*s\" proc inst.. (repo:%.*s)", 128, rec, repo->name_size, repo->name);

    switch (task->type) {
    case KND_INNER_COMMIT_STATE:
        // fall through
    case KND_INNER_STATE:
        task->type = KND_INNER_COMMIT_STATE;
        break;
    default:
        task->type = KND_COMMIT_STATE;
    }

    err = knd_proc_inst_new(mempool, &inst);
    KND_TASK_ERR("proc inst alloc failed");

    err = knd_proc_inst_entry_new(mempool, &entry);
    KND_TASK_ERR("proc inst entry alloc failed");
    entry->repo = repo;
    inst->entry = entry;
    entry->inst = inst;
    inst->blueprint = self;

    parser_err = import_proc_inst(entry, rec, total_size, task);
    if (parser_err.code) return parser_err.code;

    inst->entry->numid = atomic_fetch_add_explicit(&self->inst_id_count, 1, memory_order_relaxed);
    inst->entry->numid++;
    knd_uid_create(inst->entry->numid, inst->entry->id, &inst->entry->id_size);

    /* automatic name assignment if no explicit name given */
    if (!inst->name_size) {
        inst->name = inst->entry->id;
        inst->name_size = inst->entry->id_size;
    }

    switch (task->type) {
    case KND_INNER_COMMIT_STATE:
        //entry->next = ctx->stm_proc_insts;
        //ctx->stm_proc_insts = entry;
        //ctx->num_stm_proc_insts++;
        return knd_OK;
    default:
        break;
    }

    err = knd_state_new(mempool, &state);
    KND_TASK_ERR("state alloc failed");
    state->phase = KND_CREATED;
    state->numid = 1;
    inst->states = state;
    inst->num_states = 1;

    err = knd_state_ref_new(mempool, &state_ref);
    KND_TASK_ERR("state ref alloc for imported inst failed");
    state_ref->state = state;
    state_ref->type = KND_STATE_PROC_INST;
    state_ref->obj = (void*)entry;

    state_ref->next = ctx->proc_inst_state_refs;
    ctx->proc_inst_state_refs = state_ref;

    if (DEBUG_PROC_INST_IMPORT_LEVEL_TMP)
        knd_log("++ \"%.*s\" (%.*s) proc inst parse OK!",
                inst->name_size, inst->name, inst->entry->id_size, inst->entry->id, self->name_size, self->name);

    //name_idx = repo->proc_inst_name_idx;

    // TODO  lookup prev inst ref
    /*err = name_idx->set(name_idx,
                        inst->name, inst->name_size,
                        (void*)entry);
    */
    // err = knd_register_proc_inst(self, entry, mempool);

    task->type = KND_COMMIT_STATE;

    if (!ctx->commit) {
        err = knd_commit_new(task->mempool, &ctx->commit);
        KND_TASK_ERR("commit alloc failed");
        ctx->commit->orig_state_id = atomic_load_explicit(&task->repo->snapshot.num_commits, memory_order_relaxed);
    }
    state->commit = ctx->commit;
    return knd_OK;
}

