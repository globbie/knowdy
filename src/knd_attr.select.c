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
#include "knd_attr_stm.h"
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_logic.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>

#define DEBUG_ATTR_SELECT_LEVEL_1 0
#define DEBUG_ATTR_SELECT_LEVEL_2 0
#define DEBUG_ATTR_SELECT_LEVEL_3 0
#define DEBUG_ATTR_SELECT_LEVEL_4 0
#define DEBUG_ATTR_SELECT_LEVEL_5 0
#define DEBUG_ATTR_SELECT_LEVEL_TMP 1

struct LocalContext {
    struct kndClass   *class;
    struct kndTask    *task;
    struct kndAttrRef *selected_attr_ref;

    struct kndRepo    *repo;

    struct kndAttrStm *clauses;
    struct kndAttrStm *attr_stm;
    struct kndAttr    *attr;
    knd_logic_t logic;
};

static gsl_err_t parse_nested_attr_stm(void *obj,
                                       const char *name, size_t name_size,
                                       const char *rec, size_t *total_size);

static gsl_err_t set_logic_OR_val(void *obj, const char *val, size_t val_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    struct kndAttrStm *attr_stm;
    int err;

    if (DEBUG_ATTR_SELECT_LEVEL_2)
        knd_log("== set logic OR val: %.*s", val_size, val);

    err = knd_attr_stm_new(&attr_stm, mempool);
    if (err) return make_gsl_err_external(err);

    attr_stm->attr = ctx->attr;
    attr_stm->val = val;
    attr_stm->val_size = val_size;

    if (ctx->attr->type == KND_ATTR_NUM) {
        memcpy(buf, val, val_size);
        buf_size = val_size;
        buf[buf_size] = '\0';
        // TODO
        err = knd_parse_num(buf, &attr_stm->numval);
    }
    
    attr_stm->next = ctx->attr_stm;
    ctx->attr_stm = attr_stm;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_logic_OR_val_array(void *obj,
                                          const char *rec,
                                          size_t *total_size)
{
    struct LocalContext *ctx = obj;
    ctx->logic = KND_LOGIC_OR;

    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .run = set_logic_OR_val,
        .obj = obj
    };
    return gsl_parse_array(&item_spec, rec, total_size);
}

