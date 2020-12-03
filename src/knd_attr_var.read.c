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
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_state.h"
#include "knd_user.h"
#include "knd_repo.h"
#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_shared_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#define DEBUG_ATTR_VAR_READ_LEVEL_1 0
#define DEBUG_ATTR_VAR_READ_LEVEL_2 0
#define DEBUG_ATTR_VAR_READ_LEVEL_3 0
#define DEBUG_ATTR_VAR_READ_LEVEL_4 0
#define DEBUG_ATTR_VAR_READ_LEVEL_5 0
#define DEBUG_ATTR_VAR_READ_LEVEL_TMP 1

struct LocalContext {
    struct kndClassVar *class_var;
    struct kndClass    *class;
    struct kndClass    *inner_class;
    struct kndAttrVar  *list_parent;
    struct kndSet      *attr_idx;
    struct kndAttr     *attr;
    struct kndAttrVar  *attr_var;
    struct kndRepo     *repo;
    struct kndTask     *task;
};

static gsl_err_t read_attr_var_list_item(void *obj, const char *rec, size_t *total_size);
static gsl_err_t read_nested_attr_var(void *obj, const char *name, size_t name_size,
                                      const char *rec, size_t *total_size);
static gsl_err_t set_attr_var_value(void *obj, const char *val, size_t val_size);
static gsl_err_t confirm_attr_var(void *obj, const char *unused_var(name), size_t unused_var(name_size));

static gsl_err_t parse_text(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndText *text;
    gsl_err_t parser_err;
    int err;

    err = knd_text_new(mempool, &text);
    if (err) return *total_size = 0, make_gsl_err_external(knd_NOMEM);

    parser_err = knd_text_read(text, rec, total_size, task);
    if (parser_err.code) {
        KND_TASK_LOG("text read failed");
        return parser_err;
    }
    ctx->attr_var->text = text;
    text->attr_var = ctx->attr_var;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_nested_attr_var_list(void *obj, const char *id, size_t id_size,
                                           const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndAttrVar *parent_attr_var = ctx->attr_var;
    struct kndAttrVar *attr_var;
    struct kndAttrRef *ref;
    struct kndAttr *attr;
    int err;

    if (DEBUG_ATTR_VAR_READ_LEVEL_2)
        knd_log(".. reading nested attr_var list: \"%.*s\" REC: %.*s", id_size, id, 32, rec);

    assert(ctx->class != NULL);

    err = knd_set_get(ctx->class->attr_idx, id, id_size, (void**)&ref);
    if (err) {
        KND_TASK_LOG("class \"%.*s\" has no such attr: %.*s",
                     ctx->class->name_size, ctx->class->name, id_size, id);
        return *total_size = 0, make_gsl_err_external(err);
    }
    assert(ref->attr != NULL);
    attr = ref->attr;

    if (DEBUG_ATTR_VAR_READ_LEVEL_2)
        knd_log(">> attr decoded: %.*s  (type:%d)", attr->name_size, attr->name, attr->type);

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) {
        return make_gsl_err(err);
    }
    attr_var->attr = attr;
    attr_var->name = attr->name;
    attr_var->name_size = attr->name_size;

    attr_var->next = parent_attr_var->children;
    parent_attr_var->children = attr_var;
    parent_attr_var->num_children++;

    struct LocalContext attr_var_ctx = {
        .list_parent = attr_var,
        .task = task
    };
    struct gslTaskSpec read_attr_var_spec = {
        .is_list_item = true,
        .parse = read_attr_var_list_item,
        .obj = &attr_var_ctx
    };

    switch (attr->type) {
    case KND_ATTR_INNER:
        assert(attr->ref_class_entry != NULL);

        err = knd_class_acquire(attr->ref_class_entry, &attr_var_ctx.class, task);
        if (err) {
            KND_TASK_LOG("failed to acquire class \"%.*s\"",
                         attr->ref_class_entry->name_size, attr->ref_class_entry->name);
            return *total_size = 0, make_gsl_err_external(err);
        }
        if (DEBUG_ATTR_VAR_READ_LEVEL_2)
            knd_log(">> list inner class: \"%.*s\"",
                    attr->ref_class_entry->name_size, attr->ref_class_entry->name);
        break;
    default:
        break;
    }
    return gsl_parse_array(&read_attr_var_spec, rec, total_size);
}

