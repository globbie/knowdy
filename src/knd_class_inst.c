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

static int export_rel_inst_JSON(void *obj,
                                const char *elem_id, size_t elem_id_size,
                                size_t count,
                                void *unused_var(elem));

extern void knd_class_inst_str(struct kndClassInst *self, size_t depth)
{
    struct kndElem *elem;
    struct kndState *state = self->states;

    if (self->type == KND_OBJ_ADDR) {
        knd_log("\n%*sClass Instance \"%.*s::%.*s\"  numid:%zu",
                depth * KND_OFFSET_SIZE, "",
                self->base->name_size, self->base->name,
                self->name_size, self->name,
                self->entry->numid);
        if (state) {
            knd_log("    state:%zu  phase:%d",
                    state->numid, state->phase);
        }
    }

    for (elem = self->elems; elem; elem = elem->next) {
        knd_elem_str(elem, depth + 1);
    }
}


static gsl_err_t parse_import_elem(void *data,
                                   const char *name, size_t name_size,
                                   const char *rec, size_t *total_size)
{
    struct kndClassInst *self = data;
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
            parser_err = elem->inner->parse(elem->inner, rec, total_size);
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

    mempool = self->base->entry->repo->mempool;
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

        parser_err = obj->parse(obj, rec, total_size);
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

static gsl_err_t rel_entry_append(void *accu,
                                 void *item)
{
    struct kndRelRef *self = accu;
    struct kndRel *rel;
    struct kndRelInstEntry *entry = item;
    struct kndMemPool *mempool = self->rel->entry->repo->mempool;
    struct kndSet *set;
    int err;

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

static gsl_err_t rel_append(void *accu,
                            void *item)
{
    struct kndClassInst *self = accu;
    struct kndRelRef *ref = item;

    ref->next = self->entry->rels;
    self->entry->rels = ref;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t rel_alloc(void *obj,
                           const char *name,
                           size_t name_size,
                           size_t count,
                           void **item)
{
    struct kndClassInst *self = obj;
    struct kndRelRef *relref = NULL;
    //struct kndRel *root_rel;
    struct kndMemPool *mempool = self->base->entry->repo->mempool;
    int err;

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. %.*s OBJ to alloc rel ref %.*s count:%zu..",
                self->name_size, self->name,
                name_size, name, count);

    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY, sizeof(struct kndRelRef), (void**)&relref);
    if (err) return make_gsl_err_external(err);

    *item = (void*)relref;

    return make_gsl_err(gsl_OK);
}

/*static gsl_err_t select_rel(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndClassInst *self = obj;

    if (!self->curr_inst) {
        knd_log("-- no obj selected :(");
        return *total_size = 0, make_gsl_err(gsl_FAIL);
    }

    self->curr_inst->curr_rel = NULL;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = find_obj_rel,
          .obj = self->curr_inst
        },
        { .is_default = true,
          .run = present_rel,
          .obj = self->curr_inst
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}
*/

static gsl_err_t parse_rels(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndClassInst *self = obj;
    struct gslTaskSpec rel_spec = {
        .is_list_item = true,
        .accu = self,
        .alloc = rel_alloc,
        .append = rel_append,
        .parse = rel_read
    };
    
    if (DEBUG_INST_LEVEL_2)
        knd_log(".. reading rels of obj %.*s..",
                self->name_size, self->name);

    return gsl_parse_array(&rel_spec, rec, total_size);
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

static gsl_err_t read_state(struct kndClassInst *self,
                            const char *rec,
                            size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_state_id,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = parse_import_elem,
          .obj = self
        },
        { .is_validator = true,
          .validate = parse_import_elem,
          .obj = self
        }
    };

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. reading class inst state: %.*s", 128, rec);
 
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
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

static gsl_err_t parse_import_inst(struct kndClassInst *self,
                                   const char *rec,
                                   size_t *total_size)
{
    if (DEBUG_INST_LEVEL_2)
        knd_log(".. parsing class inst (repo:%.*s) import REC: %.*s",
                self->base->entry->repo->name_size,
                self->base->entry->repo->name,
                128, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = parse_import_elem,
          .obj = self
        },
        { .is_validator = true,
          .validate = parse_import_elem,
          .obj = self
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
          .obj = self
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

extern void kndClassInst_init(struct kndClassInst *self)
{
    self->parse = parse_import_inst;
    //self->read = parse_import_inst;
    //self->read_state  = read_state;
    self->resolve = kndClassInst_resolve;
    self->export = kndClassInst_export;
}

extern int knd_class_inst_entry_new(struct kndMemPool *mempool,
                                    struct kndClassInstEntry **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                            sizeof(struct kndClassInstEntry), &page);  RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_class_inst_new(struct kndMemPool *mempool,
                              struct kndClassInst **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                            sizeof(struct kndClassInst), &page);                  RET_ERR();
    *result = page;
    kndClassInst_init(*result);
    return knd_OK;
}
