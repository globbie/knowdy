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
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_shared_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

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

static int match_attr(void *obj, const char *unused_var(elem_id), size_t unused_var(elem_id_size),
                      size_t unused_var(count), void *elem)
{
    struct LocalContext *ctx = obj;
    struct kndAttrRef *ref = elem;
    const char *name = ctx->name;
    size_t name_size = ctx->name_size;

    if (ref->attr->name_size != name_size) return knd_OK;
    if (memcmp(ref->attr->name, name, name_size)) return knd_OK;

    ctx->attr_ref = ref;
    return knd_EXISTS;
}

static int str_attr_idx_rec(void *unused_var(obj),
                            const char *unused_var(elem_id),
                            size_t unused_var(elem_id_size),
                            size_t unused_var(count),
                            void *elem)
{
    struct kndAttrRef *src_ref = elem;

    //knd_log("   + %.*s => %p",
    //        src_ref->attr->name_size, src_ref->attr->name, src_ref->attr_var);   

    if (!src_ref->attr_var) return knd_OK;
    knd_attr_var_str(src_ref->attr_var, 2);

    return knd_OK;
}

void knd_class_str(struct kndClass *self, size_t depth)
{
    struct kndText *tr;
    struct kndClassVar *item;
    struct kndAttrVar *var;
    struct kndClassRef *ref;
    struct kndClassEntry *entry;
    const char *name;
    size_t name_size;
    char resolved_state = '-';
    int err;

    knd_log("\n{repo %.*s {class %.*s {id %.*s}  {numid %zu}",
            self->entry->repo->name_size, self->entry->repo->name,
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

    if (self->baseclass_vars) {
        FOREACH (item, self->baseclass_vars) {
            resolved_state = '-';

            if (item->entry) {
                name = item->entry->name;
                name_size = item->entry->name_size;

                knd_log("%*s_base \"%.*s\" id:%.*s [%c]",
                        (depth + 1) * KND_OFFSET_SIZE, "",
                        name_size, name,
                        item->entry->id_size, item->entry->id,
                        resolved_state);
            }

            if (item->attrs) {
                FOREACH (var, item->attrs)
                    knd_attr_var_str(var, depth + 1);
            }
        }
    }

    FOREACH (ref, self->ancestors) {
        entry = ref->entry;
        knd_log("%*s{is %.*s}", depth * KND_OFFSET_SIZE, "",
                entry->name_size, entry->name);
    }

    err = knd_set_map(self->attr_idx, str_attr_idx_rec, (void*)self);
    if (err) return;

    knd_log("%*s the end of %.*s}", depth * KND_OFFSET_SIZE, "",
            self->entry->name_size, self->entry->name);
}

int knd_get_class_inst(struct kndClass *self, const char *name, size_t name_size,
                       struct kndTask *task, struct kndClassInst **result)
{
    struct kndClassInstEntry *entry;
    struct kndClassInst *inst;
    struct kndSharedDict *name_idx = atomic_load_explicit(&self->inst_name_idx,
                                                          memory_order_acquire);
    int err;

    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. {repo %.*s {class %.*s}} to get {inst %.*s}",
                self->entry->repo->name_size, self->entry->repo->name,
                self->name_size, self->name,
                name_size, name);

    if (!name_idx) {
        if (!self->num_snapshot_insts) {
            err = knd_NO_MATCH;
            task->http_code = HTTP_NOT_FOUND;
            KND_TASK_ERR("class \"%.*s\" has no instances", self->name_size, self->name);
        }
        err = knd_class_inst_idx_fetch(self, &name_idx, task);
        KND_TASK_ERR("failed to unmarshall inst idx of class \"%.*s\"", self->name_size, self->name);
    }

    entry = knd_shared_dict_get(name_idx, name, name_size);
    if (!entry) {
        err = knd_NO_MATCH;
        task->http_code = HTTP_NOT_FOUND;
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
    return knd_OK;
}

static int commit_state(struct kndStateRef *children, knd_state_phase phase,
                        struct kndState **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndState *state;
    int err;

    err = knd_state_new(mempool, &state);
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
        knd_log(".. \"%.*s\" class (repo:%.*s) to commit its state (phase:%d) ",
                self->name_size, self->name, self->repo->name_size, self->repo->name, phase);

    err = commit_state(NULL, phase, &state, task);
    KND_TASK_ERR("failed to alloc kndState");

    if (phase == KND_SELECTED) {
        state->children = task->ctx->class_inst_state_refs;
        state->num_children = task->ctx->num_class_inst_state_refs;
    }

    err = knd_state_ref_new(mempool, &state_ref);                                 RET_ERR();
    state_ref->state = state;
    state_ref->type = KND_STATE_CLASS;
    state_ref->obj = self;

    state_ref->next = commit->class_state_refs;
    commit->class_state_refs = state_ref;
    commit->num_class_state_refs++;
    return knd_OK;
}


int knd_class_facets_export(struct kndTask *task)
{
    task->out->reset(task->out);

    switch (task->ctx->format) {
    case KND_FORMAT_JSON:
        return knd_class_facets_export_JSON(task);
    default:
        break;
        //return knd_class_set_export_GSL(self, task);
    }
    return knd_FAIL;
}

int knd_empty_set_export(struct kndClass *self,
                         knd_format format,
                         struct kndTask *task)
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

int knd_is_base(struct kndClass *self, struct kndClass *child)
{
    struct kndClassRef *ref;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. check inheritance: %.*s (repo:%.*s) [resolved: %d] => "
                " %.*s (repo:%.*s) num ancestors:%zu [base resolved:%d  resolved:%d]",
                child->name_size, child->name,
                child->entry->repo->name_size, child->entry->repo->name,
                child->is_resolved,
                self->entry->name_size, self->entry->name,
                self->entry->repo->name_size, self->entry->repo->name,
                self->num_ancestors,
                self->base_is_resolved, self->is_resolved);
    }
    FOREACH (ref, child->ancestors) {
         if (ref->entry == self->entry)
             return knd_OK;
    }
    if (DEBUG_CLASS_LEVEL_2)
        knd_log("-- no inheritance from  \"%.*s\" to \"%.*s\" :(",
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
    int err = knd_set_map(self->attr_idx, match_attr, &ctx);
    switch (err) {
    case knd_EXISTS:
        ref = ctx.attr_ref;
        *result = ref;
        return knd_OK;
    default:
        break;
    }

    // err = find_attr(self, name, name_size, result);
    // if (!err) return knd_OK;

    return knd_NO_MATCH;
}

