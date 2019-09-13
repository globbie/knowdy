#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_repo.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_proc_call.h"
#include "knd_task.h"
#include "knd_class.h"
#include "knd_attr.h"
#include "knd_text.h"
#include "knd_mempool.h"
#include "knd_output.h"

#define DEBUG_PROC_ARG_LEVEL_1 0
#define DEBUG_PROC_ARG_LEVEL_2 0
#define DEBUG_PROC_ARG_LEVEL_3 0
#define DEBUG_PROC_ARG_LEVEL_4 0
#define DEBUG_PROC_ARG_LEVEL_5 0
#define DEBUG_PROC_ARG_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
    struct kndProcArg *proc_arg;
    struct kndProcArgInst *proc_arg_inst;
    struct kndClass *class;
    struct kndAttrVar *attr_var;
};

static gsl_err_t import_nested_attr_var(void *obj,
                                        const char *name, size_t name_size,
                                        const char *rec, size_t *total_size);

void knd_proc_arg_str(struct kndProcArg *self,
                      size_t depth)
{
    if (self->classname_size) {
        knd_log("%*s{%.*s   {_c %.*s}", depth * KND_OFFSET_SIZE, "",
                self->name_size, self->name, self->classname_size, self->classname);
    } else {
        knd_log("%*s{%.*s", depth * KND_OFFSET_SIZE, "",
                self->name_size, self->name);
    }
    knd_log("%*s    }", depth * KND_OFFSET_SIZE, "");
}


static int export_gloss_JSON(struct kndTranslation *tr,
                             struct kndTask *task,
                             struct kndOutput *out,
                             bool separ_needed)
{
    int err;

    while (tr) {
        if (task->ctx->locale_size != tr->locale_size) continue;

        if (memcmp(task->ctx->locale, tr->locale, tr->locale_size)) {
            goto next_tr;
        }
        if (separ_needed) {
            err = out->write(out, ",", 1);                                        RET_ERR();
        }
        err = out->write(out, "\"_gloss\":\"", strlen("\"_gloss\":\""));          RET_ERR();
        err = out->write(out, tr->val,  tr->val_size);                            RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();
        break;
    next_tr:
        tr = tr->next;
    }
    return knd_OK;
}

static int export_JSON(struct kndProcArg *self,
                       struct kndTask *task,
                       struct kndOutput *out)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    bool in_list = false;
    int err;

    err = out->write(out, "{\"_name\":\"", strlen("{\"_name\":\""));              RET_ERR();
    err = out->write(out, self->name, self->name_size);                           RET_ERR();
    err = out->write(out, "\"", 1);                                               RET_ERR();

    if (self->tr) {
        err = export_gloss_JSON(self->tr,  task, out, in_list);                   RET_ERR();
    }

    if (self->classname_size) {
        err = out->write(out, ",\"class\":", strlen(",\"class\":"));        RET_ERR();
        err = out->writec(out, '"');                                        RET_ERR();
        err = out->write(out, self->classname, self->classname_size);       RET_ERR();
        err = out->writec(out, '"');                                        RET_ERR();
    }

    if (self->val_size) {
        err = out->write(out, ",\"val\":", strlen(",\"val\":"));            RET_ERR();
        err = out->writec(out, '"');                                        RET_ERR();
        err = out->write(out, self->val, self->val_size);                   RET_ERR();
        err = out->writec(out, '"');                                        RET_ERR();
    }

    if (self->numval) {
        err = out->write(out, ",\"num\":", strlen(",\"num\":"));                  RET_ERR();
        buf_size = sprintf(buf, "%lu",
                       (unsigned long)self->numval);
        err = out->write(out, buf, buf_size);                                     RET_ERR();
    }
    
    /*if (self->proc_call && self->proc_call->proc) {
        err = out->write(out, ",\"do\":", strlen(",\"do\":"));                    RET_ERR();

        err = knd_proc_export_JSON(self->proc_call->proc, task, false, 0);      RET_ERR();

        if (self->proc_call->num_args) {
            err = out->write(out, ",\"args\":[", strlen(",\"args\":["));          RET_ERR();
            for (arg = self->proc_call->args; arg; arg = arg->next) {
                if (in_list) {
                    err = out->write(out, ",", 1);                                RET_ERR();
                }
                err = knd_proc_call_arg_export_JSON(self, arg, out);                  RET_ERR();
                in_list = true;
            }
            err = out->write(out, "]", 1);                                        RET_ERR();
        }
    }
    */
    err = out->write(out, "}", 1);                                                RET_ERR();

    return knd_OK;
}


