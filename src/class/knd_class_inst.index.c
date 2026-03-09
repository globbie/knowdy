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
static int update_attr_stm_indices(struct kndClassInstEntry *entry, struct kndRepo *unused_var(repo),
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
            //err = knd_text_index(var->text, repo, task);
            //KND_TASK_ERR("failed to index text attr var \"%.*s\"", var->name_size, var->name);
            break;
        default:
            break;
        }
    }
    return knd_OK;
}
#endif

int knd_class_inst_update_indices(struct kndRepo *repo, struct kndClassEntry *is_a,
                                  struct kndStateRef *state_refs,
                                  struct kndTask *task)
{
    struct kndClassEntry *class_entry = is_a;
    struct kndClass *c;
    struct kndCommit *commit = state_refs->state->commit;
    int err;

    assert(commit != NULL);

    err = knd_class_acquire(is_a, &c, repo, task);
    KND_TASK_ERR("failed to acquire class %.*s", is_a->name_size, is_a->name);
   
    if (DEBUG_INST_IDX_LEVEL_2) {
        knd_log(".. {repo %.*s} to update inst indices of {cls %.*s}}",
                repo->name_size, repo->name, is_a->name_size, is_a->name);
    }

    /* user repo selected: activate copy-on-write */
    if (task->user_ctx) {
        //class_entry = knd_dict_get(task->idxs.cls_name_idx, is_a->name, is_a->name_size);
        /*if (is_a->repo != repo) {
            if (!class_entry) {
                if (DEBUG_INST_IDX_LEVEL_3) {
                    knd_log("NB: copy-on-write of class entry \"%.*s\" activated in repo %.*s",
                            class_entry->name_size, c->name, repo->name_size, repo->name);
                }
                err = knd_class_entry_clone(is_a, repo, &class_entry, task);
                KND_TASK_ERR("failed to clone class entry");
            }
            }*/
    }

    if (!class_entry) {
        err = knd_FAIL;
        KND_TASK_ERR("class entry not found: %.*s", is_a->name_size, is_a->name);
    }

#if 0
    do {
        name_idx = atomic_load_explicit(&c->inst_name_idx, memory_order_acquire);
        if (name_idx) {
            // TODO free new_name_idx if (new_name_idx != NULL) 
            break;
        }
        err = knd_dict_new(&new_name_idx, KND_MEDIUM_DICT_SIZE, mempool);
        KND_TASK_ERR("failed to create inst name idx");

    } while (!atomic_compare_exchange_weak(&c->inst_name_idx, &name_idx, new_name_idx));

    do {
        idx = atomic_load_explicit(&c->inst_idx, memory_order_acquire);
        if (idx) {
            // TODO free new_idx if (new_idx != NULL) 
            break;
        }
        err = knd_set_new(&new_idx, mempool);
        KND_TASK_ERR("failed to create inst idx");

    } while (!atomic_compare_exchange_weak(&c->inst_idx, &idx, new_idx));

    name_idx = c->inst_name_idx;
    idx = c->inst_idx;
    
    FOREACH (ref, state_refs) {
        entry = ref->obj;

        switch (ref->state->phase) {
        case KND_CREATED:
            if (entry->name_size) {
                err = knd_shared_dict_set(name_idx, entry->name, entry->name_size, (void*)entry);
                KND_TASK_ERR("name idx failed to register class inst %.*s, err:%d",
                             entry->name_size, entry->name, err);                
            }
            err = knd_shared_set_add(idx, entry->id, entry->id_size, (void*)entry);
            KND_TASK_ERR("class inst idx failed to register \"%.*s\"",
                         entry->name_size, entry->name);

            if (entry->inst->num_attr_stms) {
                err = update_attr_stm_indices(entry, repo, task);
                KND_TASK_ERR("failed to update attr inst indices with \"%.*s\"",
                             entry->id_size, entry->id);
            }
            break;
        default:
            break;
        }
    }
#endif

    return knd_OK;
}

int knd_class_inst_index(struct kndClassInst *self, struct kndRepo *repo, struct kndTask *task)
{
    struct kndClass *c;
    struct kndAttrStm *stm;
    struct kndAttr *attr;
    int err;

    assert(self->entry->is_a != NULL);

    err = knd_class_acquire(self->entry->is_a, &c, repo, task);
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
