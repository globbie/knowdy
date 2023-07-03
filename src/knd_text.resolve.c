#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_text.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_class.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_attr.h"
#include "knd_shard.h"
#include "knd_user.h"
#include "knd_utils.h"
#include "knd_mempool.h"
#include "knd_output.h"

#define DEBUG_TEXT_RESOLVE_LEVEL_0 0
#define DEBUG_TEXT_RESOLVE_LEVEL_1 0
#define DEBUG_TEXT_RESOLVE_LEVEL_2 0
#define DEBUG_TEXT_RESOLVE_LEVEL_3 0
#define DEBUG_TEXT_RESOLVE_LEVEL_TMP 1

static int resolve_class_inst(struct kndStatement *stm, struct kndClassInstEntry *entry, struct kndTask *unused_var(task))
{
    if (DEBUG_TEXT_RESOLVE_LEVEL_2)
        knd_log(".. resolve class inst %p from stm %p", entry, stm);
    // TODO
    return knd_OK;
}

int knd_statement_resolve(struct kndStatement *stm, struct kndTask *task)
{
    struct kndClassDeclar *cd;
    struct kndClassInstEntry *ci;
    int err;

    FOREACH (cd, stm->declars) {
        FOREACH (ci, cd->insts) {
            if (!ci->inst) continue;
            if (!ci->inst->class_var) continue;
            err = resolve_class_inst(stm, ci, task);
            KND_TASK_ERR("failed to resolve class inst \"%.*s\"", ci->name_size, ci->name);
        }
    }

    return knd_OK;
}

#if 0
static gsl_err_t parse_text(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndText *text;
    gsl_err_t parser_err;
    int err;

    err = knd_text_new(mempool, &text);
    if (err) return *total_size = 0, make_gsl_err_external(knd_NOMEM);

    parser_err = knd_text_import(text, rec, total_size, task);
    if (parser_err.code) {
        KND_TASK_LOG("text import failed");
        return parser_err;
    }
    ctx->attr_var->text = text;
    text->attr_var = ctx->attr_var;

    return make_gsl_err(gsl_OK);
}
#endif

int knd_text_resolve(struct kndAttrVar *attr_var, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndText *text;
    int err;

    if (DEBUG_TEXT_RESOLVE_LEVEL_TMP)
        knd_log(".. resolving text attr var: %.*s  class:%.*s",
                attr_var->name_size, attr_var->name,
                attr_var->class_var->parent->name_size,
                attr_var->class_var->parent->name);

    err = knd_text_new(mempool, &text);
    KND_TASK_ERR("failed to alloc a text field %.*s", attr_var->name_size, attr_var->name);

    err = knd_charseq_fetch(task->repo, attr_var->val, attr_var->val_size, &text->seq, task);
    KND_TASK_ERR("failed to fetch a charseq of %.*s", attr_var->name_size, attr_var->name);
    
    attr_var->text = text;
    return knd_OK;
}
