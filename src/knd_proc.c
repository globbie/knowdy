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
#include "knd_repo.h"

#define DEBUG_PROC_LEVEL_0 0
#define DEBUG_PROC_LEVEL_1 0
#define DEBUG_PROC_LEVEL_2 0
#define DEBUG_PROC_LEVEL_3 0
#define DEBUG_PROC_LEVEL_TMP 1

struct LocalContext {
    struct kndRepo *repo;
    struct kndTask *task;
    struct kndProc *proc;
};

static int resolve_proc_call(struct kndProc *self);

static int inherit_args(struct kndProc *self,
                        struct kndProc *parent,
                        struct kndTask *task);

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

static void str(struct kndProc *self)
{
    struct kndTranslation *tr;
    struct kndProcArg *arg;
    struct kndProcCallArg *call_arg;
    size_t depth = 0;

    knd_log("{proc %.*s  {_id %.*s}",
            self->name_size, self->name,
            self->entry->id_size, self->entry->id);

    for (tr = self->tr; tr; tr = tr->next) {
        knd_log("%*s~ %.*s %.*s", (depth + 1) * KND_OFFSET_SIZE, "",
                tr->locale_size, tr->locale, tr->val_size, tr->val);
    }

    /*for (base = self->bases; base; base = base->next) {
        base_str(base, depth + 1);
        }*/

    if (self->result_classname_size) {
        knd_log("%*s    {result class:%.*s}", depth * KND_OFFSET_SIZE, "",
                self->result_classname_size, self->result_classname);
    }

    for (arg = self->args; arg; arg = arg->next) {
        knd_proc_arg_str(arg, depth + 1);
    }

    if (self->proc_call) {
        knd_log("%*s    {do %.*s", depth * KND_OFFSET_SIZE, "",
                self->proc_call->name_size, self->proc_call->name);
        for (call_arg = self->proc_call->args; call_arg; call_arg = call_arg->next) {
            proc_call_arg_str(call_arg, depth + 1);
        }
        knd_log("%*s    }", depth * KND_OFFSET_SIZE, "");
    }
    knd_log("}");
}

static int kndProc_export_SVG_header(struct kndTask *task)
{
    struct glbOutput *out = task->out;
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

static int kndProc_export_SVG_footer(struct glbOutput *out)
{
    const char *svg_footer = "</g></svg>";
    size_t svg_footer_size = strlen(svg_footer);
    int err;

    err = out->write(out, svg_footer, svg_footer_size);                           RET_ERR();
    return knd_OK;
}

extern int knd_get_proc(struct kndRepo *repo,
                        const char *name, size_t name_size,
                        struct kndProc **result)
{
    struct kndProcEntry *entry;
    struct kndProc *proc;
    //int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. repo %.*s to get proc: \"%.*s\"..",
                repo->name_size, repo->name, name_size, name);

    entry = (struct kndProcEntry*)repo->proc_name_idx->get(repo->proc_name_idx,
                                                           name, name_size);
    if (!entry) {
        knd_log("-- no such proc: \"%.*s\"",
                name_size, name);
        /*repo->log->reset(repo->log);
        err = repo->log->write(repo->log, name, name_size);
        if (err) return err;
        err = repo->log->write(repo->log, " Proc name not found",
                               strlen(" Proc name not found"));
                               if (err) return err;*/
        return knd_NO_MATCH;
    }

    if (entry->phase == KND_REMOVED) {
        knd_log("-- \"%s\" proc was removed", name);
        /*repo->log->reset(repo->log);
        err = repo->log->write(repo->log, name, name_size);
        if (err) return err;
        err = repo->log->write(repo->log, " proc was removed",
                               strlen(" proc was removed"));
        if (err) return err;
        */
        //repo->root_proc->task->http_code = HTTP_GONE;
        return knd_NO_MATCH;
    }
    
    if (entry->proc) {
        proc = entry->proc;
        entry->phase = KND_SELECTED;
        *result = proc;
        return knd_OK;
    }

    // TODO: defreeze
    return knd_FAIL;
}

