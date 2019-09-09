#include "knd_class_inst.h"

#include "knd_attr.h"
#include "knd_attr_inst.h"
#include "knd_set.h"
#include "knd_output.h"

#include <string.h>

#define DEBUG_INST_LEVEL_1 0
#define DEBUG_INST_LEVEL_2 0
#define DEBUG_INST_LEVEL_3 0
#define DEBUG_INST_LEVEL_4 0
#define DEBUG_INST_LEVEL_TMP 1

static int export_inner_JSON(struct kndClassInst *self,
                             struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndAttrInst *attr_inst;
    int err;

    err = out->writec(out, '{');                                                  RET_ERR();

    attr_inst = self->attr_insts;
    while (attr_inst) {
        err = knd_attr_inst_export(attr_inst, KND_FORMAT_JSON, task);
        if (err) {
            knd_log("-- inst attr_inst export failed: %.*s",
                    attr_inst->attr->name_size, attr_inst->attr->name);
            return err;
        }
        if (attr_inst->next) {
            err = out->write(out, ",", 1);
            if (err) return err;
        }
        attr_inst = attr_inst->next;
    }

    err = out->writec(out, '}');   RET_ERR();

    return knd_OK;
}

static int export_concise_JSON(struct kndClassInst *self,
                               struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndClassInst *obj;
    struct kndAttrInst *attr_inst;
    bool is_concise = true;
    int err;

    err = out->write(out, ",\"_class\":\"", strlen(",\"_class\":\""));
    if (err) return err;

    err = out->write(out, self->base->name, self->base->name_size);
    if (err) return err;

    err = out->write(out, "\"", 1);
    if (err) return err;

    for (attr_inst = self->attr_insts; attr_inst; attr_inst = attr_inst->next) {
        if (DEBUG_INST_LEVEL_3)
            knd_log(".. export attr_inst: %.*s",
                    attr_inst->attr->name_size, attr_inst->attr->name);

        /* filter out detailed presentation */
        if (is_concise) {
            /* inner obj? */
            if (attr_inst->inner) {
                obj = attr_inst->inner;

                /*if (need_separ) {*/
                err = out->write(out, ",", 1);
                if (err) return err;

                err = out->write(out, "\"", 1);
                if (err) return err;
                err = out->write(out,
                                 attr_inst->attr->name,
                                 attr_inst->attr->name_size);
                if (err) return err;
                err = out->write(out, "\":", 2);
                if (err) return err;

                err = knd_class_inst_export(obj, KND_FORMAT_JSON, task);
                if (err) return err;

                //need_separ = true;
                continue;
            }

            if (attr_inst->attr)
                if (attr_inst->attr->concise_level)
                    goto export_attr_inst;

            if (DEBUG_INST_LEVEL_2)
                knd_log("  .. skip JSON attr_inst: %s..\n", attr_inst->attr->name);
            continue;
        }

        export_attr_inst:
        /*if (need_separ) {*/
        err = out->write(out, ",", 1);
        if (err) return err;

        /* default export */
        err = knd_attr_inst_export(attr_inst, KND_FORMAT_JSON, task);
        if (err) {
            knd_log("-- attr_inst not exported: %s", attr_inst->attr->name);
            return err;
        }
        //need_separ = true;
    }
    return knd_OK;
}

int knd_class_inst_export_JSON(struct kndClassInst *self,
                               struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndAttrInst *attr_inst;
    struct kndClassInst *obj;
    struct kndState *state = self->states;
    bool is_concise = false;
    int err;

    if (DEBUG_INST_LEVEL_2) {
        knd_log(".. JSON export class inst \"%.*s\" curr depth:%zu max depth:%zu",
                self->name_size, self->name, self->depth, task->max_depth);
        if (self->base) {
            knd_log("   (class: %.*s)",
                    self->base->name_size, self->base->name);
        }
    }

    if (self->type == KND_OBJ_INNER) {
        err = export_inner_JSON(self, task);
        if (err) {
            knd_log("-- inner obj JSON export failed");
            return err;
        }
        return knd_OK;
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

    if (self->depth >= task->max_depth) {
        /* any concise fields? */
        err = export_concise_JSON(self, task);                                     RET_ERR();
        goto final;
    }

    err = out->write(out, ",\"_class\":\"", strlen(",\"_class\":\""));
    if (err) return err;

    err = out->write(out, self->base->name, self->base->name_size);
    if (err) return err;

    err = out->write(out, "\"", 1);
    if (err) return err;

    /* TODO: id */

    for (attr_inst = self->attr_insts; attr_inst; attr_inst = attr_inst->next) {

        if (DEBUG_INST_LEVEL_2)
            knd_log(".. export attr_inst: %.*s",
                    attr_inst->attr->name_size, attr_inst->attr->name);

        /* filter out detailed presentation */
        if (is_concise) {
            /* inner obj? */
            if (attr_inst->inner) {
                obj = attr_inst->inner;
                /*if (need_separ) {*/
                err = out->write(out, ",", 1);
                if (err) return err;

                err = out->write(out, "\"", 1);
                if (err) return err;
                err = out->write(out,
                                 attr_inst->attr->name,
                                 attr_inst->attr->name_size);
                if (err) return err;
                err = out->write(out, "\":", 2);
                if (err) return err;

                err = knd_class_inst_export(obj, KND_FORMAT_JSON, task);
                if (err) return err;

                //need_separ = true;
                continue;
            }

            if (attr_inst->attr)
                if (attr_inst->attr->concise_level)
                    goto export_attr_inst;

            if (DEBUG_INST_LEVEL_TMP)
                knd_log("  .. skip JSON attr_inst: %.*s..\n",
                        attr_inst->attr->name_size, attr_inst->attr->name);
            continue;
        }

        export_attr_inst:
        /*if (need_separ) {*/
        err = out->write(out, ",", 1);
        if (err) return err;

        /* default export */
        err = knd_attr_inst_export(attr_inst, KND_FORMAT_JSON, task);
        if (err) {
            knd_log("-- attr_inst not exported: %s", attr_inst->attr->name);
            return err;
        }
        //need_separ = true;
    }

    /*if (self->entry->rels) {
        err = out->write(out, ",\"_rel\":", strlen(",\"_rel\":"));                RET_ERR();
        err = out->writec(out, '[');                                              RET_ERR();
        err = export_inst_relrefs_JSON(self, task);                                     RET_ERR();
        err = out->writec(out, ']');                                              RET_ERR();
        }*/

    final:
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
    err = knd_class_inst_export_JSON(inst, task);                                 RET_ERR();
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
