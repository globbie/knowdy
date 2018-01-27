#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_task.h"
#include "knd_state.h"
#include "knd_mempool.h"
#include "knd_output.h"
#include "knd_utils.h"
#include "knd_text.h"
#include "knd_dict.h"
#include "knd_parser.h"

#include <gsl-parser.h>

#define DEBUG_PROC_LEVEL_0 0
#define DEBUG_PROC_LEVEL_1 0
#define DEBUG_PROC_LEVEL_2 0
#define DEBUG_PROC_LEVEL_3 0
#define DEBUG_PROC_LEVEL_TMP 1

static void
del(struct kndProc *self)
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

static void base_str(struct kndProcBase *base,
		     size_t depth)
{
    struct kndArgItem *arg;

    knd_log("%*sbase => %.*s", depth * KND_OFFSET_SIZE, "",
                base->name_size, base->name);

    for (arg = base->args; arg; arg = arg->next) {
	knd_log("%*s%.*s [type:%.*s]", (depth + 1) * KND_OFFSET_SIZE, "",
                arg->name_size, arg->name,
                arg->classname_size, arg->classname);
    }
    
}

static void str(struct kndProc *self)
{
    struct kndTranslation *tr;
    struct kndProcArg *arg;
    struct kndProcCallArg *call_arg;
    struct kndProcBase *base;

    knd_log("PROC: %.*s", self->name_size, self->name);

    for (tr = self->tr; tr; tr = tr->next) {
        knd_log("%*s~ %s %.*s", (self->depth + 1) * KND_OFFSET_SIZE, "",
                tr->locale, tr->val_size, tr->val);
    }

    for (base = self->bases; base; base = base->next) {
	base_str(base, self->depth + 1);
    }
    
    for (arg = self->args; arg; arg = arg->next) {
        arg->depth = self->depth + 1;
        arg->str(arg);
    }

    if (self->proc_call.name_size) {
        knd_log("%*s    {run %.*s", self->depth * KND_OFFSET_SIZE, "",
                self->proc_call.name_size, self->proc_call.name);
        for (tr = self->proc_call.tr; tr; tr = tr->next) {
            knd_log("%*s  ~ %s %.*s", (self->depth + 1) * KND_OFFSET_SIZE, "",
                    tr->locale, tr->val_size, tr->val);
        }

        for (call_arg = self->proc_call.args; call_arg; call_arg = call_arg->next) {
            proc_call_arg_str(call_arg, self->depth + 1);
        }
        knd_log("%*s    }", self->depth * KND_OFFSET_SIZE, "");
    }

    
}

