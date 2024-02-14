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

#define DEBUG_REL_PRED_RESOLVE_LEVEL_1 0
#define DEBUG_REL_PRED_RESOLVE_LEVEL_2 0
#define DEBUG_REL_PRED_RESOLVE_LEVEL_3 0
#define DEBUG_REL_PRED_RESOLVE_LEVEL_TMP 1

int knd_rel_pred_resolve(struct kndAttrVar *var, struct kndRepo *repo, struct kndTask *task)
{
    struct kndSharedDict *class_name_idx = repo->class_name_idx;
    struct kndAttr *attr = var->attr;
    struct kndRel *rel = attr->impl;
    struct kndClassEntry *entry;
    struct kndClass *template_c, *c;
    struct kndAttrVar *item;
    int err;

    assert(rel->proc != NULL);
    assert(var->val != NULL);
    assert(var->val_size != 0);

    if (DEBUG_REL_PRED_RESOLVE_LEVEL_2) {
        knd_log("\n>> resolving REL var {class %.*s {%.*s %.*s}} "
                " {proc %.*s {arg %.*s} {impl-arg %.*s}}",
                attr->parent->name_size, attr->parent->name, var->name_size, var->name,
                var->val_size, var->val, rel->ref_proc_name_size, rel->ref_proc_name,
                rel->subj_arg_name_size, rel->subj_arg_name,
                rel->impl_arg_name_size, rel->impl_arg_name);
    }

    entry = knd_shared_dict_get(class_name_idx, var->val, var->val_size);
    if (!entry) {
	 err = knd_NO_MATCH;
         KND_TASK_ERR("no such {class %.*s} "
                      "failed to resolve {rel %.*s",
                      var->val_size, var->val, var->name_size, var->name);
    }
    
    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire class \"%.*s\"", entry->name_size, entry->name);
    if (!c->is_resolved) {
        err = knd_class_resolve(c, task);
        KND_TASK_ERR("failed to resolve class %.*s", c->name_size, c->name);
    }
    
    entry = rel->impl_arg->template;
    if (!entry) {
        entry = knd_shared_dict_get(class_name_idx,
                                    rel->impl_arg->classname, rel->impl_arg->classname_size);
        if (!entry) {
            err = knd_NO_MATCH;
            KND_TASK_ERR("no such {class %.*s} "
                         "failed to resolve {rel %.*s",
                         var->val_size, var->val, var->name_size, var->name);
        }
        rel->impl_arg->template = entry;
    }

    err = knd_class_acquire(entry, &template_c, task);
    KND_TASK_ERR("failed to acquire template class \"%.*s\"", entry->name_size, entry->name);

    err = knd_is_base(template_c, c);
    KND_TASK_ERR("no inheritance from %.*s to %.*s",
                 template_c->name_size, template_c->name, c->name_size, c->name);

    /* other args */
    FOREACH (item, var->children) {
        if (DEBUG_REL_PRED_RESOLVE_LEVEL_TMP) {
            knd_log(".. check rel {proc %.*s {arg %.*s}}",
                    attr->ref_proc_name_size, attr->ref_proc_name,
                    item->name_size, item->name);
        }
    }
    return knd_OK;
}
