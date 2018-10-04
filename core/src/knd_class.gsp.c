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

#define DEBUG_CLASS_GSP_LEVEL_1 0
#define DEBUG_CLASS_GSP_LEVEL_2 0
#define DEBUG_CLASS_GSP_LEVEL_3 0
#define DEBUG_CLASS_GSP_LEVEL_4 0
#define DEBUG_CLASS_GSP_LEVEL_5 0
#define DEBUG_CLASS_GSP_LEVEL_TMP 1

static int export_glosses(struct kndClass *self,
                          struct glbOutput *out)
{
    struct kndTranslation *tr;
    int err;
    err = out->write(out, "[!_g", strlen("[!_g"));
    if (err) return err;

    for (tr = self->tr; tr; tr = tr->next) {
        err = out->write(out, "{", 1);
        if (err) return err;
        err = out->write(out, tr->locale, tr->locale_size);
        if (err) return err;
        err = out->write(out, "{t ", 3);
        if (err) return err;
        err = out->write(out, tr->val, tr->val_size);
        if (err) return err;
        err = out->write(out, "}}", 2);
        if (err) return err;
    }
    err = out->write(out, "]", 1);
    if (err) return err;
    return knd_OK;
}

/*static int export_summary(struct kndClass *self,
                          struct glbOutput *out)
{
    struct kndTranslation *tr;
    int err;

    err = out->write(out, "[!_summary", strlen("[!_summary"));
    if (err) return err;

    for (tr = self->tr; tr; tr = tr->next) {
        err = out->write(out, "{", 1);
        if (err) return err;
        err = out->write(out, tr->locale, tr->locale_size);
        if (err) return err;
        err = out->write(out, "{t ", 3);
        if (err) return err;
        err = out->write(out, tr->val, tr->val_size);
        if (err) return err;
        err = out->write(out, "}}", 2);
        if (err) return err;
    }
    err = out->write(out, "]", 1);
    if (err) return err;
    return knd_OK;
}
*/

static int export_baseclass_vars(struct kndClass *self,
                                 struct glbOutput *out)
{
    struct kndClassVar *item;
    struct kndClass *c;
    int err;

    err = out->write(out, "[!_is", strlen("[!_is"));                              RET_ERR();
    for (item = self->baseclass_vars; item; item = item->next) {
        err = out->writec(out, '{');                                              RET_ERR();
        c = item->entry->class;
        err = out->write(out, c->entry->id, c->entry->id_size);             RET_ERR();
        if (item->attrs) {
            err = knd_attr_vars_export_GSP(item->attrs, out, 0, false);
            if (err) return err;
        }
        err = out->writec(out, '}');                                              RET_ERR();
    }
    err = out->writec(out, ']');                                                  RET_ERR();
    return knd_OK;
}

/*static gsl_err_t read_GSP(struct kndClass *self,
                          const char *rec,
                          size_t *total_size)
{
    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log(".. reading GSP: \"%.*s\"..", 256, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_name,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = parse_gloss_array,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_summary",
          .name_size = strlen("_summary"),
          .parse = parse_summary_array,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_ci",
          .name_size = strlen("_ci"),
          .parse = parse_class_var_array,
          .obj = self
        },
        { .name = "inner",
          .name_size = strlen("inner"),
          .parse = parse_inner,
          .obj = self
        },
        { .name = "str",
          .name_size = strlen("str"),
          .parse = parse_str,
          .obj = self
        },
        { .name = "bin",
          .name_size = strlen("bin"),
          .parse = parse_bin,
          .obj = self
        },
        { .name = "num",
          .name_size = strlen("num"),
          .parse = parse_num,
          .obj = self
        },
        { .name = "ref",
          .name_size = strlen("ref"),
          .parse = parse_ref,
          .obj = self
        },
        { .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc,
          .obj = self
        },
        { .name = "text",
          .name_size = strlen("text"),
          .parse = parse_text,
          .obj = self
        },
        { .name = "_desc",
          .name_size = strlen("_desc"),
          .parse = parse_descendants,
          .obj = self
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}
*/

static int export_conc_id_GSP(void *obj,
                              const char *elem_id  __attribute__((unused)),
                              size_t elem_id_size  __attribute__((unused)),
                              size_t count __attribute__((unused)),
                              void *elem)
{
    struct glbOutput *out = obj;
    struct kndClassEntry *entry = elem;
    int err;
    err = out->writec(out, ' ');                                                  RET_ERR();
    err = out->write(out, entry->id, entry->id_size);                                 RET_ERR();
    return knd_OK;
}