static int get_proc(struct kndProc *self,
                   const char *name, size_t name_size,
                   struct kndProc **result)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    size_t chunk_size = 0;
    size_t *total_size;
    struct kndProcDir *dir;
    struct kndProcArg *arg;
    struct kndProc *proc;
    const char *filename;
    size_t filename_size;
    const char *b;
    struct stat st;
    int fd;
    size_t file_size = 0;
    struct stat file_info;
    int err;

    if (DEBUG_PROC_LEVEL_1)
        knd_log(".. %.*s to get proc: \"%.*s\"  IDX:%p..",
                self->name_size, self->name, name_size, name, self->proc_idx);

    dir = (struct kndProcDir*)self->proc_idx->get(self->proc_idx, name, name_size);
    if (!dir) {
        knd_log("-- no such proc: \"%.*s\" IDX:%p :(", name_size, name, self->proc_idx);
        self->log->reset(self->log);
        err = self->log->write(self->log, name, name_size);
        if (err) return err;
        err = self->log->write(self->log, " Proc name not found",
                               strlen(" Proc name not found"));
        if (err) return err;
        return knd_NO_MATCH;
    }

    if (dir->proc) {
        proc = dir->proc;
        proc->phase = KND_SELECTED;
        proc->task = self->task;
        *result = proc;
        return knd_OK;
    }

    /* parse DB rec */
    filename = self->frozen_output_file_name;
    filename_size = self->frozen_output_file_name_size;
    if (stat(filename, &st)) {
        knd_log("-- no such file: %.*s", filename_size, filename);
        return knd_NO_MATCH; 
    }

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        knd_log("-- error reading FILE \"%.*s\": %d",
                filename_size, filename, fd);
        return knd_IO_FAIL;
    }

    fstat(fd, &file_info);
    file_size = file_info.st_size;  
    if (file_size <= KND_DIR_ENTRY_SIZE) {
        err = knd_LIMIT;
        goto final;
    }

    if (lseek(fd, dir->global_offset, SEEK_SET) == -1) {
        err = knd_IO_FAIL;
        goto final;
    }

    buf_size = dir->block_size;
    if (buf_size >= KND_TEMP_BUF_SIZE) return knd_NOMEM;

    err = read(fd, buf, buf_size);
    if (err == -1) {
        err = knd_IO_FAIL;
        goto final;
    }
    buf[buf_size] = '\0';

    if (DEBUG_PROC_LEVEL_2)
        knd_log("== frozen Proc REC: \"%.*s\" [%zu]",
                buf_size, buf, buf_size);

    /* done reading */
    close(fd);

    err = self->mempool->new_proc(self->mempool, &proc);                            RET_ERR();
    proc->out = self->out;
    proc->log = self->log;
    proc->task = self->task;
    proc->mempool = self->mempool;
    proc->proc_idx = self->proc_idx;
    proc->class_idx = self->class_idx;
    proc->dir = dir;

    memcpy(proc->name, dir->name, dir->name_size);
    proc->name_size = dir->name_size;

    proc->frozen_output_file_name = self->frozen_output_file_name;
    proc->frozen_output_file_name_size = self->frozen_output_file_name_size;

    b = buf + 1;
    bool got_separ = false;
    while (*b) {
        switch (*b) {
        case '{':
        case '}':
        case '[':
        case ']':
            got_separ = true;
            break;
        default:
            break;
        }
        if (got_separ) break;
        b++;
    }

    if (!got_separ) {
        knd_log("-- proc name not found in %.*s :(", buf_size, buf);
        return knd_FAIL;
    }
    total_size = &chunk_size;
    err = proc->read(proc, b, total_size);                                        PARSE_ERR();

    dir->proc = proc;

    /* resolve args */
    for (arg = proc->args; arg; arg = arg->next) {
        err = arg->resolve(arg);                                                  RET_ERR();
    }

    if (DEBUG_PROC_LEVEL_TMP)
        proc->str(proc);

    *result = proc;
    return knd_OK;

 final:
    close(fd);
    return err;
}

