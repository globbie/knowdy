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
#include "knd_shard.h"
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

    if (DEBUG_CLASS_LEVEL_2)
        knd_log("== attr \"%.*s\" (var: %p)", ref->attr->name_size, ref->attr->name, ref->attr_var);

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

    knd_log("   + %.*s => %p", src_ref->attr->name_size, src_ref->attr->name, src_ref->attr_var);   

    if (!src_ref->attr_var) return knd_OK;
    knd_attr_var_str(src_ref->attr_var, 2);

    return knd_OK;
}

void knd_class_entry_str(struct kndClassEntry *self, size_t depth)
{
    struct kndClassRef *ref;

    knd_log("\n%*s** class entry \"%.*s\" (repo:%.*s)   id:%.*s",
            depth * KND_OFFSET_SIZE, "", self->name_size, self->name,
            self->repo->name_size, self->repo->name, self->id_size, self->id);

    FOREACH (ref, self->class->text_idxs) {
        assert(ref->entry != NULL);
        assert(ref->entry->repo != NULL);
        knd_log(">> text idx: \"%.*s\" (repo:%.*s) num locs:%zu", ref->entry->name_size, ref->entry->name,
                ref->entry->repo->name_size, ref->entry->repo->name, ref->idx->num_locs);
    }
}

static void str(struct kndClass *self, size_t depth)
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

    knd_log("\n{class %.*s (repo:%.*s)   id:%.*s  numid:%zu",
            self->entry->name_size, self->entry->name,
            self->entry->repo->name_size, self->entry->repo->name,
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

    for (tr = self->tr; tr; tr = tr->next) {
        knd_log("%*s~ %.*s %.*s",
                (depth + 1) * KND_OFFSET_SIZE, "",
                tr->locale_size, tr->locale, tr->seq->val_size, tr->seq->val);
    }

    if (self->baseclass_vars) {
        for (item = self->baseclass_vars; item; item = item->next) {
            resolved_state = '-';

            if (item->entry) {
                name = item->entry->name;
                name_size = item->entry->name_size;

                knd_log("%*s_base \"%.*s\" id:%.*s numid:%zu [%c]",
                        (depth + 1) * KND_OFFSET_SIZE, "",
                        name_size, name,
                        item->entry->id_size, item->entry->id, item->numid,
                        resolved_state);
            }

            if (item->attrs) {
                for (var = item->attrs; var; var = var->next)
                    knd_attr_var_str(var, depth + 1);
            }
        }
    }

    for (ref = self->ancestors; ref; ref = ref->next) {
        entry = ref->entry;
        knd_log("%*s = %.*s (repo:%.*s)", depth * KND_OFFSET_SIZE, "",
                entry->name_size, entry->name, entry->repo->name_size, entry->repo->name);
    }

    err = knd_set_map(self->attr_idx, str_attr_idx_rec, (void*)self);
    if (err) return;

    knd_log("%*s the end of %.*s}", depth * KND_OFFSET_SIZE, "", self->entry->name_size, self->entry->name);
}

int knd_get_class_inst(struct kndClass *self, const char *name, size_t name_size,
                       struct kndTask *task, struct kndClassInst **result)
{
    struct kndClassInstEntry *entry;
    struct kndClassInst *inst;
    struct kndSharedDict *name_idx = atomic_load_explicit(&self->inst_name_idx, memory_order_acquire);
    int err;

    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. class \"%.*s\" (repo:%.*s) to get inst \"%.*s\"", self->name_size, self->name,
                self->entry->repo->name_size, self->entry->repo->name, name_size, name);

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

int knd_get_class_attr_value(struct kndClass *src, struct kndAttrVar *query, struct kndProcCallArg *arg)
{
    struct kndAttrRef *attr_ref;
    struct kndAttrVar *child_var;
    struct kndSharedDict *attr_name_idx = src->entry->repo->attr_name_idx;
    int err;

    attr_ref = knd_shared_dict_get(attr_name_idx, query->name, query->name_size);
    if (!attr_ref) {
        if (DEBUG_CLASS_LEVEL_2)
            knd_log("-- no such attr: %.*s", query->name_size, query->name);
        return knd_NO_MATCH;
    }

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log("++ got attr: %.*s", query->name_size, query->name);
    }

    if (!attr_ref->attr_var) return knd_NO_MATCH;
    /* no more query specs */
    if (!query->num_children) return knd_OK;

    /* check nested attrs */

    // TODO: list item selection
    for (child_var = attr_ref->attr_var->list; child_var; child_var = child_var->next) {
        err = knd_get_arg_value(child_var, query->children, arg);
        if (err) return err;
    }
    return knd_OK;
}

