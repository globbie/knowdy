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

#if 0
static int export_conc_elem_GSL(void *elem, void *ctx, struct kndTask *task)
{
    struct kndClassEntry *entry = elem;
    struct LocalContext *local_ctx = ctx; 
    //struct kndRepo *repo = local_ctx->repo;
    struct kndQueryView *view = task->ctx->query->view;
    struct kndBatchLimits *batch = view->batch;
    if (batch->size >= batch->max_items) return knd_RANGE;
    struct kndOutput *out = task->out;
    //struct kndClass *c;
    struct kndState *state;
    size_t curr_depth = 0;
    int err;

    //err = knd_class_acquire(entry, &c, repo, task);
    //KND_TASK_ERR("failed to acquire {cls %.*s}", entry->name_size, entry->name);

    //if (!view->show_removed_objs) {
    //    state = c->states;
    //    if (state && state->phase == KND_REMOVED) return knd_OK;
    //}

    curr_depth = task->depth;
    task->depth = 0;
    if (task->ctx->format_indent) {
        err = out->writec(out, '\n');                                             RET_ERR();
        err = knd_print_indent(out, task->ctx->format_indent);                         RET_ERR();
    }

    //err = knd_class_export_GSL(c, repo, task, true, 1);
    //KND_TASK_ERR("failed to export GSL {cls %.*s}", entry->name_size, entry->name);

    task->depth = curr_depth;
    batch->size++;
    return knd_OK;
}
#endif

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

    err = out->writef(out, "{total %lu",
                      (unsigned long)set->num_elems);                   RET_ERR();

    if (task->ctx->format_indent) {
        err = out->writec(out, '\n');                                             RET_ERR();
        err = knd_print_indent(out, task->ctx->format_indent);                    RET_ERR();
    }

    if (!batch->max_items) {
        batch->max_items = KND_RESULT_BATCH_SIZE;
    }

    err = out->write(out, "[cls",
                     strlen("[cls"));                                            RET_ERR();

    //err = knd_set_map(set, NULL, NULL, NULL, export_conc_elem_GSL, NULL, task);
    //if (err && err != knd_RANGE) return err;
    
    err = out->writec(out, ']');                                                  RET_ERR();

    if (task->ctx->format_indent) {
        err = out->writec(out, '\n');                                             RET_ERR();
        err = knd_print_indent(out, task->ctx->format_indent);                    RET_ERR();
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
    int err;

    assert (entry != NULL);

    OUT("{", 1);
    OUT(" ", 1);
    OUT(entry->name, entry->name_size);

    err = knd_text_glosses_export_GSL(entry->glosses, task, depth + 1);
    KND_TASK_ERR("failed to export glosses GSL");

    OUT(" ", 1);
    OUT("}", 1);
    return knd_OK;
}

static int present_subclasses(struct kndClass *self, size_t num_children,
                              struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndClassRef *ref;
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
        err = knd_print_indent(out, (depth + 1) * task->ctx->format_indent);           RET_ERR();
    }

    err = out->write(out, "[batch", strlen("[batch"));                            RET_ERR();

    // TODO apply sort by?
    FOREACH (ref, self->children) {
        state = self->states;
        if (state && state->phase == KND_REMOVED) continue;

        if (task->ctx->format_indent) {
            err = out->writec(out, '\n');                                         RET_ERR();
            err = knd_print_indent(out, (depth + 2) * task->ctx->format_indent);       RET_ERR();
        }

        err = present_subclass(ref, task, depth + 2);
        KND_TASK_ERR("failed to present a subclass GSL");       
    }

    /*    if (orig_entry) {
        err = knd_class_acquire(orig_entry, &orig_c, task);
        KND_TASK_ERR("failed to acquire {class %.*s}", orig_entry->name_size, orig_entry->name);

        FOREACH (ref, orig_c->children) {

            err = knd_class_acquire(ref->entry, &c, task);
            KND_TASK_ERR("failed to acquire class %.*s", ref->entry->name_size, ref->entry->name);

            state = c->states;
            if (state && state->phase == KND_REMOVED) continue;

            if (task->ctx->format_indent) {
                err = out->writec(out, '\n');                                     RET_ERR();
                err = knd_print_indent(out, (depth + 1) * task->ctx->format_indent);   RET_ERR();
            }
            err = present_subclass(ref, task, depth + 1);                         RET_ERR();
        }
        } */

    err = out->writec(out, ']');                                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

