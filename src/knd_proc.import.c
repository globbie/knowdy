#include <knd_proc.h>

#include <knd_utils.h>

#include <gsl-parser.h>

#include <string.h>

#include <knd_task.h>

// TODO remove this
#include <knd_mempool.h>
#include <knd_proc_arg.h>
#include <knd_text.h>
#include <knd_class.h>
#include <knd_repo.h>

#define DEBUG_PROC_LEVEL_0 0
#define DEBUG_PROC_LEVEL_1 0
#define DEBUG_PROC_LEVEL_2 0
#define DEBUG_PROC_LEVEL_3 0
#define DEBUG_PROC_LEVEL_TMP 1

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

static gsl_err_t validate_do_arg(void *obj,
                                 const char *name,
                                 size_t name_size,
                                 const char *rec,
                                 size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndProc *proc = ctx->proc;
    gsl_err_t err;

    if (DEBUG_PROC_LEVEL_TMP)
        knd_log(".. Proc Call Arg \"%.*s\" to validate: \"%.*s\"..",
                name_size, name, 32, rec);

    struct kndClassVar *class_var;
    err.code = knd_class_var_new(ctx->task->mempool, &class_var);
    if (err.code) return *total_size = 0, make_gsl_err_external(err.code);

    err = knd_import_class_var(class_var, rec, total_size, ctx->task);
    if (err.code) return err;

    struct kndProcCallArg *call_arg;
    err.code = knd_proc_call_arg_new(ctx->task->mempool, &call_arg);
    if (err.code) return *total_size = 0, make_gsl_err_external(err.code);

    kndProcCallArg_init(call_arg, name, name_size, class_var);
    kndProcCall_declare_arg(proc->proc_call, call_arg);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_proc_call_name(void *obj, const char *name, size_t name_size)
{
    struct kndProcCall *self = obj;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    self->name = name;
    self->name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t proc_call_parse(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndProc *self = ctx->proc;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. proc call parsing: \"%.*s\"..",
                32, rec);

    if (!self->proc_call) {
        int e = knd_proc_call_new(ctx->task->mempool, &self->proc_call);
        if (e) return *total_size = 0, make_gsl_err_external(e);
    }

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .run = set_proc_call_name,
            .obj = self->proc_call
        },
        {
            .validate = validate_do_arg,
            .obj = ctx
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    // TODO: lookup table
    if (self->proc_call->name_size == strlen("_mult") &&
        !strncmp("_mult", self->proc_call->name, self->proc_call->name_size))
        self->proc_call->type = KND_PROC_MULT;
    else if (!strncmp("_sum", self->proc_call->name, self->proc_call->name_size))
        self->proc_call->type = KND_PROC_SUM;
    else if (!strncmp("_mult_percent", self->proc_call->name, self->proc_call->name_size))
        self->proc_call->type = KND_PROC_MULT_PERCENT;
    else if (!strncmp("_div_percent", self->proc_call->name, self->proc_call->name_size))
        self->proc_call->type = KND_PROC_DIV_PERCENT;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t set_proc_name(void *obj, const char *name, size_t name_size)
{
    struct kndProc *self = obj;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    self->entry->name = name;
    self->entry->name_size = name_size;
    self->name = name;
    self->name_size = name_size;
    return make_gsl_err(gsl_OK);
}

int knd_inner_proc_import(struct kndProc *proc,
                          const char *rec,
                          size_t *total_size,
                          struct kndRepo *repo,
                          struct kndTask *task)
{
    if (DEBUG_PROC_LEVEL_TMP)
        knd_log(".. import an anonymous inner proc: %.*s..",
                128, rec);

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
          .obj = proc
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
        {   /* a synonym for result */
            .name = "effect",
            .name_size = strlen("effect"),
            .run = set_result_classname,
            .obj = proc
        },
        {   .name = "do",
            .name_size = strlen("do"),
            .parse = proc_call_parse,
            .obj = &ctx
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- parser error: %d", parser_err.code);
        return parser_err.code;
    }

    if (task->tr) {
        proc->tr = task->tr;
        task->tr = NULL;
    }
    if (DEBUG_PROC_LEVEL_TMP)
        proc->str(proc);

    return knd_OK;
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

    if (DEBUG_PROC_LEVEL_2)
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

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_proc_name,
          .obj = proc
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
        {   .name = "do",
            .name_size = strlen("do"),
            .parse = proc_call_parse,
            .obj = &ctx
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

    if (task->tr) {
        proc->tr = task->tr;
        task->tr = NULL;
    }

    if (!proc->name_size)
        return make_gsl_err(gsl_FORMAT);

    entry = repo->proc_name_idx->get(repo->proc_name_idx,
                                     proc->name, proc->name_size);
    if (entry) {
        if (entry->phase == KND_REMOVED) {
            knd_log("== proc was removed recently");
        } else {
            knd_log("-- %.*s proc name doublet found",
                    proc->name_size, proc->name);

            /*root_proc->entry->repo->log->reset(root_proc->entry->repo->log);
            err = root_proc->entry->repo->log->writef(root_proc->entry->repo->log,
                                                      "%*s proc name already exists",
                                                      proc->name_size, proc->name);
                                                      if (err) return make_gsl_err_external(err); */
            return make_gsl_err_external(knd_EXISTS);
        }
    }

    /*    if (!root_proc->batch_mode) {
        proc->next = root_proc->inbox;
        root_proc->inbox = proc;
        root_proc->inbox_size++;
        }*/

    /* generate ID and add to proc index */
    repo->num_procs++;
    proc->entry->numid = repo->num_procs;
    knd_uid_create(proc->entry->numid, proc->entry->id, &proc->entry->id_size);

    err = repo->proc_name_idx->set(repo->proc_name_idx,
                                   proc->name, proc->name_size,
                                   (void*)proc->entry);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_PROC_LEVEL_TMP)
        proc->str(proc);

    return make_gsl_err(gsl_OK);
}