static gsl_err_t run_get_proc(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndRepo *repo = ctx->repo;
    struct kndProc *proc;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    ctx->proc = NULL;

    err = knd_get_proc(repo, name, name_size, &proc);
    if (err) return make_gsl_err_external(err);
    ctx->proc = proc;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t present_proc_selection(void *obj,
                                        const char *unused_var(val),
                                        size_t unused_var(val_size))
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndProc *proc = ctx->proc;
    knd_format format = task->format;
    struct glbOutput *out = task->out;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. presenting proc selection..");

    if (!proc) return make_gsl_err(gsl_FAIL);

    out->reset(out);

    /* export HEADER */
    switch (format) {
    case KND_FORMAT_SVG:
        err = kndProc_export_SVG_header(task);
        if (err) return make_gsl_err_external(err);
     break;
    case KND_FORMAT_HTML:
        err = kndProc_export_SVG_header(task);
        if (err) return make_gsl_err_external(err);
     break;
    default:
        break;
    }

    /* export BODY */
    err = knd_proc_export(proc, KND_FORMAT_SVG, task, out);
    if (err) return make_gsl_err_external(err);

    /* export FOOTER */
    switch (format) {
    case KND_FORMAT_SVG:
        err = kndProc_export_SVG_footer(out);
        if (err) return make_gsl_err_external(err);
     break;
    case KND_FORMAT_HTML:
        err = kndProc_export_SVG_footer(out);
        if (err) return make_gsl_err_external(err);
     break;
    default:
        break;
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t remove_proc(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndProc *proc = ctx->proc;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. removing proc: %.*s", name_size, name);

    if (!proc) {
        knd_log("-- remove operation: no proc selected");

        /*repo->log->reset(repo->log);
        err = repo->log->write(repo->log, name, name_size);
        if (err) return make_gsl_err_external(err);
        err = repo->log->write(repo->log, " class name not specified",
                               strlen(" class name not specified"));
                               if (err) return make_gsl_err_external(err);*/
        return make_gsl_err(gsl_NO_MATCH);
    }

    if (DEBUG_PROC_LEVEL_2)
        knd_log("== proc to remove: \"%.*s\"\n",
                proc->name_size, proc->name);

    proc->entry->phase = KND_REMOVED;

    //repo->log->reset(repo->log);
    /*err = repo->log->write(repo->log, proc->name, proc->name_size);
    if (err) return make_gsl_err_external(err);
    err = repo->log->write(repo->log, " proc removed",
                           strlen(" proc removed"));
    if (err) return make_gsl_err_external(err);
    */
    /*    proc->next = self->inbox;
    self->inbox = proc;
    self->inbox_size++;
    */

    return make_gsl_err(gsl_OK);
}

extern gsl_err_t knd_proc_select(struct kndRepo *repo,
                                 const char *rec,
                                 size_t *total_size,
                                 struct kndTask *task)
{
    struct LocalContext ctx = {
        .task = task,
        .repo = repo
    };
    gsl_err_t parser_err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. parsing Proc select: \"%.*s\"",
                16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_proc,
          .obj = &ctx
        },
        { .type = GSL_SET_STATE,
          .name = "_rm",
          .name_size = strlen("_rm"),
          .run = remove_proc,
          .obj = &ctx
        }/*,
        { .type = GSL_SET_STATE,
          .name = "inst",
          .name_size = strlen("inst"),
          .parse = parse_import_instance,
          .obj = self
          }*/,
        { .is_default = true,
          .run = present_proc_selection,
          .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs,
                                sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        /*knd_log("-- proc parse error: \"%.*s\"",
                repo->log->buf_size, repo->log->buf);
        if (!repo->log->buf_size) {
            e = repo->log->write(repo->log, "proc parse failure",
                                 strlen("proc parse failure"));
            if (e) return make_gsl_err_external(e);
            }*/
        return parser_err;
    }

    /* any updates happened? */
    /*if (self->curr_proc) {
        if (self->curr_proc->inbox_size || self->curr_proc->inst_inbox_size) {
            self->curr_proc->next = self->inbox;
            self->inbox = self->curr_proc;
            self->inbox_size++;
        }
        }*/

    return make_gsl_err(gsl_OK);
}

static int proc_call_arg_export_GSP(struct kndProc *unused_var(self),
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

static int export_GSP(struct kndProc *self,
                      struct kndTask *task,
                      struct glbOutput *out)
{
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


    /* if (self->summary) {
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
    */
    if (self->args) {
        err = out->write(out, "[arg ", strlen("[arg "));                          RET_ERR();
        for (arg = self->args; arg; arg = arg->next) {

            err = knd_proc_arg_export(arg, KND_FORMAT_GSP, task, out);                  RET_ERR();
        }
        err = out->writec(out, ']');                                              RET_ERR();
    }

    if (self->proc_call->name_size) {
        err = out->write(out, "{run ", strlen("{run "));                          RET_ERR();
        err = out->write(out, self->proc_call->name,
                         self->proc_call->name_size);   RET_ERR();

        for (call_arg = self->proc_call->args;
             call_arg;
             call_arg = call_arg->next) {
            err = proc_call_arg_export_GSP(self, call_arg, out);                  RET_ERR();
        }
        err = out->write(out, "}", 1);                                            RET_ERR();
    }

    err = out->write(out, "}", 1);                                                RET_ERR();
    return knd_OK;
}

static int proc_call_arg_export_JSON(struct kndProc *unused_var(self),
                                     struct kndProcCallArg *call_arg,
                                     struct glbOutput  *out)
{
    int err;
    err = out->writec(out, '"');                                                  RET_ERR();
    err = out->write(out, call_arg->name, call_arg->name_size);                   RET_ERR();
    err = out->writec(out, '"');                                                  RET_ERR();
    err = out->writec(out, ':');                                                  RET_ERR();
    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

static int export_JSON(struct kndProc *self,
                       struct kndTask *task,
                       struct glbOutput  *out)
{
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
        if (memcmp(task->locale, tr->locale, tr->locale_size)) {
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

    /*    for (tr = self->summary; tr; tr = tr->next) { 
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
    */

    if (self->args) {
        if (in_list) {
            err = out->write(out, ",", 1);                                        RET_ERR();
        }
        err = out->write(out, "\"args\":[", strlen("\"args\":["));                RET_ERR();
        for (arg = self->args; arg; arg = arg->next) {
            if (in_list) {
                err = out->write(out, ",", 1);                                    RET_ERR();
            }

            err = knd_proc_arg_export(arg, KND_FORMAT_JSON, task, out);           RET_ERR();
            in_list = true;
        }
        err = out->write(out, "]", 1);                                            RET_ERR();
    }

    if (self->proc_call->name_size) {
        if (in_list) {
            err = out->write(out, ",", 1);                                        RET_ERR();
        }
        err = out->write(out, "\"do\":{", strlen("\"do\":{"));                  RET_ERR();
        err = out->write(out, "\"_name\":\"", strlen("\"_name\":\""));            RET_ERR();
        err = out->write(out, self->proc_call->name, self->proc_call->name_size);   RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();

        for (carg = self->proc_call->args; carg; carg = carg->next) {
            err = out->writec(out, ',');                                          RET_ERR();
            err = proc_call_arg_export_JSON(self, carg, out);                          RET_ERR();
        }

        err = out->write(out, "}", 1);                                            RET_ERR();
    }


    err = out->write(out, "}", 1);                                                RET_ERR();

    return knd_OK;
}


static int export_SVG(struct kndProc *self,
                      struct kndTask *task,
                      struct glbOutput *out)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size = 0;
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
        if (memcmp(task->locale, tr->locale, tr->locale_size)) {
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
            //y_offset += self->visual->text_line_height;

            err = out->write(out,   "<g", strlen("<g"));                          RET_ERR();
            buf_size = sprintf(buf, " transform=\"translate(%zu,%zu)\"",
                               x_offset, y_offset);
            err = out->write(out,   buf, buf_size);                               RET_ERR();
            err = out->write(out, ">", 1);                                        RET_ERR();

            err = knd_proc_arg_export(arg, KND_FORMAT_SVG, task, out);            RET_ERR();
            err = out->write(out, "</g>", strlen("</g>"));                        RET_ERR();
        }
        err = out->write(out, "</g>", strlen("</g>"));                            RET_ERR();
    }

    /*if (self->proc_call->name_size) {
        err = out->write(out, ",\"run\":{", strlen(",\"run\":{"));                RET_ERR();
        err = out->write(out, "\"_n\":\"", strlen("\"_n\":\""));                  RET_ERR();
        err = out->write(out, self->proc_call->name, self->proc_call->name_size);   RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();

        for (call_arg = self->proc_call->args; call_arg; call_arg = call_arg->next) {
            proc_call_arg_str(call_arg, self->depth + 1);
            }
        err = out->write(out, "}", 1);                                            RET_ERR();
    }
    */

    return knd_OK;
}

extern int knd_proc_export(struct kndProc *self,
                           knd_format format,
                           struct kndTask *task,
                           struct glbOutput *out)
{
    int err;

    switch (format) {
    case KND_FORMAT_JSON:
        err = export_JSON(self, task, out);
        if (err) return err;
        break;
    case KND_FORMAT_GSP:
        err = export_GSP(self, task, out);
        if (err) return err;
        break;
    case KND_FORMAT_SVG:
        err = export_SVG(self, task, out);
        if (err) return err;
        break;
    default:
        break;
    }
    
    return knd_OK;
}

static gsl_err_t set_gloss_locale(void *obj, const char *name, size_t name_size)
{
    struct kndTranslation *self = obj;
    if (name_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);
    self->curr_locale = name;
    self->curr_locale_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_gloss_value(void *obj, const char *name, size_t name_size)
{
    struct kndTranslation *self = obj;
    if (!name_size) return make_gsl_err(gsl_FAIL);
    self->val = name;
    self->val_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss_item(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndProc *self = obj;
    struct kndTranslation *tr;

    /* TODO: mempool alloc */
    //self->entry->repo->mempool->new_text_seq();
    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return *total_size = 0, make_gsl_err_external(knd_NOMEM);
    memset(tr, 0, sizeof(struct kndTranslation));

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_gloss_locale,
          .obj = tr
        },
        { .name = "t",
          .name_size = strlen("t"),
          .run = set_gloss_value,
          .obj = tr
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (tr->curr_locale_size == 0 || tr->val_size == 0)
        return make_gsl_err(gsl_FORMAT);  // error: both of them are required

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. read gloss translation: \"%.*s\",  text: \"%.*s\"",
                tr->locale_size, tr->locale, tr->val_size, tr->val);

    // append
    tr->next = self->tr;
    self->tr = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss(void *obj,
                             const char *rec,
                             size_t *total_size)
{
    struct kndProc *self = obj;
    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .parse = parse_gloss_item,
        .obj = self
    };
    return gsl_parse_array(&item_spec, rec, total_size);
}

static gsl_err_t parse_proc_arg_item(void *obj,
                                     const char *rec,
                                     size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndProc *proc = ctx->proc;
    struct kndProcArg *arg;
    struct kndMemPool *mempool = ctx->task->mempool;
    int err;
    gsl_err_t parser_err;

    err = knd_proc_arg_new(&arg, mempool);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    arg->parent = proc;

    parser_err = knd_proc_arg_parse(arg, rec, total_size, ctx->task);
    if (parser_err.code) return parser_err;

    // append
    arg->next = proc->args;
    proc->args = arg;
    proc->num_args++;

    return make_gsl_err(gsl_OK);
}

static int resolve_parents(struct kndProc *self,
                           struct kndTask *task)
{
    struct kndProcVar *base;
    struct kndProc *proc;
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

        err = knd_get_proc(self->entry->repo,
                           base->name, base->name_size, &proc);                 RET_ERR();
        if (proc == self) {
            knd_log("-- self reference detected in \"%.*s\" :(",
                    base->name_size, base->name);
            return knd_FAIL;
        }

        base->proc = proc;

        /* should we keep track of our children? */
        /*if (c->ignore_children) continue; */

        /* check base doublets */
        /*    for (size_t i = 0; i < self->num_children; i++) {
            entry = self->children[i];
            if (entry->proc == self) {
                knd_log("-- doublet proc found in \"%.*s\" :(",
                        self->name_size, self->name);
                return knd_FAIL;
            }
            }*/

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
        
        err = inherit_args(self, base->proc, task);                                     RET_ERR();

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

            err = knd_proc_arg_new(&arg, task->mempool);
            if (err) return err;

            arg->name = arg_item->name;
            arg->name_size = arg_item->name_size;

            arg->classname = arg_item->classname;
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

static int proc_resolve(struct kndProc *self,
                        struct kndTask *task)
{
    struct kndProcArg *arg = NULL;
    //struct kndProcArgEntry *arg_entry;
    //struct kndProcEntry *entry;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. resolving PROC: %.*s",
                self->name_size, self->name);

    if (!self->arg_idx) {
        err = ooDict_new(&self->arg_idx, KND_SMALL_DICT_SIZE);                    RET_ERR();

        for (arg = self->args; arg; arg = arg->next) {
            err = knd_proc_resolve_arg(arg, task->repo);                          RET_ERR();

            /* TODO: index  arg entry */
            /*arg_entry = malloc(sizeof(struct kndProcArgEntry));
            if (!arg_entry) return knd_NOMEM;

            memset(arg_entry, 0, sizeof(struct kndProcArgEntry));

            arg_entry->name = arg->name;
            arg_entry->name_size = arg->name_size;
            arg_entry->arg = arg;
            err = self->arg_idx->set(self->arg_idx,
                                     arg_entry->name,
                                     arg_entry->name_size, (void*)arg_entry);
            if (err) return err;
            */

            /*if (arg->proc_entry) {
                entry = arg->proc_entry;
                if (entry->proc) {
                    if (DEBUG_PROC_LEVEL_2)
                        knd_log("== ARG proc estimate: %zu", entry->proc->estim_cost_total);
                    self->estim_cost_total += entry->proc->estim_cost_total;
                }
                } */
        }
    }

    if (self->bases) {
        err = resolve_parents(self, task);                                              RET_ERR();
    }

    if (self->proc_call) {
        err = resolve_proc_call(self);                                                  RET_ERR();
    }
    
    self->is_resolved = true;

    return knd_OK;
}

static int inherit_args(struct kndProc *self,
                        struct kndProc *parent,
                        struct kndTask *task)
{
    struct kndProcArg *arg;
    struct kndProcArgEntry *arg_entry;
    struct kndProcVar *base;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. \"%.*s\" proc to inherit args from \"%.*s\" (num args:%zu)",
                self->name_size, self->name, parent->name_size, parent->name, parent->num_args);

    if (!parent->is_resolved) {
        err = proc_resolve(parent, task);                                            RET_ERR();
    }

    /* check circled relations */
    /*    for (size_t i = 0; i < self->num_inherited; i++) {
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
    */
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

        arg_entry->name = arg->name;
        arg_entry->name_size = arg->name_size;
        arg_entry->arg = arg;

        if (DEBUG_PROC_LEVEL_2)
            knd_log("NB: ++ proc \"%.*s\" inherits arg \"%.*s\" from \"%.*s\"",
                    self->name_size, self->name,
                    arg->name_size, arg->name,
                    parent->name_size, parent->name);

        err = self->arg_idx->set(self->arg_idx,
                                 arg_entry->name, arg_entry->name_size,
                                 (void*)arg_entry);
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

    //    self->inherited[self->num_inherited] = parent->entry;
    //self->num_inherited++;

    /* contact the grandparents */
    for (base = parent->bases; base; base = base->next) {
        if (base->proc) {
            err = inherit_args(self, base->proc, task);                                 RET_ERR();
        }
    }
    
    return knd_OK;
}

static gsl_err_t parse_proc_call_arg(void *obj,
                                     const char *name, size_t name_size,
                                     const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndProc *proc = ctx->proc;
    struct kndProcCall *proc_call = proc->proc_call;
    struct kndProcCallArg *call_arg;
    struct kndClassVar *class_var;
    struct kndMemPool *mempool = ctx->task->mempool;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. Proc Call Arg \"%.*s\" to validate: \"%.*s\"..",
                name_size, name, 32, rec);

    err = knd_proc_call_arg_new(mempool, &call_arg);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    call_arg->name = name;
    call_arg->name_size = name_size;

    call_arg->next = proc_call->args;
    proc_call->args = call_arg;
    proc_call->num_args++;

    err = knd_class_var_new(mempool, &class_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    class_var->root_class = proc->entry->repo->root_class;

    parser_err = knd_import_class_var(class_var, rec, total_size, ctx->task);
    if (parser_err.code) return parser_err;

    call_arg->class_var = class_var;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t set_proc_call_name(void *obj,
                                    const char *name, size_t name_size)
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
    struct kndProc *proc = ctx->proc;
    struct kndProcCall *proc_call = proc->proc_call;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. Proc Call parsing: \"%.*s\"..",
                32, rec);

    if (!proc_call) {
        err = knd_proc_call_new(ctx->task->mempool, &proc->proc_call);
        if (err) return make_gsl_err_external(err);
        proc_call = proc->proc_call;
    }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_proc_call_name,
          .obj = proc_call
        }/*,
        { .type = GSL_GET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = parse_gloss,
          .obj = proc_call
          }*/,
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = parse_gloss,
          .obj = proc_call
        }/*,
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_summary",
          .name_size = strlen("_summary"),
          .parse = parse_summary,
          .obj = proc_call
          }*/,
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = parse_gloss,
          .obj = proc_call
        },
        { .validate = parse_proc_call_arg,
          .obj = proc
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    // TODO: lookup table
    if (!strncmp("_mult", proc_call->name, proc_call->name_size))
        proc_call->type = KND_PROC_MULT;

    if (!strncmp("_sum", proc_call->name, proc_call->name_size))
        proc_call->type = KND_PROC_SUM;

    if (!strncmp("_mult_percent", proc_call->name, proc_call->name_size))
        proc_call->type = KND_PROC_MULT_PERCENT;

    if (!strncmp("_div_percent", proc_call->name, proc_call->name_size))
        proc_call->type = KND_PROC_DIV_PERCENT;

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

extern gsl_err_t knd_proc_read(struct kndProc *self,
                               const char *rec,
                               size_t *total_size)
{
    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. parsing proc \"%.*s\" GSL: \"%.*s\"..",
                self->name_size, self->name, 128, rec);

    struct gslTaskSpec proc_arg_spec = {
        .is_list_item = true,
        .parse = parse_proc_arg_item,
        .obj = self
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_proc_name,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = parse_gloss,
          .obj = self
        }/*,
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_summary",
          .name_size = strlen("_summary"),
          .parse = parse_summary,
          .obj = self
          }*/,
        { .type = GSL_SET_ARRAY_STATE,
          .name = "arg",
          .name_size = strlen("arg"),
          .parse = gsl_parse_array,
          .obj = &proc_arg_spec
        },
        { .name = "do",
          .name_size = strlen("do"),
          .parse = parse_proc_call,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}


static int resolve_proc_call(struct kndProc *self)
{
    struct kndProcCallArg *call_arg;
    struct kndProcArgEntry *entry;

    if (DEBUG_PROC_LEVEL_TMP)
        knd_log(".. resolving proc call %.*s ..",
                self->proc_call->name_size, self->proc_call->name);

    if (!self->arg_idx) return knd_FAIL;

    for (call_arg = self->proc_call->args; call_arg; call_arg = call_arg->next) {
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


static int resolve_procs(struct kndProc *self,
                         struct kndTask *task)
{
    struct kndProc *proc;
    struct kndRepo *repo = self->entry->repo;
    struct kndProcEntry *entry;
    const char *key;
    void *val;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. resolving procs by \"%.*s\"",
                self->name_size, self->name);
    key = NULL;
    repo->proc_name_idx->rewind(repo->proc_name_idx);
    do {
        repo->proc_name_idx->next_item(repo->proc_name_idx, &key, &val);
        if (!key) break;

        entry = (struct kndProcEntry*)val;
        proc = entry->proc;

        if (proc->is_resolved) {
            /*knd_log("--");
              proc->str(proc); */
            continue;
        }

        err = proc_resolve(proc, task);
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

extern int knd_proc_coordinate(struct kndProc *self,
                               struct kndTask *task)
{
    struct kndProc *proc;
    struct kndRepo *repo = self->entry->repo;
    struct kndProcEntry *entry;
    const char *key;
    void *val;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. proc coordination in progress ..");

    err = resolve_procs(self, task);                                                   RET_ERR();

    /* assign ids */
    key = NULL;
    repo->proc_name_idx->rewind(repo->proc_name_idx);
    do {
        repo->proc_name_idx->next_item(repo->proc_name_idx, &key, &val);
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
        repo->proc_name_idx->rewind(repo->proc_name_idx);
        do {
            repo->proc_name_idx->next_item(repo->proc_name_idx, &key, &val);
            if (!key) break;
            entry = (struct kndProcEntry*)val;
            proc = entry->proc;
            proc->str(proc);
        } while (key);
    }

    return knd_OK;
}

extern int knd_resolve_proc_ref(struct kndClass *self,
                                const char *name, size_t name_size,
                                struct kndProc *unused_var(base),
                                struct kndProc **result,
                                struct kndTask *unused_var(task))
{
    struct kndProc *proc;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. resolving proc ref:  %.*s", name_size, name);

    err = knd_get_proc(self->entry->repo,
                       name, name_size, &proc);                            RET_ERR();

    /*c = dir->conc;
    if (!c->is_resolved) {
        err = knd_class_resolve(c);                                                RET_ERR();
    }

    if (base) {
        err = is_base(base, c);                                                   RET_ERR();
    }
    */

    *result = proc;

    return knd_OK;
}


extern void knd_proc_init(struct kndProc *self)
{
    self->str = str;
}

extern int knd_proc_call_arg_new(struct kndMemPool *mempool,
                                 struct kndProcCallArg **result)
{
    void *page;
    int err;
    //knd_log("..proc call arg new [size:%zu]", sizeof(struct kndProcCallArg));
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                            sizeof(struct kndProcCallArg), &page);  RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_proc_call_new(struct kndMemPool *mempool,
                             struct kndProcCall **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndProcCall), &page);  RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_proc_var_new(struct kndMemPool *mempool,
                            struct kndProcVar **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndProcVar), &page);          RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_proc_arg_var_new(struct kndMemPool *mempool,
                                struct kndProcArgVar **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndProcArgVar), &page);          RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_proc_entry_new(struct kndMemPool *mempool,
                              struct kndProcEntry **result)
{
    void *page;
    int err;

    //knd_log("..proc entry new [size:%zu]", sizeof(struct kndProcEntry));

    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                            sizeof(struct kndProcEntry), &page);  RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_proc_new(struct kndMemPool *mempool,
                        struct kndProc **result)
{
    void *page;
    int err;

    //knd_log("++ new proc: [size:%zu]", sizeof(struct kndProc));

    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                            sizeof(struct kndProc), &page);  RET_ERR();
    *result = page;
    knd_proc_init(*result);
    return knd_OK;
}
