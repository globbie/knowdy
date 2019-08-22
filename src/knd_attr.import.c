#include "knd_attr.h"

#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#include <assert.h>
#include <string.h>

#include "knd_task.h"
#include "knd_class.h"
#include "knd_proc.h"
#include "knd_output.h"

#define DEBUG_ATTR_LEVEL_1 0
#define DEBUG_ATTR_LEVEL_2 0
#define DEBUG_ATTR_LEVEL_3 0
#define DEBUG_ATTR_LEVEL_4 0
#define DEBUG_ATTR_LEVEL_5 0
#define DEBUG_ATTR_LEVEL_TMP 1

struct LocalContext {
    struct kndClassVar *class_var;
    struct kndAttrVar *list_parent;
    struct kndAttrVar *attr_var;
    struct kndRepo *repo;
    struct kndTask *task;
};

static gsl_err_t import_attr_var_list_item(void *obj,
                                           const char *rec,
                                           size_t *total_size);

static gsl_err_t import_nested_attr_var(void *obj,
                                        const char *name, size_t name_size,
                                        const char *rec, size_t *total_size);
static void append_attr_var(struct kndClassVar *ci, struct kndAttrVar *attr_var);

static gsl_err_t run_set_name(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = obj;
    self->name = name;
    self->name_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_gloss_locale(void *obj, const char *name, size_t name_size)
{
    struct kndTranslation *self = obj;
    if (name_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);
    self->curr_locale = name;
    self->curr_locale_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_gloss_value(void *obj, const char *name, size_t name_size)
{
    struct kndTranslation *self = obj;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    self->val = name;
    self->val_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss_item(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndAttr *self = task->attr;
    struct kndTranslation *tr;
    struct kndMemPool *mempool = task->mempool;
    int err;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. %.*s: allocate gloss translation",
                self->name_size, self->name);

    err = knd_text_translation_new(mempool, &tr);
    if (err) return *total_size = 0, make_gsl_err_external(knd_NOMEM);
    memset(tr, 0, sizeof(struct kndTranslation));

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_gloss_locale,
          .obj = tr
        },
        { .name = "t",
          .name_size = strlen("t"),
          .run = set_gloss_value,
          .obj = tr
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (tr->curr_locale_size == 0 || tr->val_size == 0)
        return make_gsl_err(gsl_FORMAT);  // error: both of them are required

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. read gloss translation: \"%.*s\",  text: \"%.*s\"",
                tr->locale_size, tr->locale, tr->val_size, tr->val);

    // append
    tr->next = self->tr;
    self->tr = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;

    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .parse = parse_gloss_item,
        .obj = task
    };

    return gsl_parse_array(&item_spec, rec, total_size);
}

