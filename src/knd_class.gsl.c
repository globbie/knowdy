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
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_user.h"
#include "knd_repo.h"
#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_shared_set.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#define DEBUG_GSL_LEVEL_1 0
#define DEBUG_GSL_LEVEL_2 0
#define DEBUG_GSL_LEVEL_3 0
#define DEBUG_GSL_LEVEL_4 0
#define DEBUG_GSL_LEVEL_5 0
#define DEBUG_GSL_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
    struct kndClass *class;
    struct kndClassVar *class_var;
    struct kndText *text;
};

int knd_class_export_updates_GSL(struct kndClass *self, struct kndUpdate *unused_var(update),
                                 struct kndOutput *out)
{
    int err;

    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->write(out, self->name, self->name_size);                           RET_ERR();
    err = out->write(out, "{id ", strlen("{id "));                                RET_ERR();
    err = out->write(out, self->entry->id, self->entry->id_size);                 RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();
    
    /*    if (self->states) {
        state = self->states;
        if (state->update == update) {
            err = knd_class_export_GSL(c, out);                                   RET_ERR();
            }
            }*/
    
    err = out->writec(out, '}');                                                  RET_ERR();
    return knd_OK;
}

int knd_export_class_state_GSL(struct kndClassEntry *self, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndState *state;
    time_t timestamp;
    int err;

    err = out->write(out, "{state ", strlen("{state "));                          RET_ERR();

    state = atomic_load_explicit(&self->class->states, memory_order_relaxed);
    if (state) {
        err = out->writef(out, "%zu", state->commit->numid);                      RET_ERR();
        timestamp = state->commit->timestamp;
    } else {
        err = out->writec(out, '0');                                              RET_ERR();
        // TODO
        timestamp = self->repo->snapshots->timestamp;
    }
    
    err = out->write(out, "{time ", strlen("{time "));                            RET_ERR();

    err = out->writef(out, "%zu", timestamp);                                     RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();
    return knd_OK;
}

static int export_class_inst_state_GSL(struct kndClass *self, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    size_t latest_state_id = 0;
    int err;

    if (self->inst_states)
        latest_state_id = self->inst_states->numid;

    err = out->write(out, "\"_state\":", strlen("\"_state\":"));                                      RET_ERR();
    err = out->writef(out, "%zu", latest_state_id);                               RET_ERR();

    if (self->inst_idx) {
        err = out->write(out, ",\"_tot\":", strlen(",\"_tot\":"));                  RET_ERR();
        err = out->writef(out, "%zu", self->inst_idx->num_elems);      RET_ERR();
    } else {
        err = out->write(out, ",\"_tot\":0", strlen(",\"_tot\":"));                  RET_ERR();
    }
    return knd_OK;
}

static int export_conc_elem_GSL(void *obj,
                                 const char *elem_id,
                                 size_t elem_id_size,
                                 size_t count,
                                 void *elem)
{
    struct kndTask *task = obj;
    if (count < task->start_from) return knd_OK;
    if (task->batch_size >= task->batch_max) return knd_RANGE;
    struct kndOutput *out = task->out;
    struct kndClassEntry *entry = elem;
    struct kndState *state;
    size_t curr_depth = 0;
    int err;

    if (DEBUG_GSL_LEVEL_2)
        knd_log(".. GSL export class set elem: %.*s",
                elem_id_size, elem_id);

    if (!task->show_removed_objs) {
        // TODO
        state = entry->class->states;
        if (state && state->phase == KND_REMOVED) return knd_OK;
    }

    curr_depth = task->depth;
    task->depth = 0;
    if (task->ctx->format_indent) {
        err = out->writec(out, '\n');                                             RET_ERR();
        err = knd_print_offset(out, task->ctx->format_indent);                         RET_ERR();
    }

    err = knd_class_export_GSL(entry, task, true, 1);                                 RET_ERR();

    task->depth = curr_depth;
    task->ctx->batch_size++;
    return knd_OK;
}

