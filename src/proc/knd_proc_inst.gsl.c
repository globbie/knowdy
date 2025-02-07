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
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_state.h"
#include "knd_mempool.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_text.h"
#include "knd_dict.h"
#include "knd_repo.h"
#include "knd_output.h"

int knd_proc_inst_export_GSL(struct kndProcInst *self, bool is_list_item,
                             knd_state_phase phase, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndProcArgVar *var;
    int err;

    if (!is_list_item) {
        err = out->writec(out, '{');
        RET_ERR();
        if (phase == KND_CREATED) {
            err = out->writec(out, '!');
            RET_ERR();
        }
        OUT("inst ", strlen("inst "));
    }

    OUT("\"", 1);
    OUT(self->name, self->name_size);
    OUT("\"", 1);

    if (task->ctx->use_alias) {
        if (self->alias_size) {
            err = out->write(out, "{_as ", strlen("{_as "));                  RET_ERR();
            err = out->write(out, self->alias, self->alias_size);             RET_ERR();
            err = out->writec(out, '}');                                      RET_ERR();
        }
    }

    if (self->procvar) {
        FOREACH (var, self->procvar->args) {
            knd_proc_arg_var_export_GSL(var, task, depth + 1);
        }
    }

    if (!is_list_item) {
        err = out->writef(out, "%*s}", depth * KND_OFFSET_SIZE, "");  RET_ERR();
    }
    return knd_OK;
}
