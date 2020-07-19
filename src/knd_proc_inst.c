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

#define DEBUG_PROC_INST_LEVEL_0 0
#define DEBUG_PROC_INST_LEVEL_1 0
#define DEBUG_PROC_INST_LEVEL_2 0
#define DEBUG_PROC_INST_LEVEL_3 0
#define DEBUG_PROC_INST_LEVEL_TMP 1

void knd_proc_arg_inst_str(struct kndProcArgInst *self, size_t depth)
{
    knd_log("%*s{%.*s} {class inst:%p}",
            depth * KND_OFFSET_SIZE, "",
            self->arg->name_size, self->arg->name,
            self->class_inst);
}

void knd_proc_inst_str(struct kndProcInst *self, size_t depth)
{
    struct kndProcArgInst *arg;

    knd_log("%*s{proc-inst %.*s",
            depth * KND_OFFSET_SIZE, "",
            self->name_size, self->name);

    if (self->args) {
        knd_log("%*s    [arg", depth * KND_OFFSET_SIZE, "");
        for (arg = self->args; arg; arg = arg->next) {
            knd_proc_arg_inst_str(arg, depth + 2);
        }
        knd_log("%*s    ]", depth * KND_OFFSET_SIZE, "");
    }

    knd_log("%*s}",
            depth * KND_OFFSET_SIZE, "");
}

static int proc_arg_inst_export_GSL(struct kndProcArgInst *self,
                                    struct kndTask *task,
                                    size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndClass *c;
    int err;
    
    err = out->writef(out, "%*s{%.*s",
                      depth * KND_OFFSET_SIZE, "",
                      self->arg->name_size, self->arg->name);                     RET_ERR();
    if (self->class_inst) {
        c = self->class_inst->blueprint;

        if (task->ctx->use_alias) {
            err = out->writef(out, "%*s%.*s",
                              depth * KND_OFFSET_SIZE, "",
                              self->class_inst->alias_size, self->class_inst->alias);  RET_ERR();
        } else {
            err = out->writef(out, "%*s{class %.*s",
                              (depth + 1) * KND_OFFSET_SIZE, "",
                              c->name_size, c->name);                     RET_ERR();
            err = out->writec(out, '}');
        }
    } else {
        err = out->writef(out, "%*s%.*s",
                          depth * KND_OFFSET_SIZE, "",
                          self->val_size, self->val);                     RET_ERR();
    }
    err = out->writec(out, '}');
    return knd_OK;
}

int knd_proc_inst_export_GSL(struct kndProcInst *self,
                             bool is_list_item,
                             struct kndTask *task,
                             size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndProcArgInst *arg;
    int err;

    if (is_list_item) {
        err = out->writef(out, "%*s{", depth * KND_OFFSET_SIZE, "");        RET_ERR();
    }
    err = out->write(out, self->name, self->name_size);                       RET_ERR();

    if (task->ctx->use_alias) {
        if (self->alias_size) {
            err = out->write(out, "{_as ", strlen("{_as "));                  RET_ERR();
            err = out->write(out, self->alias, self->alias_size);             RET_ERR();
            err = out->writec(out, '}');                                      RET_ERR();
        }
    }

    for (arg = self->args; arg; arg = arg->next) {
        proc_arg_inst_export_GSL(arg, task, depth + 2);
    }

    if (is_list_item) {
        err = out->writef(out, "%*s}", depth * KND_OFFSET_SIZE, "");  RET_ERR();
    }
    return knd_OK;
}

int knd_proc_inst_export(struct kndProcInst *self,
                         knd_format format,
                         bool is_list_item,
                         struct kndTask *task)
{
    switch (format) {
        //case KND_FORMAT_JSON:
        //    return knd_proc_inst_export_JSON(self, is_list_item, task);
        case KND_FORMAT_GSL:
            return knd_proc_inst_export_GSL(self, is_list_item, task, 0);
        default:
            return knd_RANGE;
    }
}

int knd_proc_inst_entry_new(struct kndMemPool *mempool,
                            struct kndProcInstEntry **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                                sizeof(struct kndProcInstEntry), &page);              RET_ERR();
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_TINY,
                                     sizeof(struct kndProcInstEntry), &page);         RET_ERR();
    }
    *result = page;
    return knd_OK;
}

int knd_proc_inst_new(struct kndMemPool *mempool,
                      struct kndProcInst **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                                sizeof(struct kndProcInst), &page);               RET_ERR();
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_SMALL,
                                     sizeof(struct kndProcInst), &page);          RET_ERR();
    }
    *result = page;
    return knd_OK;
}

int knd_proc_inst_mem(struct kndMemPool *mempool,
                      struct kndProcInst **result)
{
    void *page;
    int err;
    err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_SMALL,
                                 sizeof(struct kndProcInst), &page);              RET_ERR();
    *result = page;
    return knd_OK;
}
