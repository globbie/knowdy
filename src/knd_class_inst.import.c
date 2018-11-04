#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_class_inst.h"
#include "knd_class.h"
#include "knd_mempool.h"
#include "knd_attr.h"
#include "knd_elem.h"
#include "knd_repo.h"

#include "knd_text.h"
#include "knd_num.h"
#include "knd_rel.h"
#include "knd_set.h"
#include "knd_rel_arg.h"

#include "knd_user.h"
#include "knd_state.h"

#include <gsl-parser.h>
#include <glb-lib/output.h>

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
    struct kndClassInstEntry *entry;
    struct kndRepo *repo = ctx->task->repo;
    struct ooDict *name_idx = repo->class_inst_name_idx;
    struct glbOutput *log = ctx->task->log;
    struct kndTask *task = ctx->task;
    int err;

    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    entry = name_idx->get(name_idx, name, name_size);
    if (entry) {
        if (entry->inst && entry->inst->states->phase == KND_REMOVED) {
            knd_log("-- this class instance has been removed lately: %.*s :(",
                    name_size, name);
            goto assign_name;
        }
        knd_log("-- class instance name doublet found: %.*s:(",
                name_size, name);
        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return make_gsl_err_external(err);
        err = log->write(log,   " inst name already exists",
                         strlen(" inst name already exists"));
        if (err) return make_gsl_err_external(err);
        task->http_code = HTTP_CONFLICT;
        return make_gsl_err(gsl_EXISTS);
    }
    assign_name:
    self->name = name;
    self->name_size = name_size;
    if (DEBUG_INST_LEVEL_2)
        knd_log("++ class inst name: \"%.*s\"",
                self->name_size, self->name);
    return make_gsl_err(gsl_OK);
}

static int kndClassInst_validate_attr(struct kndClassInst *self,
                                      const char *name,
                                      size_t name_size,
                                      struct kndAttr **result,
                                      struct kndElem **result_elem)
{
    struct kndClass *conc;
    struct kndAttrRef *attr_ref;
    struct kndAttr *attr;
    struct kndElem *elem = NULL;
    struct glbOutput *log = self->base->entry->repo->log;
    int err, e;
    if (DEBUG_INST_LEVEL_2)
        knd_log(".. \"%.*s\" to validate elem: \"%.*s\"",
                self->name_size, self->name, name_size, name);
    /* check existing elems */
    for (elem = self->elems; elem; elem = elem->next) {
        if (!memcmp(elem->attr->name, name, name_size)) {
            if (DEBUG_INST_LEVEL_2)
                knd_log("++ ELEM \"%.*s\" is already set!", name_size, name);
            *result_elem = elem;
            return knd_OK;
        }
    }

    conc = self->base;
    err = knd_class_get_attr(conc, name, name_size, &attr_ref);
    if (err) {
        knd_log("  -- \"%.*s\" attr is not approved :(", name_size, name);
        log->reset(log);
        e = log->write(log, name, name_size);
        if (e) return e;
        e = log->write(log, " attr not confirmed",
                       strlen(" attr not confirmed"));
        if (e) return e;
        return err;
    }
    attr = attr_ref->attr;
    if (DEBUG_INST_LEVEL_2) {
        const char *type_name = knd_attr_names[attr->type];
        knd_log("++ \"%.*s\" ELEM \"%s\" attr type: \"%s\"",
                name_size, name, attr->name, type_name);
    }
    *result = attr;
    return knd_OK;
}

