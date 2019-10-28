#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_class_inst.h"
#include "knd_class.h"
#include "knd_mempool.h"
#include "knd_attr.h"
#include "knd_attr_inst.h"
#include "knd_repo.h"

#include "knd_text.h"
#include "knd_num.h"
#include "knd_set.h"

#include "knd_user.h"
#include "knd_state.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_INST_LEVEL_1 0
#define DEBUG_INST_LEVEL_2 0
#define DEBUG_INST_LEVEL_3 0
#define DEBUG_INST_LEVEL_4 0
#define DEBUG_INST_LEVEL_TMP 1

struct LocalContext {
    struct kndClassInst *class_inst;
    //struct kndClass *class;
    struct kndTask *task;
};

static gsl_err_t run_set_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndClassInst *self = ctx->class_inst;
    struct kndClassEntry *class_entry;
    struct kndClassInstEntry *entry;
    struct kndRepo *repo = ctx->task->repo;
    struct kndTask *task = ctx->task;
    struct kndSharedDict *class_name_idx = repo->class_name_idx;
    struct kndSharedDict *name_idx = self->base->entry->inst_name_idx;
    struct kndClass *c;
    int err;

    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    /* inner obj? */
    if (self->type == KND_OBJ_INNER) {
        class_entry = knd_shared_dict_get(class_name_idx, name, name_size);
        if (!class_entry) {
            knd_log("-- inner obj: no such class: %.*s", name_size, name);
            return make_gsl_err(gsl_FAIL);
        }
        c = class_entry->class;

        err = knd_is_base(self->base, c);
        if (err) {
            knd_log("-- no inheritance from %.*s to %.*s",
                    self->base->name_size, self->base->name,
                    c->name_size, c->name);
            return make_gsl_err_external(err);
        }
        self->base = c;
        return make_gsl_err(gsl_OK);
    }

    if (name_idx) {
        entry = knd_shared_dict_get(name_idx, name, name_size);
        if (entry) {
            /*if (entry->inst && entry->inst->states->phase == KND_REMOVED) {
              knd_log("-- this class instance has been removed lately: %.*s",
              name_size, name);
              goto assign_name;
              }*/
            KND_TASK_LOG("class instance name doublet found: %.*s",
                         name_size, name);
            return make_gsl_err(gsl_EXISTS);
        }
    }

    self->name = name;
    self->name_size = name_size;
    self->entry->name = name;
    self->entry->name_size = name_size;

    if (DEBUG_INST_LEVEL_2)
        knd_log("++ class inst name: \"%.*s\"",
                self->name_size, self->name);
    return make_gsl_err(gsl_OK);
}

static int validate_attr(struct kndClassInst *self,
                         const char *name,
                         size_t name_size,
                         struct kndAttr **result,
                         struct kndAttrInst **result_attr_inst)
{
    struct kndClass *conc;
    struct kndAttrRef *attr_ref;
    struct kndAttr *attr;
    struct kndAttrInst *attr_inst = NULL;
    int err;

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. \"%.*s\" (base class: %.*s) to validate attr_inst: \"%.*s\"",
                self->name_size, self->name,
                self->base->name_size, self->base->name,
                name_size, name);

    /* check existing attr_insts */
    for (attr_inst = self->attr_insts; attr_inst; attr_inst = attr_inst->next) {
        if (!memcmp(attr_inst->attr->name, name, name_size)) {
            if (DEBUG_INST_LEVEL_2)
                knd_log("++ ATTR_INST \"%.*s\" is already set!", name_size, name);
            *result_attr_inst = attr_inst;
            return knd_OK;
        }
    }
    conc = self->base;
    err = knd_class_get_attr(conc, name, name_size, &attr_ref);
    if (err) {
        knd_log("  -- \"%.*s\" attr is not approved :(", name_size, name);
        /*log->reset(log);
        e = log->write(log, name, name_size);
        if (e) return e;
        e = log->write(log, " attr not confirmed",
                       strlen(" attr not confirmed"));
                       if (e) return e;*/
        return err;
    }
    attr = attr_ref->attr;
    if (DEBUG_INST_LEVEL_2) {
        const char *type_name = knd_attr_names[attr->type];
        knd_log("++ \"%.*s\" ATTR_INST \"%s\" attr type: \"%s\"",
                name_size, name, attr->name, type_name);
    }
    *result = attr;
    return knd_OK;
}

