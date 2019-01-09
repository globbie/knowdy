#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>
#include <glb-lib/output.h>

#include "knd_repo.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_task.h"
#include "knd_class.h"
#include "knd_text.h"
#include "knd_mempool.h"

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
};

void proc_call_arg_str(struct kndProcCallArg *self,
                       size_t depth)
{
     knd_log("%*s  {%.*s %.*s}", depth * KND_OFFSET_SIZE, "",
             self->name_size, self->name,
             self->val_size, self->val);
}

void knd_proc_arg_str(struct kndProcArg *self,
                      size_t depth)
{
    //struct kndTranslation *tr;
    struct kndProcCallArg *arg;

    knd_log("%*s{%.*s   {_c %.*s}", depth * KND_OFFSET_SIZE, "",
            self->name_size, self->name, self->classname_size, self->classname);

    if (self->proc_call) {
        knd_log("%*s    {run %.*s [type:%d]", depth * KND_OFFSET_SIZE, "",
                self->proc_call->name_size, self->proc_call->name,
                self->proc_call->type);
        for (arg = self->proc_call->args; arg; arg = arg->next) {
            proc_call_arg_str(arg, depth + 1);
        }

        knd_log("%*s    }", depth * KND_OFFSET_SIZE, "");
    }
}

static int proc_call_arg_export_JSON(struct kndProcArg *unused_var(self),
                                     struct kndProcCallArg *call_arg,
                                     struct glbOutput *out)
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
                                     struct glbOutput *out)
{
    int err;
    err = out->write(out, "{", 1);                                                RET_ERR();
    err = out->write(out, call_arg->name, call_arg->name_size);                   RET_ERR();
    err = out->write(out, " ", 1);                                                RET_ERR();
    err = out->write(out, call_arg->val, call_arg->val_size);                     RET_ERR();
    err = out->write(out, "}", 1);                                                RET_ERR();
    return knd_OK;
}

static int export_JSON(struct kndProcArg *self,
                       struct kndTask *task,
                       struct glbOutput *out)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndTranslation *tr;
    struct kndProcCallArg *arg;
    struct kndProc *proc;
    bool in_list = false;
    int err;

    err = out->write(out, "{\"_name\":\"", strlen("{\"_name\":\""));              RET_ERR();
    err = out->write(out, self->name, self->name_size);                           RET_ERR();
    err = out->write(out, "\"", 1);                                               RET_ERR();

    /* choose gloss */
    tr = self->tr;
    while (tr) {
        if (DEBUG_PROC_ARG_LEVEL_TMP)
            knd_log("LANG: %s == CURR LOCALE: %s [%zu] => %s",
                    tr->locale, task->locale, task->locale_size, tr->val);
        if (strncmp(task->locale, tr->locale, tr->locale_size)) {
            goto next_tr;
        }
        err = out->write(out, ",\"gloss\":\"", strlen(",\"gloss\":\""));          RET_ERR();
        err = out->write(out, tr->val,  tr->val_size);                            RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();
        break;
    next_tr:
        tr = tr->next;
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
    
    if (self->proc_call) {
        if (self->proc_entry) {
            proc = self->proc_entry->proc;
            if (proc) {
                err = out->write(out, ",\"proc\":", strlen(",\"proc\":"));        RET_ERR();
                err = knd_proc_export(proc, KND_FORMAT_JSON, task, out);                                         RET_ERR();
            }
        } else {
            err = out->write(out, ",\"run\":\"", strlen(",\"run\":\""));          RET_ERR();
            err = out->write(out, self->proc_call->name,
                             self->proc_call->name_size);                          RET_ERR();
            err = out->write(out, "\"", 1);                                       RET_ERR();
        }

        if (self->proc_call->num_args) {
            err = out->write(out, ",\"args\":[", strlen(",\"args\":["));          RET_ERR();
            for (arg = self->proc_call->args; arg; arg = arg->next) {
                if (in_list) {
                    err = out->write(out, ",", 1);                                RET_ERR();
                }
                err = proc_call_arg_export_JSON(self, arg, out);                       RET_ERR();
                in_list = true;
            }
            err = out->write(out, "]", 1);                                        RET_ERR();
        }
    }

    err = out->write(out, "}", 1);                                                RET_ERR();

    return knd_OK;
}


