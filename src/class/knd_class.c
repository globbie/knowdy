#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

/* numeric conversion by strtol */
#include <errno.h>
#include <limits.h>

#include "knd_config.h"
#include "knd_mempool.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_attr_stm.h"
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_shared_set.h"
#include "knd_utils.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_CLASS_LEVEL_1 0
#define DEBUG_CLASS_LEVEL_2 0
#define DEBUG_CLASS_LEVEL_3 0
#define DEBUG_CLASS_LEVEL_4 0
#define DEBUG_CLASS_LEVEL_5 0
#define DEBUG_CLASS_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
    struct kndClass *class;
    struct kndAttrRef *attr_ref;
    const char *name;
    size_t name_size;
};

static int match_attr(void *elem, void *ctx_obj)
{
    struct LocalContext *ctx = ctx_obj;
    struct kndAttrRef *ref = elem;
    const char *name = ctx->name;
    size_t name_size = ctx->name_size;

    if (ref->attr->name_size != name_size) return knd_OK;
    if (memcmp(ref->attr->name, name, name_size)) return knd_OK;

    ctx->attr_ref = ref;
    return knd_EXISTS;
}

static int str_attr_idx_rec(void *elem, void *unused_var(ctx))
{
    struct kndAttrRef *src_ref = elem;

    if (!src_ref->attr_stm) return knd_OK;
    knd_attr_stm_str(src_ref->attr_stm, 2);

    return knd_OK;
}

void knd_class_str(struct kndClass *self, size_t depth)
{
    struct kndText *tr;
    struct kndClassBasePred *item;
    struct kndAttrStm *var;
    struct kndClassRef *ref;
    struct kndClassEntry *entry;
    const char *name;
    size_t name_size;
    int err;

    knd_log("\n{cls %.*s {id %.*s}  {numid %zu}",
            self->entry->name_size, self->entry->name,
            self->entry->id_size, self->entry->id,
            self->entry->numid);

    /*state = atomic_load_explicit(&self->states, memory_order_relaxed);
    for (; state; state = state->next) {
        knd_log("\n%*s_state:%zu",
            depth * KND_OFFSET_SIZE, "",
            state->commit->numid);
            }*/

    /* if (self->num_inst_states) {
        knd_log("\n%*snum inst states:%zu",
            self->depth * KND_OFFSET_SIZE, "",
            self->num_inst_states);
    }
    */

    FOREACH (tr, self->tr) {
        knd_log("%*s~ %.*s %.*s",
                (depth + 1) * KND_OFFSET_SIZE, "",
                tr->locale_size, tr->locale, tr->seq->val_size, tr->seq->val);
    }

    if (self->base_preds) {
        FOREACH (item, self->base_preds) {
            if (item->entry) {
                name = item->entry->name;
                name_size = item->entry->name_size;

                knd_log("%*s_base {class %.*s} {id %.*s}",
                        (depth + 1) * KND_OFFSET_SIZE, "",
                        name_size, name,
                        item->entry->id_size, item->entry->id);
            }

            if (item->attr_stms) {
                FOREACH (var, item->attr_stms) {
                    knd_attr_stm_str(var, depth + 1);
                }
            }
        }
    }

    FOREACH (ref, self->ancestors) {
        entry = ref->entry;
        knd_log("%*s{is %.*s}", depth * KND_OFFSET_SIZE, "",
                entry->name_size, entry->name);
    }

    /* no range limits, no filtering */
    err = knd_set_map(self->attr_idx, NULL, NULL, NULL,
                      str_attr_idx_rec, (void*)self);
    if (err) return;

    knd_log("%*s the end of %.*s}", depth * KND_OFFSET_SIZE, "",
            self->entry->name_size, self->entry->name);
}

int knd_get_class_inst(struct kndClass *self, const char *name, size_t name_size,
                       struct kndTask *task, struct kndClassInst **unused_var(result))
{
    //struct kndClassInstEntry *entry;
    struct kndDict *name_idx = self->inst_name_idx;
    int err;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log("{cls %.*s}} to get {inst %.*s}",
                self->name_size, self->name, name_size, name);
    }
    if (!name_idx) {
        if (!self->num_snapshot_insts) {
            err = knd_NO_MATCH;
            KND_TASK_ERR("{cls %.*s} has no instances", self->name_size, self->name);
        }
        //err = knd_class_inst_idx_fetch(self, &name_idx, task);
        //KND_TASK_ERR("failed to unmarshall inst idx of class \"%.*s\"", self->name_size, self->name);
    }

