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


int knd_class_inst_export(struct kndClassInst *self, knd_format format, bool is_list_item, struct kndTask *task)
{
    switch (format) {
        case KND_FORMAT_JSON:
            return knd_class_inst_export_JSON(self, is_list_item, task);
        case KND_FORMAT_GSL:
            return knd_class_inst_export_GSL(self, is_list_item, KND_SELECTED, task, 0);
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
    assert(mempool->small_page_size >= sizeof(struct kndClassInstEntry));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndClassInstEntry));
    *result = page;
    return knd_OK;
}

int knd_class_inst_new(struct kndMemPool *mempool, struct kndClassInst **result)
{
    void *page;
    int err;
    assert(mempool->small_x2_page_size >= sizeof(struct kndClassInst));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X2, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndClassInst));
    *result = page;
    return knd_OK;
}