static int export_GSP(struct kndProcArg *self,
                      struct kndOutput *out)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndTranslation *tr;
    int err;
    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->write(out, self->name, self->name_size);                           RET_ERR();

    if (self->tr) {
        err = out->write(out,
                         "[_g", strlen("[_g"));                                   RET_ERR();
    }
    for (tr = self->tr; tr; tr = tr->next) {
        err = out->write(out, "{", 1);                                            RET_ERR();
        err = out->write(out, tr->locale,  tr->locale_size);                      RET_ERR();
        err = out->write(out, "{t ", 3);                                          RET_ERR();
        err = out->write(out, tr->val,  tr->val_size);                            RET_ERR();
        err = out->write(out, "}}", 2);                                           RET_ERR();
    }
    if (self->tr) {
        err = out->write(out, "]", 1);                                            RET_ERR();
    }

    if (self->classname_size) {
        err = out->write(out, "{c ", strlen("{c "));                              RET_ERR();
        err = out->write(out, self->classname, self->classname_size);             RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
    }

    if (self->val_size) {
        err = out->write(out, "{val ", strlen("{val "));                          RET_ERR();
        err = out->write(out, self->val, self->val_size);                         RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
    }

    if (self->numval) {
        err = out->write(out, "{num ", strlen("{num "));                          RET_ERR();
        buf_size = sprintf(buf, "%lu",
                       (unsigned long)self->numval);
        err = out->write(out, buf, buf_size);                                     RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
    }

    /*    if (self->proc_call->name_size) {
        err = out->write(out, "{run ", strlen("{run "));                          RET_ERR();
        err = out->write(out, self->proc_call->name, self->proc_call->name_size);   RET_ERR();
        for (arg = self->proc_call->args; arg; arg = arg->next) {
            err = proc_call_arg_export_GSP(self, arg, out);                            RET_ERR();
        }
        err = out->write(out, "}", 1);                                            RET_ERR();
    }
    */
    err = out->write(out, "}", 1);                                                RET_ERR();
    return knd_OK;
}

static int export_SVG(struct kndProcArg *self,
                      struct kndTask *task,
                      struct kndOutput *out)
{
    struct kndProc *proc;
    int err;

    //out = self->out;
    /*err = out->write(out, "<text>", strlen("<text>"));                            RET_ERR();
    err = out->write(out, self->name, self->name_size);                           RET_ERR();
    err = out->write(out, "</text>", strlen("</text>"));                          RET_ERR();
    */
    if (self->proc_entry) {
        proc = self->proc_entry->proc;
        err = knd_proc_export(proc, KND_FORMAT_SVG, task, out);                                                 RET_ERR();
    }
    return knd_OK;
}


int knd_proc_arg_export(struct kndProcArg *self,
                        knd_format format,
                        struct kndTask *task,
                        struct kndOutput *out)
{
    int err;

    switch (format) {
    case KND_FORMAT_JSON:
        err = export_JSON(self, task, out);                                             RET_ERR();
        break;
    case KND_FORMAT_GSP:
        err = export_GSP(self, out);                                              RET_ERR();
        break;
    case KND_FORMAT_SVG:
        err = export_SVG(self, task, out);                                              RET_ERR();
        break;
    default:
        break;
    }

    return knd_OK;
}

static gsl_err_t run_set_name(void *obj, const char *name, size_t name_size)
{
    struct kndProcArg *self = (struct kndProcArg*)obj;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);
    self->name = name;
    self->name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_proc_arg_classname(void *obj, const char *name, size_t name_size)
{
    struct kndProcArg *self = obj;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);
    self->classname = name;
    self->classname_size = name_size;
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

