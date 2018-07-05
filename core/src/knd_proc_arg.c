#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>
#include <glb-lib/output.h>

#include "knd_proc_arg.h"
#include "knd_proc.h"
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

static void del(struct kndProcArg *self)
{
    free(self);
}

static void proc_call_arg_str(struct kndProcCallArg *self,
                              size_t depth)
{
     knd_log("%*s  {%.*s %.*s}", depth * KND_OFFSET_SIZE, "",
             self->name_size, self->name,
             self->val_size, self->val);
}

static void str(struct kndProcArg *self)
{
    struct kndTranslation *tr;
    struct kndProcCallArg *arg;

    knd_log("%*sarg: \"%.*s\" [class:%.*s]", self->depth * KND_OFFSET_SIZE, "",
            self->name_size, self->name, self->classname_size, self->classname);

    if (self->proc_call.name_size) {
        knd_log("%*s    {run %.*s [type:%d]", self->depth * KND_OFFSET_SIZE, "",
                self->proc_call.name_size, self->proc_call.name,
                self->proc_call.type);
        for (arg = self->proc_call.args; arg; arg = arg->next) {
            proc_call_arg_str(arg, self->depth + 1);
        }
        knd_log("%*s    }", self->depth * KND_OFFSET_SIZE, "");
    }

    tr = self->tr;
    while (tr) {
        knd_log("%*s   ~ %s %s", self->depth * KND_OFFSET_SIZE, "", tr->locale, tr->val);
        tr = tr->next;
    }
}

static int proc_call_arg_export_JSON(struct kndProcArg *self,
                                     struct kndProcCallArg *call_arg)
{
    struct glbOutput *out = self->out;
    int err;
    err = out->write(out, "{\"", strlen("{\""));                                  RET_ERR();
    err = out->write(out, call_arg->name, call_arg->name_size);                   RET_ERR();
    err = out->write(out, "\":\"", strlen("\":\""));                              RET_ERR();
    err = out->write(out, call_arg->val, call_arg->val_size);                     RET_ERR();
    err = out->write(out, "\"}", strlen("\"}"));                                  RET_ERR();
    return knd_OK;
}

static int proc_call_arg_export_GSP(struct kndProcArg *self,
                                    struct kndProcCallArg *call_arg)
{
    struct glbOutput *out = self->out;
    int err;
    err = out->write(out, "{", 1);                                                RET_ERR();
    err = out->write(out, call_arg->name, call_arg->name_size);                   RET_ERR();
    err = out->write(out, " ", 1);                                                RET_ERR();
    err = out->write(out, call_arg->val, call_arg->val_size);                     RET_ERR();
    err = out->write(out, "}", 1);                                                RET_ERR();
    return knd_OK;
}

/**
 *  EXPORT
 */