static gsl_err_t run_get_proc(void *obj, const char *name, size_t name_size)
{
    struct kndProc *self = obj;
    struct kndProc *proc;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    self->curr_proc = NULL;
    err = get_proc(self, name, name_size, &proc);
    if (err) return make_gsl_err_external(err);

    self->curr_proc = proc;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_proc(void *obj,
                              const char *val __attribute__((unused)),
                              size_t val_size __attribute__((unused)))
{
    struct kndProc *self = obj;
    if (DEBUG_PROC_LEVEL_1)
        knd_log(".. confirm proc read: %p", self);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t present_proc_selection(void *obj,
                                        const char *val __attribute__((unused)),
                                        size_t val_size __attribute__((unused)))
{
    struct kndProc *self = obj;
    struct kndProc *p;
    struct kndOutput *out = self->out;
    int err;

    if (DEBUG_PROC_LEVEL_1)
        knd_log(".. presenting proc selection ..");

    out->reset(out);
    if (!self->curr_proc) return make_gsl_err(gsl_FAIL);

    p = self->curr_proc;
    p->out = out;
    p->task = self->task;
    p->format = KND_FORMAT_JSON;
    p->depth = 0;
    p->max_depth = KND_MAX_DEPTH;

    err = p->export(p);
    if (err) return make_gsl_err_external(err);
    
    return make_gsl_err(gsl_OK);
}

//static gsl_err_t run_set_translation_text(void *obj, const char *val, size_t val_size)
//{
//    struct kndTranslation *tr = obj;
//
//    if (DEBUG_PROC_LEVEL_2)
//        knd_log(".. run set translation text..");
//
//    if (!val_size) return make_gsl_err(gsl_FORMAT);
//    if (val_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);
//
//    if (DEBUG_PROC_LEVEL_2)
//        knd_log(".. run set translation text: %.*s [%lu]\n", val_size, val,
//                (unsigned long)val_size);
//
//    memcpy(tr->val, val, val_size);
//    tr->val_size = val_size;
//
//    return make_gsl_err(gsl_OK);
//}


static int parse_proc_select(struct kndProc *self,
                             const char *rec,
                             size_t *total_size)
{
    int err = knd_FAIL, e;
    gsl_err_t parser_err;

    if (DEBUG_PROC_LEVEL_1)
        knd_log(".. parsing Proc select: \"%.*s\"",
                16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_proc,
          .obj = self
        }/*,
        { .type = GSL_CHANGE_STATE,
          .name = "inst",
          .name_size = strlen("inst"),
          .parse = parse_import_instance,
          .obj = self
          }*/,
        { .name = "default",
          .name_size = strlen("default"),
          .is_default = true,
          .run = present_proc_selection,
          .obj = self
        }
    };

    self->curr_proc = NULL;

    parser_err = gsl_parse_task(rec, total_size, specs,
                         sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- proc parse error: \"%.*s\"",
                self->log->buf_size, self->log->buf);
        if (!self->log->buf_size) {
            e = self->log->write(self->log, "proc parse failure",
                                 strlen("proc parse failure"));
            if (e) return e;
        }
        return gsl_err_to_knd_err_codes(parser_err);
    }

    /* any updates happened? */
    if (self->curr_proc) {
        if (self->curr_proc->inbox_size || self->curr_proc->inst_inbox_size) {
            self->curr_proc->next = self->inbox;
            self->inbox = self->curr_proc;
            self->inbox_size++;
        }
    }
    return knd_OK;
}

static int proc_call_arg_export_GSP(struct kndProc *self,
                                    struct kndProcCallArg *call_arg)
{
    struct kndOutput *out = self->out;
    int err;
    err = out->write(out, "{", 1);                                                RET_ERR();
    err = out->write(out, call_arg->name, call_arg->name_size);                   RET_ERR();
    err = out->write(out, " ", 1);                                                RET_ERR();
    err = out->write(out, call_arg->val, call_arg->val_size);                     RET_ERR();
    err = out->write(out, "}", 1);                                                RET_ERR();
    return knd_OK;
}

