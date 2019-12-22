#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_class_inst.h"
#include "knd_class.h"
#include "knd_mempool.h"
#include "knd_attr.h"
#include "knd_attr_inst.h"
#include "knd_repo.h"

#include "knd_text.h"
#include "knd_num.h"
#include "knd_rel.h"
#include "knd_set.h"
#include "knd_rel_arg.h"

#include "knd_user.h"
#include "knd_state.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_INST_LEVEL_1 0
#define DEBUG_INST_LEVEL_2 0
#define DEBUG_INST_LEVEL_3 0
#define DEBUG_INST_LEVEL_4 0
#define DEBUG_INST_LEVEL_TMP 1

struct LocalContext {
    struct kndClassEntry *class_entry;
    struct kndClassInst *class_inst;
    struct kndTask *task;
};

static gsl_err_t run_get_inst(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClassEntry *entry = ctx->class_entry;
    struct kndClassInst *inst;
    int err;

    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_get_class_inst(entry, name, name_size, task, &inst);
    if (err) {
        KND_TASK_LOG("failed to get class inst: %.*s", name_size, name);
        return make_gsl_err_external(err);
    }

    /* to return a single object */
    task->type = KND_GET_STATE;
    inst->attr_inst_state_refs = NULL;

    if (DEBUG_INST_LEVEL_2) {
        knd_class_inst_str(inst, 0);
    }
    ctx->class_inst = inst;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t
parse_get_inst_by_numid(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndSet *inst_idx = ctx->class_entry->inst_idx;
    struct kndClassInstEntry *entry;
    struct kndClassInst *inst;
    int err;
    size_t numid;
    void *result;
    gsl_err_t parser_err = gsl_parse_size_t(&numid, rec, total_size);
    if (parser_err.code) return parser_err;

    char id[KND_ID_SIZE];
    size_t id_size;
    knd_uid_create(numid, id, &id_size);

    if (DEBUG_INST_LEVEL_TMP)
        knd_log("ID: %zu => \"%.*s\" [size: %zu]",
                numid, (int)id_size, id, id_size);

    err = inst_idx->get(inst_idx, id, id_size, &result);
    if (err) {
        knd_log("-- no such instance: %.*s", id_size, id);
        return make_gsl_err_external(err);
    }

    entry = result;
    inst = entry->inst;
    task->type = KND_GET_STATE;
    inst->attr_inst_state_refs = NULL;

    if (DEBUG_INST_LEVEL_TMP) {
        knd_class_inst_str(inst, 0);
    }
    ctx->class_inst = inst;

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
    struct kndTask *task = ctx->task;
    struct kndOutput *out = task->out;
    struct kndMemPool *mempool = task->mempool;
    struct kndSet *set;
    int err;

    if (DEBUG_INST_LEVEL_TMP) {
        knd_log(".. select delta:  gt %zu  lt %zu  eq:%zu..",
                task->state_gt, task->state_lt, task->state_eq);
    }

    task->type = KND_SELECT_STATE;

    if (task->state_gt  >= ctx->class_entry->num_inst_states) goto JSON_state;
    //if (task->gte >= base->num_inst_states) goto JSON_state;

    if (task->state_lt && task->state_lt < task->state_gt) goto JSON_state;

    err = knd_set_new(mempool, &set);
    if (err) return make_gsl_err_external(err);
    set->mempool = mempool;

    err = knd_class_get_inst_updates(ctx->class_entry,
                                     task->state_gt, task->state_lt,
                                     task->state_eq, set);
    if (err) return make_gsl_err_external(err);

    task->show_removed_objs = true;

    err = knd_class_inst_set_export_JSON(set, task);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);

    JSON_state:
    out->reset(out);
    err = out->writec(out, '{');
    if (err) return make_gsl_err_external(err);

    err = knd_export_class_inst_state_JSON(ctx->class_entry, task);
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

static int validate_attr(struct kndClassInst *self,
                         const char *name,
                         size_t name_size,
                         struct kndAttr **result,
                         struct kndAttrInst **result_attr_inst)
{
    struct kndClass *conc;
    struct kndAttrRef *attr_ref;
    struct kndAttr *attr;
    struct kndAttrInst *attr_inst = NULL;
    //struct kndOutput *log = self->blueprint->entry->repo->log;
    int err;

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. \"%.*s\" to validate attr_inst: \"%.*s\"",
                self->name_size, self->name, name_size, name);

    /* check existing attr_insts */
    for (attr_inst = self->attr_insts; attr_inst; attr_inst = attr_inst->next) {
        if (!memcmp(attr_inst->attr->name, name, name_size)) {
            if (DEBUG_INST_LEVEL_2)
                knd_log("++ ATTR_INST \"%.*s\" is already set!", name_size, name);
            *result_attr_inst = attr_inst;
            return knd_OK;
        }
    }

    conc = self->blueprint;
    err = knd_class_get_attr(conc, name, name_size, &attr_ref);
    if (err) {
        knd_log("  -- \"%.*s\" attr is not approved :(", name_size, name);
        //log->reset(log);
        /*e = log->write(log, name, name_size);
        if (e) return e;
        e = log->write(log, " attr not confirmed",
                       strlen(" attr not confirmed"));
                       if (e) return e; */
        return err;
    }

    attr = attr_ref->attr;
    if (DEBUG_INST_LEVEL_2) {
        const char *type_name = knd_attr_names[attr->type];
        knd_log("++ \"%.*s\" ATTR_INST \"%s\" attr type: \"%s\"",
                name_size, name, attr->name, type_name);
    }

    *result = attr;
    return knd_OK;
}

