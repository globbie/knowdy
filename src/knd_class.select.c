#include "knd_class.h"

#include "knd_attr.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_set.h"

#include <gsl-parser.h>
#include <glb-lib/output.h>

#include <assert.h>
#include <stddef.h>
#include <string.h>

#define DEBUG_CLASS_SELECT_LEVEL_1 0
#define DEBUG_CLASS_SELECT_LEVEL_2 0
#define DEBUG_CLASS_SELECT_LEVEL_3 0
#define DEBUG_CLASS_SELECT_LEVEL_4 0
#define DEBUG_CLASS_SELECT_LEVEL_5 0
#define DEBUG_CLASS_SELECT_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;

    struct kndClass *selected_class;  // get by class name/_id
    struct kndClass *selected_base;   // get by base class

    struct {
        size_t state_eq;
        size_t state_gt;
        size_t state_lt;
        size_t state_gte;
        size_t state_lte;
    } state_filter;

    struct kndClass *class;
    struct kndAttr *attr;
};

static gsl_err_t
run_get_class(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;

    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    /* check root name */
    if (name_size == 1 && *name == '/') {
        ctx->selected_class = ctx->repo->root_class;
        return make_gsl_err(gsl_OK);
    }

    int err = knd_get_class(ctx->repo, name, name_size, &ctx->selected_class, ctx->task);
    if (err) return make_gsl_err_external(err);

    /* NB: class instances need this */
    ctx->task->class = ctx->selected_class;

    if (DEBUG_CLASS_SELECT_LEVEL_1) {
        ctx->selected_class->str(ctx->selected_class, 1);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t
parse_get_class_by_numid(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    size_t numid;
    gsl_err_t parser_err = gsl_parse_size_t(&numid, rec, total_size);
    if (parser_err.code) return parser_err;

    char id[KND_ID_SIZE];
    size_t id_size;
    knd_uid_create(numid, id, &id_size);

    if (DEBUG_CLASS_SELECT_LEVEL_TMP)
        knd_log("ID: %zu => \"%.*s\" [size: %zu]",
                numid, (int)id_size, id, id_size);

    int err = knd_get_class_by_id(ctx->repo, id, id_size, &ctx->selected_class, ctx->task);
    if (err) return make_gsl_err_external(err);

    /* NB: class instances need this */
    ctx->task->class = ctx->selected_class;

    if (DEBUG_CLASS_SELECT_LEVEL_TMP) {
        ctx->selected_class->str(ctx->selected_class, 1);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t
run_get_baseclass(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;

    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    // FIXME(k15tfu): ?? /* check root name */
//    if (name_size == 1 && *name == '/') {
//        ctx->selected_base = ctx->repo->root_class;
//        return make_gsl_err(gsl_OK);
//    }

    int err = knd_get_class(ctx->repo, name, name_size, &ctx->selected_base, ctx->task);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_CLASS_SELECT_LEVEL_1) {
        ctx->selected_base->str(ctx->selected_base, 1);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t
validate_select_by_baseclass_attr(void *obj, const char *name, size_t name_size,
                                  const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    int err;

    if (!ctx->selected_base) {
        knd_log("-- no base class selected");
        err = ctx->task->log->writef(ctx->task->log, "no base class selected");
        if (err) return *total_size = 0, make_gsl_err_external(err);
        return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }

    struct kndAttrRef *attr_ref;
    err = knd_class_get_attr(ctx->selected_base, name, name_size, &attr_ref);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name, ctx->selected_base->name_size, ctx->selected_base->name);
        err = ctx->task->log->writef(ctx->task->log, "%.*s class has no attribute \"%.*s\"",
                                     ctx->selected_base->name_size, ctx->selected_base->name, name_size, name);
        //ctx->task->http_code = HTTP_NOT_FOUND;  // TODO(k15tfu): remove this
        return *total_size = 0, make_gsl_err_external(err);
    }

#if 0
    knd_log("-- not implemented: filter baseclass attribute");
    err = ctx->task->log->writef(ctx->task->log, "not implemented: filter baseclass attribute");
    if (err) return make_gsl_err_external(err);
    return *total_size = 0, make_gsl_err_external(knd_FAIL);
#endif

    // FIXME(k15tfu): used by knd_attr_select_clause()
    ctx->task->class = ctx->selected_base;
    ctx->task->repo = ctx->repo;

    return knd_attr_select_clause(attr_ref->attr, ctx->task, rec, total_size);
}

static gsl_err_t
parse_select_by_baseclass(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    gsl_err_t err;

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        knd_log(".. select by baseclass \"%.*s\"..", 64, rec);

    task->state_gt = 0;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_get_baseclass,
          .obj = ctx
        },
        { .validate = validate_select_by_baseclass_attr,
          .obj = ctx
        },
    // FIXME(k15tfu): vv
        { .name = "_batch",
          .name_size = strlen("_batch"),
          .parse = gsl_parse_size_t,
          .obj = &task->batch_max
        },
        { .name = "_from_batch",
          .name_size = strlen("_from_batch"),
          .parse = gsl_parse_size_t,
          .obj = &task->batch_from
        },
        { .name = "_from",
          .name_size = strlen("_from"),
          .parse = gsl_parse_size_t,
          .obj = &task->start_from
        },
        { .name = "_update",
          .name_size = strlen("_update"),
          .parse = gsl_parse_size_t,
          .obj = &task->state_gt
        },
        { .name = "_depth",
          .name_size = strlen("_depth"),
          .parse = gsl_parse_size_t,
          .obj = &task->max_depth
        }
    };

    //task->type = KND_SELECT_STATE;

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

static gsl_err_t
present_class_state(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    int err;

    assert(ctx->selected_class);

    if (ctx->state_filter.state_eq || ctx->state_filter.state_lt || ctx->state_filter.state_lte ||
        ctx->state_filter.state_gte || ctx->state_filter.state_gt) {
        knd_log("-- not implemented: filter class state");
        err = task->log->writef(task->log, "not implemented: filter class state");
        if (err) return make_gsl_err_external(err);
        return make_gsl_err_external(knd_FAIL);
    }

    err = knd_class_export_state(ctx->selected_class, task->format, task);
    if (err) {
        knd_log("-- class state export failed");
        return make_gsl_err_external(err);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t
parse_select_class_state(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    if (!ctx->selected_class) {
        knd_log("-- class not selected");
        int err = ctx->task->log->writef(ctx->task->log, "class not selected");
        if (err) return *total_size = 0, make_gsl_err_external(err);
        return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = gsl_run_set_size_t,
          .obj = &ctx->state_filter.state_eq
        },
        { .is_selector = true,
          .name = "lt",
          .name_size = strlen("lt"),
          .parse = gsl_parse_size_t,
          .obj = &ctx->state_filter.state_lt
        },
        { .is_selector = true,
          .name = "lte",
          .name_size = strlen("lte"),
          .parse = gsl_parse_size_t,
          .obj = &ctx->state_filter.state_lte
        },
        { .is_selector = true,
          .name = "gte",
          .name_size = strlen("gte"),
          .parse = gsl_parse_size_t,
          .obj = &ctx->state_filter.state_gte
        },
        { .is_selector = true,
          .name = "gt",
          .name_size = strlen("gt"),
          .parse = gsl_parse_size_t,
          .obj = &ctx->state_filter.state_gt
        },
        { .is_default = true,
          .run = present_class_state,
          .obj = ctx
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t
present_class_desc_state(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    int err;

    assert(ctx->selected_class);

    if (ctx->state_filter.state_eq || ctx->state_filter.state_lt || ctx->state_filter.state_lte ||
        ctx->state_filter.state_gte || ctx->state_filter.state_gt) {
        knd_log("-- not implemented: filter class desc state");
        err = task->log->writef(task->log, "not implemented: filter class desc state");
        if (err) return make_gsl_err_external(err);
        return make_gsl_err_external(knd_FAIL);
    }

    knd_log("-- not implemented: export class desc state");
    err = task->log->writef(task->log, "not implemented: export class desc state");
    if (err) return make_gsl_err_external(err);
    return make_gsl_err_external(knd_FAIL);
//    err = knd_class_export_desc_state(ctx->selected_class, task->format, task);
//    if (err) {
//        knd_log("-- class state export failed");
//        return make_gsl_err_external(err);
//    }
//
//    return make_gsl_err(gsl_OK);

#if 0
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

    // TODO export format

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
#endif
}

static gsl_err_t
parse_select_class_desc_state(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = gsl_run_set_size_t,
          .obj = &ctx->state_filter.state_eq
        },
        { .is_selector = true,
          .name = "lt",
          .name_size = strlen("lt"),
          .parse = gsl_parse_size_t,
          .obj = &ctx->state_filter.state_lt
        },
        { .is_selector = true,
          .name = "lte",
          .name_size = strlen("lte"),
          .parse = gsl_parse_size_t,
          .obj = &ctx->state_filter.state_lte
        },
        { .is_selector = true,
          .name = "gte",
          .name_size = strlen("gte"),
          .parse = gsl_parse_size_t,
          .obj = &ctx->state_filter.state_gte
        },
        { .is_selector = true,
          .name = "gt",
          .name_size = strlen("gt"),
          .parse = gsl_parse_size_t,
          .obj = &ctx->state_filter.state_gt
        },
        { .is_default = true,
          .run = present_class_desc_state,
          .obj = ctx
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t
present_class_desc(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct LocalContext *ctx = obj;
    int err;

    assert(ctx->selected_class);
    // FIXME(k15tfu): assert(ctx->selected_class->entry->descendants);

    if (!ctx->selected_class->entry->descendants) {
        // FIXME(k15tfu): Why it's empty??
        // DD: the index of descendants is not created for every class,
        // only for the non-terminal classes with actual children, grandchildren etc.
        knd_log("-- not implemented: export empty class desc");
        err = ctx->task->log->writef(ctx->task->log, "not implemented: export empty class desc");
        if (err) return make_gsl_err_external(err);
        return make_gsl_err_external(knd_FAIL);
    }

    err = knd_class_set_export(ctx->selected_class->entry->descendants, ctx->task->format, ctx->task);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t
parse_select_class_desc(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    if (!ctx->selected_class) {
        knd_log("-- class not selected");
        int err = ctx->task->log->writef(ctx->task->log, "class not selected");
        if (err) return *total_size = 0, make_gsl_err_external(err);
        return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }

    struct gslTaskSpec specs[] = {
        { .name = "_state",
          .name_size = strlen("_state"),
          .parse = parse_select_class_desc_state,
          .obj = ctx
        },
        { .is_default = true,
          .run = present_class_desc,
          .obj = ctx
        },
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t
parse_select_class_inst(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    if (!ctx->selected_class) {
        knd_log("-- class not selected");
        int err = ctx->task->log->writef(ctx->task->log, "class not selected");
        if (err) return *total_size = 0, make_gsl_err_external(err);
        return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }

    return knd_select_class_inst(ctx->selected_class, rec, total_size, ctx->task);
}

static gsl_err_t
parse_import_class_inst(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    if (!ctx->selected_class) {
        knd_log("-- class not selected");
        int err = ctx->task->log->writef(ctx->task->log, "class not selected");
        if (err) return *total_size = 0, make_gsl_err_external(err);
        return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }

    return knd_parse_import_class_inst(ctx->task, rec, total_size);
}

static gsl_err_t
validate_select_class_attr(void *obj, const char *name, size_t name_size,
                           const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    if (!ctx->selected_class) {
        knd_log("-- no class selected");
        int err = ctx->task->log->writef(ctx->task->log, "no class selected");
        if (err) return *total_size = 0, make_gsl_err_external(err);
        return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }

    return knd_select_attr_var(ctx->selected_class, name, name_size,
                               rec, total_size, ctx->task);
}

static gsl_err_t
run_remove_class(void *obj, const char *unused_var(name), size_t name_size)
{
    struct LocalContext *ctx = obj;
    int err;

    if (name_size) return make_gsl_err(gsl_FORMAT);

    if (!ctx->selected_class) {
        knd_log("-- no class selected");
        err = ctx->task->log->writef(ctx->task->log, "no class selected");
        if (err) return make_gsl_err_external(err);
        //ctx->task->http_code = HTTP_BAD_REQUEST;  // FIXME(k15tfu): ??
        return make_gsl_err_external(knd_FAIL);
    }

    if (ctx->selected_class->entry->num_children) {
        knd_log("-- descendants exist");
        err = ctx->task->log->writef(ctx->task->log, "descendants exist");
        if (err) return make_gsl_err_external(err);
        //ctx->task->http_code = HTTP_CONFLICT;  // FIXME(k15tfu): ??
        return make_gsl_err_external(knd_FAIL);
    }

    if (ctx->selected_class->entry->num_insts) {
        knd_log("-- instances exist");
        err = ctx->task->log->writef(ctx->task->log, "instances exist");
        if (err) return make_gsl_err_external(err);
        //ctx->task->http_code = HTTP_CONFLICT;  // FIXME(k15tfu): ??
        return make_gsl_err_external(knd_FAIL);
    }

    knd_log("-- not implemented: remove class");
    err = ctx->task->log->writef(ctx->task->log, "not implemented: remove class");
    if (err) return make_gsl_err_external(err);
    return make_gsl_err_external(knd_FAIL);
#if 0
    // TODO: copy-on-write : add special entry
    //         for deleted classes from base repo
    ctx->task->type = KND_UPDATE_STATE;
    ctx->task->phase = KND_REMOVED;

    return make_gsl_err(gsl_OK);
#endif
}

static gsl_err_t
present_class_selection(void *obj, const char *unused_var(val), size_t unused_var(val_size))
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    int err;

    // TODO move to export functions
    if (task->curr_locale_size) {
        task->locale = task->curr_locale;
        task->locale_size = task->curr_locale_size;
    }

    /* select a set of classes by base class */
    if (ctx->selected_base) {
        /* 
         *  if no sets are selected - 
         *  present all descendants of a class if any
         */
        if (!task->num_sets) {
            // FIXME(k15tfu): remove this case

            if (!ctx->selected_base->entry->descendants) {
                err = knd_empty_set_export(ctx->selected_base, task->format, task);
                if (err) return make_gsl_err_external(err);
                return make_gsl_err(gsl_OK);
            }

            err = knd_class_set_export(ctx->selected_base->entry->descendants, task->format, task);
            if (err) return make_gsl_err_external(err);
            return make_gsl_err(gsl_OK);
        }

        /* add base set */
        assert(task->num_sets + 1 <= KND_MAX_CLAUSES);  // FIXME(k15tfu): <<
        task->sets[task->num_sets] = ctx->selected_base->entry->descendants;
        task->num_sets++;

        knd_log(".. intersecting of %zu sets..", task->num_sets);

        /* intersection result set */
        struct kndSet *set;
        err = knd_set_new(task->mempool, &set);
        if (err) return make_gsl_err_external(err);
        set->type = KND_SET_CLASS;
        set->mempool = task->mempool;
        set->base = ctx->selected_base->entry;

        err = knd_set_intersect(set, task->sets, task->num_sets);
        if (err) return make_gsl_err_external(err);

        if (!set->num_elems) {
            err = knd_empty_set_export(ctx->selected_base, task->format, task);
            if (err) return make_gsl_err_external(err);
        }

        err = knd_class_set_export(set, task->format, task);
        if (err) return make_gsl_err_external(err);
        return make_gsl_err(gsl_OK);
    }
    
    /* get one class by name */
    if (ctx->selected_class) {
        err = knd_class_export(ctx->selected_class, task->format, task);
        if (err) {
            knd_log("-- class export failed");
            return make_gsl_err_external(err);
        }
        return make_gsl_err(gsl_OK);
    }

    /* display all classes */

    if (ctx->repo->class_idx->num_elems) {
        err = knd_class_set_export(ctx->repo->class_idx, task->format, task);
        if (err) return make_gsl_err_external(err);
        return make_gsl_err(gsl_OK);
    }
    if (ctx->repo->base) {
        err = knd_class_set_export(ctx->repo->base->class_idx, task->format, task);
        if (err) return make_gsl_err_external(err);
        return make_gsl_err(gsl_OK);
    }

    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_class_select(struct kndRepo *repo,
                           const char *rec, size_t *total_size,
                           struct kndTask *task)
{
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CLASS_SELECT_LEVEL_2)
        knd_log(".. parsing class select rec: \"%.*s\" (repo:%.*s)",
                32, rec, repo->name_size, repo->name);

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
        { .is_selector = true,
          .name = "_id",
          .name_size = strlen("_id"),
          .parse = parse_get_class_by_numid,
          .obj = &ctx
        },
        { .is_selector = true,
          .name = "_is",
          .name_size = strlen("_is"),
          .parse = parse_select_by_baseclass,
          .obj = &ctx
        },
        { .name = "_state",
          .name_size = strlen("_state"),
          .parse = parse_select_class_state,
          .obj = &ctx
        },
        { .name = "_desc",
          .name_size = strlen("_desc"),
          .parse = parse_select_class_desc,
          .obj = &ctx
        },
        { .name = "_inst",
          .name_size = strlen("_inst"),
          .parse = parse_select_class_inst,
          .obj = &ctx
        },
        { .type = GSL_SET_STATE,
          .name = "_inst",
          .name_size = strlen("_inst"),
          .parse = parse_import_class_inst,
          .obj = &ctx
        },
        { .validate = validate_select_class_attr,
          .obj = &ctx
        },
        { .type = GSL_SET_STATE,
          .name = "_rm",
          .name_size = strlen("_rm"),
          .run = run_remove_class,
          .obj = &ctx
        },
        { .is_default = true,
          .run = present_class_selection,
          .obj = &ctx
        },
    // TODO(k15tfu): review the specs below
        { .type = GSL_SET_STATE,
          .name = "instance",
          .name_size = strlen("instance"),
          .parse = parse_import_class_inst,
          .obj = task
        },
        { .name = "instance",
          .name_size = strlen("instance"),
          .parse = parse_select_class_inst,
          .obj = &ctx
        },
        { .name = "inst",
          .name_size = strlen("inst"),
          .parse = parse_select_class_inst,
          .obj = &ctx
        },
        { .type = GSL_SET_STATE,
          .name = "inst",
          .name_size = strlen("inst"),
          .parse = parse_import_class_inst,
          .obj = &ctx
        },
        { .name = "is",
          .name_size = strlen("is"),
          .is_selector = true,
          .parse = parse_select_by_baseclass,
          .obj = &ctx
        },
        { .name = "_depth",
          .name_size = strlen("_depth"),
          .parse = gsl_parse_size_t,
          .obj = &task->max_depth
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        if (DEBUG_CLASS_SELECT_LEVEL_TMP)
            knd_log("-- class select error %d: \"%.*s\"",
                    parser_err.code, task->log->buf_size, task->log->buf);

        return parser_err;
    }

    // FIXME(k15tfu): vv
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

int knd_class_match_query(struct kndClass *self,
                          struct kndAttrVar *query)
{
    struct kndSet *attr_idx = self->attr_idx;
    knd_logic logic = query->logic;
    struct kndAttrRef *attr_ref;
    struct kndAttrVar *attr_var;
    struct kndAttr *attr;
    void *result;
    int err;

    for (attr_var = query->children; attr_var; attr_var = attr_var->next) {
        attr = attr_var->attr;

        err = attr_idx->get(attr_idx, attr->id, attr->id_size, &result);
        if (err) return err;
        attr_ref = result;

        if (!attr_ref->attr_var) return knd_NO_MATCH;

        err = knd_attr_var_match(attr_ref->attr_var, attr_var);
        if (err == knd_NO_MATCH) {
            switch (logic) {
                case KND_LOGIC_AND:
                    return knd_NO_MATCH;
                default:
                    break;
            }
            continue;
        }

        /* got a match */
        switch (logic) {
            case KND_LOGIC_OR:
                return knd_OK;
            default:
                break;
        }
    }

    switch (logic) {
        case KND_LOGIC_OR:
            return knd_NO_MATCH;
        default:
            break;
    }

    return knd_OK;
}
