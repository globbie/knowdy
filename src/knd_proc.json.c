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

#define DEBUG_PROC_JSON_LEVEL_0 0
#define DEBUG_PROC_JSON_LEVEL_1 0
#define DEBUG_PROC_JSON_LEVEL_2 0
#define DEBUG_PROC_JSON_LEVEL_3 0
#define DEBUG_PROC_JSON_LEVEL_TMP 1


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

int knd_proc_export_JSON(struct kndProc *self,
                         struct kndTask *task,
                         struct glbOutput  *out)
{
    struct kndProcArg *arg;
    struct kndProcCallArg *carg;
    struct kndTranslation *tr;
    bool in_list = false;
    int err;

    if (DEBUG_PROC_JSON_LEVEL_2)
        knd_log(".. \"%.*s\" proc export JSON..",
                self->name_size, self->name);

    err = out->writec(out, '{');                                                RET_ERR();

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
        in_list = true;
        break;
    next_tr:
        tr = tr->next;
    }

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
        err = out->write(out, "\"do\":{", strlen("\"do\":{"));                    RET_ERR();
        err = out->write(out, "\"_name\":\"", strlen("\"_name\":\""));            RET_ERR();
        err = out->write(out, self->proc_call->name, self->proc_call->name_size); RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();

        for (carg = self->proc_call->args; carg; carg = carg->next) {
            err = out->writec(out, ',');                                          RET_ERR();
            err = proc_call_arg_export_JSON(self, carg, out);                     RET_ERR();
        }

        err = out->write(out, "}", 1);                                            RET_ERR();
    }

    err = out->write(out, "}", 1);                                                RET_ERR();

    return knd_OK;
}
 
