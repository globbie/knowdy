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

    err = knd_class_inst_set_export_JSON(set, task);
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

                err = knd_class_inst_set_export_JSON(set, task);
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
        err = knd_class_inst_set_export_JSON(set, task);
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
