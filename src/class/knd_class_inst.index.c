#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_class_inst.h"
#include "knd_class.h"
#include "knd_mempool.h"
#include "knd_attr.h"
#include "knd_attr_stm.h"
#include "knd_repo.h"

#include "knd_text.h"
#include "knd_quant.h"
#include "knd_rel.h"
#include "knd_shared_dict.h"
#include "knd_shared_set.h"
#include "knd_output.h"

#include "knd_user.h"
#include "knd_state.h"
#include "knd_commit.h"

#include <gsl-parser.h>

#define DEBUG_INST_IDX_LEVEL_1 0
#define DEBUG_INST_IDX_LEVEL_2 0
#define DEBUG_INST_IDX_LEVEL_3 0
#define DEBUG_INST_IDX_LEVEL_4 0
#define DEBUG_INST_IDX_LEVEL_TMP 1

#if 0
static int update_attr_stm_indices(struct kndClassInstEntry *entry, struct kndRepoSnapshot *unused_var(snapshot),
                                   struct kndTask *unused_var(task))
{
    struct kndAttrStm *var;
    //int err;

    if (DEBUG_INST_IDX_LEVEL_2) {
        knd_log(".. class inst \"%.*s\" attr stm indexing", entry->name_size, entry->name);
    }
    FOREACH (var, entry->inst->attr_stms) {
        switch (var->attr->type) {
        case KND_ATTR_TEXT:
            if (DEBUG_INST_IDX_LEVEL_3)
                knd_log(".. indexing text attr \"%.*s\"", var->name_size, var->name);
            //err = knd_text_index(var->text, snapshot, task);
            //KND_TASK_ERR("failed to index text attr var \"%.*s\"", var->name_size, var->name);
            break;
        default:
            break;
        }
    }
    return knd_OK;
}
#endif

int knd_class_inst_index(struct kndClassInst *self, struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndClass *c;
    struct kndAttrStm *stm;
    struct kndAttr *attr;
    int err;

    assert(self->entry->is_a != NULL);

    err = knd_class_acquire(self->entry->is_a, &c, snapshot, task);
    KND_TASK_ERR("failed to acquire {cls %.*s}",
                 self->entry->is_a->name_size, self->entry->is_a->name);

    if (DEBUG_INST_IDX_LEVEL_2) {
        knd_log(".. indexing {cls %.*s {inst %.*s}}",
                c->entry->name_size, c->entry->name,
                self->name_size, self->name);
    }

    if (!self->num_attr_stms) return knd_OK;

    FOREACH (stm, self->attr_stms) {
        if (DEBUG_INST_IDX_LEVEL_3) {
            knd_log(".. idx inst attr stm {cls %.*s {inst %.*s {%.*s %.*s}}",
                    c->name_size, c->name, self->name_size, self->name,
                    stm->name_size, stm->name, stm->val_size, stm->val);
        }
        attr = stm->attr;

        /*if (attr->is_a_set) {
            err = knd_index_attr_stm_list(self->entry->is_a, self->entry,
                                          attr, stm, task);
            KND_TASK_ERR("failed to index class inst attr stm list %.*s",
                         attr->name_size, attr->name);
            continue;
            }*/

        //err = knd_index_inst_attr_stm(self->entry, attr, stm, task);
        //KND_TASK_ERR("failed to index inst attr stm %.*s", attr->name_size, attr->name);
    }
    return knd_OK;
}