int knd_class_get_attr_var(struct kndClass *self, const char *name, size_t name_size,
                           struct kndAttrVar **result)
{
    struct kndAttrRef *ref;
    struct LocalContext ctx = {
        .name = name,
        .name_size = name_size
    };
   
    int err = knd_set_map(self->attr_idx, match_attr, &ctx);
    switch (err) {
    case knd_EXISTS:
        ref = ctx.attr_ref;
        if (!ref->attr_var) return knd_NO_MATCH;

        *result = ref->attr_var;
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
        return knd_class_set_export_JSON(self, task);
    default:
        return knd_class_set_export_GSL(self, task);
    }
    return knd_FAIL;
}

int knd_get_class(struct kndRepo *repo, const char *name, size_t name_size,
                  struct kndClass **result, struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndSharedDict *class_name_idx = task->idxs->class_name_idx;
    struct kndState *state;
    struct kndClass *c;
    int err;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. {repo %.*s} to get {class %.*s}..",
                repo->name_size, repo->name, name_size, name);
    }

    entry = knd_shared_dict_get(class_name_idx, name, name_size);
    if (!entry) {
        if (DEBUG_CLASS_LEVEL_TMP) {
            knd_log("no local class found in {repo %.*s}",
                    repo->name_size, repo->name);
        }
        /* check base repo */
        if (repo->base) {
            err = knd_get_class(repo->base, name, name_size, result, task);
            if (err) return err;
            return knd_OK;
        }
        return knd_NO_MATCH;
    }

    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire class %.*s", entry->name_size, entry->name);

    if (c->num_states) {
        state = c->states;
        if (state->phase == KND_REMOVED) {
            err = knd_NO_MATCH;
            KND_TASK_ERR("{class %s} was removed", name);
        }
    }
    *result = c;
    return knd_OK;
}

int knd_get_class_entry(struct kndRepo *repo, const char *name, size_t name_size,
                        bool check_ancestors,
                        struct kndClassEntry **result, struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndSharedDict *class_name_idx = task->idxs->class_name_idx;
    int err;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. {repo %.*s} to get {entry %.*s}",
                repo->name_size, repo->name, name_size, name);
    }

    entry = knd_shared_dict_get(class_name_idx, name, name_size);
    if (!entry) {
        if (DEBUG_CLASS_LEVEL_TMP)
            knd_log("-- no local {class %.*s} found in {repo %.*s}",
                    name_size, name, repo->name_size, repo->name);
        /* check base repo */
        if (check_ancestors && repo->base) {
            err = knd_get_class_entry(repo->base, name, name_size, check_ancestors, result, task);
            if (err) return err;
            return knd_OK;
        }
        return knd_NO_MATCH;
    }

    if (DEBUG_CLASS_LEVEL_TMP) {
        knd_log("++ {repo %.*s {entry %p {class %.*s {cached-version %p {class-name-idx %p}}}}",
                repo->name_size, repo->name, entry,
                name_size, name, entry->cached_version,
                class_name_idx);
    }

    *result = entry;
    return knd_OK;
}

