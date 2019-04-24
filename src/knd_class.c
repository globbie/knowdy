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
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
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

static void str(struct kndClass *self, size_t depth)
{
    struct kndTranslation *tr;
    struct kndClassVar *item;
    struct kndClassRef *ref;
    struct kndClass *c;
    struct kndState *state;
    //struct kndSet *set;
    //struct kndFacet *f;
    const char *name;
    size_t name_size;
    char resolved_state = '-';
    //int err;

    knd_log("\n{class %.*s (repo:%.*s)   id:%.*s  numid:%zu",
            self->entry->name_size, self->entry->name,
            self->entry->repo->name_size, self->entry->repo->name,
            self->entry->id_size, self->entry->id,
            self->entry->numid);

    state = atomic_load_explicit(&self->states,
                                 memory_order_relaxed);
    for (; state; state = state->next) {
        knd_log("\n%*s_state:%zu",
            depth * KND_OFFSET_SIZE, "",
            state->update->numid);
    }

    /* if (self->num_inst_states) {
        knd_log("\n%*snum inst states:%zu",
            self->depth * KND_OFFSET_SIZE, "",
            self->num_inst_states);
    }
    */

    for (tr = self->tr; tr; tr = tr->next) {
        knd_log("%*s~ %.*s %.*s",
                (depth + 1) * KND_OFFSET_SIZE, "",
                tr->locale_size, tr->locale,
                tr->val_size, tr->val);
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
                str_attr_vars(item->attrs, depth + 1);
            }
        }
    }

    for (ref = self->entry->ancestors; ref; ref = ref->next) {
        c = ref->class;
        knd_log("%*s ==> %.*s (repo:%.*s)", depth * KND_OFFSET_SIZE, "",
                c->entry->name_size, c->entry->name,
                c->entry->repo->name_size, c->entry->repo->name);
    }

    knd_log("%*s the end of %.*s}", depth * KND_OFFSET_SIZE, "",
            self->entry->name_size, self->entry->name);
}

int knd_get_class_inst(struct kndClass *self,
                       const char *name, size_t name_size,
                       struct kndTask *task,
                       struct kndClassInst **result)
{
    struct kndClassInstEntry *entry;
    struct kndClassInst *obj;
    struct kndDict *name_idx;
    struct kndOutput *log = task->log;
    int err, e;

    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. \"%.*s\" class (%.*s) to get instance: \"%.*s\"..",
                self->entry->name_size, self->entry->name,
                self->entry->repo->name_size, self->entry->repo->name,
                name_size, name);

    if (!self->entry) {
        knd_log("-- no frozen entry rec in \"%.*s\" :(",
                self->entry->name_size, self->entry->name);
    }

    if (!self->entry->inst_idx) {
        knd_log("-- no inst idx in \"%.*s\" :(",
                self->entry->name_size, self->entry->name);
        log->reset(log);
        e = log->write(log, self->entry->name, self->entry->name_size);
        if (e) return e;
        e = log->write(log, " class has no instances",
                             strlen(" class has no instances"));
        if (e) return e;
        task->http_code = HTTP_NOT_FOUND;
        return knd_FAIL;
    }

    name_idx = self->entry->inst_name_idx;
    entry = knd_dict_get(name_idx, name, name_size);
    if (!entry) {
        knd_log("-- no such class inst: \"%.*s\"", name_size, name);
        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return err;
        err = log->write(log, " instance not found",
                               strlen(" instance not found"));
        if (err) return err;
        task->http_code = HTTP_NOT_FOUND;
        return knd_NO_MATCH;
    }

    if (DEBUG_CLASS_LEVEL_2)
        knd_log("++ got obj entry %.*s  size: %zu",
                name_size, name, entry->block_size);

    if (!entry->inst) goto read_entry;

    if (entry->inst->states->phase == KND_REMOVED) {
        knd_log("-- \"%s\" instance was removed", name);
        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return err;
        err = log->write(log, " instance was removed",
                               strlen(" instance was removed"));
        if (err) return err;
        return knd_NO_MATCH;
    }

    obj = entry->inst;
    obj->states->phase = KND_SELECTED;
    *result = obj;
    return knd_OK;

 read_entry:

    //err = unfreeze_obj_entry(self, entry, result);
    //if (err) return err;

    return knd_OK;
}