#if 0
static int update_ancestor_state(struct kndClass *self,
                                 struct kndClass *child,
                                 struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndStateRef *state_ref = NULL;
    struct kndStateVal *state_val = NULL;
    struct kndState *state;
    struct kndState *prev_state;
    int err;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. update the state of ancestor: %.*s..",
                self->name_size, self->name);
    }

    //for (state_ref = task->class_state_refs; state_ref; state_ref = state_ref->next) {
    //  c = state_ref->obj;
    //  if (c == self) break;
    //}

    if (!state_ref) {
        err = knd_state_new(mempool, &state);
        if (err) {
            knd_log("-- class ancestor state alloc failed");
            return err;
        }
        state->phase = KND_UPDATED;
        self->num_desc_states++;
        state->numid = self->num_desc_states;

        err = knd_state_val_new(mempool, &state_val);                             RET_ERR();
        
        /* learn curr num children */
        prev_state = self->desc_states;
        if (!prev_state) {
            state_val->val_size = self->num_children;
        } else {
            state_val->val_size = prev_state->val->val_size;
        }

        state->val = state_val;
        state->next = self->desc_states;
        self->desc_states = state;

        /* register in repo */
        err = knd_state_ref_new(mempool, &state_ref);                             RET_ERR();
        state_ref->obj = (void*)self;
        state_ref->state = state;
        state_ref->type = KND_STATE_CLASS;

        // TODO state_ref->next = task->class_state_refs;
        //task->class_state_refs = state_ref;
    }
    state = state_ref->state;

    switch (child->states->phase) {
    case KND_REMOVED:
        state->val->val_size--;
        break;
    case KND_CREATED:
        state->val->val_size++;
        break;
    default:
        break;
    }

    /* new ref to a child */
    err = knd_state_ref_new(mempool, &state_ref);                                   RET_ERR();
    state_ref->obj = (void*)child->entry;
    state_ref->state = child->states;
    state_ref->type = KND_STATE_CLASS;

    state_ref->next = state->children;
    state->children = state_ref;
    
    return knd_OK;
}
#endif

static int commit_state(struct kndStateRef *children, knd_state_phase phase, struct kndState **result, struct kndTask *task)
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
        return knd_class_export_GSL(self->entry, task, false, 0);
    }
    return knd_FAIL;
}

int knd_class_export_state(struct kndClassEntry *self, knd_format format, struct kndTask *task)
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
    for (ref = child->ancestors; ref; ref = ref->next) {
         if (ref->entry == self->entry)
             return knd_OK;
         
         if (self->entry->base)
             if (self->entry->base->class->entry == ref->entry)
                 return knd_OK;
    }
    if (DEBUG_CLASS_LEVEL_2)
        knd_log("-- no inheritance from  \"%.*s\" to \"%.*s\" :(",
                self->entry->name_size, self->entry->name,
                child->name_size, child->name);
    return knd_NO_MATCH;
}

static int find_attr(struct kndClass *self, const char *name, size_t name_size, struct kndAttrRef **result)
{
    struct kndAttrRef *ref;

    assert(self->entry->repo != NULL);
    struct kndSharedDict *attr_name_idx = self->entry->repo->attr_name_idx;

    struct kndSet     *attr_idx = self->attr_idx;
    struct kndAttr    *attr = NULL;
    int err;

    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. \"%.*s\" class (repo: %.*s) to select attr \"%.*s\"",
                self->entry->name_size, self->entry->name,
                self->entry->repo->name_size, self->entry->repo->name,
                name_size, name);

    ref = knd_shared_dict_get(attr_name_idx, name, name_size);
    if (!ref) {
        if (self->entry->repo->base) {
            attr_name_idx = self->entry->repo->base->attr_name_idx;
            ref = knd_shared_dict_get(attr_name_idx, name, name_size);
        }
        if (!ref) {
            err = knd_NO_MATCH;
            goto final;
        }
    }
    /* iterating over synonymous attrs */
    for (; ref; ref = ref->next) {
        attr = ref->attr;
        if (DEBUG_CLASS_LEVEL_3)
            knd_log("== attr %.*s is used in class: %.*s (repo:%.*s)",
                    name_size, name, ref->class_entry->name_size, ref->class_entry->name,
                    ref->class_entry->repo->name_size, ref->class_entry->repo->name);

        if (attr->parent_class == self) break;
        err = knd_is_base(attr->parent_class, self);
        if (!err) break;
    }
    if (!attr) {
        err = knd_NO_MATCH;
        goto final;
    }
    err = attr_idx->get(attr_idx, attr->id, attr->id_size, (void**)&ref);
    if (err) goto final;

    *result = ref;
    return knd_OK;

 final:
    if (DEBUG_CLASS_LEVEL_2)
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name, self->entry->name_size, self->entry->name);
    return err;
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