static int export_facets_GSP(struct kndClass *self, struct kndSet *set)
{
    struct glbOutput *out = self->entry->repo->out;
    struct kndFacet *facet;
    //struct ooDict *set_name_idx;
    int err;

    err = out->write(out,  "[fc ", strlen("[fc "));                               RET_ERR();
    for (facet = set->facets; facet; facet = facet->next) {
        err = out->write(out,  "{", 1);                                           RET_ERR();
        err = out->write(out, facet->attr->name, facet->attr->name_size);
        err = out->write(out,  " ", 1);                                           RET_ERR();

        /*if (facet->set_name_idx) {
            err = out->write(out,  "[set", strlen("[set"));                       RET_ERR();
            set_name_idx = facet->set_name_idx;
            key = NULL;
            set_name_idx->rewind(set_name_idx);
            do {
                set_name_idx->next_item(set_name_idx, &key, &val);
                if (!key) break;
                subset = (struct kndSet*)val;

                err = out->writec(out, '{');                                      RET_ERR();
                err = out->write(out, subset->base->id,
                                 subset->base->id_size);                          RET_ERR();

                err = out->write(out, "[c", strlen("[c"));                        RET_ERR();
                err = subset->map(subset, export_conc_id_GSP, (void*)out);
                if (err) return err;
                err = out->writec(out, ']');                                      RET_ERR();
                err = out->writec(out, '}');                                      RET_ERR();
            } while (key);
            err = out->write(out,  "]", 1);                                       RET_ERR();
            }*/
        err = out->write(out,  "}", 1);                                           RET_ERR();
    }
    err = out->write(out,  "]", 1);                                               RET_ERR();
    return knd_OK;
}