int knd_get_class_attr_value(struct kndClass *src,
                             struct kndAttrVar *query,
                             struct kndProcCallArg *arg)
{
    struct kndAttrRef *attr_ref;
    struct kndAttrVar *child_var;
    struct kndDict *attr_name_idx = src->entry->repo->attr_name_idx;
    int err;

    attr_ref = knd_dict_get(attr_name_idx,
                            query->name, query->name_size);
    if (!attr_ref) {
        knd_log("-- no such attr: %.*s", query->name_size, query->name);
        return knd_FAIL;
    }

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log("++ got attr: %.*s",
                query->name_size, query->name);
    }

    if (!attr_ref->attr_var) return knd_FAIL;

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

static int update_ancestor_state(struct kndClass *self,
                                 struct kndClass *child,
                                 struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    //struct kndClass *c;
    struct kndStateRef *state_ref = NULL;
    struct kndStateVal *state_val = NULL;
    struct kndState *state;
    struct kndState *prev_state;
    int err;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. update the state of ancestor: %.*s..",
                self->name_size, self->name);
    }

#if 0
        for (state_ref = task->class_state_refs; state_ref; state_ref = state_ref->next) {
        c = state_ref->obj;
        if (c == self) break;
    }
#endif

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
            state_val->val_size = self->entry->num_children;
        } else {
            state_val->val_size = prev_state->val->val_size;
        }

        state->val = state_val;
        state->next = self->desc_states;
        self->desc_states = state;

        /* register in repo */
        err = knd_state_ref_new(mempool, &state_ref);                             RET_ERR();
        state_ref->obj = (void*)self->entry;
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

static int update_state(struct kndClass *self,
                        struct kndStateRef *children,
                        knd_state_phase phase,
                        struct kndState **result,
                        struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndState *head, *state;
    struct kndClass *c;
    struct kndClassRef *ref;
    int err;

    err = knd_state_new(mempool, &state);
    if (err) {
        knd_log("-- class state alloc failed");
        return err;
    }
    state->phase = phase;
    state->update = task->ctx->update;
    state->children = children;

    do {
       head = atomic_load_explicit(&self->states,
                                   memory_order_relaxed);
       state->next = head;
    } while (!atomic_compare_exchange_weak(&self->states, &head, state));
 
    /* inform your ancestors */
    for (ref = self->entry->ancestors; ref; ref = ref->next) {
        c = ref->entry->class;
        /* skip the root class */
        if (!c->entry->ancestors) continue;
        if (c->state_top) continue;
        if (self->entry->repo != ref->entry->repo) {
            err = knd_class_clone(ref->entry->class, self->entry->repo, &c, mempool);
            if (err) return err;
            ref->entry = c->entry;
        }
        err = update_ancestor_state(c, self, task);                            RET_ERR();
    }

    *result = state;
    return knd_OK;
}

#if 0
static int update_inst_state(struct kndClass *self,
                             struct kndStateRef *children,
                             struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndStateRef *ref;
    struct kndState *state;
    struct kndSet *inst_idx = self->entry->inst_idx;
    int err;

    assert(inst_idx != NULL);

    err = knd_state_new(mempool, &state);
    if (err) {
        knd_log("-- class inst state alloc failed");
        return err;
    }

    /* check removed objs */
    for (ref = children; ref; ref = ref->next) {
        switch (ref->state->phase) {
        case KND_REMOVED:
            if (inst_idx->num_valid_elems)
                inst_idx->num_valid_elems--;
            break;
        default:
            break;
        }
    }

    state->phase = KND_UPDATED;
    state->children = children;
    
    self->num_inst_states++;
    state->numid = self->num_inst_states;
    state->next = self->inst_states;
    self->inst_states = state;

    /* inform our repo */
    err = knd_state_ref_new(mempool, &ref);                                 RET_ERR();
    ref->state = state;
    ref->type = KND_STATE_CLASS;
    ref->obj = self->entry;

    // TODO    ref->next = task->class_state_refs;
    //task->class_state_refs = ref;
    
    return knd_OK;
}
#endif

