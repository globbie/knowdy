#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_class_inst.h"
#include "knd_class.h"
#include "knd_mempool.h"
#include "knd_attr.h"
#include "knd_attr_inst.h"
#include "knd_repo.h"

#include "knd_text.h"
#include "knd_num.h"
#include "knd_rel.h"
#include "knd_set.h"
#include "knd_output.h"

#include "knd_user.h"
#include "knd_state.h"

#include <gsl-parser.h>

#define DEBUG_INST_LEVEL_1 0
#define DEBUG_INST_LEVEL_2 0
#define DEBUG_INST_LEVEL_3 0
#define DEBUG_INST_LEVEL_4 0
#define DEBUG_INST_LEVEL_TMP 1

void knd_class_inst_str(struct kndClassInst *self, size_t depth)
{
    struct kndAttrInst *attr_inst;
    struct kndState *state = self->states;

    if (self->type == KND_OBJ_ADDR) {
        knd_log("\n%*sClass Instance \"%.*s::%.*s\"  numid:%zu",
                depth * KND_OFFSET_SIZE, "",
                self->base->name_size, self->base->name,
                self->name_size, self->name,
                self->entry->numid);
        if (state) {
            knd_log("    state:%zu  phase:%d",
                    state->numid, state->phase);
        }
    }

    for (attr_inst = self->attr_insts; attr_inst; attr_inst = attr_inst->next) {
        knd_attr_inst_str(attr_inst, depth + 1);
    }
}

int knd_class_inst_update_indices(struct kndClassEntry *baseclass,
                                  struct kndStateRef *state_refs,
                                  struct kndTask *task)
{
    struct kndStateRef *ref;
    struct kndClassInstEntry *entry;
    struct kndSharedDict *name_idx = baseclass->inst_name_idx;
    struct kndSet *idx = baseclass->inst_idx;
    struct kndCommit *commit = state_refs->state->commit;
    struct kndSharedDictItem *item = NULL;
    int err;

    if (!name_idx) {
        err = knd_shared_dict_new(&name_idx, KND_MEDIUM_DICT_SIZE);
        KND_TASK_ERR("failed to create inst name idx");
        // TODO: atomic
        baseclass->inst_name_idx = name_idx; 
    }
    if (!idx) {
        // TODO: thread safe
        err = knd_set_new(task->mempool, &idx);
        KND_TASK_ERR("failed to create inst idx");
        baseclass->inst_idx = idx; 
    }

    for (ref = state_refs; ref; ref = ref->next) {
        entry = ref->obj;
        switch (ref->state->phase) {
        case KND_CREATED:
            err = knd_shared_dict_set(name_idx,
                                      entry->name,  entry->name_size,
                                      (void*)entry,
                                      task->mempool,
                                      commit, &item, false);
            KND_TASK_ERR("name idx failed to register class inst %.*s",
                         entry->name_size, entry->name);
            entry->dict_item = item;

            err = knd_set_add(idx,
                              entry->id, entry->id_size,
                              (void*)entry);
            KND_TASK_ERR("failed to register class inst %.*s err:%d",
                         entry->name_size, entry->name, err);
            
            break;
        default:
            break;
        }
    }
    return knd_OK;
}

int knd_class_inst_export(struct kndClassInst *self,
                          knd_format format,
                          struct kndTask *task)
{
    switch (format) {
        case KND_FORMAT_JSON:
            return knd_class_inst_export_JSON(self, task);
        case KND_FORMAT_GSL:
            return knd_class_inst_export_GSL(self, task);
        case KND_FORMAT_GSP:
            return knd_class_inst_export_GSP(self, task);
        default:
            return knd_RANGE;
    }
}

int knd_class_inst_commit_state(struct kndClass *self,
                                struct kndStateRef *children,
                                size_t num_children,
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
        head = atomic_load_explicit(&self->inst_states,
                                    memory_order_relaxed);
        if (head) {
            state->next = head;
            state->numid = head->numid + 1;
        }
    } while (!atomic_compare_exchange_weak(&self->inst_states, &head, state));

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

int knd_class_inst_export_commit(struct kndStateRef *state_refs,
                                 struct kndTask *task)
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

        err = out->write(out, "inst ", strlen("inst "));                        RET_ERR();

        err = knd_class_inst_export(entry->inst, KND_FORMAT_GSL, task);               RET_ERR();

        err = out->writec(out, '}');                                              RET_ERR();
    }
    return knd_OK;
}

int knd_class_inst_entry_new(struct kndMemPool *mempool,
                             struct kndClassInstEntry **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                                sizeof(struct kndClassInstEntry), &page);
        RET_ERR();
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_SMALL,
                                     sizeof(struct kndClassInstEntry), &page);
        RET_ERR();
    }
    *result = page;
    return knd_OK;
}

int knd_class_inst_new(struct kndMemPool *mempool,
                       struct kndClassInst **result)
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
