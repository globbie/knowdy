#include "knd_commit.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_attr_stm.h"
#include "knd_task.h"
#include "knd_text.h"
#include "knd_repo.h"
#include "knd_user.h"
#include "knd_query.h"
#include "knd_set.h"
#include "knd_facet.h"
#include "knd_shared_set.h"
#include "knd_output.h"

#include <gsl-parser.h>

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>

#define DEBUG_CLASS_SELECT_LEVEL_1 0
#define DEBUG_CLASS_SELECT_LEVEL_2 0
#define DEBUG_CLASS_SELECT_LEVEL_3 0
#define DEBUG_CLASS_SELECT_LEVEL_4 0
#define DEBUG_CLASS_SELECT_LEVEL_5 0
#define DEBUG_CLASS_SELECT_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndQuery *query;
    struct kndRepo *repo;

    struct kndClass *cls;
    struct kndClass *base_cls;
    struct kndClass *base_subj_cls;

    struct kndAttr *attr;
    struct kndClassDeclar *declar;

    struct {
        size_t state_eq;
        size_t state_gt;
        size_t state_lt;
        size_t state_gte;
        size_t state_lte;
    } state_filter;
};

static gsl_err_t confirm_default_query(void *obj, const char *unused_var(val),
                                       size_t unused_var(val_size))
{
    struct LocalContext *ctx = obj;
    struct kndQuery *query = ctx->query;

    knd_log(">> confirm default class query - show all classes");
    query->type = KND_QUERY_SELECT;
    query->obj_type = KND_QUERY_OBJ_CLASS;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t select_cls_attr(void *obj, const char *name, size_t name_size,
                                   const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClass *bc = ctx->base_cls;
    struct kndRepo *repo = ctx->repo;
    struct kndAttr *attr;
    struct kndAttrStm *stm;
    int err;

    if (!bc) {
        KND_TASK_LOG("no base cls selected");
        return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }

    err = knd_attr_find(bc, name, name_size, &attr, repo, task);
    if (err) {
        KND_TASK_LOG("no {attr %.*s} in {cls %.*s}",
                     name_size, name, bc->name_size, bc->name);
        return make_gsl_err(gsl_FAIL);
    }

    if (DEBUG_CLASS_SELECT_LEVEL_3) {
        knd_log("{cls %.*s {attr %.*s}} confirmed by owner {cls %.*s}",
                bc->name_size, bc->name, name_size, name,
                attr->owner->name_size, attr->owner->name);

        if (attr->facet) {
            knd_facet_str(attr->facet, knd_attr_stm_present_subj, task, 0);
        }
    }

    err = knd_attr_stm_new(&stm, bc, task->mempool);
    if (err) return make_gsl_err_external(err);   
    stm->attr = attr;

    err = knd_attr_parse_query_stm(stm, rec, total_size, repo, task);
    if (err) return make_gsl_err_external(err);

    knd_query_append_attr_stm(ctx->query, stm);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t get_cls_by_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndRepo *repo = ctx->repo;
    struct kndTask *task = ctx->task;
    struct kndQuery *query = ctx->query;
    struct kndClassEntry *entry;
    struct kndClass *c;
    int err;

    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_get_cls_entry_by_name(repo, name, name_size, &entry, task);
    if (err) {
        KND_TASK_LOG("{cls %.*s} not found", name_size, name);
        task->ctx->error = knd_NO_MATCH;
        return make_gsl_err(gsl_FAIL);
    }

    err = knd_class_acquire(entry, &c, repo, task);
    if (err) {
        KND_TASK_LOG("failed to acquire {cls %.*s}", entry->name_size, entry->name);
        return make_gsl_err_external(err);
    }
    query->type = KND_QUERY_GET;
    query->obj_type = KND_QUERY_OBJ_CLASS;
    query->cls = c;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t get_base_cls(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndRepo *repo = ctx->repo;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    struct kndQuery *query = ctx->query;
    struct kndClassBasePred *base_pred;
    struct kndClassEntry *entry;
    struct kndClass *c;
    int err;

    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_get_cls_entry_by_name(repo, name, name_size, &entry, task);
    if (err) {
        KND_TASK_LOG("{cls %.*s} not found", name_size, name);
        task->ctx->error = knd_NO_MATCH;
        return make_gsl_err(gsl_FAIL);
    }

    err = knd_class_acquire(entry, &c, repo, task);
    if (err) {
        KND_TASK_LOG("failed to acquire {cls %.*s}", entry->name_size, entry->name);
        return make_gsl_err_external(err);
    }

    err = knd_class_base_pred_new(&base_pred, c, mempool);
    if (err) {
        KND_TASK_LOG("failed to alloc a base pred");
        return make_gsl_err_external(err);
    }
    base_pred->entry = entry;

    query->type = KND_QUERY_SELECT;
    query->obj_type = KND_QUERY_OBJ_CLASS;
    ctx->base_cls = c;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t get_subj_base_class(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndRepo *repo = ctx->repo;
    struct kndTask *task = ctx->task;
    struct kndClassEntry *entry;
    struct kndClass *c;
    int err;

    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_get_cls_entry_by_name(repo, name, name_size, &entry, task);
    if (err) {
        KND_TASK_LOG("{cls %.*s} not found", name_size, name);
        task->ctx->error = knd_NO_MATCH;
        return make_gsl_err(gsl_FAIL);
    }

    err = knd_class_acquire(entry, &c, repo, task);
    if (err) {
        KND_TASK_LOG("failed to acquire {cls %.*s}", entry->name_size, entry->name);
        return make_gsl_err_external(err);
    }
    ctx->base_subj_cls = c;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t select_inverse_attr(void *obj, const char *rec, size_t *total_size)
{
    gsl_err_t err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = get_subj_base_class,
          .obj = obj
        },
        { .validate = select_cls_attr,
          .obj = obj
        }
    };

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t select_by_base_cls(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndQuery *query = ctx->query;
    struct kndTask *task = ctx->task;
    struct kndClass *c = query->cls;
    gsl_err_t err;

    if (c) {
        KND_TASK_LOG("single choice already made for {cls %.*s}", c->name_size, c->name);
        return *total_size = 0, make_gsl_err_external(knd_FORMAT);
    }
 
    if (DEBUG_CLASS_SELECT_LEVEL_3) {
        knd_log(".. select by base {cls %.*s}", 64, rec);
    }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = get_base_cls,
          .obj = obj
        },
        { .name = "of",
          .name_size = strlen("of"),
          .parse = select_inverse_attr,
          .obj = obj
        },
        { .validate = select_cls_attr,
          .obj = obj
        }
    };

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t present_class_state(void *obj, const char *unused_var(name),
                                     size_t unused_var(name_size))
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    int err;

    assert(ctx->cls);

    if (ctx->state_filter.state_eq || ctx->state_filter.state_lt || ctx->state_filter.state_lte ||
        ctx->state_filter.state_gte || ctx->state_filter.state_gt) {
        knd_log("-- not implemented: filter class state");
        err = task->log->writef(task->log, "not implemented: filter class state");
        if (err) return make_gsl_err_external(err);
        return make_gsl_err_external(knd_FAIL);
    }
    err = knd_class_export_state(ctx->cls, task->ctx->format, task);
    if (err) {
        knd_log("-- class state export failed");
        return make_gsl_err_external(err);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t select_class_state(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;

    if (!ctx->cls) {
        KND_TASK_LOG("no class selected");
        return *total_size = 0, make_gsl_err_external(knd_FORMAT);
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

static gsl_err_t select_class_inst(void *obj, const char *unused_var(rec), size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClass *c = ctx->cls;
    if (!c) {
        KND_TASK_LOG("no cls selected");
        return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }
    /*err = knd_select_class_inst(c, rec, total_size, ctx->task);
    if (err) {
        knd_log("-- class state export failed");
        return make_gsl_err_external(err);
        }*/
    return make_gsl_err(gsl_OK);
}

static gsl_err_t select_cls_attrs(void *obj, const char *unused_var(rec), size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    //int err;

    if (!ctx->cls) {
        KND_TASK_LOG("no cls selected");
        return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }
    /*err = knd_select_cls_attrs(ctx->query, rec, total_size, task);
    if (err) {
        KND_TASK_LOG("failed to select attrs of {cls %.*s}", ctx->cls->name_size, ctx->cls->name);
        return make_gsl_err_external(err);
        }*/
    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_class_inst(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = ctx->repo;
    struct kndCommit *commit = task->ctx->commit;
    struct kndMemPool *mempool = task->mempool;
    struct kndRepoSnapshot *snapshot = ctx->repo->snapshot;
    struct kndClass *c = ctx->cls;
    int err;

    if (DEBUG_CLASS_SELECT_LEVEL_2) {
        knd_log("parse import class inst");
    }

    if (!c) {
        KND_TASK_LOG("no cls selected");
        return *total_size = 0, make_gsl_err_external(knd_FORMAT);
    }

    // TODO check write privileges

    switch (task->type) {
    case KND_TASK_QUERY:
        if (!commit) {
            err = knd_commit_new(&commit, mempool);
            if (err) return make_gsl_err_external(err);
            commit->orig_state_id = atomic_load_explicit(&snapshot->num_commits, memory_order_relaxed);
            task->ctx->commit = commit;
        }
        break;
    default:
        break;
    }

    err = knd_import_class_inst(c->entry, rec, total_size, repo, task);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t remove_class(void *obj, const char *unused_var(name), size_t name_size)
{
    struct LocalContext *ctx = obj;
    int err;

    if (name_size) return make_gsl_err(gsl_FORMAT);

    if (!ctx->cls) {
        knd_log("-- no class selected");
        err = ctx->task->log->writef(ctx->task->log, "no class selected");
        if (err) return make_gsl_err_external(err);
        return make_gsl_err_external(knd_FAIL);
    }

    if (ctx->cls->num_children) {
        knd_log("-- descendants exist");
        err = ctx->task->log->writef(ctx->task->log, "descendants exist");
        if (err) return make_gsl_err_external(err);
        return make_gsl_err_external(knd_FAIL);
    }

    if (ctx->cls->num_insts) {
        knd_log("-- instances exist");
        err = ctx->task->log->writef(ctx->task->log, "instances exist");
        if (err) return make_gsl_err_external(err);
        return make_gsl_err_external(knd_FAIL);
    }

    knd_log("-- not implemented: remove class");
    err = ctx->task->log->writef(ctx->task->log, "not implemented: remove class");
    if (err) return make_gsl_err_external(err);
    return make_gsl_err_external(knd_FAIL);
#if 0
    // TODO: copy-on-write : add special entry
    //         for deleted classes from base repo
    ctx->task->type = KND_TASK_COMMIT;
    ctx->task->phase = KND_REMOVED;
    return make_gsl_err(gsl_OK);
#endif
}

int knd_class_select(const char *rec, size_t *total_size,
                     struct kndRepo *repo, struct kndQuery *query, struct kndTask *task)
{
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CLASS_SELECT_LEVEL_2) {
        knd_log(".. parsing class select rec: \"%.*s\" {repo %.*s} {task-type %d}",
                32, rec, repo->name_size, repo->name, task->type);
    }

    struct LocalContext ctx = {
        .task = task,
        .query = query,
        .repo = repo
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = get_cls_by_name,
          .obj = &ctx
        },
        { .name = "state",
          .name_size = strlen("state"),
          .parse = select_class_state,
          .obj = &ctx
        },
        { .name = "del",
          .name_size = strlen("del"),
          .run = remove_class,
          .obj = &ctx
        },
        { .name = "attr",
          .name_size = strlen("attr"),
          .parse = select_cls_attrs,
          .obj = &ctx
        },
        { .type = GSL_SET_STATE,
          .name = "inst",
          .name_size = strlen("inst"),
          .parse = import_class_inst,
          .obj = &ctx
        },
        { .name = "inst",
          .name_size = strlen("inst"),
          .parse = select_class_inst,
          .obj = &ctx
        },
        { .name = "is",
          .name_size = strlen("is"),
          .parse = select_by_base_cls,
          .obj = &ctx
        },
        { .is_default = true,
          .run = confirm_default_query,
          .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        err = gsl_err_to_knd_err_codes(parser_err);
        switch (err) {
        case knd_NO_MATCH:
            KND_TASK_ERR("unrecognized {tag %.*s} in {cls}",
                         parser_err.val_size, parser_err.val);
            break;
        default:
            KND_TASK_ERR("{cls} parsing failed {err %d}", err);
            break;
        }
    }

    return knd_OK;
}