static gsl_err_t run_set_attr_stm(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttr *attr;
    struct kndAttrRef *attr_ref;
    struct kndAttrStm *attr_stm;
    struct kndOutput *log = task->log;
    struct kndMemPool *mempool = task->mempool;
    struct kndSharedSet *attr_idx = task->idxs->attr_idx;
    struct kndState *state;
    struct kndStateRef *state_ref;
    struct kndStateVal *state_val;
    void *elem;
    int err, e;

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    attr = ctx->selected_attr_ref->attr;
    if (!attr) {
        log->reset(log);
        e = log->write(log, "-- no attr selected", strlen("-- no attr selected"));
        if (e) return make_gsl_err_external(e);
        task->http_code = HTTP_BAD_REQUEST;
        return make_gsl_err_external(knd_FAIL);
    }

    /*  attr var exists? */
    err = knd_shared_set_get(attr_idx, attr->id, attr->id_size, &elem);
    if (err) {
        knd_log("-- no attr var \"%.*s\" in local attr idx",
                attr->name_size, attr->name);
        return make_gsl_err_external(err);
    }

    attr_ref = elem;
    attr_stm = attr_ref->attr_stm;

    if (DEBUG_ATTR_SELECT_LEVEL_TMP) {
        knd_log(".. updating attr var %.*s with {value %.*s}",
                attr_stm->name_size, attr_stm->name, val_size, val);
    }

    err = knd_state_new(mempool, &state);
    if (err) return make_gsl_err_external(err);
    state->phase = KND_UPDATED;

    err = knd_state_ref_new(mempool, &state_ref);
    if (err) {
        knd_log("-- state ref alloc failed");
        return make_gsl_err_external(err);
    }
    state_ref->state = state;
    state_ref->type = KND_STATE_ATTR_STM;

    err = knd_state_val_new(mempool, &state_val);
    if (err) {
        knd_log("-- state val alloc failed");
        return make_gsl_err_external(err);
    }

    state_val->obj = (void*)attr_stm;
    state_val->val = val;
    state_val->val_size = val_size;
    state->val = state_val;

    attr_stm->val = val;
    attr_stm->val_size = val_size;

    state->next = attr_stm->states;
    attr_stm->states = state;
    attr_stm->num_states++;
    state->numid = attr_stm->num_states;

    task->type = KND_COMMIT_STATE;

    /* TODO: inform parent class */
    //state_ref->next = task->inner_class_state_refs;
    //task->inner_class_state_refs = state_ref;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t present_attr_stm_selection(void *obj,
                                            const char *unused_var(val),
                                            size_t unused_var(val_size))
{
    struct LocalContext *ctx = obj;
    int err;

    // FIXME(k15tfu): assert(ctx->selected_attr_ref->attr_stm);

    if (!ctx->selected_attr_ref->attr_stm) {
        // FIXME(k15tfu): Why it's empty??
        knd_log("-- not implemented: export empty attr var");
//        err = ctx->task->log->writef(ctx->task->log, "not implemented: export empty attr var");
//        if (err) return make_gsl_err_external(err);
//        return make_gsl_err_external(knd_FAIL);

        // WORKAROUND: export attribute (not attr_stm)  FIXME(k15tfu): remove this
        err = knd_attr_export(ctx->selected_attr_ref->attr, KND_FORMAT_GSP/*ctx->task->format*/, ctx->task);
        if (err) return make_gsl_err_external(err);
        return make_gsl_err(gsl_OK);
    }

    err = knd_attr_stm_export_GSL(ctx->selected_attr_ref->attr_stm, ctx->task, 0);
    if (err) {
        knd_log("-- attr export failed");
        return make_gsl_err_external(err);
    }

    return make_gsl_err(gsl_OK);

#if 0
    if (DEBUG_ATTR_SELECT_LEVEL_2)
        knd_log(".. presenting attrs of class \"%.*s\"..",
                ctx->selected_attr_ref->name_size, self->name);

    struct kndTask *task = ctx->task;
    struct kndClass *self = ctx->class;
    struct kndAttr *attr;
    struct kndAttrStm *attr_stm;
    struct kndRepo *repo = ctx->repo;
    struct kndOutput *out = task->out;
    int err;

    if (DEBUG_ATTR_SELECT_LEVEL_2)
        knd_log(".. presenting attrs of class \"%.*s\"..",
                self->name_size, self->name);

    out->reset(out);

    if (!ctx->attr) {
        knd_log("-- no attr to present");
        return make_gsl_err_external(knd_FAIL);
    }
    attr = ctx->attr;

    if (repo->curr_attr_stm) {
        attr_stm = repo->curr_attr_stm;

        err = out->writec(out, '{');
        if (err) return make_gsl_err_external(err);

        err = knd_attr_stm_export_JSON(attr_stm, task);
        if (err) return make_gsl_err_external(err);

        err = out->writec(out, '}');
        if (err) return make_gsl_err_external(err);

        return make_gsl_err(gsl_OK);
    }

    // TODO
    err = out->writec(out, '{');
    if (err) return make_gsl_err_external(err);

    err = knd_attr_export(attr, task->format, task);
    if (err) {
        knd_log("-- attr export failed");
        return make_gsl_err_external(err);
    }

    err = out->writec(out, '}');
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
#endif
}

