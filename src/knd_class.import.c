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
static void append_attr_var(struct kndClassVar *ci, struct kndAttrVar *attr_var);

extern gsl_err_t knd_parse_import_class_inst(void *data,
                                             const char *rec,
                                             size_t *total_size)
{
    struct kndClass *self = data;
    struct kndClass *c;
    struct kndClassInst *inst;
    struct kndClassInstEntry *entry;
    struct ooDict *name_idx;
    struct kndRepo *repo = self->entry->repo;
    struct kndMemPool *mempool = repo->mempool;
    struct kndTask *task = repo->task;
    struct kndState *state;
    struct kndStateRef *state_ref;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2) {
        knd_log(".. import \"%.*s\" inst..", 128, rec);
    }

    if (!repo->curr_class) {
        knd_log("-- no class selected");
        return *total_size = 0, make_gsl_err(gsl_FAIL);
    }

    /* user ctx should have its own copy of a selected class */
    if (repo->curr_class->entry->repo != repo) {
        err = knd_class_clone(repo->curr_class, repo, &c);
        if (err) return *total_size = 0, make_gsl_err_external(err);
        repo->curr_class = c;
    }

    c = repo->curr_class;

    err = knd_class_inst_new(mempool, &inst);
    if (err) {
        knd_log("-- class inst alloc failed :(");
        return *total_size = 0, make_gsl_err_external(err);
    }

    err = knd_state_new(mempool, &state);
    if (err) {
        knd_log("-- state alloc failed :(");
        return *total_size = 0, make_gsl_err_external(err);
    }
    err = knd_class_inst_entry_new(mempool, &entry);
    if (err) return make_gsl_err_external(err);

    inst->entry = entry;
    entry->inst = inst;
    state->phase = KND_CREATED;
    state->numid = 1;
    inst->base = c;
    inst->states = state;
    inst->num_states = 1;

    parser_err = inst->parse(inst, rec, total_size);
    if (parser_err.code) return parser_err;

    err = knd_state_ref_new(mempool, &state_ref);
    if (err) {
        knd_log("-- state ref alloc for imported inst failed");
        return make_gsl_err_external(err);
    }
    state_ref->state = state;
    state_ref->type = KND_STATE_CLASS_INST;
    state_ref->obj = (void*)entry;

    state_ref->next = repo->curr_class_inst_state_refs;
    repo->curr_class_inst_state_refs = state_ref;

    repo->num_class_insts++;

    inst->entry->numid = repo->num_class_insts;
    knd_uid_create(inst->entry->numid, inst->entry->id, &inst->entry->id_size);

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log("++ %.*s class inst parse OK!",
                inst->name_size, inst->name,
                c->name_size, c->name);

    /* automatic name assignment if no explicit name given */
    if (!inst->name_size) {
        inst->name = inst->entry->id;
        inst->name_size = inst->entry->id_size;
    }
    name_idx = repo->class_inst_name_idx;

    // TODO  lookup prev class inst ref

    err = name_idx->set(name_idx,
                        inst->name, inst->name_size,
                        (void*)entry);
    if (err) return make_gsl_err_external(err);

    err = knd_register_class_inst(c, entry);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_CLASS_IMPORT_LEVEL_3) {
        knd_class_inst_str(inst, 0);
    }

    task->type = KND_UPDATE_STATE;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_attr_var_name(void *obj, const char *name, size_t name_size)
{
    struct kndAttrVar *self = obj;
    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. set attr var name: %.*s", name_size, name);

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    self->name = name;
    self->name_size = name_size;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t set_attr_var_value(void *obj, const char *val, size_t val_size)
{
    struct kndAttrVar *self = obj;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. set attr var value: %.*s", val_size, val);

    if (!val_size) return make_gsl_err(gsl_FORMAT);

    self->val = val;
    self->val_size = val_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_class_name(void *obj, const char *name, size_t name_size)
{
    struct kndClass *self = obj;
    struct kndRepo *repo = self->entry->repo;
    struct glbOutput *log = repo->log;

    struct ooDict *class_name_idx = repo->class_name_idx;
    struct kndTask *task = repo->task;
    struct kndClassEntry *entry;
    struct kndState *state;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. set class name: %.*s", name_size, name);

    if (!name_size) return make_gsl_err(gsl_FORMAT);

    entry = class_name_idx->get(class_name_idx, name, name_size);
    if (!entry) {
        entry = self->entry;
        /*err = mempool->new_class_entry(mempool, &entry);
        if (err) return make_gsl_err_external(err);
        entry->repo = repo;
        entry->class = self;
        self->entry = entry;
        */

        entry->name = name;
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

        // TODO release curr entry

        return make_gsl_err(gsl_OK);
    }

    if (entry->class->states) {
        state = entry->class->states;
        if (state->phase == KND_REMOVED) {
            entry->class = self;
            self->entry =  entry;

            if (DEBUG_CLASS_IMPORT_LEVEL_TMP)
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
    struct kndRepo *repo          = root_class->entry->repo;
    struct ooDict *class_name_idx = repo->class_name_idx;
    struct kndMemPool *mempool    = repo->mempool;
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
    err = knd_class_entry_new(mempool, &entry);
    if (err) return make_gsl_err_external(err);

    entry->name = name;
    entry->name_size = name_size;

    err = class_name_idx->set(class_name_idx,
                              entry->name, name_size,
                              (void*)entry);
    if (err) return make_gsl_err_external(err);

    entry->repo = root_class->entry->repo;
    self->entry = entry;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_state_top_option(void *obj,
                                      const char *name  __attribute__((unused)),
                                      size_t name_size __attribute__((unused)) )
{
    struct kndClass *self = obj;

    if (DEBUG_CLASS_IMPORT_LEVEL_TMP)
        knd_log("NB: set class state top option!");

    self->state_top = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_attr(void *obj,
                            const char *name, size_t name_size,
                            const char *rec, size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndAttr *attr;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    const char *c;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. parsing attr: \"%.*s\" rec:\"%.*s\"",
                name_size, name, 32, rec);

    err = knd_attr_new(mempool, &attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;

    for (size_t i = 0; i < sizeof(knd_attr_names) / sizeof(knd_attr_names[0]); i++) {
        c = knd_attr_names[i];
        if (!memcmp(c, name, name_size)) 
            attr->type = (knd_attr_type)i;
    }

    if (attr->type == KND_ATTR_NONE) {
        knd_log("-- attr no supported: %.*s", name_size, name);
        //return *total_size = 0, make_gsl_err_external(err);
    }
    
    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        if (DEBUG_CLASS_IMPORT_LEVEL_TMP)
            knd_log("-- failed to parse the attr field: %d", parser_err.code);
        return parser_err;
    }

    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    } else {
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

static gsl_err_t confirm_attr_var(void *obj,
                                  const char *name __attribute__((unused)),
                                  size_t name_size __attribute__((unused)))
{
    struct kndAttrVar *attr_var = obj;

    // TODO empty values?
    if (DEBUG_CLASS_IMPORT_LEVEL_TMP) {
        if (!attr_var->val_size)
            knd_log("NB: attr var value not set in %.*s",
                    attr_var->name_size, attr_var->name);
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

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->class_var = self;

    // TEST
    mempool->num_attr_vars++;

    attr_var->name = name;
    attr_var->name_size = name_size;

    /*struct gslTaskSpec cdata_spec = {
        .buf = attr_var->valbuf,
        .buf_size = &attr_var->val_size,
        .max_buf_size = sizeof attr_var->valbuf
        };*/
    
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_var_value,
          .obj = attr_var
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = import_nested_attr_var,
          .obj = attr_var
        },
        { .is_validator = true,
          .validate = import_nested_attr_var,
          .obj = attr_var
        },/*
        { .name = "_cdata",
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
    if (parser_err.code) return parser_err;

    append_attr_var(self, attr_var);

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log("++ attr var value: %.*s", attr_var->val_size, attr_var->val);

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

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return make_gsl_err_external(err);
    attr_var->class_var = self->class_var;

    // TEST
    mempool->num_attr_vars++;

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
    //attr_var->list_count = self->num_list_elems;

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
          .run = set_attr_var_name,
          .obj = item
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

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->class_var = self;

    attr_var->name = name;
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

static gsl_err_t import_nested_attr_var(void *obj,
                                         const char *name, size_t name_size,
                                         const char *rec, size_t *total_size)
{
    struct kndAttrVar *self = obj;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = self->class_var->root_class->entry->repo->mempool;
    gsl_err_t parser_err;
    int err;

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->class_var = self->class_var;

    // TEST
    mempool->num_attr_vars++;

    attr_var->name = name;
    attr_var->name_size = name_size;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. import nested attr: \"%.*s\" REC: %.*s",
                attr_var->name_size, attr_var->name, 16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_var_value,
          .obj = attr_var
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = import_nested_attr_var,
          .obj = attr_var
        },
        { .is_validator = true,
          .validate = import_nested_attr_var,
          .obj = attr_var
        }/*,
        { .name = "_cdata",
          .name_size = strlen("_cdata"),
          .parse = parse_attr_var_cdata,
          .obj = attr_var
          }*/,
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

extern gsl_err_t knd_import_class_var(struct kndClassVar *self,
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

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. parsing the base class: \"%.*s\"", 32, rec);

    err = knd_class_var_new(mempool, &classvar);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    classvar->root_class = self->entry->repo->root_class;
    classvar->parent = self;

    parser_err = parse_class_var(classvar, rec, total_size);
    if (parser_err.code) return parser_err;

    classvar->next = self->baseclass_vars;
    self->baseclass_vars = classvar;
    self->num_baseclass_vars++;

    return make_gsl_err(gsl_OK);
}

extern gsl_err_t knd_class_import(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndRepo *repo = self->entry->repo;
    struct kndMemPool *mempool = repo->mempool;
    struct kndTask *task = repo->task;
    struct glbOutput *log;
    int e, err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log("..import \"%.*s\" class.. [total:%zu]",
                128, rec, mempool->num_classes);

    err = knd_class_new(mempool, &c);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    err = knd_class_entry_new(mempool, &entry);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    entry->repo = repo;
    entry->class = c;
    c->entry = entry;

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
        { .name = "_state_top",
          .name_size = strlen("_state_top"),
          .run = set_state_top_option,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = parse_attr,
          .obj = c
        },
        { .is_validator = true,
          .validate = parse_attr,
          .obj = c
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- class parse failed: %d", parser_err.code);
        goto final;
    }

    if (!c->name_size) {
        knd_log("-- no class name specified?");
        log = repo->log;
        task = repo->task;
        log->reset(log);
        e = log->write(log, "class name not specified",
                       strlen("class name not specified"));
        if (e) return make_gsl_err_external(e);
        task->http_code = HTTP_BAD_REQUEST;
        parser_err = make_gsl_err(gsl_FAIL);
        goto final;
    }

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log("++  \"%.*s\" class import completed!\n",
                c->name_size, c->name);

    /* initial class load ends here */
    if (repo->task->batch_mode)
        return make_gsl_err(gsl_OK);

    err = knd_class_resolve(c);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    
    err = knd_update_state(c, KND_CREATED);
    if (err) return *total_size = 0, make_gsl_err_external(err);    

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        c->str(c);

    return make_gsl_err(gsl_OK);

 final:
    return parser_err;
}
