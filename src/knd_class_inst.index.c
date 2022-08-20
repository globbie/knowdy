#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_class_inst.h"
#include "knd_class.h"
#include "knd_mempool.h"
#include "knd_attr.h"
#include "knd_repo.h"
#include "knd_shard.h"

#include "knd_text.h"
#include "knd_num.h"
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

static int update_attr_var_indices(struct kndClassInstEntry *entry, struct kndRepo *repo, struct kndTask *task)
{
    struct kndAttrVar *var;
    int err;

    if (DEBUG_INST_IDX_LEVEL_2)
        knd_log(".. class inst \"%.*s\" attr var indexing", entry->name_size, entry->name);

    FOREACH (var, entry->inst->class_var->attrs) {
        switch (var->attr->type) {
        case KND_ATTR_TEXT:
            if (DEBUG_INST_IDX_LEVEL_3)
                knd_log(".. indexing text attr \"%.*s\"", var->name_size, var->name);
            err = knd_text_index(var->text, repo, task);
            KND_TASK_ERR("failed to index text attr var \"%.*s\"", var->name_size, var->name);
            break;
        case KND_ATTR_REL:
            if (DEBUG_INST_IDX_LEVEL_2)
                knd_log(".. indexing Rel attr \"%.*s\" (is a set:%d)",
                        var->name_size, var->name, var->attr->is_a_set);

            if (var->attr->is_a_set) {
                err = knd_index_attr_var_list(entry->blueprint, entry, var->attr, var, task);
                KND_TASK_ERR("failed to index attr var list");
                break;
            }
            break;
        default:
            break;
        }
    }
    return knd_OK;
}

int knd_class_inst_update_indices(struct kndRepo *repo, struct kndClassEntry *blueprint,
                                  struct kndStateRef *state_refs, struct kndTask *task)
{
    struct kndClass *c = blueprint->class;
    struct kndClassEntry *class_entry = blueprint;
    struct kndStateRef *ref;
    struct kndClassInstEntry *entry;
    struct kndSharedDict *name_idx = NULL;
    struct kndSharedDict *new_name_idx = NULL;
    struct kndSharedSet *idx = NULL;
    struct kndSharedSet *new_idx = NULL;
    struct kndCommit *commit = state_refs->state->commit;
    struct kndSharedDictItem *item = NULL;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    int err;

    assert(commit != NULL);
    assert(c != NULL);

    if (DEBUG_INST_IDX_LEVEL_2)
        knd_log(".. repo \"%.*s\" to update inst indices of class \"%.*s\" (repo:%.*s)",
                repo->name_size, repo->name, blueprint->name_size, blueprint->name,
                blueprint->repo->name_size, blueprint->repo->name);

    /* user repo selected: activate copy-on-write */
    if (task->user_ctx) {
        class_entry = knd_shared_dict_get(repo->class_name_idx, blueprint->name, blueprint->name_size);
        if (blueprint->repo != repo) {
            if (!class_entry) {
                if (DEBUG_INST_IDX_LEVEL_3) {
                    knd_log("NB: copy-on-write of class entry \"%.*s\" activated in repo %.*s",
                            class_entry->name_size, c->name, repo->name_size, repo->name);
                }
                err = knd_class_entry_clone(blueprint, repo, &class_entry, task);
                KND_TASK_ERR("failed to clone class entry");
            }
        }
    }

    if (!class_entry) {
        err = knd_FAIL;
        KND_TASK_ERR("class entry not found: %.*s", blueprint->name_size, blueprint->name);
    }

    do {
        name_idx = atomic_load_explicit(&c->inst_name_idx, memory_order_acquire);
        if (name_idx) {
            // TODO free new_name_idx if (new_name_idx != NULL) 
            break;
        }
        err = knd_shared_dict_new(&new_name_idx, KND_MEDIUM_DICT_SIZE);
        KND_TASK_ERR("failed to create inst name idx");

    } while (!atomic_compare_exchange_weak(&c->inst_name_idx, &name_idx, new_name_idx));

    do {
        idx = atomic_load_explicit(&c->inst_idx, memory_order_acquire);
        if (idx) {
            // TODO free new_idx if (new_idx != NULL) 
            break;
        }
        err = knd_shared_set_new(NULL, &new_idx);
        KND_TASK_ERR("failed to create inst idx");

    } while (!atomic_compare_exchange_weak(&c->inst_idx, &idx, new_idx));

    name_idx = atomic_load_explicit(&c->inst_name_idx, memory_order_acquire);
    idx = atomic_load_explicit(&c->inst_idx, memory_order_acquire);
    
    FOREACH (ref, state_refs) {
        entry = ref->obj;

        switch (ref->state->phase) {
        case KND_CREATED:
            if (entry->name_size) {
                err = knd_shared_dict_set(name_idx, entry->name, entry->name_size, (void*)entry,
                                          mempool, commit, &item, false);
                KND_TASK_ERR("name idx failed to register class inst %.*s, err:%d",
                             entry->name_size, entry->name, err);
                
                item->phase = KND_SHARED_DICT_VALID;
            }
            err = knd_shared_set_add(idx, entry->id, entry->id_size, (void*)entry);
            KND_TASK_ERR("class inst idx failed to register \"%.*s\"",
                         entry->name_size, entry->name);

            if (entry->inst->class_var->attrs) {
                err = update_attr_var_indices(entry, repo, task);
                KND_TASK_ERR("failed to update attr inst indices with \"%.*s\"",
                             entry->id_size, entry->id);
            }
            break;
        default:
            break;
        }
    }
    return knd_OK;
}