int knd_class_update_state(struct kndClass *self,
                           knd_state_phase phase,
                           struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndStateRef *state_ref;
    struct kndUpdate *update = task->ctx->update;
    struct kndState *state = NULL;
    int err;

    if (DEBUG_CLASS_LEVEL_TMP) {
        knd_log(".. \"%.*s\" class (repo:%.*s) to update its state (phase:%d)",
                self->name_size, self->name,
                self->entry->repo->name_size, self->entry->repo->name,
                phase);
    }

    err = update_state(self, NULL, phase, &state, task);                      RET_ERR();

    /* newly created class? */
    switch (phase) {
    case KND_UPDATED:
        /* any attr updates */
#if 0        
        if (task->inner_class_state_refs) {
            err = update_state(self, task->inner_class_state_refs, phase, task);  RET_ERR();
            task->inner_class_state_refs = NULL;
        }

        /* instance updates */
        if (task->class_inst_state_refs) {
            if (DEBUG_CLASS_LEVEL_2) {
                knd_log("\n .. \"%.*s\" class (repo:%.*s) to register inst updates..",
                        self->name_size, self->name,
                        self->entry->repo->name_size, self->entry->repo->name);
            }
            err = update_inst_state(self,
                                    task->class_inst_state_refs, task);           RET_ERR();
            task->class_inst_state_refs = NULL;

            return knd_OK;
        }
#endif
        break;
    default:
        break;
    }
    
    /* register state */
    err = knd_state_ref_new(mempool, &state_ref);                                 RET_ERR();
    state_ref->state = state;
    state_ref->type = KND_STATE_CLASS;

    state_ref->obj = self->entry;

    state_ref->next = update->class_state_refs;
    update->class_state_refs = state_ref;

    return knd_OK;
}


extern int knd_class_facets_export(struct kndTask *task)
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

int knd_class_export(struct kndClass *self,
                     knd_format format,
                     struct kndTask *task)
{
    task->out->reset(task->out);

    switch (format) {
    case KND_FORMAT_JSON:
        return knd_class_export_JSON(self, task);
    case KND_FORMAT_GSP:
        return knd_class_export_GSP(self, task);
    default:
        assert(format == KND_FORMAT_GSL);
        return knd_class_export_GSL(self, task, false, 0);
    }
    return knd_FAIL;
}

int knd_class_export_state(struct kndClass *self,
                           knd_format format,
                           struct kndTask *task)
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

int knd_is_base(struct kndClass *self,
                struct kndClass *child)
{
    struct kndClassEntry *entry = child->entry;
    struct kndClassRef *ref;
    struct kndClass *c;
    size_t count = 0;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. check inheritance: %.*s (repo:%.*s) [resolved: %d] => "
                " %.*s (repo:%.*s) num ancestors:%zu [base resolved:%d  resolved:%d]",
                child->name_size, child->name,
                child->entry->repo->name_size, child->entry->repo->name,
                child->is_resolved,
                self->entry->name_size, self->entry->name,
                self->entry->repo->name_size, self->entry->repo->name,
                self->entry->num_ancestors,
                self->base_is_resolved, self->is_resolved);
    }

    for (ref = entry->ancestors; ref; ref = ref->next) {
         c = ref->class;

         if (DEBUG_CLASS_LEVEL_2) {
             knd_log("  => is %zu): %.*s (repo:%.*s)  base resolved:%d",
                     count,
                     c->name_size, c->name,
                     c->entry->repo->name_size, c->entry->repo->name,
                     c->base_is_resolved);
         }
         count++;

         if (c == self) {
             return knd_OK;
         }
         if (self->entry->orig) {
             if (self->entry->orig->class == c)
                 return knd_OK;
         }
    }

    if (DEBUG_CLASS_LEVEL_2)
        knd_log("-- no inheritance from  \"%.*s\" to \"%.*s\" :(",
                self->entry->name_size, self->entry->name,
                child->name_size, child->name);
    return knd_FAIL;
}