static int export_JSON(struct kndProcArg *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct glbOutput *out;
    struct kndTranslation *tr;
    struct kndProcCallArg *arg;
    struct kndProc *proc;
    bool in_list = false;
    int err;

    out = self->out;

    err = out->write(out, "{\"_name\":\"", strlen("{\"_name\":\""));              RET_ERR();
    err = out->write(out, self->name, self->name_size);                           RET_ERR();
    err = out->write(out, "\"", 1);                                               RET_ERR();

    /* choose gloss */
    tr = self->tr;
    while (tr) {
        if (DEBUG_PROC_ARG_LEVEL_TMP)
            knd_log("LANG: %s == CURR LOCALE: %s [%zu] => %s",
                    tr->locale, self->locale, self->locale_size, tr->val);
        if (strncmp(self->locale, tr->locale, tr->locale_size)) {
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
    
    if (self->proc_call.name_size) {
        if (self->proc_entry) {
            proc = self->proc_entry->proc;
            if (proc) {
                err = out->write(out, ",\"proc\":", strlen(",\"proc\":"));        RET_ERR();
                proc->depth = self->parent->depth + 1;
                err = proc->export(proc);                                         RET_ERR();
            }
        } else {
            err = out->write(out, ",\"run\":\"", strlen(",\"run\":\""));          RET_ERR();
            err = out->write(out, self->proc_call.name,
                             self->proc_call.name_size);                          RET_ERR();
            err = out->write(out, "\"", 1);                                       RET_ERR();
        }

        if (self->proc_call.num_args) {
            err = out->write(out, ",\"args\":[", strlen(",\"args\":["));          RET_ERR();
            for (arg = self->proc_call.args; arg; arg = arg->next) {
                if (in_list) {
                    err = out->write(out, ",", 1);                                RET_ERR();
                }
                err = proc_call_arg_export_JSON(self, arg);                       RET_ERR();
                in_list = true;
            }
            err = out->write(out, "]", 1);                                        RET_ERR();
        }

    }

    err = out->write(out, "}", 1);                                                RET_ERR();

    return knd_OK;
}


static int export_GSP(struct kndProcArg *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct glbOutput *out;
    struct kndTranslation *tr;
    struct kndProcCallArg *arg;
    int err;

    out = self->out;
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
    
    if (self->proc_call.name_size) {
        err = out->write(out, "{run ", strlen("{run "));                          RET_ERR();
        err = out->write(out, self->proc_call.name, self->proc_call.name_size);   RET_ERR();
        for (arg = self->proc_call.args; arg; arg = arg->next) {
            err = proc_call_arg_export_GSP(self, arg);                            RET_ERR();
        }
        err = out->write(out, "}", 1);                                            RET_ERR();
    }

    err = out->write(out, "}", 1);                                                RET_ERR();
    return knd_OK;
}

static int export_SVG(struct kndProcArg *self)
{
    struct glbOutput *out;
    struct kndProc *proc;
    int err;

    out = self->out;
    /*err = out->write(out, "<text>", strlen("<text>"));                            RET_ERR();
    err = out->write(out, self->name, self->name_size);                           RET_ERR();
    err = out->write(out, "</text>", strlen("</text>"));                          RET_ERR();
    */
    if (self->proc_entry) {
        proc = self->proc_entry->proc;
        err = proc->export(proc);                                                 RET_ERR();
    }
    
    return knd_OK;
}

static int export_inst_GSP(struct kndProcArg *self,
                           struct kndProcArgInstance *inst)
{
    struct glbOutput *out;
    int err;

    out = self->out;
    err = out->write(out, "{class ", strlen("{class "));                          RET_ERR();
    //err = out->write(out, inst->procname, inst->procname_size);                   RET_ERR();
    err = out->write(out, "{obj ", strlen("{obj "));                              RET_ERR();
    err = out->write(out, inst->objname, inst->objname_size);                     RET_ERR();
    err = out->write(out, "}}", strlen("}}"));                                    RET_ERR();

    return knd_OK;
}

static int export_inst_JSON(struct kndProcArg *self,
                            struct kndProcArgInstance *inst)
{
    struct glbOutput *out = self->out;
    /*const char *type_name = knd_proc_arg_names[self->type];
      size_t type_name_size = strlen(knd_proc_arg_names[self->type]); */
    int err;

    err = out->write(out, "{", 1);                                               RET_ERR();
    err = out->write(out, "\"class\":\"", strlen("\"class\":\""));               RET_ERR();
    //err = out->write(out, inst->procname, inst->procname_size);                RET_ERR();
    err = out->write(out, "\"", 1);                                              RET_ERR();

    err = out->write(out, ",\"obj\":\"", strlen(",\"obj\":\""));                 RET_ERR();
    err = out->write(out, inst->objname, inst->objname_size);                    RET_ERR();
    err = out->write(out, "\"", 1);                                              RET_ERR();

    err = out->write(out, "}", 1);                                               RET_ERR();

    return knd_OK;
}

static int export(struct kndProcArg *self)
{
    int err;

    switch (self->format) {
    case KND_FORMAT_JSON:
        err = export_JSON(self);                                             RET_ERR();
        break;
    case KND_FORMAT_GSP:
        err = export_GSP(self);                                              RET_ERR();
        break;
    case KND_FORMAT_SVG:
        err = export_SVG(self);                                              RET_ERR();
        break;
    default:
        break;
    }

    return knd_OK;
}

static int export_inst(struct kndProcArg *self,
                       struct kndProcArgInstance *inst)
{
    int err = knd_FAIL;

    switch (self->format) {
    case KND_FORMAT_JSON:
        err = export_inst_JSON(self, inst);
        if (err) goto final;
        break;
    case KND_FORMAT_GSP:
        err = export_inst_GSP(self, inst);
        if (err) goto final;
        break;
    default:
        break;
    }

 final:
    return err;
}


static gsl_err_t run_set_name(void *obj, const char *name, size_t name_size)
{
    struct kndProcArg *self = (struct kndProcArg*)obj;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    memcpy(self->name, name, name_size);
    self->name_size = name_size;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t alloc_gloss_item(void *obj,
                                  const char *name,
                                  size_t name_size,
                                  size_t count,
                                  void **item)
{
    struct kndProcArg *self = obj;
    struct kndTranslation *tr;

    assert(name == NULL && name_size == 0);

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. %.*s: allocate gloss translation,  count: %zu",
                self->name_size, self->name, count);

    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return make_gsl_err_external(knd_NOMEM);

    memset(tr, 0, sizeof(struct kndTranslation));

    *item = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t append_gloss_item(void *accu,
                                   void *item)
{
    struct kndProcArg *self = accu;
    struct kndTranslation *tr = item;

    tr->next = self->tr;
    self->tr = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss_item(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndTranslation *tr = obj;
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = tr->curr_locale,
          .buf_size = &tr->curr_locale_size,
          .max_buf_size = sizeof tr->curr_locale
        },
        { .name = "t",
          .name_size = strlen("t"),
          .buf = tr->val,
          .buf_size = &tr->val_size,
          .max_buf_size = sizeof tr->val
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (tr->curr_locale_size == 0 || tr->val_size == 0)
        return make_gsl_err(gsl_FORMAT);  // error: both of them are required

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. read gloss translation: \"%.*s\",  text: \"%.*s\"",
                tr->locale_size, tr->locale, tr->val_size, tr->val);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss(void *obj,
                             const char *rec,
                             size_t *total_size)
{
    struct kndProcArg *self = obj;
    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .alloc = alloc_gloss_item,
        .append = append_gloss_item,
        .accu = self,
        .parse = parse_gloss_item
    };

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. %.*s: reading gloss",
                self->name_size, self->name);

    return gsl_parse_array(&item_spec, rec, total_size);
}

static gsl_err_t parse_proc_call_arg(void *obj,
                                     const char *name, size_t name_size,
                                     const char *rec, size_t *total_size)
{
    struct kndProcArg *self = obj;
    struct kndProcCallArg *call_arg;

    if (DEBUG_PROC_ARG_LEVEL_1)
        knd_log(".. Proc Call Arg \"%.*s\" to validate: \"%.*s\"..",
                name_size, name, 32, rec);

    call_arg = malloc(sizeof(struct kndProcCallArg));
    if (!call_arg) return *total_size = 0, make_gsl_err_external(knd_NOMEM);

    memset(call_arg, 0, sizeof(struct kndProcCallArg));
    memcpy(call_arg->name, name, name_size);
    call_arg->name_size = name_size;

    call_arg->next = self->proc_call.args;
    self->proc_call.args = call_arg;
    self->proc_call.num_args++;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf_size = &call_arg->val_size,
          .max_buf_size = KND_SHORT_NAME_SIZE,
          .buf = call_arg->val
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_proc_call(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct kndProcArg *self = obj;

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. Proc Call parsing: \"%.*s\"..", 32, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf_size = &self->proc_call.name_size,
          .max_buf_size = KND_NAME_SIZE,
          .buf = self->proc_call.name
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

static gsl_err_t parse_GSL(struct kndProcArg *self,
                     const char *rec,
                     size_t *total_size)
{
    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. Proc Arg parsing: \"%.*s\"..", 32, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = self
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
        { .name = "c",
          .name_size = strlen("c"),
          .buf = self->classname,
          .buf_size = &self->classname_size,
          .max_buf_size = sizeof self->classname,
        },
        { .name = "val",
          .name_size = strlen("val"),
          .buf = self->val,
          .buf_size = &self->val_size,
          .max_buf_size = sizeof self->val,
        },
        {  .name = "num",
           .name_size = strlen("num"),
           .parse = gsl_parse_size_t,
           .obj = &self->numval
        },
        { .name = "run",
          .name_size = strlen("run"),
          .parse = parse_proc_call,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t set_inst_procname(void *obj __attribute__((unused)), const char *name __attribute__((unused)), size_t name_size)
{
    //struct kndProcArgInstance *self = obj;

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
    struct kndProcArgInstance *self = obj;

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
    struct kndProcArgInstance *inst = data;

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
    struct kndProcArgInstance *self = data;

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

static int parse_inst_GSL(struct kndProcArg *self,
                          struct kndProcArgInstance *inst,
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
                    struct kndProcArgInstance *inst,
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

static int resolve_arg(struct kndProcArg *self)
{
    struct kndProcEntry *entry;
    int err;

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log(".. resolving arg \"%.*s\"..",
                self->name_size, self->name);

    if (self->classname_size) {
        /* TODO */
        if (DEBUG_PROC_ARG_LEVEL_2)
            knd_log(".. resolving arg class template: %.*s..",
                    self->classname_size, self->classname);

        return knd_OK;
    }

    if (!self->proc_call.name_size) {

        return knd_OK;
    }

    /* special name */
    if (self->proc_call.name[0] == '_') {
        self->proc_call.type = KND_PROC_SYSTEM;
        return knd_OK;
    }

    entry = self->parent->proc_name_idx->get(self->parent->proc_name_idx,
                                           self->proc_call.name,
                                           self->proc_call.name_size);
    if (!entry) {
        knd_log("-- Proc Arg resolve: no such proc: \"%.*s\" IDX:%p :(",
                self->proc_call.name_size,
                self->proc_call.name, self->parent->proc_name_idx);
        return knd_FAIL;
    }

    if (!entry->proc) {
        err = self->parent->get_proc(self->parent,
                                     entry->name, entry->name_size, &entry->proc);      RET_ERR();
    }
    self->proc_entry = entry;

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log("++ Proc Arg %.*s  call:\"%.*s\"  resolved!",
                self->name_size, self->name,
                self->proc_call.name_size, self->proc_call.name);


    return knd_OK;
}

static int resolve_inst(struct kndProcArg *self,
                        struct kndProcArgInstance *inst)
{
    //struct kndProcEntry *proc_entry;
    struct kndProcEntry *entry;
    struct kndObjEntry *obj;

    entry = self->parent->class_name_idx->get(self->parent->class_name_idx,
                                       inst->procname, inst->procname_size);
    if (!entry) {
        knd_log("-- no such class: %.*s :(", inst->procname_size, inst->procname);
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
                knd_log("++ obj resolved: \"%.*s\"!",  inst->objname_size, inst->objname);
        }
    }

    if (DEBUG_PROC_ARG_LEVEL_2)
        knd_log("++ Proc Arg instance resolved: \"%.*s\"!",
                inst->procname_size, inst->procname);

    return knd_OK;
}

extern void kndProcArgInstance_init(struct kndProcArgInstance *self)
{
    memset(self, 0, sizeof(struct kndProcArgInstance));
}
//extern void kndProcArgInstRef_init(struct kndProcArgInstRef *self)
//{
//    memset(self, 0, sizeof(struct kndProcArgInstRef));
//}

extern void kndProcArg_init(struct kndProcArg *self)
{
    self->del = del;
    self->str = str;
    self->parse = parse_GSL;
    self->resolve = resolve_arg;
    self->export = export;
    self->parse_inst = parse_inst_GSL;
    self->resolve_inst = resolve_inst;
    self->export_inst = export_inst;
}

extern int
kndProcArg_new(struct kndProcArg **c)
{
    struct kndProcArg *self;

    self = malloc(sizeof(struct kndProcArg));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndProcArg));

    kndProcArg_init(self);
    *c = self;

    return knd_OK;
}
