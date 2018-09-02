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

#define DEBUG_CLASS_IMPORT_LEVEL_1 0
#define DEBUG_CLASS_IMPORT_LEVEL_2 0
#define DEBUG_CLASS_IMPORT_LEVEL_3 0
#define DEBUG_CLASS_IMPORT_LEVEL_4 0
#define DEBUG_CLASS_IMPORT_LEVEL_5 0
#define DEBUG_CLASS_IMPORT_LEVEL_TMP 1

static gsl_err_t import_nested_attr_var(void *obj,
                                         const char *name, size_t name_size,
                                         const char *rec, size_t *total_size);
static void append_attr_var(struct kndClassVar *ci,
                             struct kndAttrVar *attr_var);

extern gsl_err_t knd_parse_import_class_inst(void *data,
                                             const char *rec,
                                             size_t *total_size)
{
    struct kndClass *self = data;
    struct kndClass *c;
    struct kndClassInst *obj;
    struct kndObjEntry *entry;
    struct kndSet *set;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    struct kndTask *task = self->entry->repo->task;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2) {
        knd_log(".. import \"%.*s\" inst..", 128, rec);
    }
    if (!self->curr_class) {
        knd_log("-- class not specified :(");
        return *total_size = 0, make_gsl_err(gsl_FAIL);
    }

    // TODO class copy?

    err = mempool->new_class_inst(mempool, &obj);
    if (err) {
        knd_log("-- class inst alloc failed :(");
        return *total_size = 0, make_gsl_err_external(err);
    }
    err = mempool->new_state(mempool, &obj->states);
    if (err) {
        knd_log("-- state alloc failed :(");
        return *total_size = 0, make_gsl_err_external(err);
    }
    err = mempool->new_class_inst_entry(mempool, &entry);
    if (err) return make_gsl_err_external(err);

    obj->entry = entry;
    entry->obj = obj;
    obj->states->phase = KND_SUBMITTED;
    obj->base = self->curr_class;

    parser_err = obj->parse(obj, rec, total_size);
    if (parser_err.code) return parser_err;

    c = obj->base;
    obj->next = c->obj_inbox;

    c->obj_inbox = obj;
    c->obj_inbox_size++;
    c->num_objs++;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log("++ %.*s obj parse OK! total objs in %.*s: %zu",
                obj->name_size, obj->name,
                c->name_size, c->name, c->obj_inbox_size);

    obj->entry->numid = c->num_objs;
    knd_num_to_str(obj->entry->numid, obj->entry->id, &obj->entry->id_size, KND_RADIX_BASE);

    if (!c->entry) {
        if (c->root_class) {
            knd_log("-- no entry in %.*s :(", c->name_size, c->name);
            return make_gsl_err(gsl_FAIL);
        }
        return make_gsl_err(gsl_OK);
    }

    /* automatic name assignment if no explicit name given */
    if (!obj->name_size) {
        obj->name = obj->entry->id;
        obj->name_size = obj->entry->id_size;
    }

    if (!c->entry->inst_name_idx) {
        err = ooDict_new(&c->entry->inst_name_idx, KND_HUGE_DICT_SIZE);
        if (err) return make_gsl_err_external(err);
    }

    err = c->entry->inst_name_idx->set(c->entry->inst_name_idx,
                                      obj->name, obj->name_size,
                                      (void*)entry);
    if (err) return make_gsl_err_external(err);
    c->entry->num_insts++;

    set = c->entry->inst_idx;
    /* index by id */
    if (!set) {
        err = mempool->new_set(mempool, &set);
        if (err) return make_gsl_err_external(err);
        set->type = KND_SET_CLASS_INST;
        c->entry->inst_idx = set;
    }
    err = set->add(set, entry->id, entry->id_size, (void*)entry);
    if (err) {
        knd_log("-- failed to update the class inst idx");
        return make_gsl_err_external(err);
    }

    if (DEBUG_CLASS_IMPORT_LEVEL_2) {
        knd_log("++ OBJ registered in \"%.*s\" IDX:  [total:%zu valid:%zu]",
                c->name_size, c->name, c->entry->inst_name_idx->size, c->entry->num_insts);
        obj->depth = self->depth + 1;
        obj->str(obj);
    }
    task->type = KND_UPDATE_STATE;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_class_name(void *obj, const char *name, size_t name_size)
{
    struct kndClass *self = obj;
    struct kndRepo *repo = self->root_class->entry->repo;
    struct glbOutput *log = repo->log;
    struct ooDict *class_name_idx = self->root_class->class_name_idx;
    struct kndMemPool *mempool = repo->mempool;
    struct kndTask *task = repo->task;
    struct kndClassEntry *entry;
    struct kndState *state;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. set class name: %.*s",
                name_size, name);

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= sizeof self->entry->name) return make_gsl_err(gsl_LIMIT);

    entry = class_name_idx->get(class_name_idx, name, name_size);
    if (!entry) {
        err = mempool->new_class_entry(mempool, &entry);
        if (err) return make_gsl_err_external(err);
        entry->repo = repo;
        entry->class = self;
        self->entry = entry;

        memcpy(entry->name, name, name_size);
        entry->name_size = name_size;
        self->name = self->entry->name;
        self->name_size = name_size;

        err = class_name_idx->set(class_name_idx,
                                  entry->name, name_size,
                                  (void*)entry);
        if (err) return make_gsl_err_external(err);

        return make_gsl_err(gsl_OK);
    }

    /* class entry already exists */

    if (!entry->class) {
        entry->class =    self;
        self->entry =     entry;
        self->name =      entry->name;
        self->name_size = name_size;
        return make_gsl_err(gsl_OK);
    }

    if (entry->class->states) {
        state = entry->class->states;
        if (state->phase == KND_REMOVED) {
            entry->class = self;
            self->entry =  entry;

            if (DEBUG_CLASS_IMPORT_LEVEL_2)
                knd_log("== class was removed recently");

            self->name =      entry->name;
            self->name_size = name_size;
            return make_gsl_err(gsl_OK);
        }
    }

    knd_log("-- \"%.*s\" class doublet found :(", name_size, name);
    log->reset(log);
    err = log->write(log, name, name_size);
    if (err) return make_gsl_err_external(err);

    err = log->write(log,   " class name already exists",
                     strlen(" class name already exists"));
    if (err) return make_gsl_err_external(err);

    task->http_code = HTTP_CONFLICT;

    return make_gsl_err(gsl_FAIL);
}