int knd_class_get_attr_var(struct kndClass *self, const char *name, size_t name_size, struct kndAttrVar **result)
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

int knd_get_class(struct kndRepo *self, const char *name, size_t name_size,
                  struct kndClass **result, struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndSharedDict *class_name_idx = self->class_name_idx;
    struct kndState *state;
    struct kndClass *c;
    int err;

    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. \"%.*s\" repo to get class: \"%.*s\"..",
                self->name_size, self->name, name_size, name);

    entry = knd_shared_dict_get(class_name_idx, name, name_size);
    if (!entry) {
        if (DEBUG_CLASS_LEVEL_2)
            knd_log("-- no local class found in: %.*s",
                    self->name_size, self->name);
        /* check base repo */
        if (self->base) {
            err = knd_get_class(self->base, name, name_size, result, task);
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
            KND_TASK_ERR("\"%s\" class was removed", name);
        }
    }
    *result = c;
    return knd_OK;
}

int knd_get_class_entry(struct kndRepo *repo, const char *name, size_t name_size, bool check_ancestors,
                        struct kndClassEntry **result, struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndSharedDict *class_name_idx = repo->class_name_idx;
    int err;

    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. \"%.*s\" repo to get class entry: \"%.*s\"", repo->name_size, repo->name, name_size, name);

    entry = knd_shared_dict_get(class_name_idx, name, name_size);
    if (!entry) {
        if (DEBUG_CLASS_LEVEL_2)
            knd_log("-- no local class \"%.*s\" found in repo %.*s",
                    name_size, name, repo->name_size, repo->name);
        /* check base repo */
        if (check_ancestors && repo->base) {
            err = knd_get_class_entry(repo->base, name, name_size, check_ancestors, result, task);
            if (err) return err;
            return knd_OK;
        }
        return knd_NO_MATCH;
    }

    *result = entry;
    return knd_OK;
}

int knd_get_class_by_id(struct kndRepo *repo, const char *id, size_t id_size, struct kndClass **result,
                        struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndSharedSet *class_idx = repo->class_idx;
    struct kndState *state;
    struct kndClass *c;
    int err;

    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. repo \"%.*s\" to get class by id \"%.*s\"", repo->name_size, repo->name, id_size, id);
    
    err = knd_shared_set_get(class_idx, id, id_size, (void**)&entry);
    if (err) {
        /* check parent schema */
        if (repo->base) {
            err = knd_get_class_by_id(repo->base, id, id_size, result, task);
            if (err) return err;
            return knd_OK;
        }
        err = knd_NO_MATCH;
        KND_TASK_ERR("no such class: \"%.*s\"", id_size, id);
    }

    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire class %.*s", entry->name_size, entry->name);
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

int knd_get_class_entry_by_id(struct kndRepo *repo, const char *id, size_t id_size,
                              struct kndClassEntry **result, struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndSharedSet *class_idx = repo->class_idx;
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

int knd_unregister_class_inst(struct kndClass *self, struct kndClassInstEntry *entry, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndSharedSet *inst_idx = atomic_load_explicit(&self->inst_idx, memory_order_relaxed);
    struct kndClass *c;
    struct kndState *state;
    int err;

    /* skip the root class */
    if (!self->ancestors) return knd_OK;
    if (!inst_idx) return knd_OK;

    // remove
    /* increment state */
    err = knd_state_new(mempool, &state);
    if (err) {
        knd_log("-- state alloc failed :(");
        return err;
    }
    state->val = (void*)entry;
    state->next = self->inst_states;
    self->inst_states = state;
    self->num_inst_states++;
    state->numid = self->num_inst_states;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. unregister \"%.*s\" inst with class \"%.*s\" (%.*s)  num inst states:%zu",
                entry->inst->name_size, entry->inst->name,
                self->name_size, self->name,
                self->entry->repo->name_size, self->entry->repo->name,
                self->num_inst_states);
    }

    if (entry->blueprint != self->entry) return knd_OK;

    for (struct kndClassRef *ref = self->ancestors; ref; ref = ref->next) {
        c = ref->entry->class;
        if (self->entry->repo != ref->entry->repo) continue;

        err = knd_unregister_class_inst(c, entry, task);                                     RET_ERR();
    }
    return knd_OK;
}

