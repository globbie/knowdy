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
#include "knd_attr_stm.h"
#include "knd_task.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_query.h"
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
    struct kndClassBasePred *class_var;
    struct kndText *text;
};

int knd_export_class_state_GSL(struct kndClass *self, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndState *state;
    time_t timestamp = { 0 };
    int err;

    err = out->write(out, "{state ", strlen("{state "));                          RET_ERR();

    state = atomic_load_explicit(&self->states, memory_order_relaxed);
    if (state) {
        err = out->writef(out, "%zu", state->commit->numid);                      RET_ERR();
        timestamp = state->commit->timestamp;
    } else {
        err = out->writec(out, '0');                                              RET_ERR();
        // TODO
        // timestamp = self->repo->snapshot->timestamp;
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

static int export_conc_elem_GSL(void *obj, const char *elem_id, size_t elem_id_size,
                                size_t count, void *elem)
{
    struct kndTask *task = obj;
    struct kndQueryView *view = task->ctx->query->view;
    struct kndBatchLimits *batch = view->batch;
    if (count < batch->from) return knd_OK;
    if (batch->size >= batch->max_items) return knd_RANGE;
    struct kndOutput *out = task->out;
    struct kndClassEntry *entry = elem;
    struct kndClass *c;
    struct kndState *state;
    size_t curr_depth = 0;
    int err;

    if (DEBUG_GSL_LEVEL_2) {
        knd_log(".. GSL export class set elem: %.*s",
                elem_id_size, elem_id);
    }

    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire class %.*s", entry->name_size, entry->name);

    if (!view->show_removed_objs) {
        state = c->states;
        if (state && state->phase == KND_REMOVED) return knd_OK;
    }

    curr_depth = task->depth;
    task->depth = 0;
    if (task->ctx->format_indent) {
        err = out->writec(out, '\n');                                             RET_ERR();
        err = knd_print_offset(out, task->ctx->format_indent);                         RET_ERR();
    }

    err = knd_class_export_GSL(c, task, true, 1);
    KND_TASK_ERR("failed to export GSL {class %.*s}", entry->name_size, entry->name);

    task->depth = curr_depth;
    batch->size++;
    return knd_OK;
}

static int export_concise_GSL(struct kndClass *self, struct kndTask *task, size_t depth)
{
    struct kndClassBasePred *item;
    int err;

    if (DEBUG_GSL_LEVEL_2) {
        knd_log(".. export concise GSL for %.*s..",
                self->entry->name_size, self->entry->name);
    }
    FOREACH (item, self->base_preds) {
        if (!item->attr_stms) continue;

        err = knd_attr_stms_export_GSL(item->attr_stms, task, depth);
        RET_ERR();
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

int knd_class_set_export_GSL(struct kndSet *set, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndQueryView *view = task->ctx->query->view;
    struct kndBatchLimits *batch = view->batch;
    int err;

    out->reset(out);
    err = out->write(out, "{set",
                     strlen("{set"));                                            RET_ERR();

    if (view->show_removed_objs) {
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

    if (!batch->max_items) {
        batch->max_items = KND_RESULT_BATCH_SIZE;
    }

    err = out->write(out, "[class",
                     strlen("[class"));                                            RET_ERR();

    err = knd_set_map(set, export_conc_elem_GSL, (void*)task);
    if (err && err != knd_RANGE) return err;
    
    err = out->writec(out, ']');                                                  RET_ERR();

    if (task->ctx->format_indent) {
        err = out->writec(out, '\n');                                             RET_ERR();
        err = knd_print_offset(out, task->ctx->format_indent);                    RET_ERR();
    }

    err = out->writef(out, "{batch{max %zu}",
                      batch->max_items);                       RET_ERR();
    err = out->writef(out, "{size %zu}",
                       batch->size);                     RET_ERR();
    err = out->writef(out,
                     "{from %zu}}", batch->from);        RET_ERR();

    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

static int present_subclass(struct kndClassRef *ref, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndClassEntry *entry = ref->entry;
    struct kndClass *c;
    int err;

    OUT("{", 1);
    OUT(" ", 1);
    OUT(entry->name, entry->name_size);

    /* localized glosses */
    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire class %.*s", entry->name_size, entry->name);

    if (c->tr) {
        err = knd_text_gloss_export_GSL(c->tr, true, task, depth + 1);
        RET_ERR();
    }

    err = export_concise_GSL(c, task, depth);
    RET_ERR();

    OUT("}", 1);
    return knd_OK;
}

static int present_subclasses(struct kndClass *self, size_t num_children,
                              struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndClassRef *ref;
    struct kndClassEntry *entry = self->entry;
    struct kndClassEntry *orig_entry = entry->orig;
    struct kndClass *orig_c, *c;
    struct kndState *state;
    int err;

    err = out->write(out, "{children {total ",
                     strlen("{children {total "));                             RET_ERR();
    err = out->writef(out, "%zu", num_children);                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    if (self->num_descendants) {
        err = out->write(out, " {num-desc ",
                         strlen(" {num-desc "));                             RET_ERR();
        err = out->writef(out, "%zu", self->num_descendants);                      RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
    }

    if (task->ctx->format_indent) {
        err = out->writec(out, '\n');                                             RET_ERR();
        err = knd_print_offset(out, (depth + 1) * task->ctx->format_indent);           RET_ERR();
    }
    err = out->write(out, "[batch", strlen("[batch"));                            RET_ERR();

    FOREACH (ref, self->children) {
        err = knd_class_acquire(ref->entry, &c, task);
        KND_TASK_ERR("failed to acquire {class %.*s}", ref->entry->name_size, ref->entry->name);
        
        state = c->states;
        if (state && state->phase == KND_REMOVED) continue;
        if (task->ctx->format_indent) {
            err = out->writec(out, '\n');                                         RET_ERR();
            err = knd_print_offset(out, (depth + 2) * task->ctx->format_indent);       RET_ERR();
        }
        err = present_subclass(ref, task, depth + 2);
        RET_ERR();
    }

    if (orig_entry) {
        err = knd_class_acquire(orig_entry, &orig_c, task);
        KND_TASK_ERR("failed to acquire {class %.*s}", orig_entry->name_size, orig_entry->name);

        FOREACH (ref, orig_c->children) {

            err = knd_class_acquire(ref->entry, &c, task);
            KND_TASK_ERR("failed to acquire class %.*s", ref->entry->name_size, ref->entry->name);

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

static int export_attrs(struct kndClass *self, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttr *attr;
    size_t i = 0;
    int err;

    FOREACH (attr, self->attrs) {
        if (task->ctx->format_indent) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 1) * task->ctx->format_indent);
            RET_ERR();
        }
        err = knd_attr_export_GSL(attr, task, depth + 1);
        KND_TASK_ERR("failed to export %.*s attr", attr->name_size, attr->name);
        i++;
    }
    return knd_OK;
}

static int export_base_preds(struct kndClass *self, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndClassBasePred *bp;
    size_t bp_count = 0;
    size_t indent_size = task->ctx->format_indent;
    int err;

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, depth * indent_size);
        RET_ERR();
    }
    OUT("[is", strlen("[is"));

    FOREACH (bp, self->base_preds) {
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 1) * indent_size);
            RET_ERR();
        }
        OUT("{ ", strlen("{ "));
        OUT(bp->entry->name, bp->entry->name_size);

        // TODO
        /*err = knd_class_acquire(bp->entry, &c, task);
        KND_TASK_ERR("failed to acquire baseclass %.*s",
                     bp->entry->name_size, bp->entry->name);
        if (c->tr) {
            err = knd_text_gloss_export_GSL(c->tr, true, task, depth + 2);
            KND_TASK_ERR("failed to export baseclass gloss GSL");
            }*/
       
        if (bp->attr_stms) {
            //curr_depth = task->ctx->depth;
            err = knd_attr_stms_export_GSL(bp->attr_stms, task, depth + 1);
            KND_TASK_ERR("failed to export attr stms GSL");
            //task->ctx->depth = curr_depth;   
        }

        OUT(" ", 1);
        OUT("}", 1);
        bp_count++;
    }
    OUT("]", 1);
    return knd_OK;
}

#if 0
static int export_inverse_rels(struct kndClass *self, struct kndTask *task, size_t depth)
{
    struct kndAttrHub *attr_hub;
    struct kndAttr *attr;
    struct kndOutput *out = task->out;
    bool in_list = false;
    size_t curr_depth = 0;
    size_t indent_size = task->ctx->format_indent;
    int err;

    OUT(",", 1);
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, (depth) * indent_size);
        RET_ERR();
    }

    OUT("\"rels\":", strlen("\"rels\":"));
    if (indent_size) {
        OUT(" ", 1);
    }
    OUT("[", 1);
    
    FOREACH (attr_hub, self->attr_hubs) {
        if (in_list) {
            OUT(",", 1);
        }
        if (!attr_hub->attr) {
            err = knd_attr_hub_resolve(attr_hub, task);
            KND_TASK_ERR("failed to resolve attr hub");
        }
        attr = attr_hub->attr;
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 1) * indent_size);
            RET_ERR();
        }

        OUT("{", 1);
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 2) * indent_size);
            RET_ERR();
        }
        OUT("\"cls\":", strlen("\"cls\":"));
        if (indent_size) {
            OUT(" ", 1);
        }
        OUT("\"", 1);
        OUT(attr->parent->name, attr->parent->name_size);
        OUT("\"", 1);

        OUT(",", 1);
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 2) * indent_size);
            RET_ERR();
        }
        OUT("\"attr\":", strlen("\"attr\":"));
        if (indent_size) {
            OUT(" ", 1);
        }
        OUT("\"", 1);
        OUT(attr->name, attr->name_size);
        OUT("\"", 1);
        
        if (attr_hub->topics) {
            OUT(",", 1);
            if (indent_size) {
                OUT("\n", 1);
                err = knd_print_offset(out, (depth + 2) * indent_size);
                RET_ERR();
            }
            OUT("\"total\":", strlen("\"total\":"));
            if (indent_size) {
                OUT(" ", 1);
            }
            OUTF("%zu", attr_hub->topics->num_valid_elems);
        
            curr_depth = task->ctx->max_depth;
            task->ctx->max_depth = 0;
            task->depth = depth + 3;
            task->view->batch->batch_size = 0;
            OUT(",", 1);
            if (indent_size) {
                OUT("\n", 1);
                err = knd_print_offset(out, (depth + 2) * indent_size);
                RET_ERR();
            }
            OUT("\"topics\":", strlen("\"topics\":"));
            if (indent_size) {
                OUT(" ", 1);
            }
            OUT("[", 1);
            err = knd_set_map(attr_hub->topics, export_class_ref, (void*)task);
            if (err && err != knd_RANGE) return err;

            if (indent_size) {
                OUT("\n", 1);
                err = knd_print_offset(out, (depth + 2) * indent_size);
                RET_ERR();
            }
            OUT("]", 1);
            task->ctx->max_depth = curr_depth;
        }

        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_offset(out, (depth + 1) * indent_size);
            RET_ERR();
        }
        OUT("}", 1);
        in_list = true;
    }
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, (depth) * indent_size);
        RET_ERR();
    }
    OUT("]", 1);
    return knd_OK;
}
#endif

