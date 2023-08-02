#include "knd_class_inst.h"

#include "knd_attr.h"
#include "knd_set.h"
#include "knd_output.h"

#include <string.h>

#define DEBUG_INST_LEVEL_1 0
#define DEBUG_INST_LEVEL_2 0
#define DEBUG_INST_LEVEL_3 0
#define DEBUG_INST_LEVEL_4 0
#define DEBUG_INST_LEVEL_TMP 1

static int export_class_inst(void *obj, const char *unused_var(elem_id),
                             size_t unused_var(elem_id_size),
                             size_t unused_var(count), void *elem)
{
    struct kndTask *task = obj;
    size_t indent_size = task->ctx->format_indent;
    size_t depth = task->depth;
    // if (count < task->start_from) return knd_OK;
    // if (task->batch_size >= task->batch_max) return knd_RANGE;

    struct kndOutput *out = task->out;
    struct kndClassInstEntry *inst = elem;
    struct kndClassEntry *entry = inst->is_a;
    int err;

    if (task->batch_size) {
        OUT(",", 1);
    }
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, (depth) * indent_size);
        RET_ERR();
    }
    OUT("{", 1);
    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, (depth + 1) * indent_size);
        RET_ERR();
    }

    OUT("\"class\":", strlen("\"class\":"));
    if (indent_size) {
        OUT(" ", 1);
    }
    OUT("\"", 1);
    OUT(entry->name, entry->name_size);
    OUT("\"", 1);

    OUT("\"inst\":", strlen("\"inst\":"));
    if (indent_size) {
        OUT(" ", 1);
    }
    OUT("\"", 1);
    OUT(inst->name, inst->name_size);
    OUT("\"", 1);

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, (depth) * indent_size);
        RET_ERR();
    }
    OUT("}", 1);

    task->batch_size++;
    return knd_OK;
}

static int export_inverse_rels(struct kndClassInst *self, struct kndTask *task, size_t depth)
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
        OUT("\"class\":", strlen("\"class\":"));
        if (indent_size) {
            OUT(" ", 1);
        }
        OUT("\"", 1);
        OUT(attr->parent_class->name, attr->parent_class->name_size);
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
            task->batch_size = 0;
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
            err = knd_set_map(attr_hub->topics, export_class_inst, (void*)task);
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

int knd_class_inst_export_JSON(struct kndClassInst *self, bool is_list_item,
                               knd_state_phase phase,
                               struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    struct kndState *state = self->states;
    size_t curr_depth = 0;
    int err;

    if (DEBUG_INST_LEVEL_2) {
        knd_log(".. JSON export class inst \"%.*s\" curr depth:%zu max depth:%zu is_list:%d",
                self->name_size, self->name, task->depth, task->max_depth, is_list_item);
        if (self->entry->is_a) {
            knd_log("   (class: %.*s)",
                    self->entry->is_a->name_size, self->entry->is_a->name);
        }
    }

    OUT("{\"name\":\"", strlen("{\"name\":\""));
    OUT(self->name, self->name_size);
    OUT("\"", 1);

    if (state) {
        err = out->write(out, ",\"_state\":", strlen(",\"_state\":"));            RET_ERR();
        err = out->writef(out, "%zu", state->numid);                              RET_ERR();

        switch (state->phase) {
            case KND_REMOVED:
                err = out->write(out,   ",\"_phase\":\"del\"",
                                 strlen(",\"_phase\":\"del\""));                      RET_ERR();
                // NB: no more details
                err = out->write(out, "}", 1);
                if (err) return err;
                return knd_OK;

            case KND_UPDATED:
                err = out->write(out,   ",\"_phase\":\"upd\"",
                                 strlen(",\"_phase\":\"upd\""));                      RET_ERR();
                break;
            case KND_CREATED:
                err = out->write(out,   ",\"_phase\":\"new\"",
                                 strlen(",\"_phase\":\"new\""));                      RET_ERR();
                break;
            default:
                break;
        }
    }

    OUT(",\"class\":\"", strlen(",\"class\":\""));
    OUT(self->entry->is_a->name, self->entry->is_a->name_size);
    OUT("\"", 1);

    if (self->class_var && self->class_var->attrs) {
        curr_depth = task->ctx->depth;
        err = knd_attr_vars_export_JSON(self->class_var->attrs, task, false, depth + 1);
        KND_TASK_ERR("failed to export JSON of class inst attr vars");
        task->ctx->depth = curr_depth;
    }


    switch (phase) {
    case KND_SELECTED:
        /* display inverse relations */
        if (self->attr_hubs) {
            err = export_inverse_rels(self, task, depth + 1);
            KND_TASK_ERR("failed to export GSL inverse rels");
        }
        break;
    default:
        break;
    }

    OUT("}", 1);
    return knd_OK;
}

int knd_class_inst_iterate_export_JSON(void *obj, const char *unused_var(inst_id),
                                       size_t unused_var(inst_id_size), size_t count, void *elem)
{
    struct kndTask *task = obj;
    if (count < task->start_from) return knd_OK;
    // if (task->batch_size >= task->batch_max) return knd_RANGE;
    struct kndOutput *out = task->out;
    struct kndClassInstEntry *entry = elem;
    struct kndClassInst *inst = entry->inst;
    struct kndState *state;
    int err;

    if (DEBUG_INST_LEVEL_2) {
        knd_class_inst_str(inst, 0);
    }

    if (!task->show_removed_objs) {
        state = inst->states;
        if (state && state->phase == KND_REMOVED)
            return knd_OK;
    }

    // TODO unfreeze

    /* separator */
    if (task->batch_size) {
        err = out->writec(out, ',');                                              RET_ERR();
    }

    err = knd_class_inst_export_JSON(inst, false, KND_SELECTED, task, 0);
    KND_TASK_ERR("failed to export class inst JSON");
    task->batch_size++;

    return knd_OK;
}

int knd_class_inst_set_export_JSON(struct kndSet *set, struct kndTask *task)
{
    int err;
    struct kndOutput *out = task->out;

    err = out->write(out, "{\"_set\":{",
                     strlen("{\"_set\":{"));                                      RET_ERR();

    if (task->show_removed_objs) {
        err = out->writef(out, "\"total\":%lu",
                          (unsigned long)set->num_elems);                         RET_ERR();
    } else {
        err = out->writef(out, "\"total\":%lu",
                          (unsigned long)set->num_valid_elems);                   RET_ERR();
    }
    err = out->write(out, ",\"batch\":[",
                     strlen(",\"batch\":["));                                     RET_ERR();
    err = set->map(set, knd_class_inst_iterate_export_JSON, (void*)task);
    if (err && err != knd_RANGE) return err;
    err = out->writec(out, ']');                                                  RET_ERR();

    err = out->writef(out, ",\"batch_max\":%lu",
                      (unsigned long)task->batch_max);                            RET_ERR();
    err = out->writef(out, ",\"batch_size\":%lu",
                      (unsigned long)task->batch_size);                           RET_ERR();
    err = out->writef(out, ",\"batch_from\":%lu",
                      (unsigned long)task->batch_from);                           RET_ERR();

    err = out->writec(out, '}');                                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();
    return knd_OK;
}