int knd_class_copy(struct kndClass *self, struct kndClass *c, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndRepo *repo =  task->user_ctx->repo;
    struct kndClassRef *ref, *src_ref;
    int err;

    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. copying class \"%.*s\" to repo \"%.*s\"",
                self->name_size, self->name, repo->name_size, repo->name);

    c->name = self->name;
    c->name_size = self->name_size;

    // TODO
    c->attr_idx = self->attr_idx;

    /* copy the ancestors */
    FOREACH (src_ref, self->ancestors) {
        err = knd_class_ref_new(mempool, &ref);
        KND_TASK_ERR("failed to alloc a class ref");
        ref->class = src_ref->class;
        ref->entry = src_ref->entry;

        /* check local repo?
           prev_entry = knd_shared_dict_get(class_name_idx,
                                         src_ref->class->name,
                                         src_ref->class->name_size);
        if (prev_entry) {
            ref->entry = prev_entry;
            ref->class = prev_entry->class;
            }*/

        ref->next = c->ancestors;
        c->ancestors = ref;
        c->num_ancestors++;
    }
    return knd_OK;
}

int knd_class_entry_clone(struct kndClassEntry *self, struct kndRepo *repo,
                          struct kndClassEntry **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndClassEntry *entry;
    struct kndSharedDict *name_idx = repo->class_name_idx;
    struct kndSharedDictItem *item = NULL;
    struct kndClass *c;
    //struct kndClassRef *ref, *tail_ref, *r;
    // struct kndSet *class_idx = repo->class_idx;
    int err;

    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. cloning class entry %.*s (%.*s) to repo \"%.*s\"",
                self->name_size, self->name, self->repo->name_size, self->repo->name, repo->name_size, repo->name);

    err = knd_class_entry_new(mempool, &entry);
    KND_TASK_ERR("failed to alloc a class entry");
    entry->repo = repo;
    entry->base = self;

    if (self->class) {
        err = knd_class_new(mempool, &c);
        KND_TASK_ERR("failed to alloc a class");

        err = knd_class_copy(self->class, c, task);
        KND_TASK_ERR("failed to copy a class");

        c->entry = entry;
        entry->class = c;
    }
    entry->name = self->name;
    entry->name_size = self->name_size;

    err = knd_shared_dict_set(name_idx, entry->name,  entry->name_size,
                              (void*)entry, mempool, task->ctx->commit, &item, false);
    KND_TASK_ERR("failed to register class \"%.*s\"", entry->name_size, entry->name);
    entry->dict_item = item;

    *result = entry;
    return knd_OK;
}

static void kndClass_init(struct kndClass *self)
{
    self->str = str;
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

int knd_class_idx_new(struct kndMemPool *mempool, struct kndClassIdx **result)
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

int knd_class_entry_new(struct kndMemPool *mempool, struct kndClassEntry **result)
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

int knd_class_commit_new(struct kndMemPool *mempool, struct kndClassCommit **result)
{
    void *page;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndClassCommit));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndClassCommit));
    *result = page;
    return knd_OK;
}

int knd_inner_class_new(struct kndMemPool *mempool, struct kndClass **self)
{
    void *page;
    int err;
    assert(mempool->small_x4_page_size >= sizeof(struct kndClass));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X4, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndClass));
    *self = page;
    kndClass_init(*self);
    return knd_OK;
}

int knd_class_new(struct kndMemPool *mempool, struct kndClass **self)
{
    struct kndSet *attr_idx;
    void *page;
    int err;
    assert(mempool->small_x4_page_size >= sizeof(struct kndClass));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X4, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndClass));

    err = knd_set_new(mempool, &attr_idx);
    if (err) return err;

    *self = page;
    (*self)->attr_idx = attr_idx;
    kndClass_init(*self);
    return knd_OK;
}

void knd_class_free(struct kndMemPool *mempool, struct kndClass *self)
{
    knd_mempool_free(mempool, KND_MEMPAGE_SMALL_X4, (void*)self);
}