static gsl_err_t set_ref_class(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = obj;
    if (!name_size) return make_gsl_err(gsl_FAIL);
    if (!self->name_size) {
        knd_log("-- attr name not specified");
        return make_gsl_err(gsl_FAIL);
    }
    self->ref_classname = name;
    self->ref_classname_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_procref(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = obj;
    if (!name_size) return make_gsl_err(gsl_FAIL);
    if (!self->name_size) {
        knd_log("-- attr name not specified");
        return make_gsl_err(gsl_FAIL);
    }
    self->ref_procname = name;
    self->ref_procname_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_proc(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndAttr *self = task->attr;
    struct kndProc *proc;
    struct kndProcEntry *entry;
    struct kndMemPool *mempool = task->mempool;
    int err;

    err = knd_proc_new(mempool, &proc);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    err = knd_proc_entry_new(mempool, &entry);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    entry->proc = proc;
    proc->entry = entry;

    err = knd_inner_proc_import(proc, rec, total_size, task->repo, task);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    self->proc = proc;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_set_quant(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = (struct kndAttr*)obj;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. run set quant!\n");

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    if (!memcmp("set", name, name_size)) {
        self->quant_type = KND_ATTR_SET;
        self->is_a_set = true;
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_set_quant_uniq(void *obj,
                                    const char *unused_var(name),
                                    size_t unused_var(name_size))
{
    struct kndAttr *self = (struct kndAttr*)obj;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. set is uniq");
    self->set_is_unique = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_set_quant_atomic(void *obj,
                                      const char *unused_var(name),
                                      size_t unused_var(name_size))
{
    struct kndAttr *self = (struct kndAttr*)obj;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. set is atomic");
    self->set_is_atomic = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_attr(void *obj,
                              const char *unused_var(name),
                              size_t unused_var(name_size))
{
    struct kndAttr *attr = obj;

    if (DEBUG_ATTR_LEVEL_TMP) {
        knd_log("++ confirm attr: %.*s",
                attr->name_size, attr->name);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_quant_type(void *obj, const char *rec, size_t *total_size)
{
    struct kndAttr *self = obj;
    if (!self->name_size) {
        knd_log("-- attr name not specified");
        return make_gsl_err(gsl_FAIL);
    }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_quant,
          .obj = self
        },
        { .name = "uniq",
          .name_size = strlen("uniq"),
          .run = run_set_quant_uniq,
          .obj = self
        },
        { .name = "atom",
          .name_size = strlen("atom"),
          .run = run_set_quant_atomic,
          .obj = self
        },
        { .is_default = true,
          .run = confirm_attr,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t confirm_idx(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndAttr *self = obj;

    if (DEBUG_ATTR_LEVEL_1)
        knd_log(".. confirm IDX");
    self->is_indexed = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_implied(void *obj,
                                 const char *unused_var(name),
                                 size_t unused_var(name_size))
{
    struct kndAttr *self = obj;
    self->is_implied = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_attr_var_name(void *obj, const char *name, size_t name_size)
{
    struct kndAttrVar *self = obj;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. set attr var name: %.*s is_list_item:%d val:%.*s",
                name_size, name, self->is_list_item,
                self->val_size, self->val);

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    self->name = name;
    self->name_size = name_size;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t set_attr_var_value(void *obj, const char *val, size_t val_size)
{
    struct kndAttrVar *self = obj;

    if (DEBUG_ATTR_LEVEL_2)
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
    if (DEBUG_ATTR_LEVEL_TMP) {
        if (!attr_var->val_size)
            knd_log("NB: attr var value not set in %.*s",
                    attr_var->name_size, attr_var->name);
    }
    return make_gsl_err(gsl_OK);
}


static gsl_err_t import_nested_attr_var_list(void *obj,
                                             const char *name, size_t name_size,
                                             const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    struct kndAttrVar *parent_attr_var = ctx->attr_var;
    struct kndAttrVar *attr_var;
    int err;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log("== import nested attr_var list: \"%.*s\" REC: %.*s",
                name_size, name, 32, rec);

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) {
            return make_gsl_err(err);
    }
    attr_var->name = name;
    attr_var->name_size = name_size;

    attr_var->next = parent_attr_var->children;
    parent_attr_var->children = attr_var;
    parent_attr_var->num_children++;

    struct LocalContext attr_var_ctx = {
        .list_parent = attr_var,
        .task = task
    };

    struct gslTaskSpec import_attr_var_spec = {
        .is_list_item = true,
        .parse = import_attr_var_list_item,
        .obj = &attr_var_ctx
    };

    return gsl_parse_array(&import_attr_var_spec, rec, total_size);
}

int knd_import_attr_var(struct kndClassVar *self,
                        const char *name, size_t name_size,
                        const char *rec, size_t *total_size,
                        struct kndTask *task)
{
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log("\n.. import attr var: \"%.*s\" REC: %.*s",
                name_size, name, 32, rec);

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return err;
    attr_var->class_var = self;
    attr_var->name = name;
    attr_var->name_size = name_size;

    struct LocalContext ctx = {
        .attr_var = attr_var,
        .task = task
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_var_value,
          .obj = attr_var
        },
        { .type = GSL_SET_STATE,
          .validate = import_nested_attr_var,
          .obj = &ctx
        },
        { .validate = import_nested_attr_var,
          .obj = &ctx
        },
        { .type = GSL_SET_ARRAY_STATE,
          .validate = import_nested_attr_var_list,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = import_nested_attr_var_list,
          .obj = &ctx
        },
        /*{ .name = "_cdata",
          .name_size = strlen("_cdata"),
          .parse = gsl_parse_cdata,
          .obj = &cdata_spec
          }*/
        { .is_default = true,
          .run = confirm_attr_var,
          .obj = attr_var
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- attr var parsing failed: %d", parser_err.code);
        return parser_err.code;
    }

    append_attr_var(self, attr_var);

    if (DEBUG_ATTR_LEVEL_2)
        knd_log("++ attr var value: %.*s", attr_var->val_size, attr_var->val);

    return knd_OK;
}

static gsl_err_t append_attr_var_list_item(void *accu,
                                           void *obj)
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
    //attr_var->list_count = self->num_list_elems;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_attr_var_list_item(void *obj,
                                           const char *rec,
                                           size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttrVar *self = ctx->list_parent;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = task->mempool;
    int err;

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->class_var = self->class_var;
    attr_var->is_list_item = true;
    attr_var->parent = self;
    ctx->attr_var = attr_var;

    if (DEBUG_ATTR_LEVEL_2) {
        knd_log("== importing a list item of %.*s: %.*s",
                self->name_size, self->name, 32, rec);
    }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_var_name,
          .obj = attr_var
        },
        { .validate = import_nested_attr_var,
          .obj = ctx
        },
        { .type = GSL_SET_ARRAY_STATE,
          .validate = import_nested_attr_var_list,
          .obj = ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = import_nested_attr_var_list,
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
    // append
    return append_attr_var_list_item(self, attr_var);
}

int knd_import_attr_var_list(struct kndClassVar *self,
                             const char *name, size_t name_size,
                             const char *rec, size_t *total_size,
                             struct kndTask *task)
{
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err, e;

    if (!self->entry) {
        knd_log("-- anonymous class var: %.*s?  REC:%.*s",
                64, rec);
        struct kndOutput *log = task->log; 
        log->reset(log);
        e = log->write(log, "no baseclass name specified",
                     strlen("no baseclass name specified"));
        if (e) return e;
        task->http_code = HTTP_BAD_REQUEST;
        return knd_FAIL;
    } 

    if (DEBUG_ATTR_LEVEL_2)
        knd_log("== import attr attr_var list: \"%.*s\" REC: %.*s",
                name_size, name, 32, rec);

    err = knd_attr_var_new(mempool, &attr_var);                                   RET_ERR();
    attr_var->class_var = self;
    attr_var->name = name;
    attr_var->name_size = name_size;

    append_attr_var(self, attr_var);

    struct LocalContext ctx = {
        .list_parent = attr_var,
        //.attr_var = attr_var,
        .task = task
    };

    struct gslTaskSpec import_attr_var_spec = {
        .is_list_item = true,
        .parse = import_attr_var_list_item,
        .obj = &ctx
    };

    parser_err = gsl_parse_array(&import_attr_var_spec, rec, total_size);
    if (parser_err.code) return parser_err.code;

    return knd_OK;
}

static gsl_err_t import_nested_attr_var(void *obj,
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
    attr_var->class_var = self->class_var;
    attr_var->parent = self;

    attr_var->name = name;
    attr_var->name_size = name_size;

    ctx->attr_var = attr_var;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. import nested attr var: \"%.*s\" (parent item:%.*s)",
                attr_var->name_size, attr_var->name,
                self->name_size, self->name);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_var_value,
          .obj = attr_var
        },
        { .type = GSL_SET_STATE,
          .validate = import_nested_attr_var,
          .obj = ctx
        },
        { .validate = import_nested_attr_var,
          .obj = ctx
        }/*,
        { .name = "_cdata",
          .name_size = strlen("_cdata"),
          .parse = parse_attr_var_cdata,
          .obj = attr_var
          }*/,
        { .is_default = true,
          .run = confirm_attr_var,
          .obj = attr_var
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- attr var import failed: %d", parser_err.code);
        return parser_err;
    }

    /* restore parent */
    ctx->attr_var = self;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log("++ attr var: \"%.*s\" val:%.*s (parent item: %.*s)",
                attr_var->name_size, attr_var->name,
                attr_var->val_size, attr_var->val,
                self->name_size, self->name);

    attr_var->next = self->children;
    self->children = attr_var;
    self->num_children++;

    return make_gsl_err(gsl_OK);
}

static void append_attr_var(struct kndClassVar *ci,
                            struct kndAttrVar *attr_var)
{
    struct kndAttrVar *curr_var;

    for (curr_var = ci->attrs; curr_var; curr_var = curr_var->next) {
        if (curr_var->name_size != attr_var->name_size) continue;
        if (!memcmp(curr_var->name, attr_var->name, attr_var->name_size)) {
            if (!curr_var->list_tail) {
                curr_var->list_tail = attr_var;
                curr_var->list = attr_var;
            }
            else {
                curr_var->list_tail->next = attr_var;
                curr_var->list_tail = attr_var;
            }
            curr_var->num_list_elems++;
            return;
        }
    }

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

gsl_err_t knd_import_attr(struct kndTask *task, const char *rec, size_t *total_size)
{
    struct kndAttr *self = task->attr;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = parse_gloss,
          .obj = task
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = parse_gloss,
          .obj = task
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = parse_gloss,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "c",
          .name_size = strlen("c"),
          .run = set_ref_class,
          .obj = self
        },
        { .name = "c",
          .name_size = strlen("c"),
          .run = set_ref_class,
          .obj = self
        },
        { .name = "_proc",
          .name_size = strlen("_proc"),
          .run = set_procref,
          .obj = self
        },
        { .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc,
          .obj = task
        },
        { .name = "t",
          .name_size = strlen("t"),
          .parse = parse_quant_type,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "idx",
          .name_size = strlen("idx"),
          .run = confirm_idx,
          .obj = self
        },
        { .name = "idx",
          .name_size = strlen("idx"),
          .run = confirm_idx,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "impl",
          .name_size = strlen("impl"),
          .run = confirm_implied,
          .obj = self
        },
        { .name = "impl",
          .name_size = strlen("impl"),
          .run = confirm_implied,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "concise",
          .name_size = strlen("concise"),
          .parse = gsl_parse_size_t,
          .obj = &self->concise_level
        },
        { .name = "concise",
          .name_size = strlen("concise"),
          .parse = gsl_parse_size_t,
          .obj = &self->concise_level
        }
    };
    gsl_err_t err;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. attr parsing: \"%.*s\"..", 32, rec);

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (self->type == KND_ATTR_INNER) {
        if (!self->ref_classname_size) {
            knd_log("-- ref class not specified in %.*s",
                    self->name_size, self->name);
            return make_gsl_err_external(knd_FAIL);
        }
    }

    // TODO: reject attr names starting with an underscore _

    return make_gsl_err(gsl_OK);
}
