#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

#include <gsl-parser.h>

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
#include "knd_output.h"

#define DEBUG_PROC_SVG_LEVEL_0 0
#define DEBUG_PROC_SVG_LEVEL_1 0
#define DEBUG_PROC_SVG_LEVEL_2 0
#define DEBUG_PROC_SVG_LEVEL_3 0
#define DEBUG_PROC_SVG_LEVEL_TMP 1

static int export_SVG_header(struct kndOutput *out)
{
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

static int export_SVG_footer(struct kndOutput *out)
{
    const char *svg_footer = "</g></svg>";
    size_t svg_footer_size = strlen(svg_footer);
    int err;

    err = out->write(out, svg_footer, svg_footer_size);                           RET_ERR();
    return knd_OK;
}

int knd_proc_export_SVG(struct kndProc *self,
                        struct kndTask *task,
                        struct kndOutput *out)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size = 0;
    struct kndProcArg *arg;
    struct kndTranslation *tr;
    size_t x_offset = 0;
    size_t y_offset = 0;
    int err;

    err = export_SVG_header(out);    RET_ERR();

    /*x_offset += self->visual->text_hangindent_size;
    y_offset += self->visual->text_line_height;
    */

    /* choose gloss */
    tr = self->tr;
    while (tr) {
        if (memcmp(task->ctx->locale, tr->locale, tr->locale_size)) {
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

    err = export_SVG_footer(out);    RET_ERR();

    return knd_OK;
}
