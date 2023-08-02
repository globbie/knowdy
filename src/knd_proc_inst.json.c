#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

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

int knd_proc_inst_export_JSON(struct kndProcInst *self, bool is_list_item, knd_state_phase phase,
                              struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndProcArgVar *var;
    size_t arg_count = 0;
    int err;

    OUT("{", 1);
    OUT("\"id\":", strlen("\"id\":"));
    OUT("\"", 1);
    OUT(self->entry->id, self->entry->id_size);
    OUT("\"", 1);

        /*if (task->ctx->use_alias) {
        if (self->alias_size) {
            err = out->write(out, "{_as ", strlen("{_as "));                  RET_ERR();
            err = out->write(out, self->alias, self->alias_size);             RET_ERR();
            err = out->writec(out, '}');                                      RET_ERR();
        }
        }*/

    OUT(",\"aspects\":{}", strlen(",\"aspects\":{}"));
    OUT(",\"pragma\":{}", strlen(",\"pragma\":{}"));

    if (self->procvar) {
        OUT(",\"args\":{", strlen(",\"args\":{"));
        FOREACH (var, self->procvar->args) {
            if (arg_count) {
                OUT(",", 1);
            }
            OUT("\"", 1);
            OUT(var->arg->name, var->arg->name_size);
            OUT("\"", 1);
            OUT(":", 1);
            knd_proc_arg_var_export_JSON(var, task, depth + 2);
            arg_count++;
        }
        OUT("}", 1);
    }
    OUT("}", 1);
    return knd_OK;
}
