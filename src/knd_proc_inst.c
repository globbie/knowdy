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
                                    struct kndOutput *out,
                                    size_t depth)
{
    struct kndClass *c;
    int err;
    
    err = out->writef(out, "%*s{%.*s\n",
                      depth * KND_OFFSET_SIZE, "",
                      self->arg->name_size, self->arg->name);                     RET_ERR();
    if (self->class_inst) {
        c = self->class_inst->base;

        err = out->writef(out, "%*s{class %.*s\n",
                          (depth + 1) * KND_OFFSET_SIZE, "",
                          c->name_size, c->name);                     RET_ERR();
        err = out->writec(out, '}');
        
    }

    err = out->writec(out, '}');
    return knd_OK;
}

int knd_proc_inst_export_GSL(struct kndProcInst *self,
                             struct kndOutput *out,
                             size_t depth)
{
    struct kndProcArgInst *arg;
    int err;

    err = out->writef(out, "%*s{proc-inst %.*s\n",
                      depth * KND_OFFSET_SIZE, "",
                      self->name_size, self->name);                RET_ERR();

    if (self->args) {
        err = out->writef(out, "%*s[arg\n", (depth + 1) * KND_OFFSET_SIZE, "");

        for (arg = self->args; arg; arg = arg->next) {
            proc_arg_inst_export_GSL(arg, out, depth + 2);
        }

        err = out->writef(out, "%*s]", (depth + 1) * KND_OFFSET_SIZE, "");
    }
    err = out->writef(out, "%*s}",
                      depth * KND_OFFSET_SIZE, "");
    return knd_OK;
}

int knd_proc_inst_entry_new(struct kndMemPool *mempool,
                            struct kndProcInstEntry **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndProcInstEntry), &page);              RET_ERR();
    *result = page;
    return knd_OK;
}

int knd_proc_inst_new(struct kndMemPool *mempool,
                      struct kndProcInst **result)
{
    void *page;
    int err;
    if (mempool->type == KND_ALLOC_INCR) {
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_SMALL,
                                     sizeof(struct kndProcInst), &page);          RET_ERR();
    } else {
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                                sizeof(struct kndProcInst), &page);               RET_ERR();
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