static gsl_err_t set_class_var(void *obj, const char *name, size_t name_size)
{
    struct kndClassVar *self      = obj;
    struct kndClass *root_class   = self->root_class;
    struct ooDict *class_name_idx = root_class->class_name_idx;
    struct kndMemPool *mempool    = root_class->entry->repo->mempool;
    struct kndClassEntry *entry;
    void *result;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. root class:\"%.*s\" to check class var name: %.*s",
                root_class->entry->name_size,
                root_class->entry->name,
                name_size, name);
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    result = class_name_idx->get(class_name_idx, name, name_size);
    if (result) {
        self->entry = result;
        return make_gsl_err(gsl_OK);
    }

    /* register new class entry */
    err = mempool->new_class_entry(mempool, &entry);
    if (err) return make_gsl_err_external(err);

    memcpy(entry->name, name, name_size);
    entry->name_size = name_size;

    err = class_name_idx->set(class_name_idx,
                              entry->name, name_size,
                              (void*)entry);
    if (err) return make_gsl_err_external(err);

    entry->repo = root_class->entry->repo;
    self->entry = entry;

    return make_gsl_err(gsl_OK);
}



static gsl_err_t parse_aggr(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndAttr *attr;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. parsing the AGGR attr: \"%.*s\"", 32, rec);

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_AGGR;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        if (DEBUG_CLASS_IMPORT_LEVEL_TMP)
            knd_log("-- failed to parse the AGGR attr: %d", parser_err.code);
        return parser_err;
    }

    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }
    self->num_attrs++;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        attr->str(attr);

    if (attr->is_implied)
        self->implied_attr = attr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_str(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndClass *self = (struct kndClass*)obj;
    struct kndAttr *attr;
    int err;
    gsl_err_t parser_err;

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_STR;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        knd_log("-- failed to parse the STR attr of \"%.*s\" :(",
                self->entry->name_size, self->entry->name);
        return parser_err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    if (attr->is_implied)
        self->implied_attr = attr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_bin(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndClass *self = (struct kndClass*)obj;
    struct kndAttr *attr;
    gsl_err_t parser_err;
    int  err;

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_BIN;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        knd_log("-- failed to parse the BIN attr: %d", parser_err.code);
        return parser_err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_cdata(void *obj,
                             const char *rec,
                             size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndAttr *attr;
    gsl_err_t parser_err;
    int  err;

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_CDATA;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        knd_log("-- failed to parse the BIN attr: %d", parser_err.code);
        return parser_err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_num(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndClass *self = (struct kndClass*)obj;
    struct kndAttr *attr;
    int err;
    gsl_err_t parser_err;

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_NUM;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        knd_log("-- failed to parse the NUM attr: %d", parser_err.code);
        return parser_err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    if (attr->is_implied)
        self->implied_attr = attr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_ref(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndAttr *attr;
    int err;
    gsl_err_t parser_err;

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_REF;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        knd_log("-- failed to parse the REF attr: %d", parser_err.code);
        return parser_err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    if (attr->is_implied)
        self->implied_attr = attr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_proc(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndAttr *attr;
    int err;
    gsl_err_t parser_err;

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_PROC;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        knd_log("-- failed to parse the PROC attr: %d", parser_err.code);
        return parser_err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    if (attr->is_implied)
        self->implied_attr = attr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_text(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndClass *self = (struct kndClass*)obj;
    struct kndAttr *attr;
    int err;
    gsl_err_t parser_err;

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_TEXT;
    attr->is_a_set = true;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        knd_log("-- failed to parse the TEXT attr: %d", parser_err.code);
        return parser_err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }
    return make_gsl_err(gsl_OK);
}


static gsl_err_t import_attr_var(void *obj,
                                 const char *name, size_t name_size,
                                 const char *rec, size_t *total_size)
{
    struct kndClassVar *self = obj;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool;
    struct ooDict *attr_name_idx;
    struct kndAttrEntry *entry;
    gsl_err_t parser_err;
    int err;

    // TODO

    /* class var not resolved */
    if (!self->entry) {
        knd_log("-- anonymous class var: %.*s?  REC:%.*s",
                64, rec);
        //return *total_size = 0, make_gsl_err_external(knd_FAIL);
    } 

    mempool = self->root_class->entry->repo->mempool;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. import attr var: \"%.*s\" REC: %.*s",
                name_size, name, 64, rec);

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) {
        knd_log("-- attr item mempool exhausted");
        return *total_size = 0, make_gsl_err_external(err);
    }
    attr_var->class_var = self;

    memcpy(attr_var->name, name, name_size);
    attr_var->name_size = name_size;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = attr_var->val,
          .buf_size = &attr_var->val_size,
          .max_buf_size = sizeof attr_var->val
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = import_nested_attr_var,
          .obj = attr_var
        },
        { .is_validator = true,
          .validate = import_nested_attr_var,
          .obj = attr_var
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    append_attr_var(self, attr_var);

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log("++ attr var import OK!");

    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_attr_var_alloc(void *obj,
                                        const char *name __attribute__((unused)),
                                        size_t name_size __attribute__((unused)),
                                        size_t count  __attribute__((unused)),
                                        void **result)
{
    struct kndAttrVar *self = obj;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = self->class_var->entry->repo->mempool;
    int err;

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) return make_gsl_err_external(err);
    attr_var->class_var = self->class_var;

    attr_var->is_list_item = true;

    *result = attr_var;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_attr_var_append(void *accu,
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
    attr_var->list_count = self->num_list_elems;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_nested_attr_var(void *obj,
                                        const char *rec,
                                        size_t *total_size)
{
    struct kndAttrVar *item = obj;

    if (DEBUG_CLASS_IMPORT_LEVEL_1)
        knd_log(".. parse import attr item..");

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = item->name,
          .buf_size = &item->name_size,
          .max_buf_size = sizeof item->name
        },
        { .is_validator = true,
          .validate = import_nested_attr_var,
          .obj = item
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_attr_var_list(void *obj,
                                       const char *name, size_t name_size,
                                       const char *rec, size_t *total_size)
{
    struct kndClassVar *self = obj;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_1)
        knd_log("== import attr attr_var list: \"%.*s\" REC: %.*s",
                name_size, name, 32, rec);

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->class_var = self;

    memcpy(attr_var->name, name, name_size);
    attr_var->name_size = name_size;

    append_attr_var(self, attr_var);

    struct gslTaskSpec import_attr_var_spec = {
        .is_list_item = true,
        .accu =   attr_var,
        .alloc =  import_attr_var_alloc,
        .append = import_attr_var_append,
        .parse =  parse_nested_attr_var
    };

    parser_err = gsl_parse_array(&import_attr_var_spec, rec, total_size);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_attr_var(void *obj __attribute__((unused)),
                                  const char *name __attribute__((unused)),
                                  size_t name_size __attribute__((unused)))
{
    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_nested_attr_var(void *obj,
                                         const char *name, size_t name_size,
                                         const char *rec, size_t *total_size)
{
    struct kndAttrVar *self = obj;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = self->class_var->root_class->entry->repo->mempool;
    gsl_err_t parser_err;
    int err;

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) {
        knd_log("-- mempool exhausted: attr item");
        return *total_size = 0, make_gsl_err_external(err);
    }
    attr_var->class_var = self->class_var;

    memcpy(attr_var->name, name, name_size);
    attr_var->name_size = name_size;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. import nested attr: \"%.*s\" REC: %.*s",
                attr_var->name_size, attr_var->name, 16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = attr_var->val,
          .buf_size = &attr_var->val_size,
          .max_buf_size = sizeof attr_var->val
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = import_nested_attr_var,
          .obj = attr_var
        },
        { .is_validator = true,
          .validate = import_nested_attr_var,
          .obj = attr_var
        },
        { .is_default = true,
          .run = confirm_attr_var,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log("++ attr var: \"%.*s\" val:%.*s",
                attr_var->name_size, attr_var->name,
                attr_var->val_size, attr_var->val);

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


extern gsl_err_t parse_class_var(struct kndClassVar *self,
                                 const char *rec,
                                 size_t *total_size)
{
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_var,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = import_attr_var,
          .obj = self
        },
        { .is_validator = true,
          .validate = import_attr_var,
          .obj = self
        },
        { .is_validator = true,
          .type = GSL_SET_ARRAY_STATE,
          .validate = import_attr_var_list,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

extern gsl_err_t import_class_var(struct kndClassVar *self,
                                  const char *rec,
                                  size_t *total_size)
{
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_var,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = import_attr_var,
          .obj = self
        },
        { .is_validator = true,
          .validate = import_attr_var,
          .obj = self
        },
        { .is_validator = true,
          .type = GSL_SET_ARRAY_STATE,
          .validate = import_attr_var_list,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_baseclass(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndClassVar *classvar;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_1)
        knd_log(".. parsing the base class: \"%.*s\"", 32, rec);

    err = mempool->new_class_var(mempool, &classvar);
    if (err) {
        knd_log("-- conc item alloc failed :(");
        return *total_size = 0, make_gsl_err_external(err);
    }
    classvar->root_class = self->root_class;

    parser_err = parse_class_var(classvar, rec, total_size);
    if (parser_err.code) return parser_err;

    classvar->next = self->baseclass_vars;
    self->baseclass_vars = classvar;
    self->num_baseclass_vars++;

    return make_gsl_err(gsl_OK);
}

extern gsl_err_t knd_import_class(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndClass *c;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. import \"%.*s\" class..", 128, rec);

    err = mempool->new_class(mempool, &c);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    c->root_class = self;
    
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_name,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "base",
          .name_size = strlen("base"),
          .parse = parse_baseclass,
          .obj = c
        },
        { .name = "base",
          .name_size = strlen("base"),
          .parse = parse_baseclass,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "is",
          .name_size = strlen("is"),
          .parse = parse_baseclass,
          .obj = c
        },
        { .name = "is",
          .name_size = strlen("is"),
          .parse = parse_baseclass,
          .obj = c
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = knd_parse_gloss_array,
          .obj = c
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = knd_parse_gloss_array,
          .obj = c
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_summary",
          .name_size = strlen("_summary"),
          .parse = knd_parse_summary_array,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "aggr",
          .name_size = strlen("aggr"),
          .parse = parse_aggr,
          .obj = c
        },
        { .name = "aggr",
          .name_size = strlen("aggr"),
          .parse = parse_aggr,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "str",
          .name_size = strlen("str"),
          .parse = parse_str,
          .obj = c
        },
        { .name = "str",
          .name_size = strlen("str"),
          .parse = parse_str,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "bin",
          .name_size = strlen("bin"),
          .parse = parse_bin,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "cdata",
          .name_size = strlen("cdata"),
          .parse = parse_cdata,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "num",
          .name_size = strlen("num"),
          .parse = parse_num,
          .obj = c
        },
        { .name = "num",
          .name_size = strlen("num"),
          .parse = parse_num,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "ref",
          .name_size = strlen("ref"),
          .parse = parse_ref,
          .obj = c
        },
        { .name = "ref",
          .name_size = strlen("ref"),
          .parse = parse_ref,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "text",
          .name_size = strlen("text"),
          .parse = parse_text,
          .obj = c
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- class parse failed: %d", parser_err.code);
        goto final;
    }

    if (!c->name_size) {
        parser_err = make_gsl_err(gsl_FAIL);
        goto final;
    }

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log("++  \"%.*s\" class import completed!",
                c->name_size, c->name);

    if (!self->batch_mode) {
        c->next = self->inbox;
        self->inbox = c;
        self->inbox_size++;
    }

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        c->str(c);

    return make_gsl_err(gsl_OK);

 final:
    c->del(c);
    return parser_err;
}