static gsl_err_t parse_import_elem(void *obj1,
                                   const char *name, size_t name_size,
                                   const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj1;
    struct kndClassInst *self = ctx->class_inst;
    struct kndClassInst *obj;
    struct kndElem *elem = NULL;
    struct kndAttr *attr = NULL;
    struct kndNum *num = NULL;
    struct kndMemPool *mempool;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. parsing elem import REC: %.*s", 128, rec);

    err = kndClassInst_validate_attr(self, name, name_size, &attr, &elem);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    if (elem) {
        switch (elem->attr->type) {
            case KND_ATTR_INNER:
                parser_err = knd_import_class_inst(elem->inner, rec, total_size, ctx->task);
                if (parser_err.code) return parser_err;
                break;
            case KND_ATTR_NUM:
                num = elem->num;
                parser_err = num->parse(num, rec, total_size);
                if (parser_err.code) return parser_err;
                break;
            case KND_ATTR_DATE:
                // TODO
                /*num = elem->num;
                parser_err = num->parse(num, rec, total_size);
                if (parser_err.code) return parser_err; */
                break;
            default:
                break;
        }
        return *total_size = 0, make_gsl_err(gsl_OK);
    }

    mempool = ctx->task->mempool;
    err = knd_class_inst_elem_new(mempool, &elem);
    if (err) {
        knd_log("-- elem alloc failed :(");
        return *total_size = 0, make_gsl_err_external(err);
    }
    elem->parent = self;
    elem->root = self->root ? self->root : self;
    elem->attr = attr;

    if (DEBUG_INST_LEVEL_2)
        knd_log("== basic elem type: %s", knd_attr_names[attr->type]);

    switch (attr->type) {
        case KND_ATTR_INNER:
            err = knd_class_inst_new(mempool, &obj);
            if (err) {
                knd_log("-- inner class inst alloc failed :(");
                return *total_size = 0, make_gsl_err_external(err);
            }
            err = knd_state_new(mempool, &obj->states);
            if (err) {
                knd_log("-- state alloc failed :(");
                return *total_size = 0, make_gsl_err_external(err);
            }
            obj->states->phase = KND_CREATED;
            obj->type = KND_OBJ_INNER;
            obj->name = attr->name;
            obj->name_size = attr->name_size;

            /*if (!attr->ref_class) {
                if (self->states->phase == KND_FROZEN || self->states->phase == KND_SUBMITTED) {
                    if (DEBUG_INST_LEVEL_2) {
                        knd_log(".. resolve attr \"%.*s\": \"%.*s\"..",
                                attr->name_size, attr->name,
                                attr->ref_classname_size, attr->ref_classname);
                    }
                    root_class = self->base->root_class;
                    err = root_class->get(root_class,
                                          attr->ref_classname, attr->ref_classname_size,
                                          &c);
                    if (err) return *total_size = 0, make_gsl_err_external(err);
                    attr->ref_class = c;
                }
                else {
                    knd_log("-- couldn't resolve the %.*s attr :(",
                            attr->name_size, attr->name);
                    return *total_size = 0, make_gsl_err(gsl_FAIL);
                }
                }*/

            obj->base = attr->ref_class;
            obj->root = self->root ? self->root : self;

            parser_err = knd_import_class_inst(obj, rec, total_size, ctx->task);
            if (parser_err.code) return parser_err;

            elem->inner = obj;
            obj->parent = elem;
            goto append_elem;

            /*case KND_ATTR_NUM:
            err = kndNum_new(&num);
            if (err) return *total_size = 0, make_gsl_err_external(err);
            num->elem = elem;
            parser_err = num->parse(num, rec, total_size);
            if (parser_err.code) goto final;

            elem->num = num;
            goto append_elem;
            case KND_ATTR_TEXT:
            err = kndText_new(&text);
            if (err) return *total_size = 0, make_gsl_err_external(err);

            text->elem = elem;
            parser_err = text->parse(text, rec, total_size);
            if (parser_err.code) goto final;

            elem->text = text;
            goto append_elem;
            */
        default:
            break;
    }

    parser_err = elem->parse(elem, rec, total_size);
    if (parser_err.code) goto final;

    append_elem:
    if (!self->tail) {
        self->tail = elem;
        self->elems = elem;
    }
    else {
        self->tail->next = elem;
        self->tail = elem;
    }
    self->num_elems++;

    if (DEBUG_INST_LEVEL_2)
        knd_log("++ elem %.*s parsing OK!",
                elem->attr->name_size, elem->attr->name);

    return make_gsl_err(gsl_OK);

    final:

    knd_log("-- validation of \"%.*s\" elem failed :(", name_size, name);

    // TODO elem->del(elem);

    return parser_err;
}

