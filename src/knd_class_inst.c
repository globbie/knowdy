#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_class_inst.h"
#include "knd_class.h"
#include "knd_mempool.h"
#include "knd_attr.h"
#include "knd_attr_inst.h"
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

#define DEBUG_INST_LEVEL_1 0
#define DEBUG_INST_LEVEL_2 0
#define DEBUG_INST_LEVEL_3 0
#define DEBUG_INST_LEVEL_4 0
#define DEBUG_INST_LEVEL_TMP 1

void knd_class_inst_str(struct kndClassInst *self, size_t depth)
{
    //struct kndState *state = self->states;
    struct kndAttrVar *item;

    if (self->type == KND_OBJ_ADDR) {
        knd_log("\n%*s>>> class inst \"%.*s::%.*s\"  numid:%zu",
                depth * KND_OFFSET_SIZE, "",
                self->blueprint->name_size, self->blueprint->name,
                self->name_size, self->name, self->entry->numid);
        //if (state) {
        //    knd_log("    state:%zu  phase:%d", state->numid, state->phase);
        //}
    }

    if (self->class_var->attrs) {
        for (item = self->class_var->attrs; item; item = item->next)
            knd_attr_var_str(item, depth + 1);
    }
}

static int update_attr_var_indices(struct kndClassEntry *blueprint,
                                   struct kndClassInstEntry *entry,
                                   struct kndTask *unused_var(task))
{
    knd_log(".. class inst %.*s::%.*s attr var indexing",
            blueprint->name_size, blueprint->name,
            entry->id_size, entry->id);

    return knd_OK;
}

int knd_class_inst_update_indices(struct kndRepo *repo, struct kndClassEntry *baseclass,
                                  struct kndStateRef *state_refs, struct kndTask *task)
{
    struct kndClassEntry *c = baseclass;
    struct kndStateRef *ref;
    struct kndClassInstEntry *entry;
    struct kndSharedDict *name_idx = NULL;
    struct kndSharedDict *new_name_idx = NULL;
    struct kndSharedSet *idx = NULL;
    struct kndSharedSet *new_idx = NULL;
    struct kndCommit *commit = state_refs->state->commit;
    struct kndSharedDictItem *item = NULL;
    struct kndMemPool *mempool = task->mempool;
    int err;

    assert(commit != NULL);

    if (DEBUG_INST_LEVEL_TMP)
        knd_log(".. repo \"%.*s\" to update inst indices of class \"%.*s\" (repo:%.*s)",
                repo->name_size, repo->name, baseclass->name_size, baseclass->name,
                baseclass->repo->name_size, baseclass->repo->name);

    /* user repo selected: activate copy-on-write */
    if (task->user_ctx) {
        mempool = task->shard->user->mempool;
        c = knd_shared_dict_get(repo->class_name_idx, baseclass->name, baseclass->name_size);

        if (baseclass->repo != repo) {
            if (!c) {
                if (DEBUG_INST_LEVEL_TMP) {
                    knd_log("NB: copy-on-write activated in repo %.*s",
                            repo->name_size, repo->name);
                }
                err = knd_class_entry_new(mempool, &c);
                KND_TASK_ERR("failed to alloc kndClassEntry");
                c->repo = repo;
                c->name = baseclass->name;
                c->name_size = baseclass->name_size;
                c->class = baseclass->class;

                err = knd_shared_dict_set(repo->class_name_idx, c->name, c->name_size,
                                          (void*)c, mempool, NULL, &item, false);
                KND_TASK_ERR("failed to register class entry \"%.*s\"", c->name_size, c->name);
                c->dict_item = item;
            }
        }
    }

    if (!c) {
        err = knd_FAIL;
        KND_TASK_ERR("class not found: %.*s", baseclass->name_size, baseclass->name);
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
    
    for (ref = state_refs; ref; ref = ref->next) {
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
            KND_TASK_ERR("class inst idx failed to register \"%.*s\"", entry->name_size, entry->name);

            err = update_attr_var_indices(c, entry, task);
            KND_TASK_ERR("failed to update attr inst indices with \"%.*s\"", entry->id_size, entry->id);
            break;
        default:
            break;
        }
    }
    return knd_OK;
}

int knd_class_inst_export(struct kndClassInst *self, knd_format format, bool is_list_item, struct kndTask *task)
{
    switch (format) {
        case KND_FORMAT_JSON:
            return knd_class_inst_export_JSON(self, is_list_item, task);
        case KND_FORMAT_GSL:
            return knd_class_inst_export_GSL(self, is_list_item, task, 0);
        case KND_FORMAT_GSP:
            return knd_class_inst_export_GSP(self, task);
        default:
            return knd_RANGE;
    }
}

int knd_class_inst_commit_state(struct kndClass *self, struct kndStateRef *children, size_t num_children,
                                struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndCommit *commit = task->ctx->commit;
    struct kndStateRef *ref;
    struct kndState *state, *head;
    int err;

    err = knd_state_new(mempool, &state);
    KND_TASK_ERR("class inst state alloc failed");
    state->phase = KND_SELECTED;
    state->children = children;
    state->num_children = num_children;

    do {
        head = atomic_load_explicit(&self->entry->inst_states, memory_order_relaxed);
        if (head) {
            state->next = head;
            state->numid = head->numid + 1;
        }
    } while (!atomic_compare_exchange_weak(&self->entry->inst_states, &head, state));

    /* inform our repo */
    err = knd_state_ref_new(mempool, &ref);                                 RET_ERR();
    ref->state = state;
    ref->type = KND_STATE_CLASS;
    ref->obj = self->entry;

    ref->next = commit->class_state_refs;
    commit->class_state_refs = ref;
    commit->num_class_state_refs++;

    return knd_OK;
}

int knd_class_inst_export_commit(struct kndStateRef *state_refs, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndStateRef *ref;
    struct kndClassInstEntry *entry;
    int err;

    for (ref = state_refs; ref; ref = ref->next) {
        entry = ref->obj;
        if (!entry) continue;

        err = out->writec(out, '{');                                              RET_ERR();
        if (ref->state->phase == KND_CREATED) {
            err = out->writec(out, '!');                                          RET_ERR();
        }

        err = out->write(out, "inst ", strlen("inst "));                          RET_ERR();

        err = knd_class_inst_export(entry->inst, KND_FORMAT_GSL, true, task);     RET_ERR();

        err = out->writec(out, '}');                                              RET_ERR();
    }
    return knd_OK;
}

int knd_class_inst_entry_new(struct kndMemPool *mempool, struct kndClassInstEntry **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL, sizeof(struct kndClassInstEntry), &page);
        RET_ERR();
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_SMALL, sizeof(struct kndClassInstEntry), &page);
        RET_ERR();
    }
    *result = page;
    return knd_OK;
}

int knd_class_inst_new(struct kndMemPool *mempool, struct kndClassInst **result)
{
    void *page;
    int err;

    if (mempool->type == KND_ALLOC_INCR) {
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                                     sizeof(struct kndClassInst), &page);         RET_ERR();
    } else {
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                                sizeof(struct kndClassInst), &page);                  RET_ERR();
    }
    *result = page;
    return knd_OK;
}

int knd_class_inst_mem(struct kndMemPool *mempool,
                       struct kndClassInst **result)
{
    void *page;
    int err;
    err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                                 sizeof(struct kndClassInst), &page);                  RET_ERR();
    *result = page;
    return knd_OK;
}