static gsl_err_t parse_import_attr_inst(void *obj1,
                                        const char *name, size_t name_size,
                                        const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj1;
    struct kndClassInst *self = ctx->class_inst;
    struct kndClassInst *obj;
    struct kndAttrInst *attr_inst = NULL;
    struct kndAttr *attr = NULL;
    struct kndNum *num = NULL;
    struct kndMemPool *mempool;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. parsing attr_inst import REC: %.*s", 128, rec);

    err = validate_attr(self, name, name_size, &attr, &attr_inst);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    if (attr_inst) {
        switch (attr_inst->attr->type) {
            case KND_ATTR_INNER:
                knd_log(".. inner obj import.. %p", attr_inst->inner);
                parser_err = knd_import_class_inst(attr_inst->inner, rec, total_size, ctx->task);
                if (parser_err.code) return parser_err;
                break;
            case KND_ATTR_NUM:
                num = attr_inst->num;
                parser_err = num->parse(num, rec, total_size);
                if (parser_err.code) return parser_err;
                break;
            case KND_ATTR_DATE:
                // TODO
                /*num = attr_inst->num;
                parser_err = num->parse(num, rec, total_size);
                if (parser_err.code) return parser_err; */
                break;
            default:
                break;
        }
        return *total_size = 0, make_gsl_err(gsl_OK);
    }

    mempool = ctx->task->mempool;
    err = knd_attr_inst_new(mempool, &attr_inst);
    if (err) {
        knd_log("-- attr_inst alloc failed :(");
        return *total_size = 0, make_gsl_err_external(err);
    }
    attr_inst->parent = self;
    attr_inst->root = self->root ? self->root : self;
    attr_inst->attr = attr;

    if (DEBUG_INST_LEVEL_2)
        knd_log("== basic attr_inst type: %s", knd_attr_names[attr->type]);

    switch (attr->type) {
        case KND_ATTR_INNER:
            err = knd_class_inst_new(mempool, &obj);
            if (err) {
                knd_log("-- inner class inst alloc failed :(");
                return *total_size = 0, make_gsl_err_external(err);
            }
            // CHECK: do we need a state here?
            err = knd_state_new(mempool, &obj->states);
            if (err) {
                knd_log("-- state alloc failed :(");
                return *total_size = 0, make_gsl_err_external(err);
            }
            obj->states->phase = KND_CREATED;
            obj->type = KND_OBJ_INNER;
            obj->name = attr->name;
            obj->name_size = attr->name_size;

            obj->base = attr->ref_class;
            obj->root = self->root ? self->root : self;

            if (DEBUG_INST_LEVEL_2)
                knd_log(".. import inner obj of default class: %.*s",
                        obj->base->name_size, obj->base->name);

            parser_err = knd_import_class_inst(obj, rec, total_size, ctx->task);
            if (parser_err.code) return parser_err;

            attr_inst->inner = obj;
            obj->parent = attr_inst;
            goto append_attr_inst;

            /*case KND_ATTR_NUM:
            err = kndNum_new(&num);
            if (err) return *total_size = 0, make_gsl_err_external(err);
            num->attr_inst = attr_inst;
            parser_err = num->parse(num, rec, total_size);
            if (parser_err.code) goto final;

            attr_inst->num = num;
            goto append_attr_inst;
            case KND_ATTR_TEXT:
            err = kndText_new(&text);
            if (err) return *total_size = 0, make_gsl_err_external(err);

            text->attr_inst = attr_inst;
            parser_err = text->parse(text, rec, total_size);
            if (parser_err.code) goto final;

            attr_inst->text = text;
            goto append_attr_inst;
            */
        default:
            break;
    }

    parser_err = knd_import_attr_inst(attr_inst, rec, total_size, ctx->task);
    if (parser_err.code) goto final;

    append_attr_inst:
    if (!self->tail) {
        self->tail = attr_inst;
        self->attr_insts = attr_inst;
    }
    else {
        self->tail->next = attr_inst;
        self->tail = attr_inst;
    }
    self->num_attr_insts++;

    if (DEBUG_INST_LEVEL_2)
        knd_log("++ attr_inst %.*s parsing OK!",
                attr_inst->attr->name_size, attr_inst->attr->name);

    return make_gsl_err(gsl_OK);

    final:

    knd_log("-- validation of \"%.*s\" attr_inst failed :(", name_size, name);

    // TODO attr_inst->del(attr_inst);

    return parser_err;
}

static gsl_err_t import_attr_inst_list(void *unused_var(obj),
                                  const char *name, size_t name_size,
                                  const char *rec, size_t *total_size)
{
    //struct kndClassInst *self = obj;
    //struct kndOutput *log;
    //struct kndTask *task;
    //struct kndMemPool *mempool;
    //gsl_err_t parser_err;
    //int err, e;

    //mempool = self->base->entry->repo->mempool;

    if (DEBUG_INST_LEVEL_2)
        knd_log("== import attr_inst list: \"%.*s\" REC: %.*s size:%zu",
                name_size, name, 32, rec, *total_size);

    /*    err = knd_attr_var_new(mempool, &attr_var);
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
    */

    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_import_class_inst(struct kndClassInst *self,
                                const char *rec, size_t *total_size,
                                struct kndTask *task)
{
    if (DEBUG_INST_LEVEL_2)
        knd_log(".. class inst import REC: %.*s", 128, rec);

    struct LocalContext ctx = {
        .class_inst = self,
        .task = task
    };
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = &ctx
        },
        { .type = GSL_SET_STATE,
          .validate = parse_import_attr_inst,
          .obj = &ctx
        },
        { .validate = parse_import_attr_inst,
          .obj = &ctx
        },
        { .type = GSL_SET_ARRAY_STATE,
          .validate = import_attr_inst_list,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t run_set_state_id(void *obj, const char *name, size_t name_size)
{
    struct kndClassInst *self = obj;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    if (DEBUG_INST_LEVEL_2)
        knd_log("++ class inst state: \"%.*s\" inst:%.*s",
                name_size, name, self->name_size, self->name);

    return make_gsl_err(gsl_OK);
}

gsl_err_t kndClassInst_read_state(struct kndClassInst *self,
                                  const char *rec, size_t *total_size,
                                  struct kndTask *task)
{
    struct LocalContext ctx = {
        .class_inst = self,
        .task = task
    };
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_state_id,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .validate = parse_import_attr_inst,
          .obj = &ctx
        },
        { .validate = parse_import_attr_inst,
          .obj = &ctx
        }
    };

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. reading class inst state: %.*s", 128, rec);

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}
