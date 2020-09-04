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

static int resolve_proc_inst(struct kndStatement *stm, struct kndProcInstEntry *proc_entry, struct kndTask *task)
{
    struct kndProcArgVar *var;
    struct kndClassDeclaration *cd;
    struct kndClassInstEntry *entry;
    int err = knd_OK;
    
    FOREACH (var, proc_entry->inst->procvar->args) {
        if (!var->template) continue;
        FOREACH (cd, stm->class_declars) {
            FOREACH (entry, cd->insts) {
                if (var->val_size != entry->name_size) continue;
                if (memcmp(entry->name, var->val, var->val_size)) continue;
                var->inst = entry->inst;

                err = knd_is_base(var->template->class, entry->inst->blueprint->class);
                KND_TASK_ERR("template \"%.*s\" mismatch with \"%.*s\"",
                             var->template->name_size, var->template->name,
                             entry->inst->blueprint->name_size, entry->inst->blueprint->name);

                if (DEBUG_TEXT_RESOLVE_LEVEL_TMP)
                    knd_log("++ class inst ref \"%.*s\" (%.*s) class template: \"%.*s\"",
                            var->val_size, var->val, entry->inst->blueprint->name_size, entry->inst->blueprint->name,
                            var->template->name_size, var->template->name);
            }
        }
        if (!var->inst) {
            err = knd_NO_MATCH;
            KND_TASK_ERR("failed to resolve class inst ref \"%.*s\"", var->val_size, var->val);
        }
    }
    return knd_OK;
}

int knd_statement_resolve(struct kndStatement *stm, struct kndTask *task)
{
    struct kndClassDeclaration *cd;
    struct kndProcDeclaration *pd;
    struct kndClassInstEntry *ci;
    struct kndProcInstEntry *pi;
    int err;

    FOREACH (cd, stm->class_declars) {
        FOREACH (ci, cd->insts) {
            if (!ci->inst->class_var) continue;
            err = resolve_class_inst(stm, ci, task);
            KND_TASK_ERR("failed to resolve class inst \"%.*s\"", ci->name_size, ci->name);
        }
    }

    FOREACH (pd, stm->proc_declars) {
        FOREACH (pi, pd->insts) {
            if (!pi->inst->procvar) continue;
            err = resolve_proc_inst(stm, pi, task);
            KND_TASK_ERR("failed to resolve proc inst \"%.*s\"", pi->name_size, pi->name);
        }
    }
    return knd_OK;
}