#if 0
static int export_class_ref_GSL(void *obj,
                                 const char *unused_var(elem_id),
                                 size_t unused_var(elem_id_size),
                                size_t unused_var(count),
                                 void *elem)
{
    struct kndTask *task = obj;
    struct kndOutput *out = task->out;
    struct kndClassEntry *entry = elem;
    int err;

    task->depth = 0;
    if (task->ctx->format_indent) {
        err = out->writec(out, '\n');                                             RET_ERR();
        err = knd_print_offset(out, task->ctx->format_indent);                    RET_ERR();
    }

    err = knd_class_export_GSL(entry, task, true, 1);                                 RET_ERR();

    return knd_OK;
}
#endif

static int export_concise_GSL(struct kndClass *self,
                              struct kndTask *task,
                              size_t depth)
{
    struct kndClassVar *item;
    int err;

    if (DEBUG_GSL_LEVEL_2)
        knd_log(".. export concise GSL for %.*s..",
                self->entry->name_size, self->entry->name);

    for (item = self->baseclass_vars; item; item = item->next) {
        if (!item->attrs) continue;
        err = knd_attr_vars_export_GSL(item->attrs,
                                       task, true, depth);                        RET_ERR();
    }

    /*if (DEBUG_GSL_LEVEL_2)
        knd_log(".. export inherited attrs of %.*s..",
                self->entry->name_size, self->entry->name);
    err = self->attr_idx->map(self->attr_idx,
                              knd_export_inherited_attr, (void*)task); 
    if (err && err != knd_RANGE) return err;
    */
    return knd_OK;
}

extern int knd_empty_set_export_GSL(struct kndClass *self,
                                    struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;
    out->reset(out);

    err = out->write(out, "{set", strlen("{set"));                                RET_ERR();
    err = out->write(out, "{_is ", strlen("{_is "));                              RET_ERR();
    err = out->write(out, self->name,  self->name_size);                          RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();
    err = out->write(out, "{total 0}}", strlen("{total 0}}"));

    return knd_OK;
}