int knd_get_class_by_id(struct kndRepo *repo, const char *id, size_t id_size,
                        struct kndClass **result, struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndSharedSet *class_idx = task->idxs->class_idx;
    struct kndState *state;
    struct kndClass *c;
    int err;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. {repo %.*s} to get {class {id %.*s}}", repo->name_size, repo->name, id_size, id);
    }

    err = knd_shared_set_get(class_idx, id, id_size, (void**)&entry);
    if (err) {
        /* check parent schema */
        if (repo->base) {
            err = knd_get_class_by_id(repo->base, id, id_size, result, task);
            if (err) return err;
            return knd_OK;
        }
        err = knd_NO_MATCH;
        KND_TASK_ERR("no such {class {id %.*s}}", id_size, id);
    }

    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire {class %.*s}", entry->name_size, entry->name);
    if (c->num_states) {
        state = c->states;
        if (state->phase == KND_REMOVED) {
            err = knd_NO_MATCH;
            KND_TASK_ERR("\"%s\" class was removed", id);
        }
    }
    *result = c;
    return knd_OK;
}

int knd_class_acquire(struct kndClassEntry *entry, struct kndClass **result, struct kndTask *task)
{
    struct kndClass *c = atomic_load_explicit(&entry->curr_version, memory_order_relaxed);
    struct kndStorageLeaf *leaf;
    int err;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(">> acquire {class %.*s}", entry->name_size, entry->name);
    }

    atomic_fetch_add_explicit(&entry->num_requests, 1, memory_order_relaxed);

    if (c) {
        *result = c;
        return knd_OK;
    }

    if (entry->cached_version) {
        *result = entry->cached_version;
        return knd_OK;
    }

    //leaf = task->idxs->class_idx_leaf;
    //assert (leaf != NULL);

    task->payload = (void*)entry;
    err = knd_shared_set_unmarshall_elem(task->idxs->class_idx, entry->id, entry->id_size,
                                         leaf->filepath, leaf->filepath_size,
                                         knd_class_unmarshall, (void**)&c, task);
    KND_TASK_ERR("failed to unmarshall {class %.*s}", entry->name_size, entry->name);

    c->entry = entry;
    c->name = entry->name;
    c->name_size = entry->name_size;

    /* TODO: update idx */
    //err = knd_set_add(task->cache_class_idx, entry->id, entry->id_size, (void*)entry);
    //KND_TASK_ERR("failed to update local task cache with class entry %.*s",
    //             entry->name_size, entry->name);

    *result = c;
    return knd_OK;
}

int knd_get_class_entry_by_id(struct kndRepo *repo, const char *id, size_t id_size,
                              struct kndClassEntry **result, struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndSharedSet *class_idx = task->idxs->class_idx;
    int err;

    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. repo \"%.*s\" to get class entry by id \"%.*s\"",
                repo->name_size, repo->name, id_size, id);
    
    err = knd_shared_set_get(class_idx, id, id_size, (void**)&entry);
    if (err) {
        /* check parent schema */
        if (repo->base) {
            err = knd_get_class_entry_by_id(repo->base, id, id_size, result, task);
            if (err) return err;
            return knd_OK;
        }
        err = knd_NO_MATCH;
        KND_TASK_ERR("no such class entry: \"%.*s\"", id_size, id);
    }
    *result = entry;
    return knd_OK;
}

int knd_class_entry_copy(struct kndClassEntry *orig, struct kndClassEntry **result,
                         struct kndMemPool *mempool, struct kndTask *task)
{
    struct kndClassEntry *entry;
    int err;

    err = knd_class_entry_new(&entry, mempool);
    KND_TASK_ERR("failed to alloc a class entry");
    entry->repo = orig->repo;

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
        attr->is_implied = orig_attr->is_implied;
        attr->is_indexed = orig_attr->is_indexed;
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

int knd_class_entry_clone(struct kndClassEntry *self, struct kndRepo *repo,
                          struct kndClassEntry **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndClassEntry *entry;
    struct kndSharedDict *name_idx = task->idxs->class_name_idx;
    int err;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. cloning {class entry %.*s} {repo %.*s} to {repo %.*s}",
                self->name_size, self->name, self->repo->name_size, self->repo->name,
                repo->name_size, repo->name);
    }
    err = knd_class_entry_new(&entry, mempool);
    KND_TASK_ERR("failed to alloc a class entry");
    entry->repo = repo;

    entry->name = self->name;
    entry->name_size = self->name_size;

    err = knd_shared_dict_set(name_idx, entry->name,  entry->name_size,
                              (void*)entry, task->ctx->commit, false);
    KND_TASK_ERR("failed to register {class %.*s}", entry->name_size, entry->name);

    *result = entry;
    return knd_OK;
}

int knd_class_var_new(struct kndMemPool *mempool, struct kndClassVar **result)
{
    void *page;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndClassVar));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndClassVar));
    *result = page;
    return knd_OK;
}

int knd_class_ref_new(struct kndMemPool *mempool, struct kndClassRef **result)
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

int knd_class_facet_new(struct kndMemPool *mempool, struct kndClassFacet **result)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndClassFacet));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndClassFacet));
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

int knd_class_new(struct kndClass **self, struct kndMemPool *mempool)
{
    struct kndSet *attr_idx;
    void *page;
    int err;
    assert(mempool->small_x4_page_size >= sizeof(struct kndClass));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X4, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndClass));

    err = knd_set_new(&attr_idx, mempool);
    if (err) return err;

    *self = page;
    (*self)->attr_idx = attr_idx;
    return knd_OK;
}
