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
#include "knd_attr.h"
#include "knd_repo.h"

#define DEBUG_PROC_CALL_LEVEL_0 0
#define DEBUG_PROC_CALL_LEVEL_1 0
#define DEBUG_PROC_CALL_LEVEL_2 0
#define DEBUG_PROC_CALL_LEVEL_3 0
#define DEBUG_PROC_CALL_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
    struct kndProc *proc;
    struct kndProcCall *proc_call;
    struct kndAttrVar *attr_var;
};

static gsl_err_t import_nested_attr_var(void *obj,
                                        const char *name, size_t name_size,
                                        const char *rec, size_t *total_size);

static void proc_call_arg_str(struct kndProcCallArg *self,
                              size_t depth)
{
    const char *arg_type = "";
    size_t arg_type_size = 0;

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

    /*    if (self->class_var) {
        cvar = self->class_var;
        knd_log("%*s    {", depth * KND_OFFSET_SIZE, "");
        if (cvar->attrs) {
            str_attr_vars(cvar->attrs, depth + 1);
        }
        knd_log("%*s    }", depth * KND_OFFSET_SIZE, "");
        } */

    knd_log("%*s  }", depth * KND_OFFSET_SIZE, "");
}

void knd_proc_call_str(struct kndProcCall *self,
                       size_t depth)
{
    struct kndProcCallArg *arg;

    knd_log("%*s{%.*s", depth * KND_OFFSET_SIZE, "",
            self->name_size, self->name);

    knd_log("%*s    {do %.*s [type:%d]", depth * KND_OFFSET_SIZE, "",
            self->name_size, self->name,
            self->type);
    for (arg = self->args; arg; arg = arg->next) {
        proc_call_arg_str(arg, depth + 1);
    }
    knd_log("%*s    }", depth * KND_OFFSET_SIZE, "");
}

static gsl_err_t set_proc_call_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndProcCall *self = ctx->proc_call;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    self->name = name;
    self->name_size = name_size;

    knd_log("++ proc call: \"%.*s\"", name_size, name);
    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_attr_var_value(void *obj, const char *val, size_t val_size)
{
    struct kndAttrVar *self = obj;

    if (DEBUG_PROC_CALL_LEVEL_TMP)
        knd_log(".. set proc call attr var value: %.*s %.*s",
                self->name_size, self->name, val_size, val);

    if (!val_size) return make_gsl_err(gsl_FORMAT);

    self->val = val;
    self->val_size = val_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_attr_var(void *obj,
                                  const char *unused_var(name),
                                  size_t unused_var(name_size))
{
    struct kndAttrVar *attr_var = obj;

    // TODO empty values?
    if (DEBUG_PROC_CALL_LEVEL_2) {
        if (!attr_var->val_size)
            knd_log("NB: attr var value not set in %.*s",
                    attr_var->name_size, attr_var->name);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_nested_attr_var(void *obj,
                                        const char *name, size_t name_size,
                                        const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrVar *self = ctx->attr_var;
    struct kndTask    *task = ctx->task;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err;

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->parent = self;
    attr_var->name = name;
    attr_var->name_size = name_size;

    ctx->attr_var = attr_var;

    if (DEBUG_PROC_CALL_LEVEL_2)
        knd_log(".. import nested attr var: \"%.*s\" (parent item:%.*s)",
                attr_var->name_size, attr_var->name,
                self->name_size, self->name);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_var_value,
          .obj = attr_var
        },
        { .validate = import_nested_attr_var,
          .obj = ctx
        },
        { .is_default = true,
          .run = confirm_attr_var,
          .obj = attr_var
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- attr var import failed: %d", parser_err.code);
        return parser_err;
    }

    /* restore parent */
    ctx->attr_var = self;

    attr_var->next = self->children;
    self->children = attr_var;
    self->num_children++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t validate_do_arg(void *obj,
                                 const char *name,
                                 size_t name_size,
                                 const char *rec,
                                 size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndProcCall *call = ctx->proc_call;
    struct kndAttrVar *attr_var;
    struct kndProcCallArg *call_arg;
    gsl_err_t err;
    int e;

    if (DEBUG_PROC_CALL_LEVEL_TMP)
        knd_log(".. Proc Call Arg \"%.*s\" to parse: \"%.*s\"..",
                name_size, name, 32, rec);

    err.code = knd_proc_call_arg_new(ctx->task->mempool, &call_arg);
    if (err.code) return *total_size = 0, make_gsl_err_external(err.code);

    call_arg->name = name;
    call_arg->name_size = name_size;

    e = knd_attr_var_new(ctx->task->mempool, &attr_var);
    if (e) {
        return make_gsl_err(e);
    }
    call_arg->attr_var = attr_var;
    ctx->attr_var = attr_var;
    knd_proc_call_declare_arg(call, call_arg);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_var_value,
          .obj = attr_var
        },
        { .validate = import_nested_attr_var,
          .obj = ctx
        },
        { .is_default = true,
          .run = confirm_attr_var,
          .obj = attr_var
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}


gsl_err_t knd_proc_call_parse(struct kndProcCall *self,
                              const char *rec,
                              size_t *total_size,
                              struct kndTask *task)
{
    struct LocalContext ctx = {
           .task = task,
           .proc_call = self
    };

    if (DEBUG_PROC_CALL_LEVEL_2)
        knd_log(".. Proc Call item parsing: \"%.*s\"..", 32, rec);

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .run = set_proc_call_name,
            .obj = &ctx
        },
        {   .validate = validate_do_arg,
            .obj = &ctx
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    // TODO: lookup table
    /*if (self->name_size == strlen("_mult") &&
        !strncmp("_mult", self->name, self->name_size))
        self->type = KND_PROC_MULT;
    else if (!strncmp("_sum", self->name, self->name_size))
        self->type = KND_PROC_SUM;
    else if (!strncmp("_mult_percent", self->name, self->name_size))
        self->type = KND_PROC_MULT_PERCENT;
    else if (!strncmp("_div_percent", self->name, self->name_size))
        self->type = KND_PROC_DIV_PERCENT;
    */

    return make_gsl_err(gsl_OK);
}

/*
static int proc_call_arg_export_JSON(struct kndProcArg *unused_var(self),
                                     struct kndProcCallArg *call_arg,
                                     struct kndOutput *out)
{
    int err;
    err = out->write(out, "{\"", strlen("{\""));                                  RET_ERR();
    err = out->write(out, call_arg->name, call_arg->name_size);                   RET_ERR();
    err = out->write(out, "\":\"", strlen("\":\""));                              RET_ERR();
    err = out->write(out, call_arg->val, call_arg->val_size);                     RET_ERR();
    err = out->write(out, "\"}", strlen("\"}"));                                  RET_ERR();
    return knd_OK;
}

static int proc_call_arg_export_GSP(struct kndProcArg *unused_var(self),
                                    struct kndProcCallArg *call_arg,
                                     struct kndOutput *out)
{
    int err;
    err = out->write(out, "{", 1);                                                RET_ERR();
    err = out->write(out, call_arg->name, call_arg->name_size);                   RET_ERR();
    err = out->write(out, " ", 1);                                                RET_ERR();
    err = out->write(out, call_arg->val, call_arg->val_size);                     RET_ERR();
    err = out->write(out, "}", 1);                                                RET_ERR();
    return knd_OK;
}
*/

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