#if 0    
    entry = knd_shared_dict_get(name_idx, name, name_size);
    if (!entry) {
        err = knd_NO_MATCH;
        KND_TASK_ERR("no such class inst: \"%.*s\"", name_size, name);
    }

    err = knd_class_inst_acquire(entry, &inst, task);
    KND_TASK_ERR("failed to acquire class inst %.*s", entry->name_size, entry->name);

    if (inst->states && inst->states->phase == KND_REMOVED) {
        KND_TASK_LOG("\"%s\" class inst was removed", name);
        return knd_NO_MATCH;
    }
    if (DEBUG_CLASS_LEVEL_3)
        knd_class_inst_str(inst, 1);

    *result = inst;
#endif
    return knd_OK;
}

static int commit_state(struct kndStateRef *children, knd_state_phase phase,
                        struct kndState **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndState *state;
    int err;

    err = knd_state_new(&state, mempool);
    if (err) {
        KND_TASK_ERR("class state alloc failed");
    }
    state->phase = phase;
    state->commit = task->ctx->commit;
    state->children = children;
    state->next = NULL;

    /*do {
       head = atomic_load_explicit(&self->states, memory_order_relaxed);
       if (head) {
           state->next = head;
           state->numid = head->numid + 1;
       }
    } while (!atomic_compare_exchange_weak(&self->states, &head, state));
    */
    // inform your ancestors
    /*for (ref = self->entry->ancestors; ref; ref = ref->next) {
        c = ref->entry->class;
        if (!c->entry->ancestors) continue;
        if (c->state_top) continue;
        if (self->entry->repo != ref->entry->repo) {
            err = knd_class_clone(ref->entry->class, self->entry->repo, &c, mempool);
            if (err) return err;
            ref->entry = c->entry;
        }
        err = update_ancestor_state(c, self, task);                            RET_ERR();
        }*/

    *result = state;
    return knd_OK;
}

int knd_class_commit_state(struct kndClassEntry *self, knd_state_phase phase, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndCommit *commit = task->ctx->commit;
    struct kndState *state = NULL;
    struct kndStateRef *state_ref;
    int err;

    assert(commit != NULL);

    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. \"%.*s\" class to commit its state (phase:%d) ",
                self->name_size, self->name, phase);

    err = commit_state(NULL, phase, &state, task);
    KND_TASK_ERR("failed to alloc kndState");

    if (phase == KND_SELECTED) {
        state->children = task->ctx->class_inst_state_refs;
        state->num_children = task->ctx->num_class_inst_state_refs;
    }

    err = knd_state_ref_new(&state_ref, mempool);                                 RET_ERR();
    state_ref->state = state;
    state_ref->type = KND_STATE_CLASS;
    state_ref->obj = self;

    state_ref->next = commit->class_state_refs;
    commit->class_state_refs = state_ref;
    commit->num_class_state_refs++;
    return knd_OK;
}

int knd_empty_set_export(struct kndClass *self, knd_format format, struct kndTask *task)
{
    task->out->reset(task->out);

    switch (format) {
    case KND_FORMAT_JSON:
        return knd_empty_set_export_JSON(self, task);
    default:
        return knd_empty_set_export_GSL(self, task);
    }
    return knd_FAIL;
}

int knd_class_export(struct kndClass *self, knd_format format, struct kndTask *task)
{
    task->out->reset(task->out);
    switch (format) {
    case KND_FORMAT_JSON:
        return knd_class_export_JSON(self, task, false, 0);
    case KND_FORMAT_GSP:
        return knd_class_export_GSP(self, task);
    default:
        assert(format == KND_FORMAT_GSL);
        return knd_class_export_GSL(self, task, false, 0);
    }
    return knd_FAIL;
}

int knd_class_export_state(struct kndClass *self, knd_format format, struct kndTask *task)
{
    switch (format) {
        case KND_FORMAT_JSON:
            return knd_export_class_state_JSON(self, task);
        default:
            assert(format == KND_FORMAT_GSL);
            return knd_export_class_state_GSL(self, task);
    }
    return knd_FAIL;
}

