#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

#include <gsl-parser.h>
#include <glb-lib/output.h>

#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_class.h"
#include "knd_task.h"
#include "knd_state.h"
#include "knd_mempool.h"
#include "knd_utils.h"
#include "knd_text.h"
#include "knd_dict.h"

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
    const char *arg_type = "";
    size_t arg_type_size = 0;

    if (self->arg) {
        arg_type = self->arg->classname;
        arg_type_size = self->arg->classname_size;
    }

    knd_log("%*s  {%.*s %.*s [class:%.*s]}", depth * KND_OFFSET_SIZE, "",
            self->name_size, self->name,
            self->val_size, self->val, arg_type_size, arg_type);
}

static void base_str(struct kndProcVar *base,
                     size_t depth)
{
    struct kndProcArgVar *arg;

    knd_log("%*sbase => %.*s", depth * KND_OFFSET_SIZE, "",
                base->name_size, base->name);

    for (arg = base->args; arg; arg = arg->next) {
        knd_log("%*s%.*s [class:%.*s]", (depth + 1) * KND_OFFSET_SIZE, "",
                arg->name_size, arg->name,
                arg->classname_size, arg->classname);
    }
    
}

static void str(struct kndProc *self)
{
    struct kndTranslation *tr;
    struct kndProcArg *arg;
    struct kndProcCallArg *call_arg;
    struct kndProcVar *base;

    knd_log("PROC %p: %.*s id:%.*s",
            self, self->name_size, self->name,
            self->entry->id_size, self->entry->id);

    for (tr = self->tr; tr; tr = tr->next) {
        knd_log("%*s~ %s %.*s", (self->depth + 1) * KND_OFFSET_SIZE, "",
                tr->locale, tr->val_size, tr->val);
    }

    for (base = self->bases; base; base = base->next) {
        base_str(base, self->depth + 1);
    }

    if (self->result_classname_size) {
        knd_log("%*s    {result class:%.*s}", self->depth * KND_OFFSET_SIZE, "",
                self->result_classname_size, self->result_classname);
    }

    if (self->estim_cost_total) {
        knd_log("%*s    {total_cost %zu}", self->depth * KND_OFFSET_SIZE, "",
                self->estim_cost_total);
    }
    if (self->estim_time_total) {
        knd_log("%*s    {total_time %zu}", self->depth * KND_OFFSET_SIZE, "",
                self->estim_time_total);
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

static int kndProc_export_SVG_header(struct kndProc *self)
{
    struct glbOutput *out = self->out;
    const char *svg_header = "<svg version=\"1.1\""
        " width=\"100%\" height=\"100%\""
        " xmlns=\"http://www.w3.org/2000/svg\""
        " viewBox=\"0 0 640 480\""
        " xmlns:xlink=\"http://www.w3.org/1999/xlink\">";

    size_t svg_header_size = strlen(svg_header);
    int err;

    err = out->write(out, svg_header, svg_header_size);                           RET_ERR();

    err = out->write(out, "<g", strlen("<g"));                                    RET_ERR();

    err = out->write(out, " transform=\"translate(50,50)\"",
                   strlen(" transform=\"translate(50,50)\""));                  RET_ERR();
    err = out->write(out, ">", 1);                                                RET_ERR();

    return knd_OK;
}

static int kndProc_export_SVG_footer(struct kndProc *self)
{
    struct glbOutput *out = self->out;
    const char *svg_footer = "</g></svg>";
    size_t svg_footer_size = strlen(svg_footer);
    int err;

    err = out->write(out, svg_footer, svg_footer_size);                           RET_ERR();
    return knd_OK;
}

static int get_proc(struct kndProc *self,
                   const char *name, size_t name_size,
                   struct kndProc **result)
{
    char buf[KND_MED_BUF_SIZE];
    size_t buf_size;
    size_t chunk_size = 0;
    size_t *total_size;
    struct kndProcEntry *entry;
    struct kndProc *proc;
    const char *filename;
    size_t filename_size;
    const char *b;
    struct stat st;
    int fd;
    size_t file_size = 0;
    struct stat file_info;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. %.*s to get proc: \"%.*s\"..",
                self->name_size, self->name, name_size, name);

    entry = (struct kndProcEntry*)self->proc_name_idx->get(self->proc_name_idx,
                                                       name, name_size);
    if (!entry) {
        knd_log("-- no such proc: \"%.*s\" IDX:%p :(",
                name_size, name, self->proc_name_idx);
        self->log->reset(self->log);
        err = self->log->write(self->log, name, name_size);
        if (err) return err;
        err = self->log->write(self->log, " Proc name not found",
                               strlen(" Proc name not found"));
        if (err) return err;
        return knd_NO_MATCH;
    }

    if (entry->phase == KND_REMOVED) {
        knd_log("-- \"%s\" proc was removed", name);
        self->log->reset(self->log);
        err = self->log->write(self->log, name, name_size);
        if (err) return err;
        err = self->log->write(self->log, " proc was removed",
                               strlen(" proc was removed"));
        if (err) return err;
        
        self->task->http_code = HTTP_GONE;
        return knd_NO_MATCH;
    }
    
    if (entry->proc) {
        proc = entry->proc;

        entry->phase = KND_SELECTED;
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

    if (lseek(fd, entry->global_offset, SEEK_SET) == -1) {
        err = knd_IO_FAIL;
        goto final;
    }


    buf_size = entry->block_size;
    if (buf_size >= sizeof buf) return knd_NOMEM;

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
    proc->proc_name_idx = self->proc_name_idx;
    proc->proc_idx = self->proc_idx;
    proc->class_name_idx = self->class_name_idx;
    proc->entry = entry;

    memcpy(proc->name, entry->name, entry->name_size);
    proc->name_size = entry->name_size;

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

    err = proc->resolve(proc);                                                    RET_ERR();
    entry->proc = proc;

    if (DEBUG_PROC_LEVEL_2)
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
    struct glbOutput *out = self->out;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. presenting proc selection..");

    out->reset(out);
    if (!self->curr_proc) return make_gsl_err(gsl_FAIL);

    p = self->curr_proc;
    p->out = out;
    p->task = self->task;
    p->visual = &self->task->visual;
    p->format = self->task->format;
    p->depth = 0;
    p->max_depth = KND_MAX_DEPTH;

    /* export HEADER */
    switch (p->format) {
    case KND_FORMAT_SVG:
        err = kndProc_export_SVG_header(p);
        if (err) return make_gsl_err_external(err);
     break;
    case KND_FORMAT_HTML:
        err = kndProc_export_SVG_header(p);
        if (err) return make_gsl_err_external(err);
     break;
    default:
        break;
    }

    /* export BODY */
    err = p->export(p);
    if (err) return make_gsl_err_external(err);

    /* export FOOTER */
    switch (p->format) {
    case KND_FORMAT_SVG:
        err = kndProc_export_SVG_footer(p);
        if (err) return make_gsl_err_external(err);
     break;
    case KND_FORMAT_HTML:
        err = kndProc_export_SVG_footer(p);
        if (err) return make_gsl_err_external(err);
     break;
    default:
        break;
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t remove_proc(void *obj, const char *name, size_t name_size)
{
    struct kndProc *self = obj;
    struct kndProc *proc;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. removing proc..");

    if (!self->curr_proc) {
        knd_log("-- remove operation: proc name not specified:(");

        self->log->reset(self->log);
        err = self->log->write(self->log, name, name_size);
        if (err) return make_gsl_err_external(err);
        err = self->log->write(self->log, " class name not specified",
                               strlen(" class name not specified"));
        if (err) return make_gsl_err_external(err);
        return make_gsl_err(gsl_NO_MATCH);
    }

    proc = self->curr_proc;

    if (DEBUG_PROC_LEVEL_2)
        knd_log("== proc to remove: \"%.*s\"\n", proc->name_size, proc->name);

    proc->entry->phase = KND_REMOVED;

    self->log->reset(self->log);
    err = self->log->write(self->log, proc->name, proc->name_size);
    if (err) return make_gsl_err_external(err);
    err = self->log->write(self->log, " proc removed",
                           strlen(" proc removed"));
    if (err) return make_gsl_err_external(err);

    proc->next = self->inbox;
    self->inbox = proc;
    self->inbox_size++;

    return make_gsl_err(gsl_OK);
}

static int parse_proc_select(struct kndProc *self,
                             const char *rec,
                             size_t *total_size)
{
    gsl_err_t parser_err;
    int e;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. parsing Proc select: \"%.*s\"",
                16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_proc,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "_rm",
          .name_size = strlen("_rm"),
          .run = remove_proc,
          .obj = self
        }/*,
        { .type = GSL_SET_STATE,
          .name = "inst",
          .name_size = strlen("inst"),
          .parse = parse_import_instance,
          .obj = self
          }*/,
        { .is_default = true,
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
    struct glbOutput *out = self->out;
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
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size = 0;
    struct glbOutput *out = self->out;
    struct kndProcArg *arg;
    struct kndProcCallArg *call_arg;
    struct kndTranslation *tr;
    int err;

    err = out->writec(out, '{');
    if (err) return err;
    err = out->write(out, self->entry->id, self->entry->id_size);
    if (err) return err;
    err = out->writec(out, ' ');
    if (err) return err;

    err = out->write(out, self->name, self->name_size);                           RET_ERR();
    if (self->tr) {
         err = out->write(out, "[_g", strlen("[_g"));                              RET_ERR();
    }

    tr = self->tr;
    while (tr) {
        err = out->write(out, "{", 1);                                            RET_ERR();
        err = out->write(out, tr->locale, tr->locale_size);                       RET_ERR();
        err = out->write(out, "{t ", 3);                                          RET_ERR();
        err = out->write(out, tr->val,  tr->val_size);                            RET_ERR();
        err = out->write(out, "}}", 2);                                           RET_ERR();
        tr = tr->next;
    }
    if (self->tr) {
        err = out->write(out, "]", 1);                                            RET_ERR();
    }


    if (self->summary) {
        err = out->write(out, "[_summary", strlen("[_summary"));                  RET_ERR();
    }

    tr = self->summary;
    while (tr) {
        err = out->write(out, "{", 1);                                            RET_ERR();
        err = out->write(out, tr->locale, tr->locale_size);                       RET_ERR();
        err = out->write(out, "{t ", 3);                                          RET_ERR();
        err = out->write(out, tr->val,  tr->val_size);                            RET_ERR();
        err = out->write(out, "}}", 2);                                           RET_ERR();
        tr = tr->next;
    }
    if (self->summary) {
        err = out->write(out, "]", 1);                                            RET_ERR();
    }


    if (self->estim_cost) {
        err = out->write(out, "{estim", strlen("{estim"));                        RET_ERR();

        buf_size = sprintf(buf, "{cost %zu}", self->estim_cost);
        err = out->write(out, buf, buf_size);                                     RET_ERR();

        if (self->estim_time_total) {
            buf_size = sprintf(buf, "{time %zu}", self->estim_time);
            err = out->write(out, buf, buf_size);                                 RET_ERR();
        }
        err = out->write(out, "}", 1);                                            RET_ERR();
    }
    
    if (self->args) {
        err = out->write(out, "[arg ", strlen("[arg "));                          RET_ERR();
        for (arg = self->args; arg; arg = arg->next) {
            arg->format = KND_FORMAT_GSP;
            arg->out = self->out;
            err = arg->export(arg);                                               RET_ERR();
        }
        err = out->writec(out, ']');                                              RET_ERR();
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
            err = out->write(out, "{t ", 3);                                      RET_ERR();
            err = out->write(out, tr->val,  tr->val_size);                        RET_ERR();
            err = out->write(out, "}}", 2);                                       RET_ERR();
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

static int proc_call_arg_export_JSON(struct kndProc *self,
                                     struct kndProcCallArg *call_arg)
{
    struct glbOutput  *out = self->out;
    int err;
    err = out->writec(out, '"');                                                  RET_ERR();
    err = out->write(out, call_arg->name, call_arg->name_size);                   RET_ERR();
    err = out->writec(out, '"');                                                  RET_ERR();
    err = out->writec(out, ':');                                                  RET_ERR();
    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

static int export_JSON(struct kndProc *self)
{
    struct glbOutput  *out = self->out;
    struct kndProcArg *arg;
    struct kndProcCallArg *carg;
    struct kndTranslation *tr;
    bool in_list = false;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. proc \"%.*s\" export JSON..",
                self->name_size, self->name);

    err = out->write(out, "{", 1);                                                RET_ERR();

    if (self->name_size) {
        err = out->write(out, "\"_name\":\"", strlen("\"_name\":\""));            RET_ERR();
        err = out->write(out, self->name, self->name_size);                       RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();
        in_list = true;
    }

    /* choose gloss */
    tr = self->tr;
    while (tr) {
        if (memcmp(self->task->locale, tr->locale, tr->locale_size)) {
            goto next_tr;
        }
        if (in_list) {
            err = out->write(out, ",", 1);                                    RET_ERR();
        }
        err = out->write(out, "\"_gloss\":\"", strlen("\"_gloss\":\""));        RET_ERR();
        err = out->write(out, tr->val,  tr->val_size);                            RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();
        break;
    next_tr:
        tr = tr->next;
    }

    for (tr = self->summary; tr; tr = tr->next) { 
        if (memcmp(self->task->locale, tr->locale, tr->locale_size)) {
            continue;
        }
        if (in_list) {
            err = out->write(out, ",", 1);                                        RET_ERR();
        }
        err = out->write(out, "\"_summary\":\"", strlen("\"_summary\":\""));      RET_ERR();
        err = out->write_escaped(out, tr->val,  tr->val_size);                    RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();
        break;
    }

    if (self->args) {
        if (in_list) {
            err = out->write(out, ",", 1);                                        RET_ERR();
        }
        err = out->write(out, "\"args\":[", strlen("\"args\":["));                RET_ERR();
        for (arg = self->args; arg; arg = arg->next) {
            arg->format = KND_FORMAT_JSON;
            arg->out = self->out;
            if (in_list) {
                err = out->write(out, ",", 1);                                    RET_ERR();
            }

            arg->task = self->task;
            err = arg->export(arg);                                               RET_ERR();
            in_list = true;
        }
        err = out->write(out, "]", 1);                                            RET_ERR();
    }

    if (self->proc_call.name_size) {
        if (in_list) {
            err = out->write(out, ",", 1);                                        RET_ERR();
        }
        err = out->write(out, "\"run\":{", strlen("\"run\":{"));                  RET_ERR();
        err = out->write(out, "\"_name\":\"", strlen("\"_name\":\""));            RET_ERR();
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

        for (carg = self->proc_call.args; carg; carg = carg->next) {
            err = out->writec(out, ',');                                          RET_ERR();
            err = proc_call_arg_export_JSON(self, carg);                          RET_ERR();
        }

        err = out->write(out, "}", 1);                                            RET_ERR();
    }


    err = out->write(out, "}", 1);                                                RET_ERR();

    return knd_OK;
}


static int export_SVG(struct kndProc *self)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size = 0;
    struct glbOutput *out = self->out;
    struct kndProcArg *arg;
    struct kndTranslation *tr;

    size_t x_offset = 0;
    size_t y_offset = 0;
    int err;

    /*x_offset += self->visual->text_hangindent_size;
    y_offset += self->visual->text_line_height;
    */

    /* choose gloss */
    tr = self->tr;
    while (tr) {
        if (memcmp(self->task->locale, tr->locale, tr->locale_size)) {
            goto next_tr;
        }
        err = out->write(out, "<text", strlen("<text"));          RET_ERR();
        buf_size = sprintf(buf, " x=\"%zu\"", x_offset);
        err = out->write(out, buf, buf_size);          RET_ERR();
        buf_size = sprintf(buf, " y=\"%zu\"", y_offset);
        err = out->write(out, buf, buf_size);          RET_ERR();
        err = out->write(out, ">", 1);          RET_ERR();

        err = out->write(out, tr->val,  tr->val_size);                            RET_ERR();
        err = out->write(out, "</text>", strlen("</text>"));                        RET_ERR();
        break;
    next_tr:
        tr = tr->next;
    }

    /* no gloss found - print id */
    if (!tr) {
        err = out->write(out, "<text>", strlen("<text>"));                            RET_ERR();
        err = out->write(out, self->name, self->name_size);                           RET_ERR();
        err = out->write(out, "</text>", strlen("</text>"));                          RET_ERR();
    }

    if (self->estim_cost_total) {
        x_offset = 20;
        err = out->write(out, "<text text-anchor=\"end\"",
                         strlen("<text text-anchor=\"end\""));                      RET_ERR();
        buf_size = sprintf(buf, " x=\"-%zu\"", x_offset);
        err = out->write(out, buf, buf_size);          RET_ERR();
        buf_size = sprintf(buf, " y=\"%zu\"", y_offset);
        err = out->write(out, buf, buf_size);          RET_ERR();
        err = out->write(out, ">", 1);          RET_ERR();

        buf_size = sprintf(buf, "%zu", self->estim_cost_total);
        err = out->write(out, buf, buf_size);                                     RET_ERR();

        err = out->write(out, "</text>", strlen("</text>"));                      RET_ERR();
    }

    if (self->args) {
        x_offset = 0;
        err = out->write(out,   "<g", strlen("<g"));                              RET_ERR();
        buf_size = sprintf(buf, " transform=\"translate(%zu,%zu)\"",
                           x_offset, y_offset);
        err = out->write(out,  buf, buf_size);                                   RET_ERR();
        err = out->write(out, ">", 1);                                            RET_ERR();

        x_offset = 0;
        y_offset = 0;
        for (arg = self->args; arg; arg = arg->next) {
            arg->format = self->format;
            arg->visual = self->visual;
            arg->out = self->out;
            y_offset += self->visual->text_line_height;

            err = out->write(out,   "<g", strlen("<g"));                          RET_ERR();
            buf_size = sprintf(buf, " transform=\"translate(%zu,%zu)\"",
                               x_offset, y_offset);
            err = out->write(out,   buf, buf_size);                               RET_ERR();
            err = out->write(out, ">", 1);                                        RET_ERR();

            err = arg->export(arg);                                               RET_ERR();
            err = out->write(out, "</g>", strlen("</g>"));                        RET_ERR();
        }
        err = out->write(out, "</g>", strlen("</g>"));                            RET_ERR();
    }

    /*if (self->proc_call.name_size) {
        err = out->write(out, ",\"run\":{", strlen(",\"run\":{"));                RET_ERR();
        err = out->write(out, "\"_n\":\"", strlen("\"_n\":\""));                  RET_ERR();
        err = out->write(out, self->proc_call.name, self->proc_call.name_size);   RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();

        for (call_arg = self->proc_call.args; call_arg; call_arg = call_arg->next) {
            proc_call_arg_str(call_arg, self->depth + 1);
            }
        err = out->write(out, "}", 1);                                            RET_ERR();
    }
    */

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
    case KND_FORMAT_SVG:
        err = export_SVG(self);
        if (err) return err;
        break;
    default:
        break;
    }
    
    return knd_OK;
}

static gsl_err_t alloc_gloss_item(void *obj __attribute__((unused)),
                                  const char *name,
                                  size_t name_size,
                                  size_t count __attribute__((unused)),
                                  void **item)
{
    struct kndTranslation *tr;

    assert(name == NULL && name_size == 0);

    /* TODO: mempool alloc */
    //self->mempool->new_text_seq();
    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return make_gsl_err_external(knd_NOMEM);

    memset(tr, 0, sizeof(struct kndTranslation));

    *item = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t append_gloss_item(void *accu,
                                   void *item)
{
    struct kndProc *self = accu;
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

    if (DEBUG_PROC_LEVEL_1)
        knd_log(".. read gloss translation: \"%.*s\",  text: \"%.*s\"",
                tr->locale_size, tr->locale, tr->val_size, tr->val);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss(void *obj,
                             const char *rec,
                             size_t *total_size)
{
    struct kndProc *self = obj;
    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .alloc = alloc_gloss_item,
        .append = append_gloss_item,
        .accu = self,
        .parse = parse_gloss_item
    };

    return gsl_parse_array(&item_spec, rec, total_size);
}



static gsl_err_t alloc_summary_item(void *obj __attribute__((unused)),
                                  const char *name,
                                  size_t name_size,
                                  size_t count __attribute__((unused)),
                                  void **item)
{
    struct kndTranslation *tr;

    assert(name == NULL && name_size == 0);

    /* TODO: mempool alloc */
    //self->mempool->new_text_seq();
    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return make_gsl_err_external(knd_NOMEM);

    memset(tr, 0, sizeof(struct kndTranslation));

    *item = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t append_summary_item(void *accu,
                                   void *item)
{
    struct kndProc *self = accu;
    struct kndTranslation *tr = item;

    tr->next = self->summary;
    self->summary = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_summary_item(void *obj,
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

    if (DEBUG_PROC_LEVEL_1)
        knd_log(".. read summary translation: \"%.*s\",  text: \"%.*s\"",
                tr->locale_size, tr->locale, tr->val_size, tr->val);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_summary(void *obj,
                             const char *rec,
                             size_t *total_size)
{
    struct kndProc *self = obj;
    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .alloc = alloc_summary_item,
        .append = append_summary_item,
        .accu = self,
        .parse = parse_summary_item
    };

    return gsl_parse_array(&item_spec, rec, total_size);
}

static gsl_err_t alloc_proc_arg(void *obj,
                                const char *name __attribute__((unused)),
                                size_t name_size __attribute__((unused)),
                                size_t count __attribute__((unused)),
                                void **item)
{
    struct kndProc *self = obj;
    struct kndProcArg *arg;
    int err;

    err = self->mempool->new_proc_arg(self->mempool, &arg);
    if (err) return make_gsl_err_external(err);

    arg->task = self->task;
    arg->parent = self;

    *item = arg;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t append_proc_arg(void *accu,
                                 void *item)
{
    struct kndProc *self = accu;
    struct kndProcArg *arg = item;

    arg->next = self->args;
    self->args = arg;
    self->num_args++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_proc_arg(void *obj,
                                const char *rec,
                                size_t *total_size)
{
    struct kndProcArg *arg = obj;
    int err;

    err = arg->parse(arg, rec, total_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}




static gsl_err_t arg_item_read(void *obj,
                               const char *name, size_t name_size,
                               const char *rec, size_t *total_size)
{
    struct kndProcVar *base = obj;
    struct kndProcArgVar *item;
    gsl_err_t parser_err;

    item = malloc(sizeof(struct kndProcArgVar));
    memset(item, 0, sizeof(struct kndProcArgVar));
    memcpy(item->name, name, name_size);
    item->name_size = name_size;
    item->name[name_size] = '\0';

    struct gslTaskSpec specs[] = {
        { .name = "c",
          .name_size = strlen("c"),
          .buf_size = &item->classname_size,
          .max_buf_size = KND_NAME_SIZE,
          .buf = item->classname
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs,
                                sizeof(specs) / sizeof(specs[0]));
    if (parser_err.code) return parser_err;
    
    if (!base->tail) {
        base->tail = item;
        base->args = item;
    }
    else {
        base->tail->next = item;
        base->tail = item;
    }

    base->num_args++;

    return make_gsl_err(gsl_OK);
}

static int inherit_args(struct kndProc *self, struct kndProc *parent)
{
    struct kndProcEntry *entry;
    struct kndProcArg *arg;
    struct kndProc *proc;
    struct kndProcArgEntry *arg_entry;
    struct kndProcVar *base;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. \"%.*s\" proc to inherit args from \"%.*s\" (num args:%zu)",
                self->name_size, self->name, parent->name_size, parent->name, parent->num_args);

    if (!parent->is_resolved) {
        err = parent->resolve(parent);                                            RET_ERR();
    }

    /* check circled relations */
    for (size_t i = 0; i < self->num_inherited; i++) {
        entry = self->inherited[i];
        proc = entry->proc;

        if (DEBUG_PROC_LEVEL_2)
            knd_log("== (%zu of %zu)  \"%.*s\" is a parent of \"%.*s\"", 
                    i, self->num_inherited, proc->name_size, proc->name,
                    self->name_size, self->name);
        if (entry->proc == parent) {
            knd_log("-- circle inheritance detected for \"%.*s\" :(",
                    parent->name_size, parent->name);
            return knd_FAIL;
        }
    }

    /* get args from parent */
    for (arg = parent->args; arg; arg = arg->next) {

        /* compare with exiting args */
        arg_entry = self->arg_idx->get(self->arg_idx,
                                   arg->name, arg->name_size);
        if (arg_entry) {
            knd_log("-- arg \"%.*s\" collision between \"%.*s\""
                    " and parent proc \"%.*s\"?",
                    arg_entry->name_size, arg_entry->name,
                    self->name_size, self->name,
                    parent->name_size, parent->name);
            return knd_OK;
        }

        /* register arg entry */
        arg_entry = malloc(sizeof(struct kndProcArgEntry));
        if (!arg_entry) return knd_NOMEM;

        memset(arg_entry, 0, sizeof(struct kndProcArgEntry));
        memcpy(arg_entry->name, arg->name, arg->name_size);
        arg_entry->name_size = arg->name_size;
        arg_entry->arg = arg;

        if (DEBUG_PROC_LEVEL_2)
            knd_log("NB: ++ proc \"%.*s\" inherits arg \"%.*s\" from \"%.*s\"",
                    self->name_size, self->name,
                    arg->name_size, arg->name,
                    parent->name_size, parent->name);

        err = self->arg_idx->set(self->arg_idx,
                                 arg_entry->name, arg_entry->name_size, (void*)arg_entry);
        if (err) return err;
    }
    
    if (self->num_inherited >= KND_MAX_INHERITED) {
        knd_log("-- max inherited exceeded for %.*s :(",
                self->name_size, self->name);
        return knd_FAIL;
    }

    if (DEBUG_PROC_LEVEL_2)
        knd_log(" .. add \"%.*s\" parent to \"%.*s\"",
                parent->entry->proc->name_size,
                parent->entry->proc->name,
                self->name_size, self->name);

    self->inherited[self->num_inherited] = parent->entry;
    self->num_inherited++;

    /* contact the grandparents */
    for (base = parent->bases; base; base = base->next) {
        if (base->proc) {
            err = inherit_args(self, base->proc);                                 RET_ERR();
        }
    }
    
    return knd_OK;
}

static gsl_err_t parse_base(void *data,
                            const char *rec,
                            size_t *total_size)
{
    struct kndProc *self = data;
    struct kndProcVar *base;
    gsl_err_t parser_err;

    /*err = self->mempool->new_proc_base(self->mempool, &base);                       RET_ERR();
    base->task = self->task;
    err = base->parse(base, rec, total_size);                                       PARSE_ERR();
    */

    base = malloc(sizeof(struct kndProcVar));
    memset(base, 0, sizeof(struct kndProcVar));

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf_size = &base->name_size,
          .max_buf_size = KND_NAME_SIZE,
          .buf = base->name
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = arg_item_read,
          .obj = base
        }
    };
   
    parser_err = gsl_parse_task(rec, total_size, specs,
                                sizeof(specs) / sizeof(specs[0]));
    if (parser_err.code) return parser_err;

    base->proc = self;
    base->next = self->bases;
    self->bases = base;
    self->num_bases++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_estim(void *data,
                             const char *rec,
                             size_t *total_size)
{
    struct kndProc *self = data;
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .type = GSL_SET_STATE,
          .name = "cost",
          .name_size = strlen("cost"),
          .parse = gsl_parse_size_t,
          .obj = (void*)&self->estim_cost
        },
        { .name = "cost",
          .name_size = strlen("cost"),
          .parse = gsl_parse_size_t,
          .obj = (void*)&self->estim_cost
        },
        { .type = GSL_SET_STATE,
          .name = "time",
          .name_size = strlen("time"),
          .parse = gsl_parse_size_t,
          .obj = (void*)&self->estim_time
       },
        { .name = "time",
          .name_size = strlen("time"),
          .parse = gsl_parse_size_t,
          .obj = (void*)&self->estim_time
       }
    };
   
    parser_err = gsl_parse_task(rec, total_size, specs,
                                sizeof(specs) / sizeof(specs[0]));
    if (parser_err.code) return parser_err;

    self->estim_cost_total = self->estim_cost;
    self->estim_time_total = self->estim_time;

    return make_gsl_err(gsl_OK);
}
    
static gsl_err_t parse_proc_call_arg(void *obj,
                                     const char *name, size_t name_size,
                                     const char *rec, size_t *total_size)
{
    struct kndProcCall *self = obj;
    struct kndProcCallArg *call_arg;
    struct kndClassVar *class_var;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. Proc Call Arg \"%.*s\" to validate: \"%.*s\"..",
                name_size, name, 32, rec);

    // TODO: use mempool
    call_arg = malloc(sizeof(struct kndProcCallArg));
    if (!call_arg) return make_gsl_err_external(knd_NOMEM);

    memset(call_arg, 0, sizeof(struct kndProcCallArg));
    memcpy(call_arg->name, name, name_size);
    call_arg->name_size = name_size;

    call_arg->next = self->args;
    self->args = call_arg;
    self->num_args++;

    err = self->mempool->new_conc_item(self->mempool, &class_var);
    if (err) {
        knd_log("-- class var alloc failed :(");
        return make_gsl_err_external(err);
    }

    parser_err = import_class_var(class_var, rec, total_size);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_proc_call(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct kndProcCall *self = obj;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. Proc Call parsing: \"%.*s\"..", 32, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf_size = &self->name_size,
          .max_buf_size = KND_NAME_SIZE,
          .buf = self->name
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = parse_gloss,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_summary",
          .name_size = strlen("_summary"),
          .parse = parse_summary,
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
        }/*,
        { .is_default = true,
          .run = confirm_proc,
          .obj = self
          }*/
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int import_proc(struct kndProc *self,
                       const char *rec,
                       size_t *total_size)
{
    struct kndProc *proc;
    struct kndProcEntry *entry;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. import Proc: \"%.*s\"..", 32, rec);

    err  = self->mempool->new_proc(self->mempool, &proc);                         RET_ERR();

    proc->out = self->out;
    proc->log = self->log;
    proc->task = self->task;
    proc->mempool = self->mempool;
    proc->proc_name_idx = self->proc_name_idx;
    proc->proc_idx = self->proc_idx;
    proc->class_name_idx = self->class_name_idx;
    proc->class_idx = self->class_idx;

    

    struct gslTaskSpec proc_arg_spec = {
        .is_list_item = true,
        .alloc = alloc_proc_arg,
        .append = append_proc_arg,
        .parse = parse_proc_arg,
        .accu = proc
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = proc->name,
          .buf_size = &proc->name_size,
          .max_buf_size = KND_NAME_SIZE
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = parse_gloss,
          .obj = proc
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_summary",
          .name_size = strlen("_summary"),
          .parse = parse_summary,
          .obj = proc
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = parse_gloss,
          .obj = proc
        },
        { .type = GSL_SET_STATE,
          .name = "base",
          .name_size = strlen("base"),
          .parse = parse_base,
          .obj = proc
        },
        { .name = "estim",
          .name_size = strlen("estim"),
          .parse = parse_estim,
          .obj = proc
        },
        { .type = GSL_SET_STATE,
          .name = "estim",
          .name_size = strlen("estim"),
          .parse = parse_estim,
          .obj = proc
        },
        { .type = GSL_SET_STATE,
          .name = "result",
          .name_size = strlen("result"),
          .buf = proc->result_classname,
          .buf_size = &proc->result_classname_size,
          .max_buf_size = KND_NAME_SIZE
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "arg",
          .name_size = strlen("arg"),
          .parse = gsl_parse_array,
          .obj = &proc_arg_spec
        },
        { .name = "run",
          .name_size = strlen("run"),
          .parse = parse_proc_call,
          .obj = &proc->proc_call
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        err = gsl_err_to_knd_err_codes(parser_err);
        goto final;
    }

    if (DEBUG_PROC_LEVEL_2)
        knd_log("++ import Proc: \"%.*s\"!",
                proc->name_size, proc->name);

    if (!proc->name_size) {
        err = knd_FAIL;
        goto final;
    }

    entry = self->proc_name_idx->get(self->proc_name_idx,
                                   proc->name, proc->name_size);
    if (entry) {
        if (entry->phase == KND_REMOVED) {
            knd_log("== proc was removed recently");
        } else {

            knd_log("-- %s proc name doublet found :( log:%p", proc->name, self->log);
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
    }

    if (!self->batch_mode) {
        proc->next = self->inbox;
        self->inbox = proc;
        self->inbox_size++;
    }

    err = self->mempool->new_proc_entry(self->mempool, &entry);                       RET_ERR();
    entry->proc = proc;
    proc->entry = entry;

    self->num_procs++;
    entry->numid = self->num_procs;
    entry->id_size = KND_ID_SIZE;
    knd_num_to_str(entry->numid, entry->id, &entry->id_size, KND_RADIX_BASE);

    err = self->proc_name_idx->set(self->proc_name_idx,
                                   proc->name, proc->name_size, (void*)entry);
    if (err) goto final;

    if (DEBUG_PROC_LEVEL_2)
        proc->str(proc);

    return knd_OK;

 final:
    
    //proc->del(proc);
    return err;
}

static int parse_GSL(struct kndProc *self,
                     const char *rec,
                     size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
   
    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. parse proc \"%.*s\" GSL: \"%.*s\"..",
                self->name_size, self->name, 128, rec);

    struct gslTaskSpec proc_arg_spec = {
        .is_list_item = true,
        .alloc = alloc_proc_arg,
        .append = append_proc_arg,
        .parse = parse_proc_arg,
        .accu = self
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_NAME_SIZE
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = parse_gloss,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_summary",
          .name_size = strlen("_summary"),
          .parse = parse_summary,
          .obj = self
        },
        { .name = "estim",
          .name_size = strlen("estim"),
          .parse = parse_estim,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "arg",
          .name_size = strlen("arg"),
          .parse = gsl_parse_array,
          .obj = &proc_arg_spec
        },
        { .name = "run",
          .name_size = strlen("run"),
          .parse = parse_proc_call,
          .obj = &self->proc_call
        }
    };
    gsl_err_t parser_err;
    
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    if (buf_size) {
        memcpy(self->name, buf, buf_size);
        self->name_size = buf_size;
    }

    return knd_OK;
}

static int resolve_parents(struct kndProc *self)
{
    struct kndProcVar *base;
    struct kndProc *proc;
    struct kndProcEntry *entry;
    struct kndProcArg *arg;
    struct kndProcArgEntry *arg_entry;
    struct kndProcArgVar *arg_item;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. resolve parent procs of \"%.*s\"..",
                self->name_size, self->name);

    /* resolve refs  */
    for (base = self->bases; base; base = base->next) {
        if (DEBUG_PROC_LEVEL_2)
            knd_log("\n.. \"%.*s\" proc to get its parent: \"%.*s\"..",
                    self->name_size, self->name,
                    base->name_size, base->name);

        err = get_proc(self, base->name, base->name_size, &proc);                 RET_ERR();
        if (proc == self) {
            knd_log("-- self reference detected in \"%.*s\" :(",
                    base->name_size, base->name);
            return knd_FAIL;
        }

        base->proc = proc;

        /* should we keep track of our children? */
        /*if (c->ignore_children) continue; */

        /* check base doublets */
        for (size_t i = 0; i < self->num_children; i++) {
            entry = self->children[i];
            if (entry->proc == self) {
                knd_log("-- doublet proc found in \"%.*s\" :(",
                        self->name_size, self->name);
                return knd_FAIL;
            }
        }

        /*if (proc->num_children >= KND_MAX_PROC_CHILDREN) {
            knd_log("-- %s as child to %s - max proc children exceeded :(",
                    self->name, base->name);
            return knd_FAIL;
        }
        entry = &proc->children[proc->num_children];
        entry->proc = self;
        proc->num_children++;
        */
        if (DEBUG_PROC_LEVEL_2)
            knd_log("\n\n.. children of proc \"%.*s\": %zu",
                    proc->name_size, proc->name, proc->num_children);

        
        err = inherit_args(self, base->proc);                                     RET_ERR();

        /* redefine inherited args if needed */

        for (arg_item = base->args; arg_item; arg_item = arg_item->next) {
            arg_entry = self->arg_idx->get(self->arg_idx,
                                       arg_item->name, arg_item->name_size);
            if (!arg_entry) {
                knd_log("-- no arg \"%.*s\" in proc \"%.*s\' :(",
                        arg_item->name_size, arg_item->name,
                        proc->name_size, proc->name);
                return knd_FAIL;
            }

            /* TODO: check class inheritance */

            if (DEBUG_PROC_LEVEL_2)
                knd_log(".. arg \"%.*s\" [class:%.*s] to replace \"%.*s\" [class:%.*s]",
                        arg_item->name_size, arg_item->name,
                        arg_item->classname_size, arg_item->classname,
                        arg_entry->arg->name_size, arg_entry->arg->name,
                        arg_entry->arg->classname_size, arg_entry->arg->classname);

            err = self->mempool->new_proc_arg(self->mempool, &arg);
            if (err) return err;

            memcpy(arg->name,
                   arg_item->name, arg_item->name_size);
            arg->name_size = arg_item->name_size;
            memcpy(arg->classname,
                   arg_item->classname, arg_item->classname_size);
            arg->classname_size = arg_item->classname_size;

            arg_entry->arg = arg;

            arg->parent = self;
            arg->next = self->args;
            self->args = arg;
            self->num_args++;
        }

    }

    return knd_OK;
}

static int resolve_proc_call(struct kndProc *self)
{
    struct kndProcCallArg *call_arg;
    struct kndProcArgEntry *entry;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. resolving proc call %.*s ..",
                self->proc_call.name_size, self->proc_call.name);

    if (!self->arg_idx) return knd_FAIL;

    for (call_arg = self->proc_call.args; call_arg; call_arg = call_arg->next) {

        if (DEBUG_PROC_LEVEL_2)
            knd_log(".. proc call arg %.*s ..",
                    call_arg->name_size, call_arg->name,
                    call_arg->val_size, call_arg->val);

        entry = self->arg_idx->get(self->arg_idx,
                                   call_arg->val, call_arg->val_size);
        if (!entry) {
            knd_log("-- couldn't resolve proc call arg %.*s: %.*s :(",
                call_arg->name_size, call_arg->name,
                call_arg->val_size, call_arg->val);
            return knd_FAIL;
        }

        call_arg->arg = entry->arg;
    }

    return knd_OK;
}

static int kndProc_resolve(struct kndProc *self)
{
    struct kndProcArg *arg = NULL;
    struct kndProcArgEntry *arg_entry;
    struct kndProcEntry *entry;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. resolving PROC: %.*s",
                self->name_size, self->name);

    if (!self->arg_idx) {
        err = ooDict_new(&self->arg_idx, KND_SMALL_DICT_SIZE);                    RET_ERR();

        for (arg = self->args; arg; arg = arg->next) {
            err = arg->resolve(arg);                                              RET_ERR();

            /* register arg entry */
            arg_entry = malloc(sizeof(struct kndProcArgEntry));
            if (!arg_entry) return knd_NOMEM;

            memset(arg_entry, 0, sizeof(struct kndProcArgEntry));
            memcpy(arg_entry->name, arg->name, arg->name_size);
            arg_entry->name_size = arg->name_size;
            arg_entry->arg = arg;
            err = self->arg_idx->set(self->arg_idx,
                                     arg_entry->name, arg_entry->name_size, (void*)arg_entry);
            if (err) return err;

            if (arg->proc_entry) {
                entry = arg->proc_entry;
                if (entry->proc) {
                    if (DEBUG_PROC_LEVEL_2)
                        knd_log("== ARG proc estimate: %zu", entry->proc->estim_cost_total);
                    self->estim_cost_total += entry->proc->estim_cost_total;
                }
            }
        }
    }

    if (self->bases) {
        err = resolve_parents(self);                                              RET_ERR();
    }

    if (self->proc_call.name_size) {
        err = resolve_proc_call(self);                                            RET_ERR();
    }
    
    self->is_resolved = true;

    return knd_OK;
}

static int resolve_procs(struct kndProc *self)
{
    struct kndProc *proc;
    struct kndProcEntry *entry;
    const char *key;
    void *val;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. resolving procs by \"%.*s\"",
                self->name_size, self->name);
    key = NULL;
    self->proc_name_idx->rewind(self->proc_name_idx);
    do {
        self->proc_name_idx->next_item(self->proc_name_idx, &key, &val);
        if (!key) break;

        entry = (struct kndProcEntry*)val;
        proc = entry->proc;

        if (proc->is_resolved) {
            /*knd_log("--");
              proc->str(proc); */
            continue;
        }

        err = proc->resolve(proc);
        if (err) {
            knd_log("-- couldn't resolve the \"%s\" proc :(", proc->name);
            return err;
        }

        if (DEBUG_PROC_LEVEL_2) {
            knd_log("--");
            proc->str(proc);
        }
    } while (key);

    return knd_OK;
}

static int kndProc_coordinate(struct kndProc *self)
{
    struct kndProc *proc;
    struct kndProcEntry *entry;
    const char *key;
    void *val;
    int err;

    if (DEBUG_PROC_LEVEL_1)
        knd_log(".. proc coordination in progress ..");

    err = resolve_procs(self);                                                    RET_ERR();

    /* assign ids */
    key = NULL;
    self->proc_name_idx->rewind(self->proc_name_idx);
    do {
        self->proc_name_idx->next_item(self->proc_name_idx, &key, &val);
        if (!key) break;

        entry = (struct kndProcEntry*)val;
        proc = entry->proc;

        /* assign id */
        self->next_id++;
        proc->id = self->next_id;
        proc->entry->phase = KND_CREATED;
    } while (key);

    /* display all procs */
    if (DEBUG_PROC_LEVEL_2) {
        key = NULL;
        self->proc_name_idx->rewind(self->proc_name_idx);
        do {
            self->proc_name_idx->next_item(self->proc_name_idx, &key, &val);
            if (!key) break;
            entry = (struct kndProcEntry*)val;
            proc = entry->proc;
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

    self->inbox = NULL;
    self->inbox_size = 0;

    return knd_OK;
}

static int read_proc_incipit(struct kndProc *self,
                            struct kndProcEntry *entry)
{
    char buf[KND_NAME_SIZE + 1];
    size_t buf_size;
    off_t offset = 0;
    int fd = self->fd;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log("\n.. get proc name, global offset:%zu  block size:%zu",
                entry->global_offset, entry->block_size);

    buf_size = entry->block_size;
    if (entry->block_size > KND_NAME_SIZE)
        buf_size = KND_NAME_SIZE;

    offset = entry->global_offset;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return knd_IO_FAIL;
    }
    err = read(fd, buf, buf_size);
    if (err == -1) return knd_IO_FAIL;

    if (DEBUG_PROC_LEVEL_2)
        knd_log("== PROC BODY incipit: %.*s",
                buf_size, buf);

    entry->id_size = KND_ID_SIZE;
    entry->name_size = KND_NAME_SIZE;
    err = knd_parse_incipit(buf, buf_size,
                            entry->id, &entry->id_size,
                            entry->name, &entry->name_size);
    if (err) return err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log("== PROC NAME:\"%.*s\" ID:%.*s",
                entry->name_size, entry->name, entry->id_size, entry->id);

    return knd_OK;
}

static int read_proc(struct kndProc *self,
                    struct kndProcEntry *entry)
{
    int err;

    err = read_proc_incipit(self, entry);
    if (err) return err;

    err = self->proc_name_idx->set(self->proc_name_idx,
                                   entry->name, entry->name_size, (void*)entry);
    if (err) return err;

    /*err = self->proc_idx->set(self->proc_idx,
                             dir->id, dir->id_size, (void*)dir);                  RET_ERR();

    err = read_dir_trailer(self, dir);
    if (err) {
        if (err == knd_NO_MATCH) {
            if (DEBUG_PROC_LEVEL_2)
                knd_log("NB: no dir trailer found");
            return knd_OK;
        }
        return err;
    }
    */
    return knd_OK;
}

static int freeze(struct kndProc *self,
                  size_t *total_frozen_size,
                  char *output,
                  size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct glbOutput *out;
    char *curr_dir = output;
    size_t curr_dir_size = 0;
    size_t block_size = 0;
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

    block_size = out->buf_size;

    /* write block size */
    memcpy(curr_dir, " ", 1);
    curr_dir++;
    curr_dir_size++;

    buf_size = 0;
    knd_num_to_str(block_size, buf, &buf_size, KND_RADIX_BASE);
    memcpy(curr_dir, buf, buf_size);
    curr_dir      += buf_size;
    curr_dir_size += buf_size;

    *total_frozen_size += block_size;
    *total_size = curr_dir_size;

    return knd_OK;
}

static int freeze_procs(struct kndProc *self,
                        size_t *total_frozen_size,
                        char *output,
                        size_t *total_size)
{
    struct kndProc *proc;
    struct kndProcEntry *entry;
    const char *key;
    void *val;
    char *curr_dir = output;
    size_t curr_dir_size = 0;
    size_t chunk_size;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. freezing procs..");

    chunk_size = strlen("[P");
    memcpy(curr_dir, "[P", chunk_size); 
    curr_dir += chunk_size;
    curr_dir_size += chunk_size;
    chunk_size = 0;

    key = NULL;
    self->proc_name_idx->rewind(self->proc_name_idx);
    do {
        self->proc_name_idx->next_item(self->proc_name_idx, &key, &val);
        if (!key) break;

        entry = (struct kndProcEntry*)val;
        proc = entry->proc;

        proc->out = self->out;
        proc->frozen_output_file_name = self->frozen_output_file_name;
        err = proc->freeze(proc, total_frozen_size, curr_dir, &chunk_size);
        if (err) {
            knd_log("-- couldn't freeze the \"%s\" proc :(", proc->name);
            return err;
        }
        curr_dir +=      chunk_size;
        curr_dir_size += chunk_size;
    } while (key);

    memcpy(curr_dir, "]", 1); 
    curr_dir++;
    curr_dir_size++;
    
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
    self->read_proc = read_proc;
    self->select = parse_proc_select;
    self->get_proc = get_proc;
    self->update = update_state;
    self->freeze = freeze;
    self->freeze_procs = freeze_procs;
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
