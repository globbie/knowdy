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

extern gsl_err_t knd_parse_import_class_inst(void *obj,
                                             const char *rec,
                                             size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndMemPool *mempool = task->mempool;
    struct kndClass *c;
    struct kndClassInst *inst;
    struct kndClassInstEntry *entry;
    struct ooDict *name_idx;
    struct kndRepo *repo = task->repo;
    struct kndState *state;
    struct kndStateRef *state_ref;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2) {
        knd_log(".. import \"%.*s\" inst..", 128, rec);
    }

    if (!task->class) {
        knd_log("-- no class selected");
        return *total_size = 0, make_gsl_err(gsl_FAIL);
    }

    c = task->class;

    /* user ctx should have its own copy of a selected class */
    if (c->entry->repo != repo) {
        err = knd_class_clone(task->class, repo, &c, mempool);
        if (err) return *total_size = 0, make_gsl_err_external(err);
        task->class = c;
    }

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

    parser_err = knd_import_class_inst(inst, rec, total_size, task);
    if (parser_err.code) return parser_err;

    err = knd_state_ref_new(mempool, &state_ref);
    if (err) {
        knd_log("-- state ref alloc for imported inst failed");
        return make_gsl_err_external(err);
    }
    state_ref->state = state;
    state_ref->type = KND_STATE_CLASS_INST;
    state_ref->obj = (void*)entry;

    state_ref->next = task->class_inst_state_refs;
    task->class_inst_state_refs = state_ref;

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

    err = knd_register_class_inst(c, entry, mempool);
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
    struct kndTask *task = obj;
    struct kndClass *self = task->class;
    struct kndRepo *repo = task->repo;
    struct glbOutput *log = task->log;
    struct kndClass *c;
    struct ooDict *class_name_idx = repo->class_name_idx;
    struct kndClassEntry *entry;
    struct kndState *state;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2) {
        knd_log(".. set class name: \"%.*s\"..",
                name_size, name);
    }

    if (!name_size) return make_gsl_err(gsl_FORMAT);

    if (!task->batch_mode) {
        knd_log(".. doublet checking..\n");
        err = knd_get_class(repo, name, name_size, &c, task);
        if (!err) goto doublet;
    }

    entry = class_name_idx->get(class_name_idx, name, name_size);
    if (!entry) {
        entry = self->entry;
        //knd_log("entry: %p", self->entry);
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
                                  name, name_size,
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

 doublet:
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
    struct kndTask *task = obj;
    struct kndMemPool *mempool    = task->mempool;
    struct kndClassVar *self      = task->class_var;
    struct kndClass *root_class   = self->root_class;
    struct kndRepo *repo          = root_class->entry->repo;
    struct ooDict *class_name_idx = repo->class_name_idx;
    struct kndClassEntry *entry;
    void *result;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. root class:\"%.*s\" to check class var name: %.*s [task id:%zu]",
                root_class->entry->name_size,
                root_class->entry->name,
                name_size, name, task->id);

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
                                      const char *unused_var(name),
                                      size_t unused_var(name_size) )
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
    struct kndTask *task = obj;
    struct kndClass *self = task->class;
    struct kndAttr *attr;
    struct kndMemPool *mempool = task->mempool;
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

    task->attr = attr;
    parser_err = knd_import_attr(task, rec, total_size);
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
                                  const char *unused_var(name),
                                  size_t unused_var(name_size))
{
    struct kndAttrVar *attr_var = obj;

    // TODO empty values?
    if (DEBUG_CLASS_IMPORT_LEVEL_1) {
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
    struct kndTask *task = obj;
    struct kndClassVar *self = task->class_var;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err;

    // TODO

    /* class var not resolved */

    if (!self->entry) {
        knd_log("-- anonymous class var: %.*s?  REC:%.*s",
                64, rec);
        //return *total_size = 0, make_gsl_err_external(knd_FAIL);
    } 

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
    attr_var->task = task;

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
          .validate = import_nested_attr_var,
          .obj = attr_var
        },
        { .validate = import_nested_attr_var,
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

static gsl_err_t append_attr_var_list_item(void *accu,
                                           void *obj)
{
    struct kndAttrVar *self = accu;
    struct kndAttrVar *attr_var = obj;

    /*if (!memcmp(self,
                "description", strlen("description"))) {
        str_attr_vars(attr_var, 1);
        }*/

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
    struct kndAttrVar *self = obj;
    struct kndTask *task = self->task;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = task->mempool;
    int err;

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->class_var = self->class_var;
    attr_var->task = task;

    // TEST
    mempool->num_attr_vars++;
    attr_var->is_list_item = true;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. parse import attr item..");

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_var_name,
          .obj = attr_var
        },
        { .validate = import_nested_attr_var,
          .obj = attr_var
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    // append
    return append_attr_var_list_item(self, attr_var);
}

static gsl_err_t import_attr_var_list(void *obj,
                                       const char *name, size_t name_size,
                                       const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndClassVar *self = task->class_var;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err, e;

    if (!self->entry) {
        knd_log("-- anonymous class var: %.*s?  REC:%.*s",
                64, rec);
        struct glbOutput *log = task->log; 
        log->reset(log);
        e = log->write(log, "no baseclass name specified",
                     strlen("no baseclass name specified"));
        if (e) return make_gsl_err_external(e);
        task->http_code = HTTP_BAD_REQUEST;
        return *total_size = 0, make_gsl_err_external(knd_FAIL);
    } 

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log("== import attr attr_var list: \"%.*s\" REC: %.*s",
                name_size, name, 32, rec);

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->class_var = self;
    attr_var->name = name;
    attr_var->name_size = name_size;

    append_attr_var(self, attr_var);
    attr_var->task = task;

    struct gslTaskSpec import_attr_var_spec = {
        .is_list_item = true,
        .parse = import_attr_var_list_item,
        .obj = attr_var
    };

    parser_err = gsl_parse_array(&import_attr_var_spec, rec, total_size);
    if (parser_err.code) return parser_err;

    // test
    /*   if (!memcmp(name, "description", strlen("description")))
        str_attr_vars(attr_var, 1);
    */
    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_nested_attr_var(void *obj,
                                        const char *name, size_t name_size,
                                        const char *rec, size_t *total_size)
{
    struct kndAttrVar *self = obj;
    struct kndTask *task = self->task;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err;

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->class_var = self->class_var;

    // TEST
    mempool->num_attr_vars++;

    attr_var->name = name;
    attr_var->name_size = name_size;
    attr_var->task = task;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. import nested attr: \"%.*s\" REC: %.*s",
                attr_var->name_size, attr_var->name, 16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_var_value,
          .obj = attr_var
        },
        { .type = GSL_SET_STATE,
          .validate = import_nested_attr_var,
          .obj = attr_var
        },
        { .validate = import_nested_attr_var,
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

extern gsl_err_t parse_class_var(struct kndTask *task,
                                 const char *rec,
                                 size_t *total_size)
{
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_var,
          .obj = task
        },
        { .type = GSL_SET_STATE,
          .validate = import_attr_var,
          .obj = task
        },
        { .validate = import_attr_var,
          .obj = task
        },
        { .type = GSL_SET_ARRAY_STATE,
          .validate = import_attr_var_list,
          .obj = task
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
          .validate = import_attr_var,
          .obj = self
        },
        { .validate = import_attr_var,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .validate = import_attr_var_list,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    knd_log("");
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_baseclass(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndClass *self = task->class;
    struct kndClassVar *class_var;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. parsing the base class: \"%.*s\"", 32, rec);

    err = knd_class_var_new(mempool, &class_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    class_var->root_class = self->entry->repo->root_class;
    class_var->parent = self;

    task->class_var = class_var;
    parser_err = parse_class_var(task, rec, total_size);
    if (parser_err.code) return parser_err;

    class_var->next = self->baseclass_vars;

    // TODO: atomic
    self->baseclass_vars = class_var;
    self->num_baseclass_vars++;

    return make_gsl_err(gsl_OK);
}

extern gsl_err_t knd_class_import(struct kndRepo *repo,
                                  const char *rec,
                                  size_t *total_size,
                                  struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndClass *c;
    struct kndClassEntry *entry;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_IMPORT_LEVEL_TMP)
        knd_log("..worker \"%zu\" to import class: \"%.*s\".. [total:%zu] repo:%p",
                task->id, 128, rec, mempool->num_classes, repo);

    err = knd_class_new(mempool, &c);
    if (err) return make_gsl_err_external(err);

    err = knd_class_entry_new(mempool, &entry);
    if (err) return make_gsl_err_external(err);

    entry->repo = repo;
    entry->class = c;
    c->entry = entry;
    task->class = c;
    task->repo = repo;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_name,
          .obj = task
        },
        { .type = GSL_SET_STATE,
          .name = "base",
          .name_size = strlen("base"),
          .parse = parse_baseclass,
          .obj = task
        },
        { .name = "base",
          .name_size = strlen("base"),
          .parse = parse_baseclass,
          .obj = task
        },
        { .type = GSL_SET_STATE,
          .name = "is",
          .name_size = strlen("is"),
          .parse = parse_baseclass,
          .obj = task
        },
        { .name = "is",
          .name_size = strlen("is"),
          .parse = parse_baseclass,
          .obj = task
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = knd_parse_gloss_array,
          .obj = task
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = knd_parse_gloss_array,
          .obj = task
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
          .validate = parse_attr,
          .obj = task
        },
        { .validate = parse_attr,
          .obj = task
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- class parse failed: %d", parser_err.code);
        goto final;
    }

    if (!c->name_size) {
        knd_log("-- no class name specified?");
        /*log = task->log;
        log->reset(log);
        e = log->write(log, "class name not specified",
                       strlen("class name not specified"));
                       if (e) return e; */
        task->http_code = HTTP_BAD_REQUEST;
        parser_err = make_gsl_err(gsl_FAIL);
        goto final;
    }

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log("++  \"%.*s\" class import completed! batch mode:%d",
                c->name_size, c->name, task->batch_mode);

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        c->str(c, 1);

    /* initial class load ends here */
    if (task->batch_mode) return make_gsl_err(gsl_OK);

    err = knd_class_resolve(c, task);
    if (err) return make_gsl_err_external(err);

    err = knd_update_state(c, KND_CREATED, task);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        c->str(c, 1);

    return make_gsl_err(gsl_OK);
 final:
    return parser_err;
}
