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

void knd_proc_inst_str(struct kndProcInst *self, size_t depth)
{
    struct kndProcArgVar *var;

    knd_log("%*s{proc-inst %.*s",
            depth * KND_OFFSET_SIZE, "", self->name_size, self->name);

    if (self->procvar) {
        knd_log("%*s    [arg", depth * KND_OFFSET_SIZE, "");
        for (var = self->procvar->args; var; var = var->next) {
            //
        }
        knd_log("%*s    ]", depth * KND_OFFSET_SIZE, "");
    }

    knd_log("%*s}", depth * KND_OFFSET_SIZE, "");
}

int knd_proc_inst_export_GSL(struct kndProcInst *self, bool is_list_item, knd_state_phase phase, struct kndTask *task, size_t depth)
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

    OUT(self->name, self->name_size);

    if (task->ctx->use_alias) {
        if (self->alias_size) {
            err = out->write(out, "{_as ", strlen("{_as "));                  RET_ERR();
            err = out->write(out, self->alias, self->alias_size);             RET_ERR();
            err = out->writec(out, '}');                                      RET_ERR();
        }
    }
    if (self->linear_pos) {
        err = out->write(out, "{_pos ", strlen("{_pos "));             RET_ERR();
        err = out->writef(out, "%zu", self->linear_pos);                   RET_ERR();
        err = out->writec(out, '}');                                 RET_ERR();
    }
    if (self->linear_len) {
        err = out->write(out, "{_len ", strlen("{_len "));             RET_ERR();
        err = out->writef(out, "%zu", self->linear_len);                   RET_ERR();
        err = out->writec(out, '}');                                 RET_ERR();
    }

    if (self->procvar) {
        for (var = self->procvar->args; var; var = var->next) {
            knd_proc_arg_var_export_GSL(var, task, depth + 2);
        }
    }

    if (!is_list_item) {
        err = out->writef(out, "%*s}", depth * KND_OFFSET_SIZE, "");  RET_ERR();
    }
    return knd_OK;
}

int knd_proc_inst_export(struct kndProcInst *self, knd_format format, bool is_list_item, struct kndTask *task)
{
    switch (format) {
        //case KND_FORMAT_JSON:
        //    return knd_proc_inst_export_JSON(self, is_list_item, task);
        case KND_FORMAT_GSL:
            return knd_proc_inst_export_GSL(self, is_list_item, KND_SELECTED, task, 0);
        default:
            return knd_RANGE;
    }
}

int knd_proc_inst_entry_new(struct kndMemPool *mempool, struct kndProcInstEntry **result)
{
    void *page;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndProcInstEntry));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndProcInstEntry));
    *result = page;
    return knd_OK;
}

int knd_proc_inst_new(struct kndMemPool *mempool, struct kndProcInst **result)
{
    void *page;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndProcInst));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndProcInst));
    *result = page;
    return knd_OK;
}