int knd_class_get_attr(struct kndClass *self,
                       const char *name, size_t name_size,
                       struct kndAttrRef **result)
{
    struct kndAttrRef *ref;
    struct kndDict    *attr_name_idx = self->entry->repo->attr_name_idx;
    struct kndSet     *attr_idx = self->attr_idx;
    struct kndAttr    *attr = NULL;
    int err;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. \"%.*s\" class (repo: %.*s) to select attr \"%.*s\"",
                self->entry->name_size, self->entry->name,
                self->entry->repo->name_size, self->entry->repo->name,
                name_size, name);
    }

    ref = knd_dict_get(attr_name_idx, name, name_size);
    if (!ref) {
        if (self->entry->repo->base) {
            attr_name_idx = self->entry->repo->base->attr_name_idx;
            ref = knd_dict_get(attr_name_idx, name, name_size);
        }
        if (!ref) {
            knd_log("-- no such attr: \"%.*s\"", name_size, name);
            return knd_NO_MATCH;
        }
    }

    /* iterating over synonymous attrs */
    for (; ref; ref = ref->next) {
        attr = ref->attr;

        if (DEBUG_CLASS_LEVEL_2) {
            knd_log("== attr %.*s is used in class: %.*s (repo:%.*s)",
                    name_size, name,
                    ref->class_entry->name_size,
                    ref->class_entry->name,
                    ref->class_entry->repo->name_size,
                    ref->class_entry->repo->name);
        }

        if (attr->parent_class == self) break;
        err = knd_is_base(attr->parent_class, self);
        if (!err) break;
    }

    if (!attr) {
        err = knd_NO_MATCH;
        goto final;
    }

    err = attr_idx->get(attr_idx, attr->id, attr->id_size, (void**)&ref);
    if (err) {
        goto final;
    }

    *result = ref;
    return knd_OK;

 final:
    if (DEBUG_CLASS_LEVEL_2) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name,
                self->entry->name_size, self->entry->name);
    }
    return err;
}

int knd_class_get_attr_var(struct kndClass *self,
                           const char *name, size_t name_size,
                           struct kndAttrVar **result)
{
    struct kndAttrRef *ref;
    struct kndAttr *attr;
    void *obj;
    int err;

    err = knd_class_get_attr(self, name, name_size, &ref);
    if (err) return err;
    attr = ref->attr;
    err = self->attr_idx->get(self->attr_idx,
                              attr->id, attr->id_size, &obj);
    if (err) return err;

    ref = obj;
    if (!ref->attr_var) {
        if (DEBUG_CLASS_LEVEL_2)
            knd_log("-- no attr var %.*s in class %.*s",
                    name_size, name, self->name_size, self->name);
        return knd_FAIL;
    }
    *result = ref->attr_var; 
    return knd_OK;
}

int knd_class_set_export(struct kndSet *self,
                         knd_format format,
                         struct kndTask *task)
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