int knd_class_is_direct_child(struct kndClass *base, struct kndClass *cls, size_t *numid)
{
    struct kndClassRef *ref;

    FOREACH (ref, base->children) {
        if (ref->entry == cls->entry) {
            *numid = ref->numid;
            return knd_OK;
        }
    }
    return knd_NO_MATCH;
}

int knd_class_is_base(struct kndClass *self, struct kndClass *child)
{
    struct kndClassRef *ref;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. check inheritance: %.*s => "
                " %.*s {num-ancestors %zu}",
                child->name_size, child->name,
                self->entry->name_size, self->entry->name,
                self->num_ancestors);
    }
    FOREACH (ref, child->ancestors) {
         if (ref->entry == self->entry)
             return knd_OK;
    }
    if (DEBUG_CLASS_LEVEL_2)
        knd_log("no inheritance from {cls %.*s} to {cls %.*s}",
                self->entry->name_size, self->entry->name,
                child->name_size, child->name);
    return knd_NO_MATCH;
}

int knd_is_equal_or_subclass(struct kndClass *c, struct kndClass *base)
{
    struct kndClassRef *ref;
    struct kndClassEntry *entry = base->entry;

    if (c == base) return knd_OK;

    FOREACH (ref, c->ancestors) {
         if (ref->entry == entry)
             return knd_OK;
    }
    return knd_NO_MATCH;
}

int knd_class_get_attr(struct kndClass *self, const char *name, size_t name_size,
                       struct kndAttrRef **result)
{
    struct kndAttrRef *ref;
    struct LocalContext ctx = {
        .name = name,
        .name_size = name_size
    };
    int err;

    /* no range limits, no filtering */
    err = knd_set_map(self->attr_idx, NULL, NULL, NULL,
                      match_attr, &ctx);
    switch (err) {
    case knd_EXISTS:
        ref = ctx.attr_ref;
        *result = ref;
        return knd_OK;
    default:
        break;
    }
    return knd_NO_MATCH;
}

int knd_class_get_attr_stm(struct kndClass *self, const char *name, size_t name_size,
                           struct kndAttrStm **result)
{
    struct kndAttrRef *ref;
    struct LocalContext ctx = {
        .name = name,
        .name_size = name_size
    };
    int err;

    err = knd_set_map(self->attr_idx, NULL, NULL, NULL,
                      match_attr, &ctx);
    switch (err) {
    case knd_EXISTS:
        ref = ctx.attr_ref;
        if (!ref->attr_stm) return knd_NO_MATCH;

        *result = ref->attr_stm;
        return knd_OK;
    default:
        break;
    }
    return knd_NO_MATCH;
}

int knd_class_set_export(struct kndSet *self, knd_format format, struct kndTask *task)
{
    task->out->reset(task->out);

    switch (format) {
    case KND_FORMAT_JSON:
        //return knd_class_set_export_JSON(self, task);
        break;
    default:
        return knd_class_set_export_GSL(self, task);
    }
    return knd_FAIL;
}

static int init_load_get_cls_entry_by_name(const char *name, size_t name_size,
                                           struct kndClassEntry **result, struct kndTask *task)
{
     struct kndDict *name_idx = task->snapshot->cache.class_name_idx;
     struct kndSharedDict *shared_name_idx = task->snapshot->idxs.class_name_idx;
     struct kndClassEntry *entry;

     /* lookup global read-only cache */
    entry = knd_dict_get(name_idx, name, name_size);
    if (entry) {
        *result = entry;
        return knd_OK;
    }

    /* lookup global write idx */
    entry = knd_shared_dict_get(shared_name_idx, name, name_size);
    if (entry) {
        *result = entry;
        return knd_OK;
    }

    /* lookup task local cache */
    name_idx = task->cache.cls_name_idx;
    entry = knd_dict_get(name_idx, name, name_size);
    if (entry) {
        *result = entry;
        return knd_OK;
    }

    /* lookup task local write idx */
    name_idx = task->idxs.cls_name_idx;
    entry = knd_dict_get(name_idx, name, name_size);
    if (entry) {
        *result = entry;
        return knd_OK;
    }

    return knd_NO_MATCH;
}

static int query_get_cls_entry_by_name(const char *name, size_t name_size,
                                       struct kndClassEntry **result, struct kndTask *task)
{
     struct kndDict *name_idx = task->snapshot->cache.class_name_idx;
     struct kndSharedDict *shared_name_idx = task->snapshot->idxs.class_name_idx;
     struct kndClassEntry *entry;

