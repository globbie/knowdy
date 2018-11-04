#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_class_inst.h"
#include "knd_class.h"
#include "knd_mempool.h"
#include "knd_attr.h"
#include "knd_elem.h"
#include "knd_repo.h"

#include "knd_text.h"
#include "knd_num.h"
#include "knd_rel.h"
#include "knd_set.h"
#include "knd_rel_arg.h"

#include "knd_user.h"
#include "knd_state.h"

#include <gsl-parser.h>
#include <glb-lib/output.h>

#define DEBUG_INST_LEVEL_1 0
#define DEBUG_INST_LEVEL_2 0
#define DEBUG_INST_LEVEL_3 0
#define DEBUG_INST_LEVEL_4 0
#define DEBUG_INST_LEVEL_TMP 1

struct LocalContext {
    //struct kndClass *class;
    struct kndTask *task;
    struct kndClassInst *class_inst; // remove this
};

// TODO(k15tfu): ?? remove this
static int export_inst_set_JSON(struct kndClass *self, struct kndSet *set, struct glbOutput *out, struct kndTask *task);

static gsl_err_t run_get_inst(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClassInst *self = task->class_inst;
    struct kndClass *c = self->base;
    int err;

    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    self->curr_inst = NULL;
    err = knd_get_class_inst(c, name, name_size, task, &self->curr_inst);
    if (err) {
        knd_log("-- failed to get class inst: %.*s", name_size, name);
        return make_gsl_err_external(err);
    }
    self->curr_inst->max_depth = 0;

    /* to return a single object */
    task->type = KND_GET_STATE;
    self->curr_inst->elem_state_refs = NULL;

    if (DEBUG_INST_LEVEL_2) {
        knd_class_inst_str(self->curr_inst, 0);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_curr_state(void *obj, const char *val, size_t val_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndTask *task = obj;
    long numval;
    int err;

    if (val_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    memcpy(buf, val, val_size);
    buf_size = val_size;
    buf[buf_size] = '\0';

    err = knd_parse_num(buf, &numval);
    if (err) return make_gsl_err_external(err);

    // TODO: check integer

    task->state_eq = (size_t)numval;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t present_state(void *obj,
                               const char *unused_var(name),
                               size_t unused_var(name_size))
{
    struct LocalContext *ctx = obj;
    struct kndClass *self = ctx->class_inst->base;
    struct kndTask *task = ctx->task;
    struct glbOutput *out = task->out;
    struct kndMemPool *mempool = task->mempool;
    struct kndSet *set;
    int err;

    if (DEBUG_INST_LEVEL_TMP) {
        knd_log(".. select delta:  gt %zu  lt %zu  eq:%zu..",
                task->state_gt, task->state_lt, task->state_eq);
    }

    task->type = KND_SELECT_STATE;

    if (DEBUG_INST_LEVEL_TMP) {
        self->str(self);
    }

    if (task->state_gt  >= self->num_inst_states) goto JSON_state;
    //if (task->gte >= base->num_inst_states) goto JSON_state;

    if (task->state_lt && task->state_lt < task->state_gt) goto JSON_state;

    err = knd_set_new(mempool, &set);
    if (err) return make_gsl_err_external(err);
    set->mempool = mempool;

    err = knd_class_get_inst_updates(self,
                                     task->state_gt, task->state_lt,
                                     task->state_eq, set);
    if (err) return make_gsl_err_external(err);

    task->show_removed_objs = true;

    err = export_inst_set_JSON(self, set, out, task);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);

    JSON_state:
    err = out->writec(out, '{');
    if (err) return make_gsl_err_external(err);

    err = knd_export_class_inst_state_JSON(self);
    if (err) return make_gsl_err_external(err);

    err = out->writec(out, '}');
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_select_state(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = set_curr_state,
          .obj = task
        },
        { .is_selector = true,
          .name = "gt",
          .name_size = strlen("gt"),
          .parse = gsl_parse_size_t,
          .obj = &task->state_gt
        },
        { .is_selector = true,
          .name = "lt",
          .name_size = strlen("lt"),
          .parse = gsl_parse_size_t,
          .obj = &task->state_lt
        },
        { .is_selector = true,
          .name = "gte",
          .name_size = strlen("gte"),
          .parse = gsl_parse_size_t,
          .obj = &task->state_gte
        },
        { .is_selector = true,
          .name = "lte",
          .name_size = strlen("lte"),
          .parse = gsl_parse_size_t,
          .obj = &task->state_lte
        },
        { .is_default = true,
          .run = present_state,
          .obj = ctx
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int kndClassInst_validate_attr(struct kndClassInst *self,
                                      const char *name,
                                      size_t name_size,
                                      struct kndAttr **result,
                                      struct kndElem **result_elem)
{
    struct kndClass *conc;
    struct kndAttrRef *attr_ref;
    struct kndAttr *attr;
    struct kndElem *elem = NULL;
    struct glbOutput *log = self->base->entry->repo->log;
    int err, e;

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. \"%.*s\" to validate elem: \"%.*s\"",
                self->name_size, self->name, name_size, name);

    /* check existing elems */
    for (elem = self->elems; elem; elem = elem->next) {
        if (!memcmp(elem->attr->name, name, name_size)) {
            if (DEBUG_INST_LEVEL_2)
                knd_log("++ ELEM \"%.*s\" is already set!", name_size, name);
            *result_elem = elem;
            return knd_OK;
        }
    }

    conc = self->base;
    err = knd_class_get_attr(conc, name, name_size, &attr_ref);
    if (err) {
        knd_log("  -- \"%.*s\" attr is not approved :(", name_size, name);
        log->reset(log);
        e = log->write(log, name, name_size);
        if (e) return e;
        e = log->write(log, " attr not confirmed",
                       strlen(" attr not confirmed"));
        if (e) return e;
        return err;
    }

    attr = attr_ref->attr;
    if (DEBUG_INST_LEVEL_2) {
        const char *type_name = knd_attr_names[attr->type];
        knd_log("++ \"%.*s\" ELEM \"%s\" attr type: \"%s\"",
                name_size, name, attr->name, type_name);
    }

    *result = attr;
    return knd_OK;
}

static gsl_err_t parse_select_elem(void *obj,
                                   const char *name, size_t name_size,
                                   const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndClassInst *self = task->class_inst;
    struct kndClassInst *inst;
    struct kndElem *elem = NULL;
    struct kndAttr *attr = NULL;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. parsing elem \"%.*s\" select REC: %.*s",
                name_size, name, 128, rec);

    inst = self->curr_inst;
    if (self->parent) {
        inst = self;
    }

    if (!inst) {
        knd_log("-- no inst selected");
        return *total_size = 0, make_gsl_err(gsl_FAIL);
    }

    err = kndClassInst_validate_attr(inst, name, name_size, &attr, &elem);
    if (err) {
        knd_log("-- \"%.*s\" attr not validated", name_size, name);
        return *total_size = 0, make_gsl_err_external(err);
    }

    if (!elem) {
        knd_log("-- elem not set");
        return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }

    switch (elem->attr->type) {
        case KND_ATTR_INNER:
            parser_err = knd_select_class_inst(elem->inner, rec, total_size, task);
            if (parser_err.code) return parser_err;
            break;
        default:
            parser_err = knd_elem_parse_select(elem, rec, total_size);
            if (parser_err.code) return parser_err;
            break;
    }

    if (DEBUG_INST_LEVEL_2)
        knd_log("++ elem %.*s select parsing OK!",
                elem->attr->name_size, elem->attr->name);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t remove_inst(void *obj,
                             const char *unused_var(name),
                             size_t unused_var(name_size))
{
    struct LocalContext *ctx = obj;
    struct kndClassInst *self = ctx->class_inst;

    if (!self->curr_inst) {
        knd_log("-- remove operation: no class instance selected");
        return make_gsl_err(gsl_FAIL);
    }

    struct kndClass  *base       = self->base;
    struct kndClassInst *obj1 = self->curr_inst;
    struct kndState  *state;
    struct kndStateRef  *state_ref;
    struct kndRepo *repo = base->entry->repo;
    struct kndTask *task         = ctx->task;
    struct glbOutput *log        = task->log;
    struct kndMemPool *mempool   = task->mempool;
    int err;

    if (DEBUG_INST_LEVEL_2)
        knd_log("== class inst to remove: \"%.*s\"",
                obj1->name_size, obj1->name);

    err = knd_state_new(mempool, &state);
    if (err) return make_gsl_err_external(err);
    err = knd_state_ref_new(mempool, &state_ref);
    if (err) return make_gsl_err_external(err);
    state_ref->state = state;

    state->phase = KND_REMOVED;
    state->next = obj1->states;
    obj1->states = state;
    obj1->num_states++;
    state->numid = obj1->num_states;

    log->reset(log);
    err = log->write(log, self->name, self->name_size);
    if (err) return make_gsl_err_external(err);
    err = log->write(log, " obj removed", strlen(" obj removed"));
    if (err) return make_gsl_err_external(err);

    task->type = KND_UPDATE_STATE;

    state_ref->next = repo->curr_class_inst_state_refs;
    repo->curr_class_inst_state_refs = state_ref;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t present_inst_selection(void *obj, const char *unused_var(val),
                                        size_t unused_var(val_size))
{
    struct LocalContext *ctx = obj;
    struct kndClassInst *self = ctx->class_inst;
    struct kndClassInst *inst;
    struct kndClass *base = self->base;
    struct kndTask *task = ctx->task;
    struct glbOutput *out = task->out;
    struct kndSet *set;
    int err;

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. class \"%.*s\" (repo:%.*s) inst selection: task type:%d num sets:%zu",
                base->name_size, base->name,
                base->entry->repo->name_size, base->entry->repo->name,
                task->type, task->num_sets);

    out->reset(out);
    if (task->type == KND_SELECT_STATE) {
        /* no sets found? */
        if (!task->num_sets) {
            if (base->entry->inst_idx) {
                set = base->entry->inst_idx;

                task->show_removed_objs = false;

                err = export_inst_set_JSON(self->base, set, out, task);
                if (err) return make_gsl_err_external(err);
                return make_gsl_err(gsl_OK);
            }

            err = out->write(out, "{}", strlen("{}"));
            if (err) return make_gsl_err_external(err);
            return make_gsl_err(gsl_OK);
        }

        /* TODO: intersection cache lookup  */
        set = task->sets[0];

        /* intersection required */
        /*if (task->num_sets > 1) {
            err = knd_set_new(mempool, &set);
            if (err) return make_gsl_err_external(err);

            set->type = KND_SET_CLASS;
            set->mempool = mempool;
            set->base = task->sets[0]->base;

            err = set->intersect(set, task->sets, task->num_sets);
            if (err) return make_gsl_err_external(err);
            }*/

        if (!set->num_elems) {
            err = out->write(out, "{}", strlen("{}"));
            if (err) return make_gsl_err_external(err);
            return make_gsl_err(gsl_OK);
        }

        /* final presentation in JSON
           TODO: choose output format */
        task->show_removed_objs = false;
        err = export_inst_set_JSON(self->base, set, out, task);
        if (err) return make_gsl_err_external(err);

        return make_gsl_err(gsl_OK);
    }

    if (!self->curr_inst) {
        knd_log("-- no class inst selected :(");
        return make_gsl_err(gsl_FAIL);
    }

    inst = self->curr_inst;
    inst->max_depth = self->max_depth;

    err = inst->export(inst, KND_FORMAT_JSON, out);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static int update_elem_states(struct kndClassInst *self, struct kndTask *task)
{
    struct kndStateRef *ref;
    struct kndState *state;
    struct kndClassInst *inst;
    struct kndElem *elem;
    struct kndClass *c;
    struct kndMemPool *mempool = task->mempool;
    struct kndRepo *repo = task->repo;
    int err;

    if (DEBUG_INST_LEVEL_TMP)
        knd_log("\n++ \"%.*s\" class inst updates happened:",
                self->name_size, self->name);

    for (ref = self->elem_state_refs; ref; ref = ref->next) {
        state = ref->state;
        if (state->val) {
            elem = state->val->obj;
            knd_log("== elem %.*s updated!",
                    elem->attr->name_size, elem->attr->name);
        }
        if (state->children) {
            knd_log("== child elem updated: %zu", state->numid);
        }
    }

    err = knd_state_new(mempool, &state);
    if (err) {
        knd_log("-- class inst state alloc failed");
        return err;
    }
    err = knd_state_ref_new(mempool, &ref);
    if (err) {
        knd_log("-- state ref alloc failed :(");
        return err;
    }
    ref->state = state;

    state->phase = KND_UPDATED;
    self->num_states++;
    state->numid = self->num_states;
    state->children = self->elem_state_refs;
    self->elem_state_refs = NULL;
    self->states = state;

    /* inform your immediate parent or baseclass */
    if (self->parent) {
        elem = self->parent;
        inst = elem->parent;

        knd_log(".. inst \"%.*s\" to get new updates..",
                inst->name_size, inst->name);

        ref->next = inst->elem_state_refs;
        inst->elem_state_refs = ref;
    } else {
        c = self->base;

        knd_log("\n.. class \"%.*s\" (repo:%.*s) to get new updates..",
                c->name_size, c->name,
                c->entry->repo->name_size, c->entry->repo->name);
        ref->type = KND_STATE_CLASS_INST;
        ref->obj = (void*)self;

        ref->next = repo->curr_class_inst_state_refs;
        repo->curr_class_inst_state_refs = ref;
    }

    return knd_OK;
}

gsl_err_t knd_select_class_inst(struct kndClassInst *self,  // TODO(k15tfu): use kndClass *c
                                const char *rec, size_t *total_size,
                                struct kndTask *task)
{
    struct kndClassInst *inst;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. class instance parsing: \"%.*s\"", 64, rec);

    task->type = KND_SELECT_STATE;

    self->curr_inst = NULL;
    if (self->parent) {
        self->elem_state_refs = NULL;
    }

    struct LocalContext ctx = {
        .task = task,
        .class_inst = self,
    };
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_inst,
          .obj = &ctx
        },
        { .name = "_state",
          .name_size = strlen("_state"),
          .parse = parse_select_state,
          .obj = &ctx
        },
        { .is_validator = true,
          .validate = parse_select_elem,
          .obj = task
        }/*,
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = parse_import_elem,
          .obj = self->
          }*/,
        { .is_selector = true,
          .name = "_depth",
          .name_size = strlen("_depth"),
          .parse = gsl_parse_size_t,
          .obj = &self->max_depth
        },
        { .name = "_rm",
          .name_size = strlen("_rm"),
          .run = remove_inst,
          .obj = &ctx
        },
        { .type = GSL_SET_STATE,
          .name = "_rm",
          .name_size = strlen("_rm"),
          .run = remove_inst,
          .obj = &ctx
        },
        { .is_default = true,
          .run = present_inst_selection,
          .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        struct glbOutput *log = task->log;
        knd_log("-- obj select parse error: \"%.*s\"",
                log->buf_size, log->buf);
        if (!log->buf_size) {
            err = log->write(log, "class instance selection failure",
                             strlen("class instance selection failure"));
            if (err) return make_gsl_err_external(err);
        }
        return parser_err;
    }

    /* check elem updates */
    inst = self->curr_inst;
    if (self->parent) inst = self;
    if (!inst) return make_gsl_err(gsl_OK);

    if (inst->elem_state_refs) {
        err = update_elem_states(inst, task);
        if (err) return make_gsl_err_external(err);
    }

    return make_gsl_err(gsl_OK);
}


//

static int export_inner_JSON(struct kndClassInst *self,
                             struct glbOutput *out)
{
    struct kndElem *elem;
    int err;

    err = out->writec(out, '{');                                                  RET_ERR();

    elem = self->elems;
    while (elem) {
        err = knd_elem_export(elem, KND_FORMAT_JSON, out);
        if (err) {
            knd_log("-- inst elem export failed: %.*s",
                    elem->attr->name_size, elem->attr->name);
            return err;
        }
        if (elem->next) {
            err = out->write(out, ",", 1);
            if (err) return err;
        }
        elem = elem->next;
    }

    err = out->writec(out, '}');   RET_ERR();

    return knd_OK;
}

static int export_concise_JSON(struct kndClassInst *self,
                               struct glbOutput *out)
{
    struct kndClassInst *obj;
    struct kndElem *elem;
    bool is_concise = true;
    int err;

    err = out->write(out, ",\"_class\":\"", strlen(",\"_class\":\""));
    if (err) return err;

    err = out->write(out, self->base->name, self->base->name_size);
    if (err) return err;

    err = out->write(out, "\"", 1);
    if (err) return err;

    for (elem = self->elems; elem; elem = elem->next) {
        /* NB: restricted attr */
        if (elem->attr->access_type == KND_ATTR_ACCESS_RESTRICTED)
            continue;

        if (DEBUG_INST_LEVEL_3)
            knd_log(".. export elem: %.*s",
                    elem->attr->name_size, elem->attr->name);

        /* filter out detailed presentation */
        if (is_concise) {
            /* inner obj? */
            if (elem->inner) {
                obj = elem->inner;

                /*if (need_separ) {*/
                err = out->write(out, ",", 1);
                if (err) return err;

                err = out->write(out, "\"", 1);
                if (err) return err;
                err = out->write(out,
                                 elem->attr->name,
                                 elem->attr->name_size);
                if (err) return err;
                err = out->write(out, "\":", 2);
                if (err) return err;

                err = obj->export(obj, KND_FORMAT_JSON, out);
                if (err) return err;

                //need_separ = true;
                continue;
            }

            if (elem->attr)
                if (elem->attr->concise_level)
                    goto export_elem;

            if (DEBUG_INST_LEVEL_2)
                knd_log("  .. skip JSON elem: %s..\n", elem->attr->name);
            continue;
        }

        export_elem:
        /*if (need_separ) {*/
        err = out->write(out, ",", 1);
        if (err) return err;

        /* default export */
        err = knd_elem_export(elem, KND_FORMAT_JSON, out);
        if (err) {
            knd_log("-- elem not exported: %s", elem->attr->name);
            return err;
        }
        //need_separ = true;
    }
    return knd_OK;
}

static int export_rel_inst_JSON(void *obj,
                                const char *unused_var(elem_id),
                                size_t unused_var(elem_id_size),
                                size_t count,
                                void *elem)
{
    struct glbOutput *out = obj;
    struct kndRelInstance *inst = elem;
    struct kndRel *rel = inst->rel;
    int err;

    /* separator */
    if (count) {
        err = out->writec(out, ',');                                          RET_ERR();
    }
    rel->out = out;
    rel->format = KND_FORMAT_JSON;
    err = rel->export_inst(rel, inst);
    if (err) return err;

    return knd_OK;
}

static int export_inst_relref_JSON(struct kndClassInst *self,
                                   struct kndRelRef *relref)
{
    struct glbOutput *out = self->base->entry->repo->out;
    struct kndRel *rel = relref->rel;
    struct kndSet *set;
    int err;

    err = out->write(out, "{\"_name\":\"", strlen("{\"_name\":\""));              RET_ERR();
    err = out->write(out, rel->entry->name, rel->entry->name_size);               RET_ERR();
    err = out->write(out, "\"", 1);                                               RET_ERR();

    /* show the latest state */
    if (relref->num_states) {
        err = out->write(out, ",\"_state\":", strlen(",\"_state\":"));            RET_ERR();
        err = out->writef(out, "%zu", relref->num_states);                        RET_ERR();

        /*update = relref->states->update;
        time(&update->timestamp);
        localtime_r(&update->timestamp, &tm_info);
        buf_size = strftime(buf, KND_NAME_SIZE,
                            ",\"_modif\":\"%Y-%m-%d %H:%M:%S\"", &tm_info);
        err = out->write(out, buf, buf_size);                                     RET_ERR();
        */
    }

    set = relref->idx;
    err = out->write(out, ",\"num_instances\":",
                     strlen(",\"num_instances\":"));                              RET_ERR();
    err = out->writef(out, "%zu", relref->num_insts);                             RET_ERR();

    if (self->max_depth) {
        err = out->write(out, ",\"instances\":",
                         strlen(",\"instances\":"));                              RET_ERR();
        err = out->writec(out, '[');                                              RET_ERR();
        err = set->map(set, export_rel_inst_JSON, (void*)out);
        if (err) return err;
        err = out->writec(out, ']');                                              RET_ERR();
    }
    err = out->writec(out, '}');                                                  RET_ERR();
    return knd_OK;
}

static int export_inst_relrefs_JSON(struct kndClassInst *self)
{
    struct kndRelRef *relref;
    int err;

    for (relref = self->entry->rels; relref; relref = relref->next) {
        if (!relref->idx) continue;
        err = export_inst_relref_JSON(self, relref);                              RET_ERR();
    }
    return knd_OK;
}

static int export_JSON(struct kndClassInst *self,
                       struct glbOutput *out)
{
    struct kndElem *elem;
    struct kndClassInst *obj;
    struct kndState *state = self->states;
    bool is_concise = true;
    int err;

    if (DEBUG_INST_LEVEL_2) {
        knd_log(".. JSON export class inst \"%.*s\"",
                self->name_size, self->name);
        if (self->base) {
            knd_log("   (class: %.*s)",
                    self->base->name_size, self->base->name);
        }
    }

    if (self->type == KND_OBJ_INNER) {
        err = export_inner_JSON(self, out);
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

    if (self->depth >= self->max_depth) {
        /* any concise fields? */
        err = export_concise_JSON(self, out);                                     RET_ERR();
        goto final;
    }

    err = out->write(out, ",\"_class\":\"", strlen(",\"_class\":\""));
    if (err) return err;

    err = out->write(out, self->base->name, self->base->name_size);
    if (err) return err;

    err = out->write(out, "\"", 1);
    if (err) return err;

    /* TODO: id */

    for (elem = self->elems; elem; elem = elem->next) {
        /* NB: restricted attr */
        if (elem->attr->access_type == KND_ATTR_ACCESS_RESTRICTED)
            continue;

        if (DEBUG_INST_LEVEL_3)
            knd_log(".. export elem: %.*s",
                    elem->attr->name_size, elem->attr->name);

        /* filter out detailed presentation */
        if (is_concise) {
            /* inner obj? */
            if (elem->inner) {
                obj = elem->inner;

                /*if (need_separ) {*/
                err = out->write(out, ",", 1);
                if (err) return err;

                err = out->write(out, "\"", 1);
                if (err) return err;
                err = out->write(out,
                                 elem->attr->name,
                                 elem->attr->name_size);
                if (err) return err;
                err = out->write(out, "\":", 2);
                if (err) return err;

                err = obj->export(obj, KND_FORMAT_JSON, out);
                if (err) return err;

                //need_separ = true;
                continue;
            }

            if (elem->attr)
                if (elem->attr->concise_level)
                    goto export_elem;

            if (DEBUG_INST_LEVEL_2)
                knd_log("  .. skip JSON elem: %s..\n", elem->attr->name);
            continue;
        }

        export_elem:
        /*if (need_separ) {*/
        err = out->write(out, ",", 1);
        if (err) return err;

        /* default export */
        err = knd_elem_export(elem, KND_FORMAT_JSON, out);
        if (err) {
            knd_log("-- elem not exported: %s", elem->attr->name);
            return err;
        }
        //need_separ = true;
    }

    if (self->entry->rels) {
        err = out->write(out, ",\"_rel\":", strlen(",\"_rel\":"));                RET_ERR();
        err = out->writec(out, '[');                                              RET_ERR();
        err = export_inst_relrefs_JSON(self);                                     RET_ERR();
        err = out->writec(out, ']');                                              RET_ERR();
    }

    final:
    err = out->write(out, "}", 1);
    if (err) return err;

    return err;
}

static int export_inner_GSP(struct kndClassInst *self,
                            struct glbOutput *out)
{
    struct kndElem *elem;
    int err;

    /* anonymous obj */
    //err = out->writec(out, '{');   RET_ERR();

    elem = self->elems;
    while (elem) {
        err = knd_elem_export(elem, KND_FORMAT_GSP, out);
        if (err) {
            knd_log("-- inst elem GSP export failed: %.*s",
                    elem->attr->name_size, elem->attr->name);
            return err;
        }
        elem = elem->next;
    }
    //err = out->writec(out, '}');   RET_ERR();

    return knd_OK;
}

static int export_GSP(struct kndClassInst *self,
                      struct glbOutput *out)
{
    struct kndElem *elem;
    int err;

    if (self->type == KND_OBJ_INNER) {
        err = export_inner_GSP(self, out);
        if (err) {
            knd_log("-- inner obj GSP export failed");
            return err;
        }
        return knd_OK;
    }

    /* elems */
    for (elem = self->elems; elem; elem = elem->next) {
        err = knd_elem_export(elem, KND_FORMAT_GSP, out);
        if (err) {
            knd_log("-- export of \"%s\" elem failed: %d :(",
                    elem->attr->name, err);
            return err;
        }
    }

    return knd_OK;
}

static int kndClassInst_export(struct kndClassInst *self,
                               knd_format format,
                               struct glbOutput *out)
{
    int err;
    switch (format) {
        case KND_FORMAT_JSON:
            err = export_JSON(self, out);
            if (err) return err;
            break;
        case KND_FORMAT_GSP:
            err = export_GSP(self, out);
            if (err) return err;
            break;
        default:
            break;
    }

    return knd_OK;
}

static int export_inst_JSON(void *obj,
                            const char *unused_var(elem_id),
                            size_t unused_var(elem_id_size),
                            size_t count,
                            void *elem)
{
    struct kndTask *task = obj;
    struct kndClass *self = task->class;
    if (count < task->start_from) return knd_OK;
    if (task->batch_size >= task->batch_max) return knd_RANGE;
    struct glbOutput *out = self->entry->repo->out;
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
    inst->depth = 0;
    inst->max_depth = 0;
    if (self->max_depth) {
        inst->max_depth = self->max_depth;
    }
    err = kndClassInst_export(inst, KND_FORMAT_JSON, out);                        RET_ERR();
    task->batch_size++;

    return knd_OK;
}

static int export_inst_set_JSON(struct kndClass *self,
                                struct kndSet *set,
                                struct glbOutput *out,
                                struct kndTask *task)
{
    int err;

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
    task->class = self; // FIXME(k15tfu): pass class to export_inst_JSON
    err = set->map(set, export_inst_JSON, (void*)task);
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