int knd_get_class(struct kndRepo *self,
                  const char *name, size_t name_size,
                  struct kndClass **result,
                  struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndClass *c = NULL;
    struct kndOutput *log = task->ctx->log;
    struct kndDict *class_name_idx = self->class_name_idx;
    struct kndState *state;
    int err;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. %.*s repo to get class: \"%.*s\"..",
                self->name_size, self->name,
                name_size, name);
    }

    entry = knd_dict_get(class_name_idx, name, name_size);
    if (!entry) {
        if (DEBUG_CLASS_LEVEL_2)
            knd_log("-- no local class found in: %.*s (idx:%p)",
                    self->name_size, self->name, class_name_idx);

        /* check parent schema */
        if (self->base) {
            err = knd_get_class(self->base, name, name_size, result, task);
            if (err) return err;
            return knd_OK;
        }
        return knd_NO_MATCH;
    }

    if (entry->class) {
        c = entry->class;
        
        if (c->num_states) {
            state = c->states;
            if (state->phase == KND_REMOVED) {
                knd_log("-- \"%s\" class was removed", name);
                log->reset(log);
                err = log->write(log, name, name_size);
                if (err) return err;
                err = log->write(log, " class was removed",
                                 strlen(" class was removed"));
                if (err) return err;

                task->http_code = HTTP_GONE;
                return knd_NO_MATCH;
            }
        }
        c->next = NULL;
        if (DEBUG_CLASS_LEVEL_2)
            c->str(c, 1);

        *result = c;
        return knd_OK;
    }

    if (self->base) {
        err = knd_get_class(self->base, name, name_size, result, task);
        if (err) return err;
        return knd_OK;
    }

    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. unfreezing the \"%.*s\" class ..", name_size, name);

    /*err = unfreeze_class(self, entry, &c);
    if (err) {
        knd_log("-- failed to unfreeze class: %.*s",
                entry->name_size, entry->name);
        return err;
        }*/
    //*result = c;

    return knd_FAIL;
}

int knd_get_class_by_id(struct kndRepo *repo,
                        const char *id, size_t id_size,
                        struct kndClass **result,
                        struct kndTask *task)
{
    struct kndClassEntry *entry;
    struct kndClass *c = NULL;
    struct kndOutput *log = task->log;
    struct kndSet *class_idx = repo->class_idx;
    void *elem;
    struct kndState *state;
    int err;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. repo \"%.*s\" to get class by id: \"%.*s\"..",
                repo->name_size, repo->name, id_size, id);
    }

    err = class_idx->get(class_idx, id, id_size, &elem);
    if (err) {
        /* check parent schema */
        if (repo->base) {
            err = knd_get_class_by_id(repo->base, id, id_size, result, task);
            if (err) return err;
            return knd_OK;
        }
        knd_log("-- no such class: \"%.*s\":(", id_size, id);
        log->reset(log);
        err = log->write(log, id, id_size);
        if (err) return err;
        err = log->write(log, " class not found",
                               strlen(" class not found"));
        if (err) return err;
        if (task)
            task->http_code = HTTP_NOT_FOUND;
        return knd_NO_MATCH;
    }

    entry = elem;
    if (entry->class) {
        c = entry->class;
        
        if (c->num_states) {
            state = c->states;
            if (state->phase == KND_REMOVED) {
                knd_log("-- \"%s\" class was removed", id);
                log->reset(log);
                err = log->write(log, id, id_size);
                if (err) return err;
                err = log->write(log, " class was removed",
                                 strlen(" class was removed"));
                if (err) return err;

                task->http_code = HTTP_GONE;
                return knd_NO_MATCH;
            }
        }
        c->next = NULL;
        if (DEBUG_CLASS_LEVEL_2)
            c->str(c, 1);

        *result = c;
        return knd_OK;
    }

    if (repo->base) {
        err = knd_get_class_by_id(repo->base, id, id_size, result, task);
        if (err) return err;
        return knd_OK;
    }

    if (DEBUG_CLASS_LEVEL_1)
        knd_log(".. unfreezing the \"%.*s\" class ..", id_size, id);

    /*err = unfreeze_class(self, entry, &c);
    if (err) {
        knd_log("-- failed to unfreeze class: %.*s",
                entry->name_size, entry->name);
        return err;
        }*/
    //*result = c;

    return knd_FAIL;
}

int knd_unregister_class_inst(struct kndClass *self,
                              struct kndClassInstEntry *entry,
                              struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndSet *inst_idx;
    struct kndClass *c;
    struct kndState *state;
    int err;

    /* skip the root class */
    if (!self->entry->ancestors) return knd_OK;

    inst_idx = self->entry->inst_idx;
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

    if (entry->inst->base != self) return knd_OK;

    for (struct kndClassRef *ref = self->entry->ancestors; ref; ref = ref->next) {
        c = ref->entry->class;
        if (self->entry->repo != ref->entry->repo) continue;

        err = knd_unregister_class_inst(c, entry, task);                                     RET_ERR();
    }
    return knd_OK;
}