static int export_attrs(struct kndClass *cls, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndAttrRef *ref;
    struct kndAttr *attr;
    size_t i = 0;
    int err;

    FOREACH (ref, cls->attr_refs) {
        attr = ref->attr;

        if (task->ctx->format_indent) {
            OUT("\n", 1);
            err = knd_print_indent(out, (depth + 1) * task->ctx->format_indent);
            RET_ERR();
        }

        err = knd_attr_export_GSL(attr, task, depth + 1);
        KND_TASK_ERR("failed to export {attr %.*s}", attr->name_size, attr->name);

        i++;
    }
    return knd_OK;
}

static int export_base_preds(struct kndClass *cls, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndClassBasePred *bp;
    size_t bp_count = 0;
    size_t indent_size = task->ctx->format_indent;
    int err;

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_indent(out, depth * indent_size);
        RET_ERR();
    }

    OUT("[is", strlen("[is"));

    FOREACH (bp, cls->base_preds) {
        assert (bp->entry != NULL);

        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_indent(out, (depth + 1) * indent_size);
            RET_ERR();
        }
        OUT("{", 1);
        if (indent_size) OUT(" ", 1);
        OUT(bp->entry->name, bp->entry->name_size);

        err = knd_text_glosses_export_GSL(bp->entry->glosses, task, depth + 2);
        KND_TASK_ERR("failed to export baseclass gloss GSL");

        if (bp->num_attr_stms) {
            err = knd_attr_stms_export_GSL(bp->attr_stms, task, depth + 2);
            KND_TASK_ERR("failed to export attr stms GSL");
        }

        OUT(" ", 1);
        OUT("}", 1);
        bp_count++;
    }
    OUT("]", 1);
    return knd_OK;
}

int knd_class_export_GSL(struct kndClass *cls, struct kndTask *task, bool is_list_item, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndState *state = cls->states;
    size_t indent_size = task->ctx->format_indent;
    size_t num_children;
    int err;

    if (DEBUG_GSL_LEVEL_2) {
        knd_log(".. GSL export {cls %.*s} "
                " {depth %zu} {max-depth %zu} {indent-size %zu}",
                cls->name_size, cls->name,
                task->depth, task->max_depth, indent_size);
    }

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_indent(out, depth * indent_size);
        RET_ERR("output failure");
    }

    OUT("{", 1);

    if (!is_list_item) {
        OUT("cls", strlen("cls"));
    }

    OUT(" ", 1);

    if (cls->name_size) {
        OUT(cls->name, cls->name_size);
    }

    if (is_list_item) {
        OUT(" ", 1);
    }

    /* TODO if (task->max_depth == 0) {
        goto final;
        } */

    if (indent_size) {
        OUT(" ", 1);
    }

    if (state) {
        if (indent_size) {
            err = out->writec(out, '\n');                                         RET_ERR();
            err = knd_print_indent(out, (depth + 1) * indent_size);       RET_ERR();
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

    if (cls->entry->glosses) {
        err = knd_text_glosses_export_GSL(cls->entry->glosses, task, depth + 1);
        KND_TASK_ERR("failed to export cls glosses to GSL");
    }

    if (cls->num_base_preds && !cls->base_preds->is_root) {
        err = export_base_preds(cls, task, depth + 1);
        KND_TASK_ERR("failed to export cls base preds GSL");
    }

    if (cls->num_attr_refs) {
        err = export_attrs(cls, task, depth + 1);
        KND_TASK_ERR("failed to export cls attrs GSL");
    }

    num_children = cls->num_children;
    if (cls->desc_states) {
        state = cls->desc_states;
        num_children = state->val? state->val->val_size : 0;
    }

    /*if (orig_entry) {
        err = knd_class_acquire(orig_entry, &c, task);
        KND_TASK_ERR("failed to acquire class %.*s", orig_entry->name_size, orig_entry->name);
        num_children += c->num_children;
    }*/
    
    if (num_children) {
        if (indent_size) {
            err = out->writec(out, '\n');
            RET_ERR();
            err = knd_print_indent(out, (depth + 1) * indent_size);
            RET_ERR();
        }
        err = present_subclasses(cls, num_children, task, depth + 1);
        RET_ERR();
    }

    /* TODO inverse rels */

    // final:
    OUT(" ", 1);
    OUT("}", 1);
    return knd_OK;
}