     /* lookup task local write idx */
    name_idx = task->idxs.cls_name_idx;
    entry = knd_dict_get(name_idx, name, name_size);
    if (entry) {
        *result = entry;
        return knd_OK;
    }

    /* lookup global write idx */
    entry = knd_shared_dict_get(shared_name_idx, name, name_size);
    if (entry) {
        *result = entry;
        return knd_OK;
    }

    /* lookup global read-only cache */
    entry = knd_dict_get(name_idx, name, name_size);
    if (entry) {
        *result = entry;
        return knd_OK;
    }

    /* lookup task local cache */
    name_idx = task->cache.cls_name_idx;
    entry = knd_dict_get(name_idx, name, name_size);
    if (entry) {
        *result = entry;
        return knd_OK;
    }

    return knd_NO_MATCH;
}

int knd_get_cls_entry_by_name(const char *name, size_t name_size,
                              struct kndClassEntry **result, struct kndTask *task)
{
    int err;

    switch (task->type) {
    case KND_TASK_BULK_LOAD:
        err = init_load_get_cls_entry_by_name(name, name_size, result, task);
        KND_TASK_ERR("no such {cls %.*s}", name_size, name);
        return knd_OK;
    case KND_TASK_QUERY:
        err = query_get_cls_entry_by_name(name, name_size, result, task);
        KND_TASK_ERR("no such {cls %.*s}", name_size, name);
        return knd_OK;
    default:
        break;
    }

    return knd_NO_MATCH;
}

int knd_get_cls_entry_by_id(const char *id, size_t id_size,
                            struct kndClassEntry **result, struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndSet *class_idx = task->idxs.cls_idx;
    int err;

    err = knd_set_get(class_idx, id, id_size, (void**)&entry);
    if (err == knd_OK) {
        *result = entry;
        return knd_OK;
    }

    return knd_NO_MATCH;
}

int knd_class_acquire(struct kndClassEntry *entry, struct kndClass **result,
                      struct kndTask *unused_var(task))
{
    struct kndClass *c = entry->cls;
    //struct kndStorageLeaf *leaf;
    //int err;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(">> acquire {cls %.*s {id %.*s}}",
                entry->name_size, entry->name, entry->id_size, entry->id);
    }


    if (c) {
        // check curr status, maybe deleted?
        *result = c;
        return knd_OK;
    }

    return knd_FAIL;
    // TODO check local task cache

    /*  err = knd_set_find_leaf(task->idxs.class_idx, entry->id, entry->id_size, &leaf, task);
    KND_TASK_ERR("no storage leaf found for unmarshalling {cls %.*s}", entry->id_size, entry->id);

    err = knd_set_leaf_read_elem(leaf, task->idxs.class_idx->dir,
                                        entry->id, entry->id_size,
                                        knd_class_unmarshall, entry, (void**)&c, task);
    KND_TASK_ERR("failed to read {cls %.*s}", entry->name_size, entry->name);

    err = knd_class_decode(c, task);
    KND_TASK_ERR("failed to decode {cls %.*s}", c->name_size, c->name);
    */

    *result = c;
    return knd_OK;
}

int knd_get_cls_by_name(const char *name, size_t name_size,
                        struct kndClass **result, struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndState *state;
    struct kndClass *c;
    int err;

    err = knd_get_cls_entry_by_name(name, name_size, &entry, task);
    KND_TASK_ERR("no such entry {cls %.*s}", name_size, name);

    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire {cls %.*s}", entry->name_size, entry->name);

    if (c->num_states) {
        state = c->states;
        if (state->phase == KND_REMOVED) {
            err = knd_NO_MATCH;
            KND_TASK_ERR("{cls %s} was removed", name);
        }
    }
    *result = c;
    return knd_OK;
}

int knd_get_cls_by_id(const char *id, size_t id_size,
                      struct kndClass **result, struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndClass *c;
    int err;

    err = knd_get_cls_entry_by_id(id, id_size, &entry, task);
    KND_TASK_ERR("no such entry {cls %.*s}", id_size, id);

    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire {cls %.*s}", entry->name_size, entry->name);

    *result = c;
    return knd_OK;
}

int knd_class_entry_copy(struct kndClassEntry *orig, struct kndClassEntry **result,
                         struct kndMemPool *mempool, struct kndTask *task)
{
    struct kndClassEntry *entry;
    int err;