static int export_GSP(struct kndProcArg *self,
                      struct glbOutput *out)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndTranslation *tr;
    struct kndProcCallArg *arg;
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
    
    if (self->proc_call->name_size) {
        err = out->write(out, "{run ", strlen("{run "));                          RET_ERR();
        err = out->write(out, self->proc_call->name, self->proc_call->name_size);   RET_ERR();
        for (arg = self->proc_call->args; arg; arg = arg->next) {
            err = proc_call_arg_export_GSP(self, arg, out);                            RET_ERR();
        }
        err = out->write(out, "}", 1);                                            RET_ERR();
    }

    err = out->write(out, "}", 1);                                                RET_ERR();
    return knd_OK;
}

static int export_SVG(struct kndProcArg *self,
                      struct kndTask *task,
                      struct glbOutput *out)
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


extern int knd_proc_arg_export(struct kndProcArg *self,
                               knd_format format,
                               struct kndTask *task,
                               struct glbOutput *out)
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

/*static gsl_err_t parse_proc_call(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct kndProcArg *self = obj;

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. Proc Call parsing: \"%.*s\"..", 32, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_proc_call_name,
          .obj = self->proc_call
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = parse_gloss,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = parse_gloss,
          .obj = self
        },
        { .is_validator = true,
          .validate = parse_proc_call_arg,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}
*/

gsl_err_t knd_proc_arg_parse(struct kndProcArg *self,
                             const char *rec,
                             size_t *total_size,
                             struct kndTask *unused_var(task))
{
    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. Proc Arg parsing: \"%.*s\"..", 32, rec);

    /*    struct LocalContext ctx = {
        .task = task,
        .proc_arg = self
        }; */
    
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
        }/*,
        { .name = "val",
          .name_size = strlen("val"),
          .buf = self->val,
          .buf_size = &self->val_size,
          .max_buf_size = sizeof self->val,
          }*/,
        {  .name = "num",
           .name_size = strlen("num"),
           .parse = gsl_parse_size_t,
           .obj = &self->numval
        }/*,
        { .name = "run",
          .name_size = strlen("run"),
          .parse = parse_proc_call,
          .obj = self
          }*/
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

    self->objname = name;
    self->objname_size = name_size;

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log("++ INST ARG OBJ NAME: \"%.*s\"",
                self->objname_size, self->objname);

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
    //struct kndProcEntry *entry;
    struct kndClassEntry *entry;

    if (DEBUG_PROC_ARG_LEVEL_TMP)
        knd_log(".. resolving arg \"%.*s\"  repo:%.*s..",
                self->name_size, self->name, repo->name_size, repo->name);

    if (self->classname_size) {
        if (DEBUG_PROC_ARG_LEVEL_TMP)
            knd_log(".. resolving arg class template: %.*s..",
                    self->classname_size, self->classname);
        entry = repo->class_name_idx->get(repo->class_name_idx,
                                          self->classname, self->classname_size);
        if (!entry) {
            knd_log("-- no such class: %.*s",
                    self->classname_size, self->classname);
            return knd_FAIL;
        }
    }

    if (self->proc_call) {
        // TODO: resolve proc call
    }
    
    return knd_OK;
}

int knd_proc_arg_resolve_inst(struct kndProcArg *self,
                              struct kndProcArgInst *inst)
{
    struct kndProcEntry *entry;
    struct kndRepo *repo = self->parent->entry->repo;
    struct kndObjEntry *obj;

    entry = repo->class_name_idx->get(repo->class_name_idx,
                                      inst->procname, inst->procname_size);
    if (!entry) {
        knd_log("-- no such proc: %.*s",
                inst->procname_size, inst->procname);
        return knd_FAIL;
    }

    /* TODO: check inheritance or role */

    inst->proc_entry = entry;

    /* resolve obj ref */
    if (inst->objname_size) {
        if (entry->inst_idx) {
            obj = entry->inst_idx->get(entry->inst_idx,
                                    inst->objname, inst->objname_size);
            if (!obj) {
                knd_log("-- no such obj: %.*s :(", inst->objname_size, inst->objname);
                return knd_FAIL;
            }
            //err = link_proc(self, inst, obj);        RET_ERR();
            inst->obj = obj;

            if (DEBUG_PROC_ARG_LEVEL_2)
                knd_log("++ obj resolved: \"%.*s\"!",
                        inst->objname_size, inst->objname);
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
                                KND_MEMPAGE_TINY,
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
                                KND_MEMPAGE_SMALL,
                                sizeof(struct kndProcArg),
                                (void**)self);                     RET_ERR();
    return knd_OK;
}