static int export_GSP(struct kndProc *self)
{
    struct kndOutput *out = self->out;
    struct kndProcArg *arg;
    struct kndProcCallArg *call_arg;
    struct kndTranslation *tr;
    int err;

    err = out->write(out, "{", strlen("{"));                                      RET_ERR();
    err = out->write(out, self->name, self->name_size);                           RET_ERR();
    if (self->tr) {
        err = out->write(out, "[_g", strlen("[_g"));                              RET_ERR();
    }
    tr = self->tr;
    while (tr) {
        err = out->write(out, "{", 1);                                            RET_ERR();
        err = out->write(out, tr->locale, tr->locale_size);                       RET_ERR();
        err = out->write(out, " ", 1);                                            RET_ERR();
        err = out->write(out, tr->val,  tr->val_size);                            RET_ERR();
        err = out->write(out, "}", 1);                                            RET_ERR();
        tr = tr->next;
    }
    if (self->tr) {
        err = out->write(out, "]", 1);                                            RET_ERR();
    }

    if (self->args) {
        for (arg = self->args; arg; arg = arg->next) {
            arg->format = KND_FORMAT_GSP;
            arg->out = self->out;
            err = arg->export(arg);                                               RET_ERR();
        }
    }

    if (self->proc_call.name_size) {
        err = out->write(out, "{run ", strlen("{run "));                          RET_ERR();
        err = out->write(out, self->proc_call.name, self->proc_call.name_size);   RET_ERR();

        if (self->proc_call.tr) {
            err = out->write(out, "[_g", strlen("[_g"));                          RET_ERR();
        }
        tr = self->proc_call.tr;
        while (tr) {
            err = out->write(out, "{", 1);                                        RET_ERR();
            err = out->write(out, tr->locale, tr->locale_size);                   RET_ERR();
            err = out->write(out, " ", 1);                                        RET_ERR();
            err = out->write(out, tr->val,  tr->val_size);                        RET_ERR();
            err = out->write(out, "}", 1);                                        RET_ERR();
            tr = tr->next;
        }
        if (self->proc_call.tr) {
            err = out->write(out, "]", 1);                                        RET_ERR();
        }

        for (call_arg = self->proc_call.args;
             call_arg;
             call_arg = call_arg->next) {
            err = proc_call_arg_export_GSP(self, call_arg);                       RET_ERR();
        }
        err = out->write(out, "}", 1);                                            RET_ERR();
    }

    err = out->write(out, "}", 1);                                                RET_ERR();
    return knd_OK;
}

static int export_JSON(struct kndProc *self)
{
    struct kndOutput *out = self->out;
    struct kndProcArg *arg;
    struct kndTranslation *tr;
    bool in_list = false;
    int err;

    err = out->write(out, "{", 1);                                                RET_ERR();
    err = out->write(out, "\"_n\":\"", strlen("\"_n\":\""));                      RET_ERR();
    err = out->write(out, self->name, self->name_size);                           RET_ERR();
    err = out->write(out, "\"", 1);                                               RET_ERR();

    /* choose gloss */
    tr = self->tr;
    while (tr) {
        if (memcmp(self->task->locale, tr->locale, tr->locale_size)) {
            goto next_tr;
        }
        err = out->write(out, ",\"gloss\":\"", strlen(",\"gloss\":\""));          RET_ERR();
        err = out->write(out, tr->val,  tr->val_size);                            RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();
        break;
    next_tr:
        tr = tr->next;
    }

    if (self->args) {
        err = out->write(out, ",\"args\":[", strlen(",\"args\":["));              RET_ERR();
        for (arg = self->args; arg; arg = arg->next) {
            arg->format = KND_FORMAT_JSON;
            arg->out = self->out;
            if (in_list) {
                err = out->write(out, ",", 1);                                    RET_ERR();
            }
            err = arg->export(arg);                                               RET_ERR();
            in_list = true;
        }
        err = out->write(out, "]", 1);                                            RET_ERR();
    }


    if (self->proc_call.name_size) {
        err = out->write(out, ",\"run\":{", strlen(",\"run\":{"));                RET_ERR();
        err = out->write(out, "\"_n\":\"", strlen("\"_n\":\""));                  RET_ERR();
        err = out->write(out, self->proc_call.name, self->proc_call.name_size);   RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();

        tr = self->proc_call.tr;
        while (tr) {
            if (memcmp(self->task->locale, tr->locale, tr->locale_size)) {
                goto next_run_tr;
            }
            err = out->write(out, ",\"gloss\":\"", strlen(",\"gloss\":\""));      RET_ERR();
            err = out->write(out, tr->val,  tr->val_size);                        RET_ERR();
            err = out->write(out, "\"", 1);                                       RET_ERR();
            break;
        next_run_tr:
            tr = tr->next;
        }

        /*for (call_arg = self->proc_call.args; call_arg; call_arg = call_arg->next) {
            proc_call_arg_str(call_arg, self->depth + 1);
            }*/
        err = out->write(out, "}", 1);                                            RET_ERR();
    }


    err = out->write(out, "}", 1);                                                RET_ERR();

    return knd_OK;
}