    err = knd_class_entry_new(&entry, mempool);
    KND_TASK_ERR("failed to alloc a class entry");

    memcpy(entry->id, orig->id, orig->id_size);
    entry->id_size = orig->id_size;

    entry->name = orig->name;
    entry->name_size = orig->name_size;

    *result = entry;
    return knd_OK;
}

static int class_attrs_copy(struct kndClass *orig, struct kndClass *c,
                            struct kndMemPool *mempool, struct kndTask *task)
{
    struct kndAttr *orig_attr, *attr;
    int err;

    FOREACH (orig_attr, orig->attrs) {
        err = knd_attr_new(&attr, mempool);
        KND_TASK_ERR("failed to alloc an attr");

        attr->type = orig_attr->type;
        memcpy(attr->id, orig_attr->id, orig_attr->id_size);
        attr->id_size = orig_attr->id_size;

        attr->name = orig_attr->name;
        attr->name_size = orig_attr->name_size;

        attr->is_a_set = orig_attr->is_a_set;
        attr->is_unique = orig_attr->is_unique;

        if (!c->attr_tail) {
            c->attr_tail = attr;
            c->attrs = attr;
        } else {
            c->attr_tail->next = attr;
            c->attr_tail = attr;
        }
        c->num_attrs++;
    }

    return knd_OK;
}

int knd_class_copy(struct kndClass *orig, struct kndClass **result,
                   struct kndMemPool *mempool, struct kndTask *task)
{
    struct kndClass *c;
    //struct kndClassRef *ref, *r;
    int err;

    err = knd_class_new(&c, mempool);
    KND_TASK_ERR("failed to alloc a class");

    c->name = orig->name;
    c->name_size = orig->name_size;

    err = class_attrs_copy(orig, c, mempool, task);
    KND_TASK_ERR("failed to copy class attrs");

    /* copy the ancestors */
    /*FOREACH (ref, orig->ancestors) {
        err = knd_class_ref_new(mempool, &r);
        KND_TASK_ERR("failed to alloc a class ref");
        r->entry = src_ref->entry;
        ref->next = c->ancestors;
        c->ancestors = ref;
        c->num_ancestors++;
        }*/
    *result = c;
    return knd_OK;
}

int knd_class_entry_clone(struct kndClassEntry *self, struct kndRepo *unused_var(repo),
                          struct kndClassEntry **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndClassEntry *entry;
    struct kndDict *name_idx = task->idxs.cls_name_idx;
    int err;

    err = knd_class_entry_new(&entry, mempool);
    KND_TASK_ERR("failed to alloc a class entry");

    entry->name = self->name;
    entry->name_size = self->name_size;

    err = knd_dict_set(name_idx, entry->name,  entry->name_size, (void*)entry);
    KND_TASK_ERR("failed to register {cls %.*s}", entry->name_size, entry->name);

    *result = entry;
    return knd_OK;
}

int knd_class_base_pred_new(struct kndClassBasePred **result, struct kndClass *cls,
                            struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndClassBasePred));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndClassBasePred));
    *result = page;
    (*result)->subj = cls;
    return knd_OK;
}

int knd_class_ref_new(struct kndClassRef **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndClassRef));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndClassRef));
    *result = page;
    return knd_OK;
}

int knd_class_idx_new(struct kndClassIdx **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndClassIdx));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndClassIdx));
    *result = page;
    return knd_OK;
}

int knd_class_entry_new(struct kndClassEntry **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndClassEntry));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndClassEntry));
    *result = page;
    return knd_OK;
}

void knd_class_entry_free(struct kndClassEntry *entry, struct kndMemPool *mempool)
{
    if (entry->seq) {
        // free seq
    }

    if (entry->cls) {
        // free cls
    }

    knd_mempool_free(mempool, KND_MEMPAGE_SMALL, (void*)entry);
}

int knd_class_new(struct kndClass **result, struct kndMemPool *mempool)
{
    struct kndSet *attr_idx;
    void *page;
    int err;

    assert(mempool->small_x4_page_size >= sizeof(struct kndClass));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X4, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndClass));

    err = knd_set_new(&attr_idx, KND_SET_UNIQUE_VALUES, mempool);
    if (err) return err;

    *result = page;
    (*result)->attr_idx = attr_idx;
    return knd_OK;
}