int knd_class_set_export_GSL(struct kndSet *set,
                             struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;
    out->reset(out);
    err = out->write(out, "{set",
                     strlen("{set"));                                            RET_ERR();

    // TODO: present child clauses
    if (set->base) {
        err = out->write(out, "{_is ",
                         strlen("{_is "));                                        RET_ERR();
        err = out->write(out, set->base->name,  set->base->name_size);            RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
    }

    if (task->show_removed_objs) {
        err = out->writef(out, "{total %lu",
                          (unsigned long)set->num_elems);                         RET_ERR();
    } else {
        err = out->writef(out, "{total %lu",
                          (unsigned long)set->num_valid_elems);                   RET_ERR();
    }

    if (task->ctx->format_indent) {
        err = out->writec(out, '\n');                                             RET_ERR();
        err = knd_print_offset(out, task->ctx->format_indent);                    RET_ERR();
    }

    if (!task->ctx->batch_max) {
        task->ctx->batch_max = KND_RESULT_BATCH_SIZE;
    }

    err = out->write(out, "[class",
                     strlen("[class"));                                            RET_ERR();

    err = set->map(set, export_conc_elem_GSL, (void*)task);
    if (err && err != knd_RANGE) return err;
    
    err = out->writec(out, ']');                                                  RET_ERR();

    if (task->ctx->format_indent) {
        err = out->writec(out, '\n');                                             RET_ERR();
        err = knd_print_offset(out, task->ctx->format_indent);                    RET_ERR();
    }

    err = out->writef(out, "{batch{max %lu}",
                      (unsigned long)task->ctx->batch_max);                       RET_ERR();
    err = out->writef(out, "{size %lu}",
                       (unsigned long)task->ctx->batch_size);                     RET_ERR();
    err = out->writef(out,
                     "{from %lu}}", (unsigned long)task->ctx->batch_from);        RET_ERR();

    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

static int present_subclass(struct kndClassRef *ref,
                            struct kndTask *task,
                            size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndClassEntry *entry = ref->entry;
    struct kndClass *c;
    int err;

    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->write(out, entry->name, entry->name_size);                         RET_ERR();
    //err = out->writec(out, ' ');                                                  RET_ERR();

    if (task->ctx->format_indent) {
        err = out->writec(out, '\n');                                             RET_ERR();
        err = knd_print_offset(out, (depth + 1) * task->ctx->format_indent);           RET_ERR();
    }

    /*if (ref->entry->num_terminals) {
        err = out->write(out, ",\"_num_terminals\":",
                         strlen(",\"_num_terminals\":"));                     RET_ERR();
        err = out->writef(out, "%zu", entry->num_terminals);                  RET_ERR();
        }*/

    /* localized glosses */
    c = entry->class;
    if (!c) {
        //err = unfreeze_class(self, entry, &c);                                  RET_ERR();
    }

    if (c->tr) {
        err = knd_text_gloss_export_GSL(c->tr, task, depth);
        RET_ERR();
    }
    err = out->write(out, "{_id ", strlen("{_id "));                              RET_ERR();
    err = out->writef(out, "%zu", entry->numid);                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    err = export_concise_GSL(c, task, depth);                                     RET_ERR();

    err = out->writec(out, '}');                                                  RET_ERR();
    return knd_OK;
}

static int present_subclasses(struct kndClass *self, size_t num_children, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndClassRef *ref;
    struct kndClassEntry *entry = self->entry;
    struct kndClassEntry *orig_entry = entry->base;
    struct kndClass *c;
    struct kndState *state;
    int err;

    err = out->write(out, "{_subclasses {total ",
                     strlen("{_subclasses {total "));                             RET_ERR();
    err = out->writef(out, "%zu", num_children);                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    if (self->num_terminals) {
        err = out->write(out, " {num_terminals ",
                         strlen(" {num_terminals "));                             RET_ERR();
        err = out->writef(out, "%zu", self->num_terminals);                      RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
    }

    if (task->ctx->format_indent) {
        err = out->writec(out, '\n');                                             RET_ERR();
        err = knd_print_offset(out, (depth + 1) * task->ctx->format_indent);           RET_ERR();
    }
    err = out->write(out, "[batch", strlen("[batch"));                            RET_ERR();

    // TODO
    for (ref = self->children; ref; ref = ref->next) {
        c = ref->class;
        // TODO: defreeze
        if (!c) continue;
        
        state = c->states;
        if (state && state->phase == KND_REMOVED) continue;
        if (task->ctx->format_indent) {
            err = out->writec(out, '\n');                                         RET_ERR();
            err = knd_print_offset(out, (depth + 2) * task->ctx->format_indent);       RET_ERR();
        }
        err = present_subclass(ref, task, depth + 2);                             RET_ERR();
    }

    if (orig_entry) {
        // TODO acquire
        for (ref = orig_entry->class->children; ref; ref = ref->next) {
            c = ref->class;
            // TODO: defreeze
            if (!c) continue;
            state = c->states;
            if (state && state->phase == KND_REMOVED) continue;

            if (task->ctx->format_indent) {
                err = out->writec(out, '\n');                                     RET_ERR();
                err = knd_print_offset(out, (depth + 1) * task->ctx->format_indent);   RET_ERR();
            }
            err = present_subclass(ref, task, depth + 1);                         RET_ERR();
        }
    }
    err = out->writec(out, ']');                                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

static int export_attrs(struct kndClass *self,
                        struct kndTask *task,
                        size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttr *attr;
    size_t i = 0;
    int err;

    err = out->write(out, "[attr", strlen("[attr"));                            RET_ERR();

    for (attr = self->attrs; attr; attr = attr->next) {
        if (task->ctx->format_indent) {
            err = out->writec(out, '\n');                                         RET_ERR();
            err = knd_print_offset(out, (depth + 2) * task->ctx->format_indent);       RET_ERR();
        }

        err = knd_attr_export_GSL(attr, task, depth + 1);
        if (err) {
            knd_log("-- failed to export %.*s attr",
                    attr->name_size, attr->name);
            return err;
        }
        i++;
    }
    err = out->writec(out, ']');                                                  RET_ERR();
    
    return knd_OK;
}

static int export_baseclasses(struct kndClass *self, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndClassVar *cvar;
    struct kndClass *c;
    size_t cvar_count = 0;
    size_t curr_depth = task->ctx->depth;
    size_t indent_size = task->ctx->format_indent;
    int err;

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, depth * indent_size);
        RET_ERR();
    }
    OUT("[is", strlen("[is"));

    FOREACH (cvar, self->baseclass_vars) {
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 1) * indent_size);
            RET_ERR();
        }
        OUT("{", 1);
        OUT(cvar->entry->name, cvar->entry->name_size);

        err = knd_class_acquire(cvar->entry, &c, task);
        KND_TASK_ERR("failed to acquire baseclass %.*s", cvar->entry->name_size, cvar->entry->name);

        if (DEBUG_GSL_LEVEL_TMP)
            knd_log(">> base class: %.*s", c->name_size, c->name);

        if (c->tr) {
            err = knd_text_gloss_export_GSL(c->tr, task, depth + 2);
            KND_TASK_ERR("failed to export baseclass gloss GSL");
        }
       
        if (cvar->attrs) {
            curr_depth = task->ctx->depth;
            err = knd_attr_vars_export_GSL(cvar->attrs, task, false, depth + 2);
            KND_TASK_ERR("failed to export attr vars GSL");
            task->ctx->depth = curr_depth;   
        }
        OUT("}", 1);
        cvar_count++;
    }
    OUT("]", 1);
    return knd_OK;
}