static int export(struct kndProc *self)
{
    int err;

    switch (self->format) {
    case KND_FORMAT_JSON:
        err = export_JSON(self);
        if (err) return err;
        break;
    case KND_FORMAT_GSP:
        err = export_GSP(self);
        if (err) return err;
        break;
    default:
        break;
    }
    
    return knd_OK;
}


static gsl_err_t read_gloss(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndTranslation *tr = obj;
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = tr->val,
          .buf_size = &tr->val_size,
          .max_buf_size = KND_NAME_SIZE
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t gloss_append(void *accu,
                              void *item)
{
    struct kndProc *self = accu;
    struct kndTranslation *tr = item;

    tr->next = self->tr;
    self->tr = tr;
   
    return make_gsl_err(gsl_OK);
}

static gsl_err_t gloss_alloc(void *obj,
                             const char *name,
                             size_t name_size,
                             size_t count,
                             void **item)
{
    struct kndProc *self = obj;
    struct kndTranslation *tr;

    if (name_size > KND_LOCALE_SIZE) return make_gsl_err(gsl_LIMIT);


    /* TODO: mempool alloc */
    //self->mempool->new_text_seq();
    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return make_gsl_err_external(knd_NOMEM);

    memset(tr, 0, sizeof(struct kndTranslation));
    memcpy(tr->curr_locale, name, name_size);
    tr->curr_locale_size = name_size;

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;
    *item = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t proc_call_gloss_alloc(void *obj,
                                       const char *name,
                                       size_t name_size,
                                       size_t count,
                                       void **item)
{
    struct kndProcCall *self = obj;
    struct kndTranslation *tr;

    if (name_size > KND_LOCALE_SIZE) return make_gsl_err(gsl_LIMIT);

    /* TODO: mempool alloc */
    //self->mempool->new_text_seq();
    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return make_gsl_err_external(knd_NOMEM);

    memset(tr, 0, sizeof(struct kndTranslation));
    memcpy(tr->curr_locale, name, name_size);
    tr->curr_locale_size = name_size;

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;
    *item = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t proc_call_gloss_append(void *accu,
                                        void *item)
{
    struct kndProcCall *self = accu;
    struct kndTranslation *tr = item;

    tr->next = self->tr;
    self->tr = tr;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_arg(void *data,
                           const char *rec,
                           size_t *total_size)
{
    struct kndProc *self = data;
    struct kndProcArg *arg;
    int err;

    err = self->mempool->new_proc_arg(self->mempool, &arg);
    if (err) return make_gsl_err_external(err);

    arg->task = self->task;
    err = arg->parse(arg, rec, total_size);
    if (err) return make_gsl_err_external(err);

    arg->parent = self;
    arg->next = self->args;
    self->args = arg;
    self->num_args++;
    return make_gsl_err(gsl_OK);
}

static int arg_item_read(void *obj,
                           const char *name, size_t name_size,
                           const char *rec, size_t *total_size)
{
    struct kndProcBase *base = obj;
    struct kndArgItem *item;
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    int err;

    item = malloc(sizeof(struct kndArgItem));
    memset(item, 0, sizeof(struct kndArgItem));
    memcpy(item->name, name, name_size);
    item->name_size = name_size;
    item->name[name_size] = '\0';

    struct kndTaskSpec specs[] = {
        { .name = "c",
          .name_size = strlen("c"),
          .buf_size = &item->classname_size,
          .max_buf_size = KND_NAME_SIZE,
          .buf = item->classname
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    if (!base->tail) {
        base->tail = item;
        base->args = item;
    }
    else {
        base->tail->next = item;
        base->tail = item;
    }

    base->num_args++;

    return knd_OK;
}

static int parse_base(void *data,
		     const char *rec,
		     size_t *total_size)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size;
    struct kndProc *self = data;
    struct kndProcBase *base;
    int err;

    base = malloc(sizeof(struct kndProcBase));
    memset(base, 0, sizeof(struct kndProcBase));

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .buf_size = &base->name_size,
          .max_buf_size = KND_NAME_SIZE,
          .buf = base->name
        },
        { .type = KND_CHANGE_STATE,
	  .name = "arg_item",
          .name_size = strlen("arg_item"),
          .is_validator = true,
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_SHORT_NAME_SIZE,
          .validate = arg_item_read,
          .obj = base
        }
    };
   
    err = knd_parse_task(rec, total_size, specs,
			 sizeof(specs) / sizeof(struct kndTaskSpec));             RET_ERR();

    /*err = self->mempool->new_proc_base(self->mempool, &base);                       RET_ERR();
    base->task = self->task;
    err = base->parse(base, rec, total_size);                                       PARSE_ERR();
    */

    base->proc = self;
    base->next = self->bases;
    self->bases = base;
    self->num_bases++;

    return knd_OK;
}

static gsl_err_t parse_proc_call_arg(void *obj,
				     const char *name, size_t name_size,
				     const char *rec, size_t *total_size)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size;
    struct kndProc *self = obj;
    struct kndProcCallArg *call_arg;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. Proc Call Arg \"%.*s\" to validate: \"%.*s\"..",
                name_size, name, 32, rec);

    call_arg = malloc(sizeof(struct kndProcCallArg));
    if (!call_arg) return make_gsl_err_external(knd_NOMEM);

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
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size;
    struct kndProc *self = obj;
    int err;

    if (DEBUG_PROC_LEVEL_1)
        knd_log(".. Proc Call parsing: \"%.*s\"..", 32, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf_size = &self->proc_call.name_size,
          .max_buf_size = KND_NAME_SIZE,
          .buf = self->proc_call.name
        },
        { .is_list = true,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .accu = &self->proc_call,
          .alloc = proc_call_gloss_alloc,
          .append = proc_call_gloss_append,
          .parse = read_gloss
        },
        { .is_list = true,
          .name = "_g",
          .name_size = strlen("_g"),
          .accu = &self->proc_call,
          .alloc = proc_call_gloss_alloc,
          .append = proc_call_gloss_append,
          .parse = read_gloss
        },
        { .name = "call_arg",
          .name_size = strlen("call_arg"),
          .is_validator = true,
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_SHORT_NAME_SIZE,
          .validate = parse_proc_call_arg,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int import_proc(struct kndProc *self,
                       const char *rec,
                       size_t *total_size)
{
    struct kndProc *proc;
    struct kndProcDir *dir;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. import Proc: \"%.*s\"..", 32, rec);

    err  = self->mempool->new_proc(self->mempool, &proc);                         RET_ERR();
    proc->out = self->out;
    proc->log = self->log;
    proc->task = self->task;
    proc->mempool = self->mempool;
    proc->proc_idx = self->proc_idx;
    proc->class_idx = self->class_idx;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = proc->name,
          .buf_size = &proc->name_size,
          .max_buf_size = KND_NAME_SIZE
        },
        { .type = GSL_CHANGE_STATE,
          .is_list = true,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .accu = proc,
          .alloc = gloss_alloc,
          .append = gloss_append,
          .parse = read_gloss
        },
        { .is_list = true,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .accu = proc,
          .alloc = gloss_alloc,
          .append = gloss_append,
          .parse = read_gloss
        },
        { .is_list = true,
          .name = "_g",
          .name_size = strlen("_g"),
          .accu = self,
          .alloc = gloss_alloc,
          .append = gloss_append,
          .parse = read_gloss
        },
        { .type = GSL_CHANGE_STATE,
          .name = "base",
          .name_size = strlen("base"),
          .parse = parse_base,
          .obj = proc
        },
        { .type = GSL_CHANGE_STATE,
          .name = "arg",
          .name_size = strlen("arg"),
          .parse = parse_arg,
          .obj = proc
        },
        { .name = "run",
          .name_size = strlen("run"),
          .parse = parse_proc_call,
          .obj = proc
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        err = gsl_err_to_knd_err_codes(parser_err);
        goto final;
    }

    if (!proc->name_size) {
        err = knd_FAIL;
        goto final;
    }

    dir = (struct kndProcDir*)self->proc_idx->get(self->proc_idx,
                                                  proc->name, proc->name_size);
    if (dir) {
        knd_log("-- %s proc name doublet found :(", proc->name);
        self->log->reset(self->log);
        err = self->log->write(self->log,
                               proc->name,
                               proc->name_size);
        if (err) goto final;
        err = self->log->write(self->log,
                               " proc name already exists",
                               strlen(" proc name already exists"));
        if (err) goto final;
        err = knd_FAIL;
        goto final;
    }

    if (!self->batch_mode) {
        proc->next = self->inbox;
        self->inbox = proc;
        self->inbox_size++;
    }

    err = self->mempool->new_proc_dir(self->mempool, &dir);                       RET_ERR();
    dir->proc = proc;
    proc->dir = dir;

    err = self->proc_idx->set(self->proc_idx,
                              proc->name, proc->name_size, (void*)dir);
    if (err) goto final;

    if (DEBUG_PROC_LEVEL_TMP)
        proc->str(proc);
    
    return knd_OK;

 final:
    
    proc->del(proc);
    return err;
}

static int parse_GSL(struct kndProc *self,
                     const char *rec,
                     size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = self->name,
          .buf_size = &self->name_size,
          .max_buf_size = KND_NAME_SIZE
        },
        { .is_list = true,
          .name = "_g",
          .name_size = strlen("_g"),
          .accu = self,
          .alloc = gloss_alloc,
          .append = gloss_append,
          .parse = read_gloss
        },
        { .name = "arg",
          .name_size = strlen("arg"),
          .parse = parse_arg,
          .obj = self
        },
        { .name = "run",
          .name_size = strlen("run"),
          .parse = parse_proc_call,
          .obj = self
        },
        { .name = "default",
          .name_size = strlen("default"),
          .is_default = true,
          .run = confirm_proc,
          .obj = self
        }
    };
    gsl_err_t parser_err;
    
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}

