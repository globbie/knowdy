#include "knd_attr.h"
#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_shared_set.h"
#include "knd_set.h"
#include "knd_repo.h"
#include "knd_user.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#include <assert.h>
#include <string.h>

#include "knd_task.h"
#include "knd_class.h"
#include "knd_proc.h"
#include "knd_rel.h"
#include "knd_repo.h"

#define DEBUG_REL_RESOLVE_LEVEL_1 0
#define DEBUG_REL_RESOLVE_LEVEL_2 0
#define DEBUG_REL_RESOLVE_LEVEL_TMP 1

int knd_rel_resolve(struct kndRel *rel, struct kndRepo *unused_var(repo), struct kndTask *task)
{
    struct kndDict *proc_name_idx = task->idxs.proc_name_idx;
    const char *proc_name = rel->ref_proc_name;
    size_t proc_name_size = rel->ref_proc_name_size;
    struct kndProcEntry *proc_entry;
    struct kndProcArg *arg;
    int err;

    if (DEBUG_REL_RESOLVE_LEVEL_2) {
        knd_log("\n>> resolving REL attr {class %.*s {%.*s %.*s}} "
                " {self-arg %.*s} {impl-arg %.*s}}",
                rel->attr->owner->name_size, rel->attr->owner->name,
                rel->attr->name_size, rel->attr->name,
                proc_name_size, proc_name,
                rel->subj_arg_name_size, rel->subj_arg_name,
                rel->impl_arg_name_size, rel->impl_arg_name);
    }

    assert(proc_name_size != 0 && proc_name != NULL);

    err = knd_dict_get(proc_name_idx, proc_name, proc_name_size, (void**)&proc_entry, task);
    if (err) {
        err = knd_NO_MATCH;
        KND_TASK_ERR("no such {proc %.*s}", proc_name_size, proc_name);
    }

    // TODO acquire
    rel->proc = proc_entry->proc;
    
    FOREACH (arg, rel->proc->args) {
        if (arg->name_size == rel->subj_arg_name_size) {
            if (!memcmp(arg->name, rel->subj_arg_name, rel->subj_arg_name_size)) {
                rel->subj_arg = arg;
                continue;
            }
        }
        if (arg->name_size == rel->impl_arg_name_size) {
            if (!memcmp(arg->name, rel->impl_arg_name, rel->impl_arg_name_size)) {
                rel->impl_arg = arg;
            }
        }
    }

    if (!rel->subj_arg) {
        err = knd_NO_MATCH;
        KND_TASK_ERR("no such arg \"%.*s\"", rel->subj_arg_name_size, rel->subj_arg_name);
    }
    if (!rel->impl_arg) {
        err = knd_NO_MATCH;
        KND_TASK_ERR("no such impl arg \"%.*s\"", rel->impl_arg_name_size, rel->impl_arg_name);
    }
    return knd_OK;
}