static gsl_err_t select_by_attr(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    struct kndClass *self = ctx->class;
    struct kndClassEntry *entry;
    struct kndClass *c;
    struct kndAttrStm *attr_stm;
    struct kndAttrFacet *facet;
    struct kndOutput *log = task->log;
    struct kndAttr *attr = ctx->attr;
    int err;

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    entry = attr->ref_class_entry;
    err = knd_class_acquire(entry, &c, task);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_ATTR_SELECT_LEVEL_TMP) {
        knd_log("\n\n== _is class:%.*s  select %.*s attr (idx:%d) "
                " by value: \"%.*s\" (class: \"%.*s\" "
                " id:%.*s repo:%.*s)",
                self->name_size, self->name,
                attr->name_size, attr->name, attr->is_indexed,
                val_size, val,
                c->name_size, c->name,
                c->entry->id_size, c->entry->id,
                c->entry->repo->name_size, c->entry->repo->name);
    }

    /* TODO: special value: _null */
    if (val_size == strlen("_null")) {
        if (!memcmp(val, "_null", val_size)) {
            err = knd_attr_stm_new(&attr_stm, mempool);
            if (err) return make_gsl_err_external(err);
            attr_stm->attr = ctx->attr;

            attr_stm->next = ctx->clauses;
            ctx->clauses = attr_stm;
            return make_gsl_err(gsl_OK);
        }
    }
                
    err = knd_get_class(self->entry->repo, val, val_size, &c, task);
    if (err) {
        log->writef(log, "-- no such class: %.*s", val_size, val);
        task->http_code = HTTP_NOT_FOUND;
        return make_gsl_err_external(err);
    }
    
    if (!attr->facet_idx) {
        knd_log("-- no facet idx found in %.*s", attr->name_size, attr->name);
        // TODO: add plain clause
        return make_gsl_err(gsl_OK);
    }

    /* try direct lookup */
    err = attr->facet_idx->get(attr->facet_idx,
                               c->entry->id, c->entry->id_size,
                               (void**)&facet);
    if (err) {
        log->reset(log);
        log->writef(log, "-- no such facet value: %.*s", val_size, val);
        task->ctx->error = knd_NO_MATCH;
        task->ctx->http_code = HTTP_NOT_FOUND;
        return make_gsl_err_external(knd_NO_MATCH);
    }

    if (task->num_sets + 1 > KND_MAX_CLAUSES)
        return make_gsl_err(gsl_LIMIT);

    task->sets[task->num_sets] = facet->topics;
    task->num_sets++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_classref_clause(struct kndAttr *attr,
                                       struct LocalContext *ctx,
                                       const char *rec, size_t *total_size)
{
    if (DEBUG_ATTR_SELECT_LEVEL_2)
        knd_log(".. parse classref attr \"%.*s\"..",
                attr->name_size, attr->name);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = select_by_attr,
          .obj = ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "_or",
          .name_size = strlen("_or"),
          .parse = parse_logic_OR_val_array,
          .obj = ctx
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t set_attr_stm_value(void *obj, const char *val, size_t val_size)
{
    struct kndAttrStm *self = obj;

    if (DEBUG_ATTR_SELECT_LEVEL_2)
        knd_log(".. set attr var value: %.*s %.*s",
                self->name_size, self->name, val_size, val);

    if (!val_size) return make_gsl_err(gsl_FORMAT);

    self->val = val;
    self->val_size = val_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_attr_stm(void *obj,
                                  const char *unused_var(name),
                                  size_t unused_var(name_size))
{
    struct kndAttrStm *attr_stm = obj;
    // TODO empty values?
    if (DEBUG_ATTR_SELECT_LEVEL_1) {
        if (!attr_stm->val_size)
            knd_log("NB: attr var value not set in %.*s",
                    attr_stm->name_size, attr_stm->name);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t select_spec_by_baseclass(void *obj, const char *name, size_t name_size)
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

    /* TODO: check attr hubs */
    //ctx->class = c;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_baseclass_select(void *obj,
                                        const char *rec,
                                        size_t *total_size)
{
    struct LocalContext *ctx = obj;
    gsl_err_t err;

    if (DEBUG_ATTR_SELECT_LEVEL_TMP)
        knd_log(".. select spec by baseclass: \"%.*s\"..",
                64, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = select_spec_by_baseclass,
          .obj = ctx
        }/*,
        { .validate = parse_base_attr_select,
          .obj = ctx
          }*/
    };

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_nested_attr_stm(void *obj,
                                       const char *name, size_t name_size,
                                       const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrStm *self = ctx->attr_stm;
    struct kndTask    *task = ctx->task;
    struct kndAttrStm *attr_stm;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err;

    err = knd_attr_stm_new(&attr_stm, mempool);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_stm->parent = self;

    attr_stm->name = name;
    attr_stm->name_size = name_size;
    ctx->attr_stm = attr_stm;

    if (DEBUG_ATTR_SELECT_LEVEL_TMP)
        knd_log(".. select nested attr: \"%.*s\" REC: %.*s",
                attr_stm->name_size, attr_stm->name, 64, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_stm_value,
          .obj = attr_stm
        },
        { .name = "_is",
          .name_size = strlen("_is"),
          .is_selector = true,
          .parse = parse_baseclass_select,
          .obj = ctx
        },
        { .validate = parse_nested_attr_stm,
          .obj = ctx
        },
        { .is_default = true,
          .run = confirm_attr_stm,
          .obj = attr_stm
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (DEBUG_ATTR_SELECT_LEVEL_TMP)
        knd_log("++ attr var: \"%.*s\" val:%.*s",
                attr_stm->name_size, attr_stm->name,
                attr_stm->val_size, attr_stm->val);

    //attr_stm->next = self->children;
    //self->children = attr_stm;
    //self->num_children++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_inner_class_clause(struct kndAttr *attr,
                                          struct LocalContext *parent_ctx,
                                          const char *rec, size_t *total_size)
{
    gsl_err_t parser_err;

    if (DEBUG_ATTR_SELECT_LEVEL_TMP)
        knd_log(".. parse inner class clause: \"%.*s\"..",
                attr->name_size, attr->name);

    struct LocalContext ctx = {
        .task = parent_ctx->task,
        .attr = parent_ctx->attr,
        .attr_stm = parent_ctx->attr_stm
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = select_by_attr,
          .obj = &ctx
        },
        { .validate = parse_nested_attr_stm,
          .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    knd_log("== clauses: %p", ctx.clauses);

    if (ctx.clauses) {
        ctx.clauses->next = parent_ctx->clauses;
        parent_ctx->clauses = ctx.clauses;
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_num_clause(struct kndAttr *attr,
                                  struct LocalContext *ctx,
                                  const char *rec, size_t *total_size)
{
    if (DEBUG_ATTR_SELECT_LEVEL_2)
        knd_log(".. parse num attr \"%.*s\"..",
                attr->name_size, attr->name);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = select_by_attr,
          .obj = ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "_or",
          .name_size = strlen("_or"),
          .parse = parse_logic_OR_val_array,
          .obj = ctx
        }
    };
   
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_str_clause(struct kndAttr *attr,
                                  struct LocalContext *ctx,
                                  const char *rec, size_t *total_size)
{
    if (DEBUG_ATTR_SELECT_LEVEL_2)
        knd_log(".. parse num attr \"%.*s\"..",
                attr->name_size, attr->name);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = select_by_attr,
          .obj = ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "_or",
          .name_size = strlen("_or"),
          .parse = parse_logic_OR_val_array,
          .obj = ctx
        }
    };
   
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

int knd_attr_select_clause(struct kndAttr *attr, struct kndClass *c, struct kndRepo *repo,
                           struct kndTask *task, const char *rec, size_t *total_size)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndAttrStm *attr_stm;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_ATTR_SELECT_LEVEL_TMP) {
        knd_log(".. select by attr \"%.*s\"..",
                attr->name_size, attr->name);
        knd_attr_str(attr, 1);
    }

    struct LocalContext ctx = {
        .task = task,
        .repo = repo,
        .class = c,
        .attr = attr
    };

    switch (attr->type) {
    case KND_ATTR_INNER:
        parser_err = parse_inner_class_clause(attr, &ctx, rec, total_size);
        if (parser_err.code) return parser_err.code;
        break;
    case KND_ATTR_REF:
        parser_err = parse_classref_clause(attr, &ctx, rec, total_size);
        if (parser_err.code) return parser_err.code;
        break;
    case KND_ATTR_NUM:
        parser_err = parse_num_clause(attr, &ctx, rec, total_size);
        if (parser_err.code) return parser_err.code;
        break;
    case KND_ATTR_STR:
        parser_err = parse_str_clause(attr, &ctx, rec, total_size);
        if (parser_err.code) return parser_err.code;
        break;
    default:
        knd_log("-- no clause filtering in attr %.*s",
                attr->name_size, attr->name);
        return knd_FAIL;
    }

    /* if this attr is not indexed (= no precomputed sets available), 
       just add a logical clause to the query */
    if (!attr->is_indexed) {
        /* some logical clauses present */
        if (ctx.clauses) {
            err = knd_attr_stm_new(&attr_stm, mempool);
            if (err) return err;

            attr_stm->attr = attr;
            attr_stm->logic = ctx.logic;
            attr_stm->children = ctx.clauses;

            // attr_stm->next = task->attr_stm;
            // task->attr_stm = attr_stm;
        }
    }
    return knd_OK;
}

extern int knd_attr_stm_match(struct kndAttrStm *self,
                              struct kndAttrStm *query)
{
    // TODO not just numeric types

    if (self->numval != query->numval) return knd_NO_MATCH; 

    return knd_OK;
}

gsl_err_t knd_select_attr_stm(struct kndClass *class, const char *name, size_t name_size,
                              const char *rec, size_t *total_size, struct kndTask *task)
{
    struct kndAttrRef *selected_attr_ref;
    int err;

    err = knd_class_get_attr(class, name, name_size, &selected_attr_ref);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name, class->name_size, class->name);
        task->log->writef(task->log, "%.*s: no such attribute",
                          (int)name_size, name);
        return *total_size = 0, make_gsl_err_external(err);
    }

    if (DEBUG_ATTR_SELECT_LEVEL_TMP) {
        knd_log("++ attr selected: \"%.*s\"..", name_size, name);
    }

    struct LocalContext ctx = {
        .class = class,
        .task = task,
        .selected_attr_ref = selected_attr_ref
    };
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_attr_stm,
          .obj = &ctx
        },
        { .is_default = true,
          .run = present_attr_stm_selection,
          .obj = &ctx
        }
    };

    // TODO array selection
    //if (attr->is_a_set) {
    //    knd_log(".. parsing array selection..");
    //}
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}
