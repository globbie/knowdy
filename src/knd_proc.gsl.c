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
#include "knd_proc_call.h"
#include "knd_class.h"
#include "knd_task.h"
#include "knd_state.h"
#include "knd_mempool.h"
#include "knd_utils.h"
#include "knd_text.h"
#include "knd_dict.h"
#include "knd_repo.h"
#include "knd_output.h"

#define DEBUG_PROC_GSL_LEVEL_0 0
#define DEBUG_PROC_GSL_LEVEL_1 0
#define DEBUG_PROC_GSL_LEVEL_2 0
#define DEBUG_PROC_GSL_LEVEL_3 0
#define DEBUG_PROC_GSL_LEVEL_TMP 1


static int proc_call_arg_export_GSL(struct kndProc *unused_var(self),
                                     struct kndProcCallArg *call_arg,
                                     struct kndOutput  *out)
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

int knd_proc_export_GSL(struct kndProc *self, struct kndTask *task, bool is_list_item, size_t depth)
{
    struct kndProcArg *arg;
    struct kndProcCallArg *carg;
    struct kndOutput  *out = task->out;
    int err;

    if (DEBUG_PROC_GSL_LEVEL_2)
        knd_log(".. \"%.*s\" proc export GSL..", self->name_size, self->name);

    err = out->writec(out, '{');                                                  RET_ERR();

    if (!is_list_item) {
        err = out->write(out, "proc ", strlen("proc "));                          RET_ERR();
    }
    err = out->write(out, self->name, self->name_size);                           RET_ERR();

    if (task->ctx->format_offset) {
        err = out->writec(out, '\n');                                             RET_ERR();
        err = knd_print_offset(out, (depth + 1) * task->ctx->format_offset);      RET_ERR();
    }

    err = out->write(out, "{_id ", strlen("{_id "));                              RET_ERR();
    err = out->writef(out, "%zu", self->entry->numid);                            RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    if (task->max_depth == 0) {
        goto final;
    }

    if (task->ctx->format_offset) {
        err = out->writec(out, ' ');                                              RET_ERR();
    }

    if (self->tr) {
        err = knd_text_gloss_export_GSL(self->tr, task, depth);
        RET_ERR();
    }

    if (self->args) {
        err = out->write(out, "[arg", strlen("[arg"));                            RET_ERR();
        for (arg = self->args; arg; arg = arg->next) {
            err = knd_proc_arg_export_GSL(arg, task, true, depth + 1);            RET_ERR();
        }
        err = out->write(out, "]", 1);                                            RET_ERR();
    }

    if (self->calls) {
        err = out->write(out, "{do ", strlen("{do "));                            RET_ERR();
        err = out->write(out, self->calls->name, self->calls->name_size); RET_ERR();

        for (carg = self->calls->args; carg; carg = carg->next) {
            err = proc_call_arg_export_GSL(self, carg, out);                      RET_ERR();
        }
        err = out->write(out, "}", 1);                                            RET_ERR();
    }

    if (self->estimate.cost) {
        if (task->ctx->format_offset) {
            err = out->writec(out, '\n');                                         RET_ERR();
            err = knd_print_offset(out,
                                   (depth + 1) * task->ctx->format_offset);       RET_ERR();
        }
        err = out->write(out, "{estim ", strlen("{estim "));                      RET_ERR();
        err = out->writef(out, "%zu", self->estimate.cost);                       RET_ERR();
        err = out->write(out, "{time ", strlen("{time "));                      RET_ERR();
        err = out->writef(out, "%zu", self->estimate.time);                       RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
    }

 final:
    err = out->write(out, "}", 1);                                                RET_ERR();

    return knd_OK;
}
 