static int kndProc_resolve(struct kndProc *self)
{
    struct kndProcArg *arg = NULL;
    int err;

    if (DEBUG_PROC_LEVEL_TMP)
        knd_log(".. resolving PROC: %.*s",
                self->name_size, self->name);

    for (arg = self->args; arg; arg = arg->next) {
        err = arg->resolve(arg);                                                  RET_ERR();
    }

    return knd_OK;
}

static int resolve_procs(struct kndProc *self)
{
    struct kndProc *proc;
    struct kndProcDir *dir;
    const char *key;
    void *val;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. resolving procs by \"%.*s\"",
                self->name_size, self->name);
    key = NULL;
    self->proc_idx->rewind(self->proc_idx);
    do {
        self->proc_idx->next_item(self->proc_idx, &key, &val);
        if (!key) break;

        dir = (struct kndProcDir*)val;
        proc = dir->proc;
        if (proc->is_resolved) continue;

        err = proc->resolve(proc);
        if (err) {
            knd_log("-- couldn't resolve the \"%s\" proc :(", proc->name);
            return err;
        }

	if (DEBUG_PROC_LEVEL_TMP) {
	    knd_log("--");
	    proc->str(proc);
	}
    } while (key);

    return knd_OK;
}

static int kndProc_coordinate(struct kndProc *self)
{
    struct kndProc *proc;
    struct kndProcDir *dir;
    const char *key;
    void *val;
    int err;

    if (DEBUG_PROC_LEVEL_1)
        knd_log(".. proc coordination in progress ..");

    err = resolve_procs(self);                                                    RET_ERR();

    /* assign ids */
    key = NULL;
    self->proc_idx->rewind(self->proc_idx);
    do {
        self->proc_idx->next_item(self->proc_idx, &key, &val);
        if (!key) break;

        dir = (struct kndProcDir*)val;
        proc = dir->proc;

        /* assign id */
        self->next_id++;
        proc->id = self->next_id;
        proc->phase = KND_CREATED;
    } while (key);

    /* display all procs */
    if (DEBUG_PROC_LEVEL_2) {
        key = NULL;
        self->proc_idx->rewind(self->proc_idx);
        do {
            self->proc_idx->next_item(self->proc_idx, &key, &val);
            if (!key) break;
            dir = (struct kndProcDir*)val;
            proc = dir->proc;
            proc->depth = self->depth + 1;
            proc->str(proc);
        } while (key);
    }

    return knd_OK;
}