static gsl_err_t parse_proc_call(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndProcArg *self = ctx->proc_arg;

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. proc call parsing: \"%.*s\"..", 32, rec);

    if (!self->proc_call) {
        int e = knd_proc_call_new(ctx->task->mempool, &self->proc_call);
        if (e) return *total_size = 0, make_gsl_err_external(e);
    }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_proc_call_name,
          .obj = self->proc_call
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t set_attr_var_value(void *obj, const char *val, size_t val_size)
{
    struct kndAttrVar *self = obj;

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. set attr var value: %.*s %.*s",
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
    if (DEBUG_PROC_ARG_LEVEL_2) {
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

    if (DEBUG_PROC_ARG_LEVEL_2)
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

static gsl_err_t parse_proc_arg_defin(void *obj,
                                      const char *rec,
                                      size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndProcArg *self = ctx->proc_arg;
    struct kndAttrVar *attr_var;
    int err;

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. proc call arg defin parsing: \"%.*s\"..",
                32, rec);

    err = knd_attr_var_new(ctx->task->mempool, &attr_var);
    if (err) {
        return make_gsl_err(err);
    }

    self->attr_var = attr_var;
    ctx->attr_var = attr_var;

    struct gslTaskSpec specs[] = {
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

gsl_err_t knd_proc_arg_parse(struct kndProcArg *self,
                             const char *rec,
                             size_t *total_size,
                             struct kndTask *task)
{
    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. Proc Arg parsing: \"%.*s\"..", 32, rec);

    struct LocalContext ctx = {
        .task = task,
        .proc_arg = self
    };
   
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = self
        }/*,
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = parse_gloss,
          .obj = &ctx
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = parse_gloss,
          .obj = &ctx
          }*/,
        { .name = "_c",
          .name_size = strlen("_c"),
          .run = set_proc_arg_classname,
          .obj = self
        },
        { .name = "_is",
          .name_size = strlen("_is"),
          .parse = parse_proc_arg_defin,
          .obj = &ctx
        },
        {  .name = "num",
           .name_size = strlen("num"),
           .parse = gsl_parse_size_t,
           .obj = &self->numval
        },
        { .name = "do",
          .name_size = strlen("do"),
          .parse = parse_proc_call,
          .obj = &ctx
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t set_inst_procname(void *unused_var(obj), const char *unused_var(name), size_t name_size)
{
    //struct kndProcArgInst *self = obj;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    /*self->procname = name;
    self->procname_size = name_size;

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log("++ INST ARG CLASS NAME: \"%.*s\"",
                self->procname_size, self->procname);
    */
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_inst_objname(void *obj, const char *name, size_t name_size)
{
    struct kndProcArgInst *self = obj;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    self->class_inst_name = name;
    self->class_inst_name_size = name_size;

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log("++ ARG class inst NAME: \"%.*s\"",
                self->class_inst_name_size, self->class_inst_name);

    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_inst_obj(void *data,
                                const char *rec,
                                size_t *total_size)
{
    struct kndProcArgInst *inst = data;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_inst_objname,
          .obj = inst
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_inst_class(void *data,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndProcArgInst *self = data;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_inst_procname,
          .obj = self
        },
        { .name = "obj",
          .name_size = strlen("obj"),
          .parse = parse_inst_obj,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

int knd_parse_inst_GSL(struct kndProcArg *self,
                       struct kndProcArgInst *inst,
                       const char *rec,
                       size_t *total_size)
{
    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. %.*s Proc Arg instance parsing: \"%.*s\"..",
                self->name_size, self->name, 32, rec);

    struct gslTaskSpec specs[] = {
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_inst_class,
          .obj = inst
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}

/*static int link_proc(struct kndProcArg *self,
                    struct kndProcArgInst *inst,
                    struct kndObjEntry *obj_entry)
{
    struct kndProc *proc = self->proc;
    struct kndProcEntry *ref = NULL;
    struct kndProcArgInstRef *proc_arg_inst_ref = NULL;
    int err;

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. %.*s OBJ to link proc %.*s..",
                obj_entry->name_size, obj_entry->name,
                proc->name_size, proc->name);

    for (ref = obj_entry->procs; ref; ref = ref->next) {
        if (ref->proc == proc) break;
    }

    if (!ref) {
        err = proc->mempool->new_proc_ref(proc->mempool, &ref);                      RET_ERR();
        ref->proc = proc;
        ref->next = obj_entry->procs;
        obj_entry->procs = ref;
    }

    err = proc->mempool->new_proc_arg_inst_ref(proc->mempool, &proc_arg_inst_ref);    RET_ERR();
    proc_arg_inst_ref->inst = inst;
    proc_arg_inst_ref->next = ref->insts;
    ref->insts = proc_arg_inst_ref;
    ref->num_insts++;

    return knd_OK;
}
*/

int knd_proc_arg_resolve(struct kndProcArg *self,
                         struct kndRepo *repo)
{
    struct kndClassEntry *entry;
    struct kndProcEntry *proc_entry;

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. resolving arg \"%.*s\"  repo:%.*s..",
                self->name_size, self->name, repo->name_size, repo->name);

    if (self->classname_size) {
        if (DEBUG_PROC_ARG_LEVEL_2)
            knd_log(".. resolving arg class template: %.*s..",
                    self->classname_size, self->classname);
        entry = knd_dict_get(repo->class_name_idx,
                             self->classname, self->classname_size);
        if (!entry) {
            knd_log("-- no such class: %.*s",
                    self->classname_size, self->classname);
            return knd_FAIL;
        }
        self->class = entry->class;
    }

    if (self->proc_call) {
        proc_entry = knd_dict_get(repo->proc_name_idx,
                                  self->proc_call->name, self->proc_call->name_size);
        if (!proc_entry) {
            knd_log("-- no such proc: %.*s",
                    self->proc_call->name_size, self->proc_call->name);
            return knd_FAIL;
        }
        self->proc_call->proc = proc_entry->proc;
    }
    
    return knd_OK;
}

int knd_proc_arg_compute(struct kndProcArg *self,
                         struct kndTask *task)
{
    int err;

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. computing arg \"%.*s\"..",
                self->name_size, self->name);
    if (!self->proc_call) return knd_OK;

    if (!self->proc_call->proc) return knd_OK;

    if (!self->proc_call->proc->is_computed) {
        err = knd_proc_compute(self->proc_call->proc, task);
        if (err) return err;
    }
 
    return knd_OK;
}

static void register_state(struct kndProcArgInst *self,
                           struct kndState *state,
                           struct kndStateRef *state_ref)
{
    if (!self->states)
        state->phase = KND_CREATED;
    else
        state->phase = KND_UPDATED;

    self->states = state;
    self->num_states++;
    state->numid = self->num_states;
    state_ref->next =  self->parent->arg_inst_state_refs;
    self->parent->arg_inst_state_refs = state_ref;
}

static gsl_err_t run_set_val(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndProcArgInst *self = ctx->proc_arg_inst;
    struct kndState *state;
    struct kndStateVal *state_val;
    struct kndStateRef *state_ref;
    struct kndMemPool *mempool = ctx->task->mempool;
    int err;

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size >= KND_VAL_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_state_new(mempool, &state);
    if (err) {
        knd_log("-- state alloc failed");
        return make_gsl_err_external(err);
    }
    err = knd_state_val_new(mempool, &state_val);
    if (err) {
        knd_log("-- state val alloc failed");
        return make_gsl_err_external(err);
    }
    err = knd_state_ref_new(mempool, &state_ref);
    if (err) {
        knd_log("-- state ref alloc failed");
        return make_gsl_err_external(err);
    }
    state_ref->state = state;

    state_val->obj = (void*)self;
    state_val->val      = val;
    state_val->val_size = val_size;
    state->val          = state_val;

    self->val = val;
    self->val_size = val_size;

    register_state(self, state, state_ref);

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log("++ arg inst val set: \"%.*s\" [state:%zu]",
                self->val_size, self->val, state->numid);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_empty_val_warning(void *obj,
                                       const char *unused_var(val),
                                       size_t unused_var(val_size))
{
    struct kndProcArgInst *self = obj;
    knd_log("-- empty val of \"%.*s\" not accepted",
            self->arg->name_size, self->arg->name);
    return make_gsl_err(gsl_FAIL);
}

static gsl_err_t check_class_inst_name(void *obj,
                                       const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndProcArgInst *self = ctx->proc_arg_inst;
    struct kndClass *c = ctx->class;
    struct kndClassInst *inst;
    int err;

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. class \"%.*s\" to check inst name: \"%.*s\"",
                c->name_size, c->name, name_size, name);

    err = knd_get_class_inst(c, name, name_size, task, &inst);
    if (err) return make_gsl_err_external(err);

    self->class_inst = inst;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_inst_ref(void *obj,
                                      const char *rec,
                                      size_t *total_size)
{
    struct LocalContext *ctx = obj;

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. parse class inst: \"%.*s\"", 16, rec);

    if (!ctx->class) {
        knd_log("-- no class specified");
        return make_gsl_err(gsl_FAIL);
    }
    
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = check_class_inst_name,
          .obj = ctx
        },
        { .is_default = true,
          .run = run_empty_val_warning,
          .obj = ctx
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t check_class_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndProcArgInst *self = ctx->proc_arg_inst;
    struct kndClass *ref_class = self->arg->class;
    struct kndRepo *repo = ctx->repo;
    struct kndClass *c;
    int err;

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. attr \"%.*s\" to check class name: \"%.*s\"",
                self->arg->name_size, self->arg->name,
                name_size, name);

    if (!ref_class)  {
        knd_log("-- no ref template class specified");
        return make_gsl_err(gsl_FAIL);
    }

    err = knd_get_class(repo, name, name_size, &c, ctx->task);
    if (err) {
        knd_log("-- no such class");
        return make_gsl_err_external(err);
    }

    err = knd_is_base(ref_class, c);
    if (err) return make_gsl_err_external(err);

    ctx->class = c;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_ref(void *obj,
                                const char *rec,
                                size_t *total_size)
{
    struct LocalContext *ctx = obj;

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. parse class ref: \"%.*s\"", 16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = check_class_name,
          .obj = ctx
        },
        { .name = "inst",
          .name_size = strlen("inst"),
          .parse = parse_class_inst_ref,
          .obj = ctx
        },
        { .is_default = true,
          .run = run_empty_val_warning,
          .obj = ctx
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

gsl_err_t knd_arg_inst_import(struct kndProcArgInst *self,
                              const char *rec, size_t *total_size,
                              struct kndTask *task)
{
    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. proc arg inst \"%.*s\" parse REC: \"%.*s\"",
                self->arg->name_size, self->arg->name,
                16, rec);

    struct LocalContext ctx = {
        .proc_arg_inst = self,
        .task = task
    };
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_val,
          .obj = &ctx
        },
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_ref,
          .obj = &ctx
        },
        { .is_default = true,
          .run = run_empty_val_warning,
          .obj = self
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

int knd_proc_arg_inst_resolve(struct kndProcArg *self,
                              struct kndProcArgInst *inst)
{
    struct kndProcEntry *entry;
    struct kndRepo *repo = self->parent->entry->repo;
    struct kndClassInstEntry *class_inst_entry;

    entry = knd_dict_get(repo->class_name_idx,
                         inst->procname, inst->procname_size);
    if (!entry) {
        knd_log("-- no such proc: %.*s",
                inst->procname_size, inst->procname);
        return knd_FAIL;
    }

    /* TODO: check inheritance or role */

    inst->proc_entry = entry;

    /* resolve class inst ref */
    if (inst->class_inst_name_size) {
        if (entry->inst_idx) {
            class_inst_entry = knd_dict_get(entry->inst_idx,
                                            inst->class_inst_name,
                                            inst->class_inst_name_size);
            if (!class_inst_entry) {
                knd_log("-- no such class_inst_entry: %.*s",
                        inst->class_inst_name_size,
                        inst->class_inst_name);
                return knd_FAIL;
            }
            //err = link_proc(self, inst, class_inst_entry);        RET_ERR();
            inst->class_inst = class_inst_entry->inst;

            if (DEBUG_PROC_ARG_LEVEL_2)
                knd_log("++ class inst ref resolved: \"%.*s\"!",
                        inst->class_inst_name_size, inst->class_inst_name);
        }
    }

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log("++ Proc Arg instance resolved: \"%.*s\"!",
                inst->procname_size, inst->procname);

    return knd_OK;
}

int knd_proc_arg_inst_new(struct kndMemPool *mempool,
                          struct kndProcArgInst **self)
{
    int err = knd_mempool_alloc(mempool,
                                KND_MEMPAGE_SMALL,
                                sizeof(struct kndProcArgInst),
                                (void**)self);                      RET_ERR();
    return knd_OK;
}

int knd_proc_arg_inst_mem(struct kndMemPool *mempool,
                          struct kndProcArgInst **self)
{
    int err = knd_mempool_incr_alloc(mempool,
                                KND_MEMPAGE_SMALL,
                                sizeof(struct kndProcArgInst),
                                (void**)self);                      RET_ERR();
    return knd_OK;
}

int knd_proc_arg_ref_new(struct kndMemPool *mempool,
                         struct kndProcArgRef **self)
{
    int err = knd_mempool_alloc(mempool,
                                KND_MEMPAGE_TINY,
                                sizeof(struct kndProcArgRef),
                                (void**)self);                      RET_ERR();
    return knd_OK;
}

int knd_proc_arg_new(struct kndMemPool *mempool,
                     struct kndProcArg **self)
{
    int err = knd_mempool_alloc(mempool,
                                KND_MEMPAGE_SMALL_X2,
                                sizeof(struct kndProcArg),
                                (void**)self);                     RET_ERR();
    return knd_OK;
}
