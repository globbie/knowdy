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
    struct kndProcArgVar *proc_arg_var;
    struct kndClass *class;
    struct kndAttrVar *attr_var;
};

static gsl_err_t import_nested_attr_var(void *obj,
                                        const char *name, size_t name_size,
                                        const char *rec, size_t *total_size);

void knd_proc_arg_str(struct kndProcArg *self, size_t depth)
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


static int export_gloss_JSON(struct kndText *tr,
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
        err = out->write(out, tr->seq->val,  tr->seq->val_size);                            RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();
        break;
    next_tr:
        tr = tr->next;
    }
    return knd_OK;
}

static int export_JSON(struct kndProcArg *self, struct kndTask *task, struct kndOutput *out)
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

static int export_GSP(struct kndProcArg *self, struct kndOutput *out)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndText *tr;
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
        err = out->write(out, tr->seq->val,  tr->seq->val_size);                            RET_ERR();
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

int knd_proc_arg_export(struct kndProcArg *self, knd_format format,
                        struct kndTask *task, struct kndOutput *out)
{
    int err;

    switch (format) {
    case KND_FORMAT_JSON:
        err = export_JSON(self, task, out);
        RET_ERR();
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

int knd_proc_arg_resolve(struct kndProcArg *self, struct kndRepo *repo, struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndProcEntry *proc_entry;
    int err;
    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. resolving arg \"%.*s\"", self->name_size, self->name);

    if (self->classname_size) {
        if (DEBUG_PROC_ARG_LEVEL_2)
            knd_log(".. resolving arg class template: %.*s..", self->classname_size, self->classname);
        entry = knd_shared_dict_get(repo->class_name_idx, self->classname, self->classname_size);
        if (!entry) {
            err = knd_NO_MATCH;
            KND_TASK_ERR("no such class: %.*s", self->classname_size, self->classname);
        }
        self->template = entry;
    }

    if (self->proc_call) {
        proc_entry = knd_shared_dict_get(repo->proc_name_idx, self->proc_call->name, self->proc_call->name_size);
        if (!proc_entry) {
            knd_log("-- no such proc: %.*s",
                    self->proc_call->name_size, self->proc_call->name);
            return knd_FAIL;
        }
        self->proc_call->proc = proc_entry->proc;
    }
    return knd_OK;
}

int knd_resolve_proc_arg_var(struct kndProc *proc, struct kndProcArgVar *var, struct kndTask *task)
{
    struct kndProcArgRef *ref;
    struct kndClassEntry *entry;
    struct kndRepo *repo = proc->entry->repo;
    struct kndClass *c = NULL;
    int err;

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log("\n.. resolving proc arg var \"%.*s\"  of \"%.*s\"",
                var->name_size, var->name, proc->name_size, proc->name);

    err = knd_proc_get_arg(proc, var->name, var->name_size, &ref);
    KND_TASK_ERR("\"%.*s\" proc arg not approved", var->name_size, var->name);

    if (ref->var && ref->var->template)
        c = ref->var->template->class;

    if (var->val_size) {
        entry = knd_shared_dict_get(repo->class_name_idx, var->val, var->val_size);
        if (!entry) {
            err = knd_NO_MATCH;
            KND_TASK_ERR("no such class: %.*s", var->val_size, var->val);
        }
        var->template = entry;

        if (c) {
            if (c == entry->class) {
                err = knd_FORMAT;
                KND_TASK_ERR("same class template specified twice");
            }
            err = knd_is_base(c, entry->class);
            KND_TASK_ERR("\"%.*s\" is not a subclass of arg var template class \"%.*s\"",
                         entry->name_size, entry->name, c->name_size, c->name);
        }
    }

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log("NB: \"%.*s\" proc arg gets a new class template from \"%.*s\"",
                var->name_size, var->name, proc->name_size, proc->name);

    ref->var = var;
    return knd_OK;
}

int knd_proc_arg_compute(struct kndProcArg *self, struct kndTask *unused_var(task))
{
    //int err;

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. computing arg \"%.*s\"..",
                self->name_size, self->name);
    if (!self->proc_call) return knd_OK;

    if (!self->proc_call->proc) return knd_OK;

    /*if (!self->proc_call->proc->is_computed) {
        err = knd_proc_compute(self->proc_call->proc, task);
        if (err) return err;
        }*/
 
    return knd_OK;
}

int knd_proc_arg_ref_new(struct kndMemPool *mempool, struct kndProcArgRef **result)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndProcArgRef));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndProcArgRef));
    *result = page;
    return knd_OK;
}

int knd_proc_arg_var_new(struct kndMemPool *mempool, struct kndProcArgVar **result)
{
    void *page;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndProcArgVar));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndProcArgVar));
    *result = page;
    return knd_OK;
}

int knd_proc_arg_new(struct kndMemPool *mempool, struct kndProcArg **result)
{
    void *page;
    int err;
    assert(mempool->small_x2_page_size >= sizeof(struct kndProcArg));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X2, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndProcArgVar));
    *result = page;
    return knd_OK;
}