static int update_state(struct kndProc *self,
                        struct kndUpdate *update)
{
    struct kndProc *proc;
    struct kndProcUpdate *proc_update;
    struct kndProcUpdate **proc_updates;
    int err;

    /* create index of PROC updates */
    proc_updates = realloc(update->procs,
                          (self->inbox_size * sizeof(struct kndProcUpdate*)));
    if (!proc_updates) return knd_NOMEM;
    update->procs = proc_updates;

    for (proc = self->inbox; proc; proc = proc->next) {
        err = proc->resolve(proc);                                                  RET_ERR();
        err = self->mempool->new_proc_update(self->mempool, &proc_update);          RET_ERR();

        /*self->next_id++;
        proc->id = self->next_id;
        */

        proc_update->proc = proc;

        update->procs[update->num_procs] = proc_update;
        update->num_procs++;
    }
    return knd_OK;
}

static int freeze(struct kndProc *self,
                  size_t *total_frozen_size,
                  char *output,
                  size_t *total_size)
{
    struct kndOutput *out;
    char *curr_dir = output;
    size_t chunk_size;
    size_t curr_dir_size = 0;
    int err;

    out = self->out;
    out->reset(out);

    err = export_GSP(self);
    if (err) {
        knd_log("-- GSP export of %.*s Proc failed :(",
                self->name_size, self->name);
        return err;
    }

    if (DEBUG_PROC_LEVEL_2)
        knd_log("GSP: %.*s  FILE: %s",
                out->buf_size, out->buf, self->frozen_output_file_name);

    /* persistent write */
    err = knd_append_file(self->frozen_output_file_name,
                          out->buf, out->buf_size);
    if (err) return err;

    /* add dir entry */
    memcpy(curr_dir, "{", 1); 
    curr_dir++;
    curr_dir_size++;

    chunk_size = sprintf(curr_dir, "%lu",
                         (unsigned long)self->id);
    curr_dir += chunk_size;
    curr_dir_size += chunk_size;

    memcpy(curr_dir, " ", 1); 
    curr_dir++;
    curr_dir_size++;

    chunk_size = sprintf(curr_dir, "%lu}",
                         (unsigned long)out->buf_size);
    curr_dir_size += chunk_size;

    *total_frozen_size += out->buf_size;
    *total_size = curr_dir_size;

    return knd_OK;
}

extern void kndProc_init(struct kndProc *self)
{
    self->del = del;
    self->str = str;
    self->export = export;
    self->import = import_proc;
    self->resolve = kndProc_resolve;
    self->coordinate = kndProc_coordinate;
    self->read = parse_GSL;
    self->select = parse_proc_select;
    self->get_proc = get_proc;
    self->import = import_proc;
    self->update = update_state;
    self->freeze = freeze;
}

extern int 
kndProc_new(struct kndProc **proc)
{
    struct kndProc *self;

    self = malloc(sizeof(struct kndProc));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndProc));

    kndProc_init(self);

    *proc = self;
    return knd_OK;
}