#if 0
static int export_attr_hub_GSL(struct kndAttrHub *hub, struct kndOutput *out, struct kndTask *task, size_t depth)
{
    struct kndSet *set;
    int err;

    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->write(out,
                     hub->attr->name,
                     hub->attr->name_size);                                       RET_ERR();

    if (hub->parent) {
        err = export_attr_hub_GSL(hub->parent, out, task, depth);                 RET_ERR();
    }

    if (hub->topics) {
        set = hub->topics;
        err = out->write(out, " {total ",
                         strlen(" {total "));                                     RET_ERR();
        err = out->writef(out, "%zu", set->num_elems);                            RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
        
        //if (task->show_rels) {
            err = out->write(out, "[topic",
                             strlen("[topic"));                                   RET_ERR();
            //task->max_depth = 0;
            err = set->map(set, export_class_ref_GSL, (void*)task);
            if (err && err != knd_RANGE) return err;
            err = out->writec(out, ']');                                          RET_ERR();
            //}
    }

    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}
#endif

int knd_class_export_GSL(struct kndClassEntry *entry, struct kndTask *task, bool is_list_item, size_t depth)
{
    struct kndClass *self = entry->class;
    struct kndClassEntry *orig_entry = entry->base;
    struct kndOutput *out = task->out;
    // struct kndAttrHub *attr_hub;
    struct kndState *state = self->states;
    size_t indent_size = task->ctx->format_indent;
    size_t num_children;
    int err;

    if (DEBUG_GSL_LEVEL_TMP) {
        knd_log(".. GSL export: \"%.*s\" (repo:%.*s) "
                " depth:%zu max depth:%zu indent size:%zu",
                entry->name_size, entry->name, entry->repo->name_size, entry->repo->name,
                task->depth, task->max_depth, indent_size);
    }
    OUT("{", 1);
    if (!is_list_item) {
        OUT("class ", strlen("class "));
    }
    if (entry->name_size) {
        err = out->write_escaped(out, entry->name, entry->name_size);
        RET_ERR();
    }

    if (task->max_depth == 0) {
        goto final;
    }
    if (indent_size) {
        OUT(" ", 1);
    }

    if (state) {
        if (indent_size) {
            err = out->writec(out, '\n');                                         RET_ERR();
            err = knd_print_offset(out, (depth + 1) * indent_size);       RET_ERR();
        }

        err = out->write(out, "{_state ", strlen("{_state "));                    RET_ERR();
        err = out->writef(out, "%zu", state->numid);                              RET_ERR();

        switch (state->phase) {
        case KND_REMOVED:
            err = out->write(out,   "{phase del}",
                             strlen("{phase del}"));                              RET_ERR();
            // NB: no more details
            err = out->writec(out, '}');  RET_ERR();
            return knd_OK;
            
        case KND_UPDATED:
            err = out->write(out,   "{phase upd}",
                             strlen("{phase upd}"));                              RET_ERR();
            break;
        case KND_CREATED:
            err = out->write(out,   "{phase new}",
                             strlen("{phase new}"));                              RET_ERR();
            break;
        default:
            break;
        }
        OUT("}", 1);
    }

    if (self->tr) {
        err = knd_text_gloss_export_GSL(self->tr, task, depth + 1);
        RET_ERR();
    }

    if (depth >= task->max_depth) {
        err = export_concise_GSL(self, task, depth);
        RET_ERR();
        goto final;
    }

    /* display base classes only once */
    if (self->num_baseclass_vars) {
        err = export_baseclasses(self, task, depth + 1);
        RET_ERR();
    }
    /*else {
        if (orig_entry && orig_entry->class->num_baseclass_vars) {
            if (indent_size) {
                err = out->writec(out, '\n');                                     RET_ERR();
                err = knd_print_offset(out, (depth + 1) * indent_size);   RET_ERR();
            }
            
            err = export_baseclass_vars(orig_entry->class, task, depth + 1);      RET_ERR();
        }
        }*/

    if (self->attrs) {
        err = export_attrs(self, task, depth + 1);
        RET_ERR();
    }
    /*else {
        if (orig_entry && orig_entry->class->num_attrs) {
            if (indent_size) {
                err = out->writec(out, '\n');                                     RET_ERR();
                err = knd_print_offset(out, (depth + 1) * indent_size);   RET_ERR();
            }
            err = export_attrs(orig_entry->class, task, depth + 1);               RET_ERR();
        }
        }*/

    num_children = self->num_children;
    if (self->desc_states) {
        state = self->desc_states;
        if (state->val) 
            num_children = state->val->val_size;
    }

    // TODO atomic
    if (orig_entry)
        num_children += orig_entry->class->num_children;

    if (num_children) {
        if (indent_size) {
            err = out->writec(out, '\n');                                         RET_ERR();
            err = knd_print_offset(out, (depth + 1) * indent_size);       RET_ERR();
        }
        err = present_subclasses(self, num_children, task, depth + 1);            RET_ERR();
    }

    /* instances */
    if (self->inst_idx) {
        err = out->write(out, "{instance-state",
                         strlen("{instance-state"));                                   RET_ERR();

        if (self->inst_states) {
            err = export_class_inst_state_GSL(self, task);                          RET_ERR();
        }

        // TODO navigation facets?

        err = out->writec(out, '}');
        if (err) return err;
    }

    /* reverse attr paths */
    /*if (self->attr_hubs) {
        if (indent_size) {
            err = out->writec(out, '\n');                                         RET_ERR();
            err = knd_print_offset(out, (depth + 1) * indent_size);  RET_ERR();
        }
        err = out->write(out, "[_rev_attrs", strlen("[_rev_attrs"));              RET_ERR();
        FOREACH (attr_hub, self->attr_hubs) {
            if (indent_size) {
                err = out->writec(out, '\n');                                     RET_ERR();
                err = knd_print_offset(out,
                                       (depth + 2) * indent_size);   RET_ERR();
            }
            err = export_attr_hub_GSL(attr_hub, out, task, depth);                RET_ERR();
        }
        err = out->writec(out, ']');                                              RET_ERR();
    } */

 final:
    err = out->writec(out, '}');                                                  RET_ERR();
    return knd_OK;
}
