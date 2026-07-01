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

#define DEBUG_INST_LEVEL_1 0
#define DEBUG_INST_LEVEL_2 0
#define DEBUG_INST_LEVEL_3 0
#define DEBUG_INST_LEVEL_4 0
#define DEBUG_INST_LEVEL_TMP 1

void knd_class_inst_append_attr_stm(struct kndClassInst *ci, struct kndAttrStm *attr_stm)
{
    if (!ci->attr_stms_tail) {
        ci->attr_stms_tail  = attr_stm;
        ci->attr_stms = attr_stm;
    }
    else {
        ci->attr_stms_tail->next = attr_stm;
        ci->attr_stms_tail = attr_stm;
    }
    ci->num_attr_stms++;
}

void knd_class_inst_str(struct kndClassInst *self, size_t depth)
{
    //struct kndState *state = self->states;
    struct kndAttrStm *item;

    if (self->type == KND_OBJ_ADDR) {
        knd_log("\n%*s>>> class inst \"%.*s::%.*s\"  numid:%zu",
                depth * KND_OFFSET_SIZE, "",
                self->entry->is_a->name_size, self->entry->is_a->name,
                self->name_size, self->name, self->entry->numid);
        //if (state) {
        //    knd_log("    state:%zu  phase:%d", state->numid, state->phase);
        //}
    }

    if (self->attr_stms) {
        FOREACH (item, self->attr_stms) {
            knd_attr_stm_str(item, depth + 1);
        }
    }
}

int knd_class_inst_export(struct kndClassInst *self, knd_format format,
                          bool is_list_item, knd_state_phase phase,
                          struct kndTask *task)
{
    switch (format) {
        case KND_FORMAT_JSON:
            return knd_class_inst_export_JSON(self, is_list_item, phase, task, 0);
        case KND_FORMAT_GSL:
            return knd_class_inst_export_GSL(self, is_list_item, phase, task, 0);
        case KND_FORMAT_GSP:
            return knd_class_inst_export_GSP(self, task);
        default:
            return knd_RANGE;
    }
}

int knd_class_inst_commit_state(struct kndClass *self, struct kndStateRef *children, size_t num_children, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndCommit *commit = task->ctx->commit;
    struct kndStateRef *ref;
    struct kndState *state;
    int err;

    err = knd_state_new(&state, mempool);
    KND_TASK_ERR("class inst state alloc failed");
    state->phase = KND_SELECTED;
    state->children = children;
    state->num_children = num_children;
    /*    do {
        head = atomic_load_explicit(&self->inst_states, memory_order_relaxed);
        if (head) {
            state->next = head;
            state->numid = head->numid + 1;
        }
    } while (!atomic_compare_exchange_weak(&self->inst_states, &head, state));
    */
    /* inform our repo */
    err = knd_state_ref_new(&ref, mempool);                                 RET_ERR();
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

    FOREACH (ref, state_refs) {
        entry = ref->obj;
        if (!entry) continue;

        OUT("{", 1);
        if (ref->state->phase == KND_CREATED) {
            OUT("!", 1);
        }
        OUT("inst ", strlen("inst "));

        err = knd_class_inst_export(entry->inst, KND_FORMAT_GSL, true, ref->state->phase, task);
        RET_ERR();

        OUT("}", 1);
    }
    return knd_OK;
}

int knd_class_inst_ref_new(struct kndClassInstRef **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndClassInstRef));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndClassInstRef));
    *result = page;
    return knd_OK;
}

int knd_class_inst_entry_new(struct kndClassInstEntry **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(KND_SMALL_MEMPAGE_SIZE >= sizeof(struct kndClassInstEntry));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndClassInstEntry));
    *result = page;
    return knd_OK;
}

int knd_class_inst_new(struct kndClassInst **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(KND_SMALL_X2_MEMPAGE_SIZE >= sizeof(struct kndClassInst));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X2, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndClassInst));
    *result = page;
    return knd_OK;
}