int knd_register_class_inst(struct kndClass *self,
                            struct kndClassInstEntry *entry,
                            struct kndMemPool *mempool)
{
    struct kndRepo *repo = self->entry->repo;
    struct kndSet *inst_idx;
    struct kndClass *c;
    struct kndClassEntry *prev_entry;
    struct kndDict *class_name_idx = repo->class_name_idx;
    int err;

    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. the %.*s class (repo:%.*s) to register \"%.*s\" inst",
                self->name_size, self->name,
                self->entry->repo->name_size, self->entry->repo->name,
                entry->inst->name_size, entry->inst->name);

    inst_idx = self->entry->inst_idx;
    if (!inst_idx) {
        err = knd_set_new(mempool, &inst_idx);                                    RET_ERR();
        inst_idx->type = KND_SET_CLASS_INST;
        self->entry->inst_idx = inst_idx;
    }

    err = inst_idx->add(inst_idx, entry->id, entry->id_size, (void*)entry);
    if (err) {
        knd_log("-- failed to update the class inst idx");
        return err;
    }

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. register \"%.*s\" inst with class \"%.*s\" (%.*s)"
                " num inst states:%zu",
                entry->inst->name_size, entry->inst->name,
                self->name_size, self->name,
                self->entry->repo->name_size, self->entry->repo->name,
                self->num_inst_states);
    }

    if (entry->inst->base != self) return knd_OK;

    for (struct kndClassRef *ref = self->entry->ancestors; ref; ref = ref->next) {
        c = ref->entry->class;
        /* skip the root class */
        if (!c->entry->ancestors) continue;
        if (c->state_top) continue;
       
        if (self->entry->repo != ref->entry->repo) {
            /* search local repo */
            prev_entry = knd_dict_get(class_name_idx,
                                      ref->entry->name,
                                      ref->entry->name_size);
            if (prev_entry) {
                ref->entry = prev_entry;
            } else {
                //knd_log(".. cloning %.*s..", c->name_size, c->name);
                err = knd_class_clone(ref->entry->class, self->entry->repo, &c, mempool);
                if (err) return err;
                ref->entry = c->entry;
            }
        }
        err = knd_register_class_inst(c, entry, mempool);                         RET_ERR();
    }

    return knd_OK;
}

int knd_class_clone(struct kndClass *self,
                    struct kndRepo *target_repo,
                    struct kndClass **result,
                    struct kndMemPool *mempool)
{
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndDict *class_name_idx = target_repo->class_name_idx;
    struct kndSet *class_idx = target_repo->class_idx;
    void *ref;
    int err;

    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. cloning class %.*s (%.*s) to repo %.*s..",
                self->name_size, self->name,
                self->entry->repo->name_size, self->entry->repo->name,
                target_repo->name_size, target_repo->name);

    err = knd_class_new(mempool, &c);                                             RET_ERR();
    err = knd_class_entry_new(mempool, &entry);                                   RET_ERR();
    entry->repo = target_repo;
    entry->orig = self->entry;
    entry->class = c;
    c->entry = entry;

    target_repo->num_classes++;
    entry->numid = target_repo->num_classes;
    knd_uid_create(entry->numid, entry->id, &entry->id_size);

    err = knd_class_copy(self, c, mempool);
    if (err) {
        knd_log("-- class copy failed");
        return err;
    }

    /* idx register */
    ref = knd_dict_get(class_name_idx,
                       entry->name, entry->name_size);
    if (!ref) {
        err = knd_dict_set(class_name_idx,
                           entry->name, entry->name_size,
                           (void*)entry);                                         RET_ERR();
    }

    err = class_idx->add(class_idx,
                         entry->id, entry->id_size,
                         (void*)entry);                                           RET_ERR();

    if (DEBUG_CLASS_LEVEL_2)
        c->str(c, 1);

    *result = c;
    return knd_OK;
}