static gsl_err_t parse_select_attr_inst(void *obj,
                                   const char *name, size_t name_size,
                                   const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClassInst *inst = ctx->class_inst;
    struct kndAttrInst *attr_inst = NULL;
    struct kndAttr *attr = NULL;
    int err;
    gsl_err_t parser_err;

    if (!inst) {
        knd_log("-- no inst selected");
        return *total_size = 0, make_gsl_err(gsl_FAIL);
    }

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. parsing attr_inst \"%.*s\" select REC: %.*s",
                name_size, name, 128, rec);

    err = validate_attr(inst, name, name_size, &attr, &attr_inst);
    if (err) {
        knd_log("-- \"%.*s\" attr not validated", name_size, name);
        return *total_size = 0, make_gsl_err_external(err);
    }

    if (!attr_inst) {
        knd_log("-- attr_inst not set");
        return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }

    switch (attr_inst->attr->type) {
    case KND_ATTR_INNER:
        parser_err = knd_select_class_inst(attr_inst->inner->blueprint->entry,
                                           rec, total_size, task);
        if (parser_err.code) return parser_err;
        break;
        /*case KND_ATTR_TEXT:
        parser_err = knd_text_parse_select(attr_inst, rec, total_size, task);
        if (parser_err.code) return parser_err;
        break;*/
    default:
        parser_err = knd_attr_inst_parse_select(attr_inst, rec, total_size, task);
        if (parser_err.code) return parser_err;
    }

    if (DEBUG_INST_LEVEL_2)
        knd_log("++ attr_inst %.*s select parsing OK!",
                attr_inst->attr->name_size, attr_inst->attr->name);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t remove_inst(void *obj,
                             const char *unused_var(name),
                             size_t unused_var(name_size))
{
    struct LocalContext *ctx = obj;
    struct kndClassInst *self = ctx->class_inst;

    if (!self) {
        knd_log("-- remove operation: no class instance selected");
        return make_gsl_err(gsl_FAIL);
    }

    struct kndState  *state;
    struct kndStateRef  *state_ref;
    struct kndTask *task         = ctx->task;
    struct kndOutput *log        = task->log;
    struct kndMemPool *mempool   = task->mempool;
    int err;

    if (DEBUG_INST_LEVEL_TMP)
        knd_log("== class inst to be deleted: \"%.*s\"",
                self->name_size, self->name);

    err = knd_state_new(mempool, &state);
    if (err) return make_gsl_err_external(err);
    err = knd_state_ref_new(mempool, &state_ref);
    if (err) return make_gsl_err_external(err);
    state_ref->state = state;

    state->phase = KND_REMOVED;
    state->next = self->states;
    self->states = state;
    self->num_states++;
    state->numid = self->num_states;

    log->reset(log);
    err = log->write(log, self->name, self->name_size);
    if (err) return make_gsl_err_external(err);
    err = log->write(log, " class inst removed", strlen(" class inst removed"));
    if (err) return make_gsl_err_external(err);

    task->type = KND_COMMIT_STATE;

    // TODO state_ref->next = task->class_inst_state_refs;
    //task->class_inst_state_refs = state_ref;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t present_inst_selection(void *obj, const char *unused_var(val),
                                        size_t unused_var(val_size))
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndOutput *out = task->out;
    struct kndSet *set;
    struct kndClassEntry *entry = ctx->class_entry;
    int err;

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. presenting inst selection of class \"%.*s\"",
                entry->name_size, entry->name);

    out->reset(out);
    if (task->type == KND_SELECT_STATE) {
        /* no sets found? */
        if (!task->num_sets) {
            if (entry->inst_idx) {
                set = entry->inst_idx;

                task->show_removed_objs = false;

                err = knd_class_inst_set_export_JSON(set, task);
                if (err) return make_gsl_err_external(err);
                return make_gsl_err(gsl_OK);
            }

            // TODO
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

    if (!ctx->class_inst) {
        KND_TASK_LOG("no class inst selected");
        return make_gsl_err(gsl_FAIL);
    }

    err = knd_class_inst_export(ctx->class_inst, task->ctx->format, false, task);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

/*
static int update_attr_inst_states(struct kndClassInst *self, struct kndTask *task)
{
    struct kndStateRef *ref;
    struct kndState *state;
    struct kndClassInst *inst;
    struct kndAttrInst *attr_inst;
    struct kndClass *c;
    struct kndMemPool *mempool = task->mempool;
    int err;

    if (DEBUG_INST_LEVEL_TMP)
        knd_log("\n++ \"%.*s\" class inst updates happened:",
                self->name_size, self->name);

    for (ref = self->attr_inst_state_refs; ref; ref = ref->next) {
        state = ref->state;
        if (state->val) {
            attr_inst = state->val->obj;
            knd_log("== attr_inst %.*s updated!",
                    attr_inst->attr->name_size, attr_inst->attr->name);
        }
        if (state->children) {
            knd_log("== child attr_inst updated: %zu", state->numid);
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
    state->children = self->attr_inst_state_refs;
    self->attr_inst_state_refs = NULL;
    self->states = state;

    // inform your immediate parent or baseclass
    if (self->parent) {
        attr_inst = self->parent;
        inst = attr_inst->parent;

        knd_log(".. inst \"%.*s\" to get new updates..",
                inst->name_size, inst->name);

        ref->next = inst->attr_inst_state_refs;
        inst->attr_inst_state_refs = ref;
    } else {
        c = self->base;

        knd_log("\n.. class \"%.*s\" (repo:%.*s) to get new updates..",
                c->name_size, c->name,
                c->entry->repo->name_size, c->entry->repo->name);
        ref->type = KND_STATE_CLASS_INST;
        ref->obj = (void*)self;

        ref->next = task->class_inst_state_refs;
        task->class_inst_state_refs = ref;
    }

    return knd_OK;
}
*/

gsl_err_t knd_select_class_inst(struct kndClassEntry *c,
                                const char *rec, size_t *total_size,
                                struct kndTask *task)
{
    gsl_err_t parser_err;

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. class instance parsing: \"%.*s\"", 64, rec);

    task->type = KND_SELECT_STATE;

    struct LocalContext ctx = {
        .task = task,
        .class_entry = c,
        .class_inst = NULL
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_inst,
          .obj = &ctx
        },
        { .is_selector = true,
          .name = "_id",
          .name_size = strlen("_id"),
          .parse = parse_get_inst_by_numid,
          .obj = &ctx
        },
        { .name = "_state",
          .name_size = strlen("_state"),
          .parse = parse_select_state,
          .obj = &ctx
        },
        { .validate = parse_select_attr_inst,
          .obj = &ctx
        }/*,
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = parse_import_attr_inst,
          .obj = self->
          }*/,
        { .is_selector = true,
          .name = "_depth",
          .name_size = strlen("_depth"),
          .parse = gsl_parse_size_t,
          .obj = &task->max_depth
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
        KND_TASK_LOG("class instance selection failure");
        return parser_err;
    }

    /* check attr_inst updates */
    /*inst = ctx.class_inst;
    if (!inst) return make_gsl_err(gsl_OK);

    if (inst->attr_inst_state_refs) {
        err = update_attr_inst_states(inst, task);
        if (err) return make_gsl_err_external(err);
    }
    */
    return make_gsl_err(gsl_OK);
}