static gsl_err_t read_nested_attr_var(void *obj, const char *id, size_t id_size,
                                      const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrVar *self = ctx->attr_var;
    struct kndTask    *task = ctx->task;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndAttrRef *ref;
    struct kndAttr *attr;
    gsl_err_t parser_err;
    int err;

    assert(ctx->class != NULL);

    err = knd_set_get(ctx->class->attr_idx, id, id_size, (void**)&ref);
    if (err) {
        KND_TASK_LOG("class \"%.*s\" has no such attr: %.*s",
                     ctx->class->name_size, ctx->class->name, id_size, id);
        return *total_size = 0, make_gsl_err_external(err);
    }
    assert(ref->attr != NULL);
    attr = ref->attr;

    if (DEBUG_ATTR_VAR_READ_LEVEL_2)
        knd_log(">> attr decoded: %.*s (type:%d)", attr->name_size, attr->name, attr->type);
    
    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->parent = self;
    attr_var->attr = attr;
    attr_var->name = attr->name;
    attr_var->name_size = attr->name_size;

    struct LocalContext attr_var_ctx = {
        .attr_var = attr_var,
        .task = task
    };

    switch (attr->type) {
    case KND_ATTR_INNER:
        assert(attr->ref_class_entry != NULL);
        err = knd_class_acquire(attr->ref_class_entry, &attr_var_ctx.class, task);
        if (err) {
            KND_TASK_LOG("failed to acquire class \"%.*s\"",
                         attr->ref_class_entry->name_size, attr->ref_class_entry->name);
            return *total_size = 0, make_gsl_err_external(err);
        }
        if (DEBUG_ATTR_VAR_READ_LEVEL_2)
            knd_log(">> attr var inner class: \"%.*s\"",
                    attr->ref_class_entry->name_size, attr->ref_class_entry->name);
        break;
    default:
        break;
    }
    if (DEBUG_ATTR_VAR_READ_LEVEL_2)
        knd_log(".. read nested attr var: \"%.*s\" (parent item:%.*s)",
                attr_var->name_size, attr_var->name, self->name_size, self->name);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_var_value,
          .obj = &attr_var_ctx
        },
        { .name = "_t",
          .name_size = strlen("_t"),
          .parse = parse_text,
          .obj = &attr_var_ctx
        },
        { .validate = read_nested_attr_var,
          .obj = &attr_var_ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = read_nested_attr_var_list,
          .obj = &attr_var_ctx
        },
        { .is_default = true,
          .run = confirm_attr_var,
          .obj = attr_var
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        KND_TASK_LOG("attr var reading failed: %d", parser_err.code);
        return parser_err;
    }

    if (DEBUG_ATTR_VAR_READ_LEVEL_2)
        knd_log("++ attr var: \"%.*s\" val:%.*s (parent item: %.*s)",
                attr_var->name_size, attr_var->name, attr_var->val_size, attr_var->val,
                self->name_size, self->name);

    attr_var->next = self->children;
    self->children = attr_var;
    self->num_children++;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_attr_var_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrVar *self = ctx->attr_var;
    struct kndTask    *task = ctx->task;
    struct kndClass *c = ctx->class;
    struct kndCharSeq *seq;
    struct kndAttr *attr = self->parent ? self->parent->attr : self->attr;
    int err;

    assert(attr != NULL);

    if (DEBUG_ATTR_VAR_READ_LEVEL_2)
        knd_log(".. set \"%.*s\" (%d) attr var name \"%.*s\"",
                attr->name_size, attr->name, attr->type, name_size, name);

    if (!name_size) return make_gsl_err(gsl_FORMAT);

    if (c && c->implied_attr) {
        if (DEBUG_ATTR_VAR_READ_LEVEL_2)
            knd_log(">> implied attr: %.*s (type:%d)",
                    c->implied_attr->name_size, c->implied_attr->name, c->implied_attr->type);

        self->implied_attr = c->implied_attr;
        attr = c->implied_attr;
    }

    switch (attr->type) {
    case KND_ATTR_REL:
        // fall through
    case KND_ATTR_REF:
        err = knd_get_class_entry_by_id(task->repo, name, name_size, &self->class_entry, task);
        if (err) {
            KND_TASK_LOG("no such class entry: %.*s", name_size, name);
            if (err) return make_gsl_err_external(err);
        }
        if (DEBUG_ATTR_VAR_READ_LEVEL_3)
            knd_log("== REF: %.*s", self->class_entry->name_size, self->class_entry->name);
            break;
    case KND_ATTR_STR:
        err = knd_charseq_decode(task->repo, name, name_size, &seq, task);
        if (err) {
            KND_TASK_LOG("failed to decode a charseq");
            if (err) return make_gsl_err_external(err);
        }
        self->name = seq->val;
        self->name_size = seq->val_size;
        break;
    default:
        self->name = name;
        self->name_size = name_size;
        break;
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_attr_var_value(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndAttrVar *self = ctx->attr_var;
    struct kndTask    *task = ctx->task;
    struct kndRepo *repo = task->repo;
    struct kndClassEntry *entry;
    struct kndCharSeq *seq;
    int err;

    if (DEBUG_ATTR_VAR_READ_LEVEL_2)
        knd_log(".. set \"%.*s\" (%d) attr var value: \"%.*s\" => \"%.*s\"",
                self->attr->name_size, self->attr->name, self->attr->type,
                self->name_size, self->name, val_size, val);

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    self->val = val;
    self->val_size = val_size;

    switch (self->attr->type) {
    case KND_ATTR_NUM:
    case KND_ATTR_FLOAT:
    case KND_ATTR_STR:
        err = knd_charseq_decode(task->repo, val, val_size, &seq, task);
        if (err) {
            KND_TASK_LOG("failed to decode a charseq");
            if (err) return make_gsl_err_external(err);
        }
        if (DEBUG_ATTR_VAR_READ_LEVEL_2)
            knd_log(">> \"%.*s\" => decoded str val:%.*s",
                    self->name_size, self->name, seq->val_size, seq->val);
        self->val = seq->val;
        self->val_size = seq->val_size;
        break;
    case KND_ATTR_REL:
        // fall through
    case KND_ATTR_REF:
        err = knd_shared_set_get(repo->class_idx, val, val_size, (void**)&entry);
        if (err) {
            KND_TASK_LOG("class \"%.*s\" not found in repo %.*s",
                         val_size, val, repo->name_size, repo->name);
            return make_gsl_err(gsl_FAIL);
        }
        self->class_entry = entry;
        
        if (DEBUG_ATTR_VAR_READ_LEVEL_3)
            knd_log(">> set class ref: %.*s (id:%.*s)", entry->name_size, entry->name, val_size, val);
        break;
    default:
        break;
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_attr_var(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttrVar *attr_var = obj;

    // TODO empty values?
    if (DEBUG_ATTR_VAR_READ_LEVEL_2) {
        if (!attr_var->val_size)
            knd_log("NB: attr var value not set in %.*s (class: %.*s)",
                    attr_var->name_size, attr_var->name,
                    attr_var->class_var->entry->name_size,
                    attr_var->class_var->entry->name);
    }
    return make_gsl_err(gsl_OK);
}

static void append_attr_var(struct kndClassVar *ci, struct kndAttrVar *attr_var)
{
    if (!ci->tail) {
        ci->tail  = attr_var;
        ci->attrs = attr_var;
    }
    else {
        ci->tail->next = attr_var;
        ci->tail = attr_var;
    }
    ci->num_attrs++;
}

static gsl_err_t append_attr_var_list_item(void *accu, void *obj)
{
    struct kndAttrVar *self = accu;
    struct kndAttrVar *attr_var = obj;

    if (!self->list_tail) {
        self->list_tail = attr_var;
        self->list = attr_var;
    }
    else {
        self->list_tail->next = attr_var;
        self->list_tail = attr_var;
    }
    self->num_list_elems++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_attr_var_list_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttrVar *self = ctx->list_parent;
    struct kndAttrVar *attr_var, *prev_attr_var;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    int err;

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->is_list_item = true;
    attr_var->parent = self;

    prev_attr_var = ctx->attr_var;
    ctx->attr_var = attr_var;

    if (DEBUG_ATTR_VAR_READ_LEVEL_2)
        knd_log("== reading a list var of \"%.*s\": %.*s", self->name_size, self->name, 32, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_var_name,
          .obj = ctx
        },
        { .validate = read_nested_attr_var,
          .obj = ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = read_nested_attr_var_list,
          .obj = ctx
        },
        { .is_default = true,
          .run = confirm_attr_var,
          .obj = attr_var
        }        
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        return parser_err;
    }
    ctx->attr_var = prev_attr_var;

    // append
    return append_attr_var_list_item(self, attr_var);
}

int knd_read_attr_var_list(struct kndClassVar *self, const char *id, size_t id_size,
                           const char *rec, size_t *total_size, struct kndTask *task)
{
    struct kndAttrVar *var;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndAttrRef *ref;
    struct kndAttr *attr;
    struct kndClassEntry *entry = self->entry;
    struct kndClass *c;
    gsl_err_t parser_err;
    int err;

    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire class \"%.*s\"", entry->name_size, entry->name);

    err = knd_set_get(c->attr_idx, id, id_size, (void**)&ref);
    KND_TASK_ERR("\"%.*s\" attr not found in class \"%.*s\" ", id_size, id, c->name_size, c->name);
    attr = ref->attr;

    if (DEBUG_ATTR_VAR_READ_LEVEL_2)
        knd_log(">> class %.*s to read var list \"%.*s\" (%.*s) REC: %.*s",
                entry->name_size, entry->name, attr->name_size, attr->name, id_size, id, 32, rec);

    err = knd_attr_var_new(mempool, &var);
    KND_TASK_ERR("failed to alloc attr var");
    var->class_var = self;
    var->attr = ref->attr;
    var->name = ref->attr->name;
    var->name_size = ref->attr->name_size;
    append_attr_var(self, var);

    struct LocalContext ctx = {
        .list_parent = var,
        .class = c,
        .task = task
    };

    switch (attr->type) {
    case KND_ATTR_INNER:
        assert(attr->ref_class_entry != NULL);

        err = knd_class_acquire(attr->ref_class_entry, &ctx.class, task);
        KND_TASK_ERR("failed to acquire class \"%.*s\"",
                     attr->ref_class_entry->name_size, attr->ref_class_entry->name);

        if (DEBUG_ATTR_VAR_READ_LEVEL_2)
            knd_log(">> a list of inner class \"%.*s\"",
                    attr->ref_class_entry->name_size, attr->ref_class_entry->name);
        break;
    default:
        break;
    }

    struct gslTaskSpec read_attr_var_spec = {
        .is_list_item = true,
        .parse = read_attr_var_list_item,
        .obj = &ctx
    };
    parser_err = gsl_parse_array(&read_attr_var_spec, rec, total_size);
    if (parser_err.code) return parser_err.code;
    return knd_OK;
}

int knd_read_attr_var(struct kndClassVar *self, const char *id, size_t id_size,
                      const char *rec, size_t *total_size, struct kndTask *task)
{
    struct kndAttrRef *ref;
    struct kndAttrVar *var;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndClassEntry *entry = self->entry;
    struct kndClass *c;
    gsl_err_t parser_err;
    int err;

    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to get class %.*s", entry->name_size, entry->name);

    err = knd_set_get(c->attr_idx, id, id_size, (void**)&ref);
    KND_TASK_ERR("no attr \"%.*s\" in class \"%.*s\"", id_size, id, entry->name_size, entry->name);

    if (DEBUG_ATTR_VAR_READ_LEVEL_TMP)
        knd_log(".. reading attr var \"%.*s\" of class: \"%.*s\"",
                ref->attr->name_size, ref->attr->name, entry->name_size, entry->name);

    err = knd_attr_var_new(mempool, &var);
    KND_TASK_ERR("failed to alloc an attr var");
    var->class_var = self;
    var->name = ref->attr->name;
    var->name_size = ref->attr->name_size;
    var->attr = ref->attr;

    struct LocalContext ctx = {
        .attr_var = var,
        .class = c,
        .task = task
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_var_value,
          .obj = &ctx
        },
        { .name = "_t",
          .name_size = strlen("_t"),
          .parse = parse_text,
          .obj = &ctx
        },
        { .validate = read_nested_attr_var,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = read_nested_attr_var_list,
          .obj = &ctx
        },
        { .is_default = true,
          .run = confirm_attr_var,
          .obj = var
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- attr var parsing failed: %d", parser_err.code);
        return parser_err.code;
    }
    append_attr_var(self, var);
    return knd_OK;
}
