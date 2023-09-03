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
#include "knd_task.h"
#include "knd_state.h"

int knd_proc_arg_var_export_JSON(struct kndProcArgVar *self, struct kndTask *task,
                                 size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndClassInst *arg_inst = self->inst;
    int err;
    assert (self->template != NULL);
    assert (arg_inst != NULL);

    OUT("{", 1);
    OUT("\"template\":", strlen("\"template\":"));
    OUT("\"", 1);
    OUT(self->template->name, self->template->name_size);
    OUT("\"", 1);

    OUT(",", 1);
    OUT("\"class\":", strlen("\"class\":"));
    OUT("\"", 1);
    OUT(arg_inst->is_a->name, arg_inst->is_a->name_size);
    OUT("\"", 1);

    if (arg_inst->is_a->tr) {
        err = knd_text_gloss_export_JSON(arg_inst->is_a->tr, task, depth + 1);
        KND_TASK_ERR("failed to export subclass gloss JSON");
    }

    OUT(",", 1);
    OUT("\"inst_id\":", strlen("\"inst_id\":"));
    OUT("\"", 1);
    OUT(arg_inst->entry->id, arg_inst->entry->id_size);
    OUT("\"", 1);
    
    if (self->repr) {
        OUT(",\"synode\":", strlen(",\"synode\":"));
        err = knd_synode_export_JSON(self->repr->synode, task);
        KND_TASK_ERR("failed to present synode JSON");
    }

    OUT("}", 1);
    return knd_OK;
}

int knd_proc_arg_export_JSON(struct kndProcArg *self, struct kndTask *task, bool is_list_item, size_t depth)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    //struct kndProcCallArg *arg;
    struct kndOutput  *out = task->out;
    int err;

    err = out->writec(out, '{');                                                  RET_ERR();
    if (!is_list_item) {
        err = out->write(out, "arg ", strlen("arg "));                          RET_ERR();
    }
    err = out->write(out, self->name, self->name_size);                           RET_ERR();

    if (task->ctx->format_offset) {
        err = out->writec(out, '\n');                                             RET_ERR();
        err = knd_print_offset(out, (depth + 1) * task->ctx->format_offset);      RET_ERR();
    }

    err = out->write(out, "{_id ", strlen("{_id "));                              RET_ERR();
    err = out->writef(out, "%zu", self->numid);                                   RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    if (task->max_depth == 0) {
        goto final;
    }

    if (task->ctx->format_offset) {
        err = out->writec(out, ' ');                                              RET_ERR();
    }

    if (self->tr) {
        err = knd_text_gloss_export_JSON(self->tr, task, depth);
        RET_ERR();
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
    
    /*if (self->proc_call) {
        err = out->write(out, "{do ", strlen("{do "));                          RET_ERR();
        err = out->write(out, self->proc_call->name, self->proc_call->name_size); RET_ERR();
        for (arg = self->proc_call->args; arg; arg = arg->next) {
            err = proc_call_arg_export_JSON(arg, out);                   RET_ERR();
        }
        err = out->write(out, "}", 1);                                            RET_ERR();
        }*/

 final:
    err = out->writec(out, '}');                                                RET_ERR();
    return knd_OK;
}

