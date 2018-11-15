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
#include "knd_mempool.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>
#include <glb-lib/output.h>

#define DEBUG_CLASS_SELECT_LEVEL_1 0
#define DEBUG_CLASS_SELECT_LEVEL_2 0
#define DEBUG_CLASS_SELECT_LEVEL_3 0
#define DEBUG_CLASS_SELECT_LEVEL_4 0
#define DEBUG_CLASS_SELECT_LEVEL_5 0
#define DEBUG_CLASS_SELECT_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
    //struct kndClass *selected_class;

    struct kndClass *class;
    struct kndAttrRef *attr_ref;
    struct kndAttr *attr;
};

static gsl_err_t run_get_class(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = ctx->repo;
    struct kndClass *c;
    int err;

    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    /* check root name */
    if (name_size == 1 && *name == '/') {
        ctx->class = repo->root_class;
        if (repo->base)
            ctx->class = repo->base->root_class;
        return make_gsl_err(gsl_OK);
    }

    err = knd_get_class(repo, name, name_size, &c, ctx->task);
    if (err) return make_gsl_err_external(err);

    ctx->task->class = c;
    ctx->class = c;

    task->inner_class_state_refs = NULL;
    task->class_inst_state_refs = NULL;

    if (DEBUG_CLASS_SELECT_LEVEL_TMP) {
        c->str(c, 1);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_get_class_by_numid(void *obj, const char *id, size_t id_size)
{
    struct LocalContext *ctx = obj;
    struct kndRepo *repo = ctx->repo;
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct kndTask *task = ctx->task;
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndSet *class_idx = repo->class_idx;
    void *result;
    long numval;
    int err;

    if (id_size >= KND_NAME_SIZE)
        return make_gsl_err(gsl_FAIL);

    memcpy(buf, id, id_size);
    buf[id_size] = '0';

    err = knd_parse_num((const char*)buf, &numval);
    if (err) return make_gsl_err_external(err);

    if (numval <= 0) return make_gsl_err(gsl_FAIL);

    buf_size = 0;

    knd_uid_create((size_t)numval, buf, &buf_size);

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        knd_log("ID: %zu => \"%.*s\" [size: %zu]",
                numval, buf_size, buf, buf_size);

    err = class_idx->get(class_idx, buf, buf_size, &result);
    if (err) return make_gsl_err(gsl_FAIL);
    entry = result;

    err = knd_get_class(repo, entry->name, entry->name_size, &c, task);
    if (err) return make_gsl_err_external(err);

    // TODO
    task->class = c;
    ctx->class = c;

    if (DEBUG_CLASS_SELECT_LEVEL_2) {
        c->str(c, 1);
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

    if (!val_size) return make_gsl_err_external(knd_FAIL);
    if (val_size >= KND_NAME_SIZE) return make_gsl_err_external(knd_LIMIT);

    memcpy(buf, val, val_size);
    buf_size = val_size;
    buf[buf_size] = '\0';

    err = knd_parse_num(buf, &numval);
    if (err) return make_gsl_err_external(err);

    // TODO: check integer

    task->state_eq = (size_t)numval;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t present_desc_state(void *obj,
                                    const char *unused_var(name),
                                    size_t unused_var(name_size))
{
    struct kndTask *task = obj;
    struct kndClass *self = task->class;
    struct glbOutput *out = task->out;
    struct kndMemPool *mempool = task->mempool;
    struct kndSet *set;
    struct kndState *latest_state;
    int err;

    task->type = KND_SELECT_STATE;

    if (!self->desc_states)                                goto show_curr_state;
    latest_state = self->desc_states;
    if (task->state_gt >= latest_state->numid)             goto show_curr_state;
    if (task->state_lt && task->state_lt < task->state_gt) goto show_curr_state;

    if (DEBUG_CLASS_SELECT_LEVEL_TMP) {
        knd_log(".. select class descendants update delta:  gt %zu  lt %zu  eq:%zu..",
                task->state_gt, task->state_lt, task->state_eq);
    }

    err = knd_set_new(mempool, &set);
    if (err) return make_gsl_err_external(err);
    set->mempool = mempool;

    err = knd_class_get_desc_updates(self,
                                     task->state_gt, task->state_lt,
                                     task->state_eq, set);
    if (err) return make_gsl_err_external(err);
    task->show_removed_objs = true;

    err =  knd_class_set_export_JSON(set, task);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);

    show_curr_state:
    err = out->writec(out, '{');
    if (err) return make_gsl_err_external(err);

    err = knd_export_class_state_JSON(self, task);
    if (err) return make_gsl_err_external(err);

    err = out->writec(out, '}');
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_select_class_desc_state(void *obj, const char *rec,
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
        { .name = "gt",
          .name_size = strlen("gt"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_gt
        },
        { .name = "gte",
          .name_size = strlen("gte"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_gte
        },
        { .name = "lt",
          .name_size = strlen("lt"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_lt
        },
        { .name = "lte",
          .name_size = strlen("lte"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_lte
        },
        { .is_default = true,
          .run = present_desc_state,
          .obj = task
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t present_state(void *obj,
                               const char *unused_var(name),
                               size_t unused_var(name_size))
{
    struct kndTask *task = obj;
    struct kndClass *self = task->class;
    struct glbOutput *out = task->out;
    struct kndMemPool *mempool = task->mempool;
    struct kndSet *set;
    struct kndState *latest_state;
    int err;

    task->type = KND_SELECT_STATE;

    if (!self->states)                                     goto show_curr_state;
    latest_state = self->states;
    if (task->state_gt >= latest_state->numid)             goto show_curr_state;
    if (task->state_lt && task->state_lt < task->state_gt) goto show_curr_state;

    if (DEBUG_CLASS_SELECT_LEVEL_TMP) {
        knd_log(".. select class delta:  gt %zu  lt %zu  eq:%zu..",
                task->state_gt, task->state_lt, task->state_eq);
    }

    err = knd_set_new(mempool, &set);
    if (err) return make_gsl_err_external(err);
    set->mempool = mempool;

    err = knd_class_get_updates(self,
                                task->state_gt, task->state_lt,
                                task->state_eq, set);
    if (err) return make_gsl_err_external(err);
    task->show_removed_objs = true;

    err =  knd_class_set_export_JSON(set, task);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);

    show_curr_state:
    err = out->writec(out, '{');
    if (err) return make_gsl_err_external(err);

    err = knd_export_class_state_JSON(self, task);
    if (err) return make_gsl_err_external(err);

    err = out->writec(out, '}');
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_select_class_state(void *obj,
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
        { .name = "gt",
          .name_size = strlen("gt"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_gt
        },
        { .name = "gte",
          .name_size = strlen("gte"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_gte
        },
        { .name = "lt",
          .name_size = strlen("lt"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_lt
        },
        { .name = "lte",
          .name_size = strlen("lte"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_lte
        },
        { .name = "desc",
          .name_size = strlen("desc"),
          .parse = parse_select_class_desc_state,
          .obj = ctx
        },
        { .is_default = true,
          .run = present_state,
          .obj = task
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t present_class_selection(void *obj,
                                         const char *unused_var(val),
                                         size_t unused_var(val_size))
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct glbOutput *out = task->out;
    struct kndMemPool *mempool = task->mempool;
    struct kndClass *c = ctx->class;
    struct kndSet *set;
    int err;

    if (task->curr_locale_size) {
        task->locale = task->curr_locale;
        task->locale_size = task->curr_locale_size;
    }

    out->reset(out);
    if (task->type == KND_SELECT_STATE) {
        if (DEBUG_CLASS_SELECT_LEVEL_TMP)
            knd_log(".. batch selection: batch size: %zu   start from: %zu",
                    task->batch_max, task->batch_from);

        /* no sets found? */
        if (!task->num_sets) {
            if (c && c->entry->descendants) {
                set = c->entry->descendants;

                err = knd_class_set_export(set, task->format, task);
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
        if (task->num_sets > 1) {
            err = knd_set_new(mempool, &set);
            if (err) return make_gsl_err_external(err);

            set->type = KND_SET_CLASS;
            set->mempool = mempool;
            set->base = task->sets[0]->base;

            err = knd_set_intersect(set, task->sets, task->num_sets);
            if (err) return make_gsl_err_external(err);
        }

        if (!set->num_elems) {
            knd_log("== empty set? %p", set);
            err = out->write(out, "{}", strlen("{}"));
            if (err) return make_gsl_err_external(err);
            return make_gsl_err(gsl_OK);
        }

        /* final presentation in JSON
           TODO: choose output format */

        switch (set->type) {
            case KND_SET_STATE_UPDATE:
                knd_log(".. export state set..");
                err = out->write(out, "{}", strlen("{}"));
                if (err) return make_gsl_err_external(err);
                break;
            default:
                err = knd_class_set_export(set, task->format, task);
                if (err) return make_gsl_err_external(err);
        }

        return make_gsl_err(gsl_OK);
    }

    if (!c) {
        knd_log("-- no specific class selected");
        set = ctx->repo->class_idx;
        if (set->num_elems) {
            err = knd_class_set_export(set, task->format, task);
            if (err) return make_gsl_err_external(err);
            return make_gsl_err(gsl_OK);
        }
        if (ctx->repo->base) {
            set = ctx->repo->base->class_idx;
            err = knd_class_set_export(set, task->format, task);
            if (err) return make_gsl_err_external(err);
            return make_gsl_err(gsl_OK);
        }
        err = out->write(out, "{}", strlen("{}"));
        if (err) return make_gsl_err_external(err);
        return make_gsl_err(gsl_OK);
    }

    //c->depth = 0;
    //c->max_depth = 1;

    err = knd_class_export(c, task->format, task);
    if (err) {
        knd_log("-- class export failed");
        return make_gsl_err_external(err);
    }

//    err = out->writec(out, '\n');
//    if (err) {
//        return make_gsl_err_external(err);
//    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_remove_class(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClass *c;
    struct glbOutput *log = task->log;
    size_t num_children;
    int err;

    if (!task->class) {
        knd_log("-- remove operation: class name not specified");
        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return make_gsl_err_external(err);
        err = log->write(log, " class name not specified",
                         strlen(" class name not specified"));
        if (err) return make_gsl_err_external(err);
        task->http_code = HTTP_BAD_REQUEST;
        return make_gsl_err(gsl_NO_MATCH);
    }

    c = task->class;

    /* check dependants */
    num_children = c->entry->num_children;
    if (c->desc_states && c->desc_states->val)
        num_children = c->desc_states->val->val_size;

    if (num_children) {
        knd_log("-- remove operation: descendants exist");
        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return make_gsl_err_external(err);
        err = log->write(log, "class conflict: descendants exist",
                         strlen("class conflict: descendants exist"));
        if (err) return make_gsl_err_external(err);
        task->http_code = HTTP_CONFLICT;
        return make_gsl_err(gsl_FAIL);
    }

    if (c->entry->num_insts) {
        knd_log("-- remove operation: instances exist");
        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return make_gsl_err_external(err);
        err = log->write(log, "class conflict: instances exist",
                         strlen("class conflict: instances exist"));
        if (err) return make_gsl_err_external(err);
        task->http_code = HTTP_CONFLICT;
        return make_gsl_err(gsl_FAIL);
    }

    // TODO: copy-on-write : add special entry
    //         for deleted classes from base repo


    task->type = KND_UPDATE_STATE;
    task->phase = KND_REMOVED;

    if (DEBUG_CLASS_SELECT_LEVEL_TMP)
        knd_log("NB: class removed: \"%.*s\"\n",
                c->name_size, c->name);

    log->reset(log);
    err = log->write(log, name, name_size);
    if (err) return make_gsl_err_external(err);
    err = log->write(log, " class removed",
                     strlen(" class removed"));
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_select_class_inst(void *obj,
                                         const char *rec,
                                         size_t *total_size)
{
    struct LocalContext *ctx = obj;
    gsl_err_t parser_err;

    if (!ctx->task->class)  {
        knd_log("-- no baseclass selected");
        return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }

    parser_err = knd_select_class_inst(ctx->task->class, rec, total_size, ctx->task);

    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t select_by_baseclass(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = ctx->repo;
    struct kndClass *c;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_get_class(repo, name, name_size, &c, task);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        c->str(c, 1);

    if (!c->entry->class) {
        knd_log(".. resolve class to %.*s", c->name_size, c->name);
        c->entry->class = c;
    }

    if (!c->entry->descendants) {
        knd_log("-- no set of descendants found :(");
        return make_gsl_err(gsl_OK);
    }

    ctx->class = c;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t rels_presentation(void *obj,
                                   const char *unused_var(val),
                                   size_t unused_var(val_size))
{
    struct kndTask *self = obj;
    self->show_rels = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_attr_select(void *obj,
                                   const char *name, size_t name_size,
                                   const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClass *self = ctx->class;
    struct glbOutput *log = task->log;
    struct kndAttrRef *attr_ref;
    struct kndAttr *attr;
    int err;

    if (!self) return *total_size = 0, make_gsl_err_external(knd_FAIL);

    err = knd_class_get_attr(self, name, name_size, &attr_ref);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name,
                self->name_size, self->name);
        log->writef(log, ": %.*s class has no attribute named \"%.*s\"",
                    self->name_size, self->name, name_size, name);
        task->http_code = HTTP_NOT_FOUND;
        return *total_size = 0, make_gsl_err_external(err);
    }
    attr = attr_ref->attr;

    return knd_attr_select_clause(attr, task, rec, total_size);
}

static gsl_err_t parse_baseclass_select(void *obj,
                                        const char *rec,
                                        size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    gsl_err_t err;

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        knd_log(".. select by baseclass \"%.*s\"..", 64, rec);

    task->state_gt = 0;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = select_by_baseclass,
          .obj = ctx
        },
        { .name = "_batch",
          .name_size = strlen("_batch"),
          .parse = gsl_parse_size_t,
          .obj = &task->batch_max
        },
        { .name = "_from",
          .name_size = strlen("_from"),
          .parse = gsl_parse_size_t,
          .obj = &task->batch_from
        },
        { .name = "_depth",
          .name_size = strlen("_depth"),
          .parse = gsl_parse_size_t,
          .obj = &task->max_depth
        },
        { .name = "_update",
          .name_size = strlen("_update"),
          .parse = gsl_parse_size_t,
          .obj = &task->state_gt
        },
        { .name = "_rels",
          .name_size = strlen("_rels"),
          .is_selector = true,
          .run = rels_presentation,
          .obj = task
        },
        { .validate = parse_attr_select,
          .obj = ctx
        }
    };

    task->type = KND_SELECT_STATE;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (task->batch_max > KND_RESULT_MAX_BATCH_SIZE) {
        knd_log("-- batch size exceeded: %zu (max limit: %d) :(",
                task->batch_max, KND_RESULT_MAX_BATCH_SIZE);
        return make_gsl_err(gsl_LIMIT);
    }

    task->start_from = task->batch_max * task->batch_from;

    return make_gsl_err(gsl_OK);
}


gsl_err_t knd_class_select(struct kndRepo *repo,
                           const char *rec, size_t *total_size,
                           struct kndTask *task)
{
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CLASS_SELECT_LEVEL_TMP)
        knd_log(".. parsing class select rec: \"%.*s\"", 32, rec);

    struct LocalContext ctx = {
        .task = task,
        .repo = repo
    };
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_class,
          .obj = &ctx
        },
        { .name = "_id",
          .name_size = strlen("_id"),
          .is_selector = true,
          .run = run_get_class_by_numid,
          .obj = &ctx
        },
        { .name = "_state",
          .name_size = strlen("_state"),
          .parse = parse_select_class_state,
          .obj = &ctx
        },
        { .is_default = true,
          .run = present_class_selection,
          .obj = &ctx
        },
    // TODO(k15tfu): review the specs below
        { .type = GSL_SET_STATE,
          .name = "_rm",
          .name_size = strlen("_rm"),
          .run = run_remove_class,
          .obj = &ctx
        },
        { .type = GSL_SET_STATE,
          .name = "instance",
          .name_size = strlen("instance"),
          .parse = knd_parse_import_class_inst,
          .obj = task
        },
        { .name = "instance",
          .name_size = strlen("instance"),
          .parse = parse_select_class_inst,
          .obj = &ctx
        }, /* shortcuts */
        { .type = GSL_SET_STATE,
          .name = "inst",
          .name_size = strlen("inst"),
          .parse = knd_parse_import_class_inst,
          .obj = task
        },
        { .name = "inst",
          .name_size = strlen("inst"),
          .parse = parse_select_class_inst,
          .obj = &ctx
        },
        { .name = "_is",
          .name_size = strlen("_is"),
          .is_selector = true,
          .parse = parse_baseclass_select,
          .obj = &ctx
        },
        {  .name = "_depth",
           .name_size = strlen("_depth"),
           .parse = gsl_parse_size_t,
           .obj = &task->max_depth
        },
        { .validate = knd_parse_attr_var_select,
          .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        if (DEBUG_CLASS_SELECT_LEVEL_TMP) {
            knd_log("-- class select error %d: \"%.*s\"",
                    parser_err.code, task->log->buf_size, task->log->buf);
        }
        if (!task->log->buf_size) {
            err = task->log->write(task->log, "class parse failure",
                                       strlen("class parse failure"));
            if (err) return make_gsl_err_external(err);
        }

        /* TODO: release resources */
        return parser_err;
    }

    if (!task->class) return make_gsl_err(gsl_OK);

    knd_state_phase phase;

    /* any updates happened? */
    switch (task->type) {
    case KND_UPDATE_STATE:
        phase = KND_UPDATED;
        if (task->phase == KND_REMOVED)
            phase = KND_REMOVED;
        err = knd_update_state(task->class, phase, task);
        if (err) return make_gsl_err_external(err);
        break;
    default:
        break;
    }

    return make_gsl_err(gsl_OK);
}