extern int knd_class_copy(struct kndClass *self,
                          struct kndClass *c,
                          struct kndMemPool *mempool)
{
    struct kndRepo *repo =  c->entry->repo;
    struct kndDict *class_name_idx = repo->class_name_idx;
    struct kndClassEntry *entry, *src_entry, *prev_entry;
    struct kndClassRef   *ref,   *src_ref;
    int err;

    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. copying class %.*s..", self->name_size, self->name);

    entry = c->entry;
    src_entry = self->entry;

    entry->name = src_entry->name;
    entry->name_size = src_entry->name_size;
    c->name = entry->name;
    c->name_size = entry->name_size;

    entry->class = c;
    c->entry = entry;

    /*err = knd_dict_set(class_name_idx,
                              entry->name, entry->name_size,
                              (void*)entry);                                      RET_ERR();
    */

    /* copy the attrs */
    c->attr_idx->mempool = mempool;
    err = self->attr_idx->map(self->attr_idx,
                              knd_register_attr_ref,
                              (void*)c);                                          RET_ERR();

    /* copy the ancestors */
    for (src_ref = self->entry->ancestors; src_ref; src_ref = src_ref->next) {
        err = knd_class_ref_new(mempool, &ref);                                   RET_ERR();
        ref->class = src_ref->class;
        ref->entry = src_ref->entry;

        prev_entry = knd_dict_get(class_name_idx,
                                  src_ref->class->name,
                                  src_ref->class->name_size);
        if (prev_entry) {
            ref->entry = prev_entry;
            ref->class = prev_entry->class;
        }
        
        ref->next = entry->ancestors;
        entry->ancestors = ref;
        entry->num_ancestors++;
    }
   
    return knd_OK;
}

extern void kndClass_init(struct kndClass *self)
{
    self->str = str;
}

extern int knd_class_var_new(struct kndMemPool *mempool,
                             struct kndClassVar **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL, sizeof(struct kndClassVar), &page);
    if (err) return err;
    *result = page;
    return knd_OK;
}

extern int knd_class_ref_new(struct kndMemPool *mempool,
                             struct kndClassRef **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY, sizeof(struct kndClassRef), &page);
    if (err) return err;
    *result = page;
    return knd_OK;
}

extern int knd_class_facet_new(struct kndMemPool *mempool,
                             struct kndClassFacet **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY, sizeof(struct kndClassFacet), &page);
    if (err) return err;
    *result = page;
    return knd_OK;
}

extern int knd_class_entry_new(struct kndMemPool *mempool,
                               struct kndClassEntry **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                            sizeof(struct kndClassEntry), &page);
    if (err) return err;

    *result = page;
    return knd_OK;
}

extern void knd_class_free(struct kndMemPool *mempool,
                           struct kndClass *self)
{
    knd_mempool_free(mempool, KND_MEMPAGE_SMALL_X4, (void*)self);
}

extern int knd_class_update_new(struct kndMemPool *mempool,
                                struct kndClassUpdate **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                            sizeof(struct kndClassUpdate), &page);  RET_ERR();
    *result = page;
    return knd_OK;
}


extern int knd_inner_class_new(struct kndMemPool *mempool,
                               struct kndClass **self)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X4,
                            sizeof(struct kndClass), &page);                      RET_ERR();
    if (err) return err;
    *self = page;
    kndClass_init(*self);
    return knd_OK;
}

extern int knd_class_new(struct kndMemPool *mempool,
                         struct kndClass **self)
{
    struct kndSet *attr_idx;
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                            sizeof(struct kndClass), &page);                      RET_ERR();
    if (err) return err;
    *self = page;

    err = knd_set_new(mempool, &attr_idx);                                        RET_ERR();
    (*self)->attr_idx = attr_idx;

    kndClass_init(*self);
    return knd_OK;
}