static gsl_err_t import_elem_list(void *unused_var(obj),
                                  const char *name, size_t name_size,
                                  const char *rec, size_t *total_size)
{
    //struct kndClassInst *self = obj;
    //struct glbOutput *log;
    //struct kndTask *task;
    //struct kndMemPool *mempool;
    //gsl_err_t parser_err;
    //int err, e;

    //mempool = self->base->entry->repo->mempool;

    if (DEBUG_INST_LEVEL_TMP)
        knd_log("== import elem list: \"%.*s\" REC: %.*s size:%zu",
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

static gsl_err_t rel_alloc(void *accu,
                           const char *name,
                           size_t name_size,
                           size_t count,
                           void **item)
{
    struct LocalContext *ctx = accu;
    struct kndRelRef *relref = NULL;
    //struct kndRel *root_rel;
    struct kndMemPool *mempool = ctx->task->mempool;
    int err;

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. %.*s OBJ to alloc rel ref %.*s count:%zu..",
                ctx->class_inst->name_size, ctx->class_inst->name,
                name_size, name, count);

    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY, sizeof(struct kndRelRef), (void**)&relref);
    if (err) return make_gsl_err_external(err);

    *item = (void*)relref;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t rel_append(void *accu,
                            void *item)
{
    struct LocalContext *ctx = accu;
    struct kndClassInst *self = ctx->class_inst;
    struct kndRelRef *ref = item;

    ref->next = self->entry->rels;
    self->entry->rels = ref;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t rel_entry_alloc(void *obj,
                                 const char *name,
                                 size_t name_size,
                                 size_t unused_var(count),
                                 void **item)
{
    struct kndRelRef *self = obj;
    struct kndSet *set;
    void *elem;
    int err;

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. %.*s Rel Ref to find rel inst \"%.*s\"",
                self->rel->name_size, self->rel->name,
                name_size, name);

    set = self->rel->entry->inst_idx;
    if (!set) return make_gsl_err(gsl_FAIL);

    err = set->get(set, name, name_size, &elem);
    if (err) return make_gsl_err(gsl_FAIL);

    *item = elem;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t rel_entry_append(void *accu,
                                  void *item)
{
    struct kndRelRef *self = accu;
    struct kndRel *rel;
    struct kndRelInstEntry *entry = item;
    struct kndMemPool *mempool = NULL; // = self->rel->entry->repo->mempool; // FIXME(k15tfu): <--
    struct kndSet *set;
    int err;

    assert(mempool);
    return make_gsl_err_external(knd_FAIL);

    if (DEBUG_INST_LEVEL_2)
        knd_log("== Rel Instance entry:\"%.*s\"",
                entry->id_size, entry->id);

    set = self->idx;
    if (!set) {
        err = knd_set_new(mempool, &set);
        if (err) return make_gsl_err_external(err);
        set->mempool = mempool;
        set->type = KND_SET_REL_INST;
        self->idx = set;
    }

    err = set->add(set, entry->id, entry->id_size, (void*)entry);
    if (err) return make_gsl_err_external(err);

    rel = self->rel;
    err = rel->unfreeze_inst(rel, entry);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_rel_insts(void *obj,
                                const char *rec,
                                size_t *total_size)
{
    struct kndRelRef *self = obj;
    struct gslTaskSpec rel_inst_spec = {
        .is_list_item = true,
        .accu = self,
        .alloc = rel_entry_alloc,
        .append = rel_entry_append,
    };

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. reading insts of rel %.*s..",
                self->rel->name_size, self->rel->name);

    return gsl_parse_array(&rel_inst_spec, rec, total_size);
}

static gsl_err_t rel_read(void *obj,
                          const char *rec,
                          size_t *total_size)
{
    struct kndRelRef *relref = obj;
    struct gslTaskSpec specs[] = {
        /*{ .is_implied = true,
          .run = resolve_relref,
          .obj = relref
          },*/
        { .type = GSL_SET_ARRAY_STATE,
            .name = "i",
            .name_size = strlen("i"),
            .parse = read_rel_insts,
            .obj = relref
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_rels(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct gslTaskSpec rel_spec = {
        .is_list_item = true,
        .accu = ctx,
        .alloc = rel_alloc,
        .append = rel_append,
        .parse = rel_read
    };

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. reading rels of obj %.*s..",
                ctx->class_inst->name_size, ctx->class_inst->name);

    return gsl_parse_array(&rel_spec, rec, total_size);
}

gsl_err_t knd_import_class_inst(struct kndClassInst *self,
                                const char *rec, size_t *total_size,
                                struct kndTask *task) {
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
          .is_validator = true,
          .validate = parse_import_elem,
          .obj = &ctx
        },
        { .is_validator = true,
          .validate = parse_import_elem,
          .obj = &ctx
        },
        { .is_validator = true,
          .type = GSL_SET_ARRAY_STATE,
          .validate = import_elem_list,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_rel",
          .name_size = strlen("_rel"),
          .parse = parse_rels,
          .obj = &ctx
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}