static int export_descendants_GSP(struct kndClass *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct glbOutput *out = self->entry->repo->out;
    struct kndSet *set;
    int err;

    set = self->entry->descendants;

    err = out->write(out, "{_desc", strlen("{_desc"));                            RET_ERR();
    buf_size = sprintf(buf, "{tot %zu}", set->num_elems);
    err = out->write(out, buf, buf_size);                                         RET_ERR();

    err = out->write(out, "[c", strlen("[c"));                                    RET_ERR();
    err = set->map(set, export_conc_id_GSP, (void*)out);
    if (err) return err;
    err = out->writec(out, ']');                                                  RET_ERR();

    if (set->num_facets) {
        err = export_facets_GSP(self, set);                                       RET_ERR();
    }

    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

/*static int ref_list_export_GSP(struct kndClass *self,
                               struct kndAttrVar *parent_item)
{
    struct kndAttrVar *item;
    struct glbOutput *out;
    struct kndClass *c;
    int err;

    out = self->entry->repo->out;

    err = out->writec(out, '{');
    if (err) return err;
    err = out->write(out, parent_item->name, parent_item->name_size);
    if (err) return err;

    err = out->write(out, "[r", strlen("[r"));
    if (err) return err;

    if (parent_item->val_size) {
        c = parent_item->class;
        if (c) {
            err = out->writec(out, ' ');
            if (err) return err;
            err = out->write(out, c->entry->id, c->entry->id_size);
            if (err) return err;
        }
    }

    for (item = parent_item->list; item; item = item->next) {
        c = item->class;
        if (!c) continue;

        err = out->writec(out, ' ');
        if (err) return err;
        err = out->write(out, c->entry->id, c->entry->id_size);
        if (err) return err;
    }
    err = out->writec(out, ']');
    if (err) return err;

    err = out->writec(out, '}');
    if (err) return err;

    return knd_OK;
}
*/

 /*static int inner_item_export_GSP(struct kndClass *self,
                                struct kndAttrVar *parent_item)
{
    struct glbOutput *out = self->entry->repo->out;
    struct kndClass *c = parent_item->class;
    struct kndAttrVar *item;
    struct kndAttr *attr;
    int err;

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log("== GSP export of inner item: %.*s val:%.*s",
                parent_item->name_size, parent_item->name,
                parent_item->val_size, parent_item->val);

    err = out->writec(out, '{');
    if (err) return err;

    if (c) {
        err = out->write(out, c->entry->id, c->entry->id_size);
        if (err) return err;
    } else {

        if (parent_item->val_size) {
            c = parent_item->attr->ref_class;
            if (c && c->implied_attr) {
                attr = c->implied_attr;
                err = out->writec(out, ' ');
                if (err) return err;
                err = out->write(out,
                                 parent_item->val,
                                 parent_item->val_size);
                if (err) return err;
            } else {
                err = out->writec(out, ' ');
                if (err) return err;
                err = out->write(out,
                                 parent_item->val,
                                 parent_item->val_size);
                if (err) return err;
            }
        } else {
            err = out->write(out,
                             parent_item->id,
                             parent_item->id_size);
            if (err) return err;
        }
    }

    c = parent_item->attr->ref_class;

    for (item = parent_item->children; item; item = item->next) {
        err = out->writec(out, '{');
        if (err) return err;

        err = out->write(out, item->name, item->name_size);
        if (err) return err;

        switch (item->attr->type) {
        case KND_ATTR_REF:
            //knd_log("ref item: %.*s", item->name_size, item->name);
            break;
        case KND_ATTR_INNER:
            err = inner_item_export_GSP(self, item);
            if (err) return err;
            break;
        default:
            err = out->writec(out, ' ');
            if (err) return err;
            err = out->write(out, item->val, item->val_size);
            if (err) return err;
            break;
        }

        err = out->writec(out, '}');
        if (err) return err;
    }

    //if (parent_item->class) {
    err = out->writec(out, '}');
    if (err) return err;

    return knd_OK;
}
 */
 
/*static int inner_list_export_GSP(struct kndClass *self,
                                struct kndAttrVar *parent_item)
{
    struct kndAttrVar *item;
    struct glbOutput *out = self->entry->repo->out;
    struct kndClass *c;
    int err;

    if (DEBUG_CLASS_GSP_LEVEL_1) {
        knd_log(".. export inner list: %.*s  val:%.*s",
                parent_item->name_size, parent_item->name,
                parent_item->val_size, parent_item->val);
    }

    err = out->writec(out, '[');
    if (err) return err;
    err = out->write(out, parent_item->name, parent_item->name_size);
    if (err) return err;

    if (parent_item->class) {
        err = inner_item_export_GSP(self, parent_item);
        if (err) return err;
    }

    for (item = parent_item->list; item; item = item->next) {
        c = item->class;
        err = inner_item_export_GSP(self, item);
        if (err) return err;
    }

    err = out->writec(out, ']');
    if (err) return err;

    return knd_OK;
}
*/

static int export_class_body_updates(struct kndClass *self,
                                     struct kndClassUpdate *class_update  __attribute__((unused)),
                                     struct glbOutput *out)
{
    struct kndState *state = self->states;
    struct kndAttr *attr;
    int err;

    switch (state->phase) {
    case KND_CREATED:
        err = out->write(out, "{_new}", strlen("{_new}"));                        RET_ERR();
        break;
    case KND_REMOVED:
        err = out->write(out, "{_rm}", strlen("{_rm}"));                          RET_ERR();
        break;
    default:
        break;
    }

    // TODO

    if (self->tr) {
        err = export_glosses(self, out);                                          RET_ERR();
    }

    if (self->baseclass_vars) {
        err = export_baseclass_vars(self, out);                                   RET_ERR();
    }

    if (self->attrs) {
        for (attr = self->attrs; attr; attr = attr->next) {
            err = knd_attr_export(attr, KND_FORMAT_GSP, out);
            if (err) return err;
        }
    }
    
    return knd_OK;
}

static int export_class_inst_updates(struct kndClass *self  __attribute__((unused)),
                                     struct kndClassUpdate *class_update,
                                     struct glbOutput *out)
{
    struct kndClassInst *inst;
    int err;

    err = out->write(out, "[!inst", strlen("[!inst"));                            RET_ERR();
    for (size_t i = 0; i < class_update->num_insts; i++) {
        inst = class_update->insts[i];
        err = out->writec(out, '{');                                              RET_ERR();
        err = out->write(out, inst->entry->id, inst->entry->id_size);             RET_ERR();

        err = out->write(out, "{_n ", strlen("{_n "));                            RET_ERR();
        err = out->write(out, inst->name, inst->name_size);                       RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();

        //err = inst->export_state(inst, KND_FORMAT_GSP, out);                      RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
    }
    err = out->writec(out, ']');                                                  RET_ERR();

    return knd_OK;
}

extern int knd_class_export_updates_GSP(struct kndClass *self,
                                        struct kndClassUpdate *class_update,
                                        struct glbOutput *out)
{
    struct kndUpdate *update = class_update->update;
    struct kndState *state = self->states;
    int err;
    
    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->write(out, self->entry->id, self->entry->id_size);                 RET_ERR();
    err = out->write(out, "{_n ", strlen("{_n "));                                RET_ERR();
    err = out->write(out, self->name, self->name_size);                           RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    err = out->write(out, "{_st", strlen("{_st"));                                RET_ERR();

    if (state && state->update == update) {
        err = out->writec(out, ' ');                                              RET_ERR();

        // TODO
        //err = out->write(out, state->id, state->id_size);                         RET_ERR();

        /* any updates of the class body? */
        err = export_class_body_updates(self, class_update, out);                 RET_ERR();
    }

    if (self->inst_states) {
        state = self->inst_states;
        /* any updates of the class insts? */
        if (state->update == update) {
            err = export_class_inst_updates(self, class_update, out);             RET_ERR();
        }
    }

    err = out->writec(out, '}');                                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();
    return knd_OK;
}

extern int knd_class_export_GSP(struct kndClass *self,
                                struct glbOutput *out)
{
    struct kndAttr *attr;
    int err;

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log(".. GSP export of \"%.*s\" [%.*s]",
                self->entry->name_size, self->entry->name,
                self->entry->id_size, self->entry->id);

    err = out->writec(out, '{');
    if (err) return err;
    err = out->write(out, self->entry->id, self->entry->id_size);
    if (err) return err;
    err = out->write(out, "{_n ", strlen("{_n "));
    if (err) return err;
    err = out->write(out, self->entry->name, self->entry->name_size);
    if (err) return err;
    err = out->writec(out, '}');
    if (err) return err;

    if (self->tr) {
        err = export_glosses(self, out);                                          RET_ERR();
    }

    /*if (self->summary) {
        err = export_summary(self, out);                                          RET_ERR();
        }*/

    if (self->baseclass_vars) {
        err = export_baseclass_vars(self, out);                                   RET_ERR();
    }

    if (self->attrs) {
        for (attr = self->attrs; attr; attr = attr->next) {
            err = knd_attr_export(attr, KND_FORMAT_GSP, out);                        RET_ERR();
        }
    }

    if (self->entry->descendants) {
        err = export_descendants_GSP(self);                                       RET_ERR();
    }

    err = out->writec(out, '}');
    if (err) return err;

    return knd_OK;
}

/*
static gsl_err_t atomic_elem_alloc(void *obj,
                                   const char *val,
                                   size_t val_size,
                                   size_t count  __attribute__((unused)),
                                   void **item __attribute__((unused)))
{
    struct kndClass *self = obj;
    struct kndSet *class_idx, *set;
    struct kndClassEntry *entry;
    void *elem;
    int err;

    if (DEBUG_CLASS_GSP_LEVEL_2) {
        knd_log("Conc %.*s: atomic elem alloc: \"%.*s\"",
                self->entry->name_size, self->entry->name,
                val_size, val);
    }
    class_idx = self->entry->repo->class_idx;

    err = class_idx->get(class_idx, val, val_size, &elem);
    if (err) {
        knd_log("-- IDX:%p couldn't resolve class id: \"%.*s\" [size:%zu] :(",
                class_idx, val_size, val, val_size);
        return make_gsl_err_external(knd_NO_MATCH);
    }

    entry = elem;

    set = self->entry->descendants;
    err = set->add(set, entry->id, entry->id_size, (void*)entry);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}
*/

 /*static gsl_err_t facet_alloc(void *obj,
                             const char *name,
                             size_t name_size,
                             size_t count __attribute__((unused)),
                             void **item)
{
    struct kndSet *set = obj;
    struct kndFacet *f;
    struct kndMemPool *mempool = set->base->repo->mempool;
    int err;

    assert(name == NULL && name_size == 0);

    if (DEBUG_CLASS_GSP_LEVEL_1)
        knd_log(".. \"%.*s\" set to alloc a facet..",
                set->base->name_size, set->base->name);

    err = knd_facet_new(mempool, &f);
    if (err) return make_gsl_err_external(err);

    //err = ooDict_new(&f->set_name_idx, KND_SMALL_DICT_SIZE);
    //if (err) return make_gsl_err_external(err);

    f->parent = set;

    *item = (void*)f;

    return make_gsl_err(gsl_OK);
}
 */

  /*
static gsl_err_t facet_append(void *accu,
                              void *item)
{
    struct kndSet *set = accu;
    struct kndFacet *f = item;

    if (set->num_facets >= KND_MAX_ATTRS)
        return make_gsl_err_external(knd_LIMIT);

    set->facets[set->num_facets] = f;
    set->num_facets++;

    return make_gsl_err(gsl_OK);
}
*/

   /*static gsl_err_t set_facet_name(void *obj, const char *name, size_t name_size)
{
    struct kndFacet *f = obj;
    struct kndSet *parent = f->parent;
    struct kndClass *c;
    struct kndAttr *attr;
    int err;

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log(".. set %.*s to add facet \"%.*s\"..",
                parent->base->name_size, parent->base->name, name_size, name);

    c = parent->base->class;
    err = knd_class_get_attr(c, name, name_size, &attr);
    if (err) {
        knd_log("-- no such facet attr: \"%.*s\"",
                name_size, name);
        return make_gsl_err_external(err);
    }

    f->attr = attr;
    return make_gsl_err(gsl_OK);
}
   */

    /*
static gsl_err_t set_alloc(void *obj,
                           const char *name,
                           size_t name_size,
                           size_t count __attribute__((unused)),
                           void **item)
{
    struct kndFacet *self = obj;
    struct kndSet *set;
    struct kndMemPool *mempool = self->attr->parent_class->entry->repo->mempool;
    int err;

    assert(name == NULL && name_size == 0);

    err = knd_set_new(mempool, &set);
    if (err) return make_gsl_err_external(err);

    set->type = KND_SET_CLASS;
    //set->mempool = self->entry->repo->mempool;
    set->parent_facet = self;

    *item = (void*)set;

    return make_gsl_err(gsl_OK);
}
    */
    
/*static gsl_err_t set_append(void *accu,
                            void *item)
{
    struct kndFacet *self = accu;
    struct kndSet *set = item;
    int err;

    return make_gsl_err(gsl_OK);
}
*/


 /*
static gsl_err_t atomic_classref_alloc(void *obj,
                                       const char *val,
                                       size_t val_size,
                                       size_t count  __attribute__((unused)),
                                       void **item __attribute__((unused)))
{
    struct kndSet *self = obj;
    struct kndSet *class_idx;
    struct kndClassEntry *entry;
    void *elem;
    int err;

    entry = self->base;

    if (DEBUG_CLASS_GSP_LEVEL_2) {
        knd_log(".. base class \"%.*s\": atomic classref alloc: \"%.*s\"",
                entry->name_size, entry->name,
                val_size, val);
    }

    class_idx = entry->repo->class_idx;

    err = class_idx->get(class_idx, val, val_size, &elem);
    if (err) {
        knd_log("-- IDX:%p couldn't resolve class id: \"%.*s\" [size:%zu] :(",
                class_idx, val_size, val, val_size);
        return make_gsl_err_external(knd_NO_MATCH);
    }
    entry = elem;

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log("++ classref resolved: %.*s \"%.*s\"",
                val_size, val, entry->name_size, entry->name);

    err = self->add(self, val, val_size, (void*)entry);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}
 */

  /*
static gsl_err_t resolve_set_base(void *obj,
                                  const char *id, size_t id_size)
{
    struct kndSet *set = obj;
    struct kndFacet *parent_facet = set->parent_facet;
    struct kndClass *c;
    struct kndSet *class_idx;
    void *result;
    int err;

    c = parent_facet->parent->base->class;
    class_idx = c->entry->repo->class_idx;

    err = class_idx->get(class_idx,
                         id, id_size, &result);
    if (err) {
        knd_log("-- no such class: \"%.*s\":(", id_size, id);
        return make_gsl_err_external(err);
    }
    set->base = result;

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log("++ base class: %.*s \"%.*s\"",
                id_size, id, set->base->name_size, set->base->name);

    return make_gsl_err(gsl_OK);
}
  */
  
/*
static gsl_err_t set_read(void *obj,
                          const char *rec,
                          size_t *total_size)
{
    struct kndSet *set = obj;

    struct gslTaskSpec classref_spec = {
        .is_list_item = true,
        .alloc = atomic_classref_alloc,
        //.append = atomic_elem_append,
        .accu = set
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = resolve_set_base,
          .obj = set
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "c",
          .name_size = strlen("c"),
          .parse = gsl_parse_array,
          .obj = &classref_spec
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (!set->base) {
        knd_log("-- no base class provided");
        return make_gsl_err(gsl_FORMAT);
    }

    return make_gsl_err(gsl_OK);
}
*/

/*
static gsl_err_t facet_read(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndFacet *f = obj;

    struct gslTaskSpec set_item_spec = {
        .is_list_item = true,
        .alloc = set_alloc,
        //.append = set_append,
        .accu = f,
        .parse = set_read
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_facet_name,
          .obj = f
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "set",
          .name_size = strlen("set"),
          .parse = gsl_parse_array,
          .obj = &set_item_spec
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (f->attr == NULL) {
        knd_log("-- no facet name provided :(");
        return make_gsl_err(gsl_FORMAT);
    }

    return make_gsl_err(gsl_OK);
}
*/
static gsl_err_t attr_var_alloc(void *obj,
                                const char *name __attribute__((unused)),
                                size_t name_size __attribute__((unused)),
                                size_t count __attribute__((unused)),
                                void **result)
{
    struct kndAttrVar *self = obj;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = self->attr->parent_class->entry->repo->mempool;
    int err;

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return make_gsl_err_external(err);

    //attr_var->name_size = sprintf(attr_var->name, "%lu", (unsigned long)count);
    attr_var->attr = self->attr;
    attr_var->parent = self;

    *result = attr_var;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t attr_var_append(void *accu,
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

static gsl_err_t check_list_item_id(void *obj,
                                    const char *id, size_t id_size)
{
    struct kndAttrVar *attr_var = obj;
    struct kndClass *parent_class = attr_var->attr->parent_class;
    struct kndClass *c;
    int err;

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    memcpy(attr_var->id, id, id_size);
    attr_var->id_size = id_size;

    switch (attr_var->attr->type) {
    case KND_ATTR_REF:
        err = knd_get_class_by_id(parent_class, id, id_size, &c);
        if (err) {
            knd_log("-- no such class: %.*s", id_size, id);
            return make_gsl_err(gsl_FAIL);
        }

        if (DEBUG_CLASS_GSP_LEVEL_2)
            c->str(c);

        attr_var->class = c;
        attr_var->class_entry = c->entry;
        break;
    case KND_ATTR_INNER:
        // TODO
        //knd_log(".. checking inner item id: %.*s", id_size, id);

        err = knd_get_class_by_id(parent_class, id, id_size, &c);
        if (err) {
            knd_log("-- no such class: %.*s", id_size, id);
            return make_gsl_err(gsl_FAIL);
        }

        if (DEBUG_CLASS_GSP_LEVEL_2)
            c->str(c);

        attr_var->class = c;
        attr_var->class_entry = c->entry;
        
    default:
        break;
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_nested_attr_var(void *obj,
                                      const char *name, size_t name_size,
                                      const char *rec, size_t *total_size)
{
    struct kndAttrVar *self = obj;
    struct kndAttrVar *attr_var;
    struct kndAttr *attr;
    struct kndAttrRef *ref;
    struct kndClass *c = NULL;
    struct ooDict *class_name_idx;
    struct kndClassEntry *entry;
    struct kndMemPool *mempool = self->attr->parent_class->entry->repo->mempool;
    gsl_err_t parser_err;
    int err;

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->parent = self;
    attr_var->class_var = self->class_var;

    attr_var->name = name;
    attr_var->name_size = name_size;

    if (DEBUG_CLASS_GSP_LEVEL_2) {
        knd_log("== reading nested attr item: \"%.*s\" REC: %.*s VAL:%.*s",
                name_size, name, 16, rec, attr_var->val_size, attr_var->val);
    }

    if (!self->attr->ref_class) {
        knd_log("-- no class ref in attr: \"%.*s\"",
                self->attr->name_size, self->attr->name);
        return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }

    c = self->attr->ref_class;

    err = knd_class_get_attr(c, name, name_size, &ref);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\" :(",
                name_size, name,
                c->entry->name_size, c->entry->name);
        if (err) return *total_size = 0, make_gsl_err_external(err);
    }
    attr = ref->attr;

    switch (attr->type) {
    case KND_ATTR_INNER:
        if (attr->ref_class) break;

        class_name_idx = c->entry->repo->class_name_idx;
        entry = class_name_idx->get(class_name_idx,
                                    attr->ref_classname,
                                    attr->ref_classname_size);
        if (!entry) {
            knd_log("-- inner ref not resolved :( no such class: %.*s",
                    attr->ref_classname_size,
                    attr->ref_classname);
            return *total_size = 0, make_gsl_err(gsl_FAIL);
        }

        if (!entry->class) {
            //err = unfreeze_class(conc, entry, &entry->class);
            //if (err) return *total_size = 0, make_gsl_err_external(err);
        }
        attr->ref_class = entry->class;
        break;
    default:
        break;
    }
    attr_var->attr = attr;

    struct gslTaskSpec specs[] = {
        /*{ .is_implied = true,
          .buf = attr_var->val,
          .buf_size = &attr_var->val_size,
          .max_buf_size = sizeof attr_var->val
          }*/
        { .is_validator = true,
          .validate = read_nested_attr_var,
          .obj = attr_var
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log("== got attr var value: %.*s => %.*s",
                attr_var->name_size, attr_var->name,
                attr_var->val_size, attr_var->val);

    /* resolve refs */
    switch (attr->type) {
    case KND_ATTR_REF:
        err = knd_get_class_by_id(self->attr->parent_class,
                                  attr_var->val, attr_var->val_size, &c);
        if (err) {
            knd_log("-- no such class: %.*s", attr_var->val_size, attr_var->val);
            return make_gsl_err(gsl_FAIL);
        }

        if (DEBUG_CLASS_GSP_LEVEL_2)
            c->str(c);

        attr_var->class = c;
        attr_var->class_entry = c->entry;
        break;
    default:
        break;
    }

    attr_var->next = self->children;
    self->children = attr_var;
    self->num_children++;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t attr_var_parse(void *obj,
                                const char *rec,
                                size_t *total_size)
{
    struct kndAttrVar *item = obj;
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = check_list_item_id,
          .obj = item
        },
        { .is_validator = true,
          .validate = read_nested_attr_var,
          .obj = item
        }
    };

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log(".. parsing nested item: \"%.*s\"", 64, rec);

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}


/*static gsl_err_t attr_var_alloc(void *obj,
                                 const char *name,
                                 size_t name_size,
                                 size_t count  __attribute__((unused)),
                                 void **result)
{
    struct kndAttrVar *self = obj;
    struct kndAttrVar *attr_var;
    struct kndSet *class_idx;
    struct kndClassEntry *entry;
    struct kndMemPool *mempool = self->attr->parent_class->entry->repo->mempool;
    void *elem;
    int err;

    if (DEBUG_CLASS_GSP_LEVEL_2) {
        knd_log(".. alloc attr item conc id: \"%.*s\" attr:%p",
                name_size, name, self->attr);
    }

    class_idx = self->attr->parent_class->class_idx;

    err = class_idx->get(class_idx, name, name_size, &elem);
    if (err) {
        knd_log("-- couldn't resolve class id: \"%.*s\" [size:%zu] :(",
                name_size, name, name_size);
        return make_gsl_err_external(knd_NO_MATCH);
    }

    entry = elem;

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) return make_gsl_err_external(err);

    attr_var->class_entry = entry;
    attr_var->attr = self->attr;

    *result = attr_var;
    return make_gsl_err(gsl_OK);
}
*/

static gsl_err_t alloc_class_inst(void *obj,
                                  const char *val  __attribute__((unused)),
                                  size_t val_size __attribute__((unused)),
                                  size_t count  __attribute__((unused)),
                                  void **item)
{
    struct kndClass *self = obj;
    struct kndClassInst *inst;
    struct kndClassInstEntry *entry;
    struct kndState *state;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    int err;

    err = knd_class_inst_new(mempool, &inst);
    if (err) {
        knd_log("-- class inst alloc failed :(");
        return make_gsl_err_external(err);
    }

    err = knd_state_new(mempool, &state);
    if (err) {
        knd_log("-- state alloc failed :(");
        return make_gsl_err_external(err);
    }
    state->phase = KND_CREATED;
    state->next = inst->states;
    inst->states = state;

    err = knd_class_inst_entry_new(mempool, &entry);
    if (err) return make_gsl_err_external(err);

    inst->entry = entry;
    entry->inst = inst;
    inst->base = self;
    *item = inst;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t append_class_inst(void *accu,
                                   void *item)
{
    struct kndClass *self =   accu;
    struct kndClassInst *inst = item;
    struct ooDict *name_idx = self->entry->repo->class_inst_name_idx;
    struct kndSet *set = self->entry->inst_idx;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    int err;

    if (DEBUG_CLASS_GSP_LEVEL_2) {
        knd_log(".. append inst: %.*s %.*s",
                inst->entry->id_size, inst->entry->id,
                inst->entry->name_size, inst->entry->name);
        knd_class_inst_str(inst, 0);
    }

    if (!name_idx) {
        err = ooDict_new(&self->entry->repo->class_inst_name_idx, KND_HUGE_DICT_SIZE);
        if (err) return make_gsl_err_external(err);
        name_idx = self->entry->repo->class_inst_name_idx;
    }

    err = name_idx->set(name_idx,
                        inst->name, inst->name_size,
                        (void*)inst->entry);
    if (err) return make_gsl_err_external(err);

    self->entry->num_insts++;

    /* index by id */
    if (!set) {
        err = knd_set_new(mempool, &set);
        if (err) return make_gsl_err_external(err);
        set->type = KND_SET_CLASS_INST;
        self->entry->inst_idx = set;
    }
    err = set->add(set, inst->entry->id, inst->entry->id_size, (void*)inst->entry);
    if (err) {
        knd_log("-- failed to update the class inst idx");
        return make_gsl_err_external(err);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_class_inst_id(void *obj, const char *name, size_t name_size)
{
    struct kndClassInst *self = obj;

    memcpy(self->entry->id, name, name_size);
    self->entry->id_size = name_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_class_inst_name(void *obj, const char *name, size_t name_size)
{
    struct kndClassInst *self = obj;

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log(".. class inst name: %.*s ..", name_size, name);

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= sizeof self->entry->name) return make_gsl_err(gsl_LIMIT);

    self->entry->name = name;
    self->entry->name_size = name_size;

    self->name = self->entry->name;
    self->name_size = self->entry->name_size;
    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_inst_state(void *obj,
                                        const char *rec,
                                        size_t *total_size)
{
    struct kndClassInst *inst = obj;
    return inst->read_state(inst, rec, total_size);
}

static gsl_err_t parse_class_inst(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndClassInst *inst = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_inst_id,
          .obj = inst
        },
        { .name = "_n",
          .name_size = strlen("_n"),
          .run = set_class_inst_name,
          .obj = inst
        },
        { .name = "_st",
          .name_size = strlen("_st"),
          .parse = parse_class_inst_state,
          .obj = inst
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_class_state(void *obj,
                                 const char *name  __attribute__((unused)),
                                 size_t name_size  __attribute__((unused)))
{
    struct kndClass *self = obj;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    struct kndState *state;
    int err;

    err = knd_state_new(mempool, &state);
    if (err) {
        knd_log("-- state alloc failed :(");
        return make_gsl_err_external(err);
    }

    state->next = self->states;
    self->states = state;

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

static gsl_err_t validate_attr_var(void *obj,
                                    const char *name, size_t name_size,
                                    const char *rec, size_t *total_size)
{
    struct kndClassVar *class_var = obj;
    struct kndAttrVar *attr_var;
    struct kndAttr *attr;
    struct kndAttrRef *ref;
    struct kndProc *root_proc;
    struct kndRepo *repo = class_var->entry->repo;
    struct kndMemPool *mempool = repo->mempool;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log(".. class var \"%.*s\" to validate attr var: %.*s..",
                class_var->entry->name_size, class_var->entry->name,
                name_size, name);

    if (!class_var->entry->class) {
        knd_log("-- class var not yet resolved: %.*s",
                class_var->entry->name_size, class_var->entry->name);
        return make_gsl_err(gsl_FAIL);
    }

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->class_var = class_var;

    err = knd_class_get_attr(class_var->entry->class,
                             name, name_size, &ref);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name,
                class_var->entry->name_size, class_var->entry->name);
        return *total_size = 0, make_gsl_err_external(err);
    }
    attr = ref->attr;
    attr_var->attr = attr;
    attr_var->name = name;
    attr_var->name_size = name_size;

    struct gslTaskSpec attr_var_spec = {
        .is_list_item = true,
        .accu = attr_var,
        .alloc = attr_var_alloc,
        .append = attr_var_append
    };

    struct gslTaskSpec specs[] = {
        /*{ .is_implied = true,
          .buf = attr_var->val,
          .buf_size = &attr_var->val_size,
          .max_buf_size = sizeof attr_var->val
          },*/
        { .is_validator = true,
          .validate = read_nested_attr_var,
          .obj = attr_var
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "r",
          .name_size = strlen("r"),
          .parse = gsl_parse_array,
          .obj = &attr_var_spec
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    switch (attr->type) {
    case KND_ATTR_PROC:

        if (DEBUG_CLASS_GSP_LEVEL_2)
            knd_log("== proc attr: %.*s => %.*s",
                    attr_var->name_size, attr_var->name,
                    attr_var->val_size, attr_var->val);

        root_proc = class_var->root_class->proc;
        err = knd_get_proc(repo,
                           attr_var->val,
                           attr_var->val_size, &attr_var->proc);
        if (err) return make_gsl_err_external(err);
        break;
    default:
        break;
    }

    append_attr_var(class_var, attr_var);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t validate_attr_var_list(void *obj,
                                         const char *name, size_t name_size,
                                         const char *rec, size_t *total_size)
{
    struct kndClassVar *class_var = obj;
    struct kndAttrVar *attr_var;
    struct kndAttr *attr;
    struct kndAttrRef *ref;
    struct ooDict *class_name_idx;
    struct kndClassEntry *entry;
    struct kndMemPool *mempool = class_var->entry->repo->mempool;
    int err;

    if (DEBUG_CLASS_GSP_LEVEL_1)
        knd_log("\n.. \"%.*s\" class to validate list attr: %.*s..",
                class_var->entry->name_size, class_var->entry->name,
                name_size, name);

    err = knd_attr_var_new(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    err = knd_class_get_attr(class_var->entry->class,
                             name, name_size, &ref);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name,
                class_var->entry->name_size, class_var->entry->name);
        return *total_size = 0, make_gsl_err_external(err);
    }
    attr = ref->attr;
    attr_var->attr = attr;
    attr_var->class_var = class_var;

    attr_var->name = name;
    attr_var->name_size = name_size;

    switch (attr->type) {
    case KND_ATTR_REF:
        //knd_log("== array of refs: %.*s", name_size, name);
        break;
    case KND_ATTR_INNER:
        if (attr->ref_class) break;

        // TODO
        class_name_idx = class_var->entry->repo->class_name_idx;
        entry = class_name_idx->get(class_name_idx,
                                    attr->ref_classname,
                                    attr->ref_classname_size);
        if (!entry) {
            knd_log("-- inner ref not resolved :( no such class: %.*s",
                    attr->ref_classname_size,
                    attr->ref_classname);
            return *total_size = 0, make_gsl_err(gsl_FAIL);
        }

        if (!entry->class) {
            //err = unfreeze_class(class_var->root_class, entry, &entry->class);
            //if (err) return *total_size = 0, make_gsl_err_external(err);
        }
        attr->ref_class = entry->class;
        break;
    default:
        break;
    }

    struct gslTaskSpec attr_var_spec = {
        .is_list_item = true,
        .accu = attr_var,
        .alloc = attr_var_alloc,
        .append = attr_var_append,
        .parse = attr_var_parse
    };

    append_attr_var(class_var, attr_var);

    return gsl_parse_array(&attr_var_spec, rec, total_size);
}


static gsl_err_t set_class_var_baseclass(void *obj,
                                         const char *id, size_t id_size)
{
    struct kndClassVar *class_var = obj;
    struct kndClass *c;
    int err;

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    memcpy(class_var->id, id, id_size);
    class_var->id_size = id_size;

    err = knd_get_class_by_id(class_var->root_class, id, id_size, &c);
    if (err) {
        return make_gsl_err(gsl_FAIL);
    }

    class_var->entry = c->entry;

    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log("== conc item baseclass: %.*s (id:%.*s)",
                c->entry->name_size, c->entry->name, id_size, id);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t class_var_alloc(void *obj,
                                 const char *name __attribute__((unused)),
                                 size_t name_size __attribute__((unused)),
                                 size_t count  __attribute__((unused)),
                                 void **item)
{
    struct kndClass *self = obj;
    struct kndClassVar *class_var;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    int err;

    err = knd_class_var_new(mempool, &class_var);
    if (err) return make_gsl_err_external(err);
    class_var->root_class = self;

    *item = class_var;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t class_var_append(void *accu,
                                  void *item)
{
    struct kndClass *self = accu;
    struct kndClassVar *ci = item;

    ci->next = self->baseclass_vars;
    self->baseclass_vars = ci;
    self->num_baseclass_vars++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t class_var_read(void *obj,
                                const char *rec, size_t *total_size)
{
    struct kndClassVar *cvar = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_var_baseclass,
          .obj = cvar
        },
        { .is_validator = true,
          .type = GSL_SET_ARRAY_STATE,
          .validate = validate_attr_var_list,
          .obj = cvar
        },
        { .is_validator = true,
          .validate = validate_attr_var,
          .obj = cvar
        }
    };

    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size,
                                specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    knd_calc_num_id(cvar->id, cvar->id_size, &cvar->numid);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_baseclass_array(void *obj,
                                       const char *rec,
                                       size_t *total_size)
{
    struct kndClass *self = obj;

    struct gslTaskSpec cvar_spec = {
        .is_list_item = true,
        .accu = self,
        .alloc = class_var_alloc,
        .append = class_var_append,
        .parse = class_var_read
    };

    return gsl_parse_array(&cvar_spec, rec, total_size);
}

extern gsl_err_t
knd_read_class_state(struct kndClass *self,
                     struct kndClassUpdate *class_update __attribute__((unused)),
                     const char *rec,
                     size_t *total_size)
{
    if (DEBUG_CLASS_GSP_LEVEL_2)
        knd_log(".. reading %.*s class state GSP: \"%.*s\"..",
                self->name_size, self->name, 256, rec);

    struct gslTaskSpec inst_update_spec = {
        .is_list_item = true,
        .alloc  = alloc_class_inst,
        .append = append_class_inst,
        .parse  = parse_class_inst,
        .accu = self
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_state,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = knd_parse_gloss_array,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_is",
          .name_size = strlen("_is"),
          .parse = parse_baseclass_array,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "inst",
          .name_size = strlen("inst"),
          .parse = gsl_parse_array,
          .obj = &inst_update_spec
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    // TODO
    //self->str(self);
    /*state = self->states;
    state->val = (void*)class_update;
    state->update = class_update->update;
    */
    return make_gsl_err(gsl_OK);
}
