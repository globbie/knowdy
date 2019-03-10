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

    struct kndAttrVar *clauses;
    struct kndAttrVar *attr_var;
    struct kndAttr    *attr;
    knd_logic logic;
};

static gsl_err_t parse_nested_attr_var(void *obj,
                                       const char *name, size_t name_size,
                                       const char *rec, size_t *total_size);

static gsl_err_t set_logic_OR_val(void *obj,
                                  const char *val,
                                  size_t val_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    struct kndAttrVar *attr_var;
    int err;

    if (DEBUG_ATTR_SELECT_LEVEL_2)
        knd_log("== set logic OR val: %.*s", val_size, val);

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return make_gsl_err_external(err);

    attr_var->attr = ctx->attr;
    attr_var->val = val;
    attr_var->val_size = val_size;

    if (ctx->attr->type == KND_ATTR_NUM) {
        memcpy(buf, val, val_size);
        buf_size = val_size;
        buf[buf_size] = '\0';
        err = knd_parse_num(buf, &attr_var->numval);
    }
    
    attr_var->next = ctx->attr_var;
    ctx->attr_var = attr_var;

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

static gsl_err_t run_set_attr_var(void *obj,
                                  const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = ctx->repo;
    struct kndAttr *attr;
    struct kndAttrRef *attr_ref;
    struct kndAttrVar *attr_var;
    struct kndOutput *log = task->log;
    struct kndMemPool *mempool = task->mempool;
    struct kndSet *attr_idx = repo->attr_idx;
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
        e = log->write(log, "-- no attr selected",
                       strlen("-- no attr selected"));
        if (e) return make_gsl_err_external(e);
        task->http_code = HTTP_BAD_REQUEST;
        return make_gsl_err_external(knd_FAIL);
    }

    /*  attr var exists? */
    err = attr_idx->get(attr_idx, attr->id, attr->id_size, &elem);
    if (err) {
        knd_log("-- no attr var \"%.*s\" in local attr idx",
                attr->name_size, attr->name);
        return make_gsl_err_external(err);
    }

    attr_ref = elem;
    attr_var = attr_ref->attr_var;

    if (DEBUG_ATTR_SELECT_LEVEL_TMP) {
        knd_log(".. updating attr var %.*s with value: \"%.*s\"",
                attr_var->name_size, attr_var->name, val_size, val);
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
    state_ref->type = KND_STATE_ATTR_VAR;

    err = knd_state_val_new(mempool, &state_val);
    if (err) {
        knd_log("-- state val alloc failed");
        return make_gsl_err_external(err);
    }

    state_val->obj = (void*)attr_var;
    state_val->val      = val;
    state_val->val_size = val_size;
    state->val = state_val;

    attr_var->val = val;
    attr_var->val_size = val_size;

    state->next = attr_var->states;
    attr_var->states = state;
    attr_var->num_states++;
    state->numid = attr_var->num_states;

    task->type = KND_UPDATE_STATE;

    /* TODO: inform parent class */
    //state_ref->next = task->inner_class_state_refs;
    //task->inner_class_state_refs = state_ref;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t present_attr_var_selection(void *obj,
                                            const char *unused_var(val),
                                            size_t unused_var(val_size))
{
    struct LocalContext *ctx = obj;
    int err;

    // FIXME(k15tfu): assert(ctx->selected_attr_ref->attr_var);

    if (!ctx->selected_attr_ref->attr_var) {
        // FIXME(k15tfu): Why it's empty??
        knd_log("-- not implemented: export empty attr var");
//        err = ctx->task->log->writef(ctx->task->log, "not implemented: export empty attr var");
//        if (err) return make_gsl_err_external(err);
//        return make_gsl_err_external(knd_FAIL);

        // WORKAROUND: export attribute (not attr_var)  FIXME(k15tfu): remove this
        err = knd_attr_export(ctx->selected_attr_ref->attr, KND_FORMAT_GSP/*ctx->task->format*/, ctx->task);
        if (err) return make_gsl_err_external(err);
        return make_gsl_err(gsl_OK);
    }

    err = knd_attr_var_export_GSL(ctx->selected_attr_ref->attr_var, ctx->task, 0);
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
    struct kndAttrVar *attr_var;
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

    if (repo->curr_attr_var) {
        attr_var = repo->curr_attr_var;

        err = out->writec(out, '{');
        if (err) return make_gsl_err_external(err);

        err = knd_attr_var_export_JSON(attr_var, task);
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
    struct kndClass *c;
    struct kndAttrVar *attr_var;
    struct kndAttrFacet *facet;
    struct kndOutput *log = task->log;
    struct kndAttr *attr = ctx->attr;
    int err;

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    c = attr->ref_class;

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
            err = knd_attr_var_new(mempool, &attr_var);
            if (err) return make_gsl_err_external(err);
            attr_var->attr = ctx->attr;

            attr_var->next = ctx->clauses;
            ctx->clauses = attr_var;
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
        log->writef(log, "-- no such facet value: %.*s",
                    val_size, val);
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

static gsl_err_t set_attr_var_value(void *obj, const char *val, size_t val_size)
{
    struct kndAttrVar *self = obj;

    if (DEBUG_ATTR_SELECT_LEVEL_2)
        knd_log(".. set attr var value: %.*s %.*s",
                self->name_size, self->name, val_size, val);

    if (!val_size) return make_gsl_err(gsl_FORMAT);

    self->val = val;
    self->val_size = val_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_attr_var(void *obj,
                                  const char *unused_var(name),
                                  size_t unused_var(name_size))
{
    struct kndAttrVar *attr_var = obj;
    // TODO empty values?
    if (DEBUG_ATTR_SELECT_LEVEL_1) {
        if (!attr_var->val_size)
            knd_log("NB: attr var value not set in %.*s",
                    attr_var->name_size, attr_var->name);
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

    if (DEBUG_ATTR_SELECT_LEVEL_TMP)
        c->str(c, 1);

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

static gsl_err_t parse_nested_attr_var(void *obj,
                                       const char *name, size_t name_size,
                                       const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrVar *self = ctx->attr_var;
    struct kndTask    *task = ctx->task;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err;

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->parent = self;

    attr_var->name = name;
    attr_var->name_size = name_size;
    ctx->attr_var = attr_var;

    if (DEBUG_ATTR_SELECT_LEVEL_TMP)
        knd_log(".. select nested attr: \"%.*s\" REC: %.*s",
                attr_var->name_size, attr_var->name, 64, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_var_value,
          .obj = attr_var
        },
        { .name = "_is",
          .name_size = strlen("_is"),
          .is_selector = true,
          .parse = parse_baseclass_select,
          .obj = ctx
        },
        { .validate = parse_nested_attr_var,
          .obj = ctx
        },
        { .is_default = true,
          .run = confirm_attr_var,
          .obj = attr_var
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (DEBUG_ATTR_SELECT_LEVEL_TMP)
        knd_log("++ attr var: \"%.*s\" val:%.*s",
                attr_var->name_size, attr_var->name,
                attr_var->val_size, attr_var->val);

    //attr_var->next = self->children;
    //self->children = attr_var;
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
        .attr_var = parent_ctx->attr_var
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = select_by_attr,
          .obj = &ctx
        },
        { .validate = parse_nested_attr_var,
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

int knd_attr_select_clause(struct kndAttr *attr,
                           struct kndClass *c,
                           struct kndRepo *repo,
                           struct kndTask *task,
                           const char *rec, size_t *total_size)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndAttrVar *attr_var;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_ATTR_SELECT_LEVEL_TMP) {
        knd_log(".. select by attr \"%.*s\"..",
                attr->name_size, attr->name);
        attr->str(attr, 1);
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
            err = knd_attr_var_new(mempool, &attr_var);
            if (err) return err;

            attr_var->attr = attr;
            attr_var->logic = ctx.logic;
            attr_var->children = ctx.clauses;

            attr_var->next = task->attr_var;
            task->attr_var = attr_var;
        }
    }
     return knd_OK;
}

extern int knd_attr_var_match(struct kndAttrVar *self,
                              struct kndAttrVar *query)
{
    // TODO not just numeric types

    if (self->numval != query->numval) return knd_NO_MATCH; 

    return knd_OK;
}

gsl_err_t
knd_select_attr_var(struct kndClass *class,
                    const char *name, size_t name_size,
                    const char *rec, size_t *total_size,
                    struct kndTask *task)
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
          .run = run_set_attr_var,
          .obj = &ctx
        },
        { .is_default = true,
          .run = present_attr_var_selection,
          .obj = &ctx
        }
    };

    // TODO array selection
    //if (attr->is_a_set) {
    //    knd_log(".. parsing array selection..");
    //}
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}