int knd_class_export_GSL(struct kndClass *self, struct kndTask *task,
                         bool is_list_item, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndState *state = self->states;
    size_t indent_size = task->ctx->format_indent;
    size_t num_children;
    bool use_locale = true;
    int err;

    if (DEBUG_GSL_LEVEL_2) {
        knd_log(".. GSL export {repo %.*s {cls %.*s}} "
                " depth:%zu max depth:%zu indent size:%zu",
                self->entry->repo->name_size, self->entry->repo->name,
                self->name_size, self->name,
                task->depth, task->max_depth, indent_size);
    }
    OUT("{", 1);

    if (!is_list_item) {
        OUT("cls", strlen("cls"));
    }

    OUT(" ", 1);

    if (self->name_size) {
        OUT(self->name, self->name_size);
    }

    if (is_list_item) {
        OUT(" ", 1);
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
        err = knd_text_gloss_export_GSL(self->tr, use_locale, task, depth + 1);
        KND_TASK_ERR("failed to export gloss GSL");
    }

    if (depth >= task->max_depth) {
        err = export_concise_GSL(self, task, depth);
        KND_TASK_ERR("failed to export concise cls GSL");
        goto final;
    }

    /* display base classes only once */
    if (self->num_base_preds) {
        err = export_base_preds(self, task, depth + 1);
        KND_TASK_ERR("failed to export base preds GSL");
    }

    if (self->attrs) {
        err = export_attrs(self, task, depth + 1);
        RET_ERR();
    }

    num_children = self->num_children;
    if (self->desc_states) {
        state = self->desc_states;
        if (state->val) 
            num_children = state->val->val_size;
    }

    /*if (orig_entry) {
        err = knd_class_acquire(orig_entry, &c, task);
        KND_TASK_ERR("failed to acquire class %.*s", orig_entry->name_size, orig_entry->name);
        num_children += c->num_children;
        }*/

    if (num_children) {
        if (indent_size) {
            err = out->writec(out, '\n');                                         RET_ERR();
            err = knd_print_offset(out, (depth + 1) * indent_size);       RET_ERR();
        }
        err = present_subclasses(self, num_children, task, depth + 1);            RET_ERR();
    }

    /* inverse relations */
    /*if (self->attr_hubs) {
        err = export_inverse_rels(self, task, depth + 1);
        KND_TASK_ERR("failed to export GSL inverse rels");
    }*/

 final:
    OUT(" ", 1);
    OUT("}", 1);
    return knd_OK;
}
