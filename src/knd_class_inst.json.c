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

#if 0
static int export_concise_JSON(struct kndClassInst *self, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    err = out->write(out, ",\"_class\":\"", strlen(",\"_class\":\""));
    if (err) return err;

    err = out->write(out, self->blueprint->name, self->blueprint->name_size);
    if (err) return err;

    err = out->write(out, "\"", 1);
    if (err) return err;

    return knd_OK;
}
#endif

int knd_class_inst_export_JSON(struct kndClassInst *self, bool is_list_item, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndState *state = self->states;
    int err;

    if (DEBUG_INST_LEVEL_2) {
        knd_log(".. JSON export class inst \"%.*s\" curr depth:%zu max depth:%zu is_list:%d",
                self->name_size, self->name, task->depth, task->max_depth, is_list_item);
        if (self->blueprint) {
            knd_log("   (class: %.*s)",
                    self->blueprint->name_size, self->blueprint->name);
        }
    }

    err = out->write(out, "{\"_name\":\"", strlen("{\"_name\":\""));              RET_ERR();
    err = out->write(out, self->name, self->name_size);
    if (err) return err;
    err = out->write(out, "\"", 1);
    if (err) return err;

    err = out->write(out, ",\"_id\":", strlen(",\"_id\":"));                      RET_ERR();
    err = out->writef(out, "%zu", self->entry->numid);                            RET_ERR();

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

    err = out->write(out, ",\"_class\":\"", strlen(",\"_class\":\""));
    if (err) return err;

    err = out->write(out, self->blueprint->name, self->blueprint->name_size);
    if (err) return err;

    err = out->write(out, "\"", 1);
    if (err) return err;

    err = out->write(out, "}", 1);
    if (err) return err;

    return err;
}

static int export_class_inst_JSON(void *obj,
                                  const char *unused_var(attr_inst_id),
                                  size_t unused_var(attr_inst_id_size),
                                  size_t count,
                                  void *attr_inst)
{
    struct kndTask *task = obj;
    if (count < task->start_from) return knd_OK;
    if (task->batch_size >= task->batch_max) return knd_RANGE;
    struct kndOutput *out = task->out;
    struct kndClassInstEntry *entry = attr_inst;
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

    // TODO: depth
    err = knd_class_inst_export_JSON(inst, false, task);                                 RET_ERR();
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
    err = set->map(set, export_class_inst_JSON, (void*)task);
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
