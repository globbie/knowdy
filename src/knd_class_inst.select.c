#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_class_inst.h"
#include "knd_class.h"
#include "knd_mempool.h"
#include "knd_attr.h"
#include "knd_repo.h"

#include "knd_text.h"
#include "knd_num.h"
#include "knd_rel.h"
#include "knd_shared_set.h"
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
    struct kndClassEntry *entry;
    struct kndClass *class;
    struct kndClassInst *inst;
    struct kndTask *task;
};

static gsl_err_t run_get_inst(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClass *c = ctx->class;
    struct kndClassInst *inst;
    int err;

    assert(c != NULL);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_get_class_inst(c, name, name_size, task, &inst);
    if (err) {
        KND_TASK_LOG("failed to get class inst: %.*s", name_size, name);
        return make_gsl_err_external(err);
    }
    /* to return a single object */
    task->type = KND_GET_STATE;

    if (DEBUG_INST_LEVEL_2)
        knd_class_inst_str(inst, 0);

    ctx->inst = inst;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_get_inst_by_numid(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndSharedSet *inst_idx = atomic_load_explicit(&ctx->class->inst_idx, memory_order_acquire);
    struct kndClassInstEntry *entry;
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;
    int err;

    assert(inst_idx != NULL);

    gsl_err_t parser_err = gsl_parse_size_t(&numid, rec, total_size);
    if (parser_err.code) return parser_err;
    knd_uid_create(numid, id, &id_size);

    if (DEBUG_INST_LEVEL_TMP)
        knd_log("class inst id: %zu => \"%.*s\" [size: %zu]", numid, (int)id_size, id, id_size);

    err = knd_shared_set_get(inst_idx, id, id_size, (void**)&entry);
    if (err) {
        KND_TASK_LOG("failed to open class inst \"%.*s\"", id_size, id);
        return make_gsl_err_external(err);
    }
    task->type = KND_GET_STATE;

    if (DEBUG_INST_LEVEL_TMP) {
        knd_class_inst_str(entry->inst, 0);
    }
    ctx->inst = entry->inst;

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

static gsl_err_t present_state(void *obj, const char *unused_var(name), size_t unused_var(name_size))
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

    if (task->state_gt  >= ctx->class->num_inst_states) goto JSON_state;
    //if (task->gte >= base->num_inst_states) goto JSON_state;

    if (task->state_lt && task->state_lt < task->state_gt) goto JSON_state;

    err = knd_set_new(mempool, &set);
    if (err) return make_gsl_err_external(err);
    set->mempool = mempool;

    err = knd_class_get_inst_updates(ctx->class, task->state_gt, task->state_lt, task->state_eq, set);
    if (err) return make_gsl_err_external(err);

    task->show_removed_objs = true;

    //err = knd_class_inst_set_export_JSON(set, task);
    //if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);

    JSON_state:
    out->reset(out);
    err = out->writec(out, '{');
    if (err) return make_gsl_err_external(err);

    err = knd_export_class_inst_state_JSON(ctx->class, task);
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

static gsl_err_t remove_inst(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct LocalContext *ctx = obj;
    struct kndClassInst *self = ctx->inst;

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

static gsl_err_t present_inst_selection(void *obj, const char *unused_var(val), size_t unused_var(val_size))
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndOutput *out = task->out;
    struct kndSet *set;
    struct kndClass *c = ctx->class;
    int err;

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. presenting inst selection of class \"%.*s\"", c->name_size, c->name);

    out->reset(out);
    if (task->type == KND_SELECT_STATE) {
        /* no sets found? */
        if (!task->num_sets) {
            /*if (entry->inst_idx) {
                set = entry->inst_idx;

                task->show_removed_objs = false;

                err = knd_class_inst_set_export_JSON(set, task);
                if (err) return make_gsl_err_external(err);
                return make_gsl_err(gsl_OK);
            }
            */
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
        //err = knd_class_inst_set_export_JSON(set, task);
        //if (err) return make_gsl_err_external(err);

        return make_gsl_err(gsl_OK);
    }

    /*  KND_GET_STATE */
    if (!ctx->inst) {
        KND_TASK_LOG("no class inst selected");
        return make_gsl_err(gsl_FAIL);
    }

    if (task->ctx->max_depth == 0) task->ctx->max_depth = 1;

    err = knd_class_inst_export(ctx->inst, task->ctx->format, false, task);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_select_class_inst(struct kndClass *c, const char *rec, size_t *total_size, struct kndTask *task)
{
    gsl_err_t parser_err;

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. class instance parsing: \"%.*s\"", 64, rec);

    task->type = KND_SELECT_STATE;
    struct LocalContext ctx = {
        .task = task,
        .class = c,
        .inst = NULL
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
