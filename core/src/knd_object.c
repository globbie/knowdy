#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_concept.h"
#include "knd_mempool.h"
#include "knd_attr.h"
#include "knd_elem.h"
#include "knd_repo.h"
#include "knd_object.h"

#include "knd_text.h"
#include "knd_num.h"
#include "knd_rel.h"
#include "knd_rel_arg.h"

#include "knd_user.h"
#include "knd_state.h"

#include <gsl-parser.h>
#include <glb-lib/output.h>

#define DEBUG_OBJ_LEVEL_1 0
#define DEBUG_OBJ_LEVEL_2 0
#define DEBUG_OBJ_LEVEL_3 0
#define DEBUG_OBJ_LEVEL_4 0
#define DEBUG_OBJ_LEVEL_TMP 1

static int
export_rel_dir_JSON(struct kndObject *self);

static void del(struct kndObject *self)
{
    self->state->phase = KND_FREED;
}

static void str(struct kndObject *self)
{
    struct kndElem *elem;
    if (self->type == KND_OBJ_ADDR) {
        knd_log("\n%*sOBJ %.*s::%.*s  numid:%zu  entry:%p",
                self->depth * KND_OFFSET_SIZE, "",
                self->conc->name_size, self->conc->name,
                self->name_size, self->name,
                self->numid, self->entry);
    }

    for (elem = self->elems; elem; elem = elem->next) {
        elem->depth = self->depth + 1;
        elem->str(elem);
    }
}

static int 
kndObject_export_aggr_JSON(struct kndObject *self)
{
    struct kndElem *elem;
    int err;

    /* anonymous obj */
    err = self->out->write(self->out, "{", 1);
    if (err) return err;

    elem = self->elems;
    while (elem) {
        elem->out = self->out;
        elem->format = KND_FORMAT_JSON;
        err = elem->export(elem);
        if (err) return err;
        
        if (elem->next) {
            err = self->out->write(self->out, ",", 1);
            if (err) return err;
        }
        elem = elem->next;
    }

    err = self->out->write(self->out, "}", 1);
    if (err) return err;

    return knd_OK;
}



static int
export_rel_dir_JSON(struct kndObject *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndRel *rel;
    struct kndRelRef *relref;
    struct glbOutput *out = self->out;
    int err;

    /* sort refs by class */
    if (DEBUG_OBJ_LEVEL_2)
        knd_log("..export rel dir of %.*s.. entry:%p",
                self->name_size, self->name, self->entry);

    err = out->writec(out, '[');                                                 RET_ERR();

    /* class conc */
    for (relref = self->entry->rels; relref; relref = relref->next) {
        rel = relref->rel;

        err = out->write(out, "{\"n\":\"", strlen("{\"n\":\""));                  RET_ERR();
        err = out->write(out, rel->name, rel->name_size);                         RET_ERR();
        err = out->writec(out, '"');                                              RET_ERR();

        err = out->write(out, ",\"num_instances\":",
                       strlen(",\"num_instances\":"));                            RET_ERR();
        buf_size = snprintf(buf, KND_NAME_SIZE, "%zu", relref->num_insts);
        err = out->write(out, buf, buf_size);                                     RET_ERR();

        if (self->expand_depth) {
            err = out->write(out, ",\"instances\":",
                             strlen(",\"instances\":"));                          RET_ERR();
            rel->out = self->out;
            err = rel->export_insts(rel, relref->insts);                          RET_ERR();
        }

        err = out->writec(out, '}');                                              RET_ERR();
    }

    err = out->writec(out, ']');                                                  RET_ERR();

    return knd_OK;
}

static int
kndObject_export_JSON(struct kndObject *self)
{
    struct kndElem *elem;
    struct kndObject *obj;
    struct glbOutput *out = self->out;
    bool is_concise = true;
    bool need_separ;
    int err;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log("   .. export OBJ \"%s\"  (class: %.*s)  is_concise: %d\n",
                self->name, self->conc->name_size, self->conc->name, is_concise);

    if (self->type == KND_OBJ_AGGR) {
        err = kndObject_export_aggr_JSON(self);
        return err;
    }
    err = out->write(out, "{\"n\":\"", strlen("{\"n\":\""));                      RET_ERR();
    err = out->write(out, self->name, self->name_size);
    if (err) return err;
    err = out->write(out, "\"", 1);
    if (err) return err;

    /* TODO: conditional conc name  output */

    err = out->write(out, ",\"c\":\"", strlen(",\"c\":\""));
    if (err) return err;
    err = out->write(out, self->conc->name, self->conc->name_size);
    if (err) return err;
    err = out->write(out, "\"", 1);
    if (err) return err;

    /* TODO: id */

    need_separ = false;
    for (elem = self->elems; elem; elem = elem->next) {

        /* NB: restricted attr */
        if (elem->attr->access_type == KND_ATTR_ACCESS_RESTRICTED)
            continue;
        
        /* filter out detailed presentation */
        if (is_concise) {
            /* aggr obj? */
            if (elem->aggr) {
                obj = elem->aggr;
                obj->out = out;

                /*if (need_separ) {*/
                err = out->write(out, ",", 1);
                if (err) return err;

                err = out->write(out, "\"", 1);
                if (err) return err;
                err = out->write(out, elem->attr->name, elem->attr->name_size);
                if (err) return err;
                err = out->write(out, "\":", 2);
                if (err) return err;
                
                err = obj->export(obj);
                if (err) return err;

                need_separ = true;
                continue;
            }
            
            if (elem->attr) 
                if (elem->attr->concise_level)
                    goto export_elem;

            if (DEBUG_OBJ_LEVEL_2)
                knd_log("  .. skip JSON elem: %s..\n", elem->attr->name);

            continue;
        }

    export_elem:
        /*if (need_separ) {*/
        err = out->write(out, ",", 1);
        if (err) return err;

        /* default export */
        elem->out = out;
        elem->format =  KND_FORMAT_JSON;
        err = elem->export(elem);
        if (err) {
            knd_log("-- elem not exported: %s", elem->attr->name);
            return err;
        }
        
        need_separ = true;
    }

    if (self->entry->rels) {
        err = out->write(out, ",\"_rels\":", strlen(",\"_rels\":"));   RET_ERR();
        err = export_rel_dir_JSON(self);      RET_ERR();
    }

    err = out->write(out, "}", 1);
    if (err) return err;
    
    return err;
}

static int
export_rels_GSP(struct kndObject *self)
{
    struct kndRel *rel;
    struct kndRelRef *relref;
    struct kndRelInstance *rel_inst;
    struct kndRelArgInstance *rel_arg_inst;
    struct kndRelArgInstRef *rel_arg_inst_ref;

    struct glbOutput *out = self->out;
    int err;

    /* sort refs by class */
    if (DEBUG_OBJ_LEVEL_2)
        knd_log(".. GSP export rels of %.*s..",
                self->name_size, self->name);

    err = out->write(out, "[_rel", strlen("[_rel"));                              RET_ERR();
    for (relref = self->entry->rels; relref; relref = relref->next) {
        rel = relref->rel;
        err = out->writec(out, '{');                                              RET_ERR();
        err = out->write(out, rel->id, rel->id_size);                         RET_ERR();

        err = out->write(out, "[i", strlen("[i"));                          RET_ERR();
        for (rel_arg_inst_ref = relref->insts;
             rel_arg_inst_ref;
             rel_arg_inst_ref = rel_arg_inst_ref->next) {

            rel_inst = rel_arg_inst_ref->inst->rel_inst;
        
            err = out->writec(out, ' ');                                          RET_ERR();
            err = out->write(out, rel_inst->id, rel_inst->id_size);               RET_ERR();
        }
        err = out->writec(out, ']');                                              RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
    }
    err = out->writec(out, ']');                                                  RET_ERR();

    return knd_OK;
}

static int 
kndObject_export_GSP(struct kndObject *self)
{
    struct kndElem *elem;
    struct glbOutput *out = self->out;
    size_t start_size = 0;
    int err;

    if (self->type == KND_OBJ_ADDR) {
        start_size = self->out->buf_size;

        if (DEBUG_OBJ_LEVEL_2)
            knd_log("%*s.. export GSP obj \"%.*s\" [id: %.*s]..",
                    self->depth *  KND_OFFSET_SIZE, "",
                    self->name_size, self->name, KND_ID_SIZE, self->id);

        err = out->writec(out, '{');
        if (err) return err;
        err = out->write(out, self->id, self->id_size);
        if (err) return err;

        err = out->writec(out, ' ');
        if (err) return err;

        err = out->write(out, self->name, self->name_size);
        if (err) return err;
    }

    /* elems */
    for (elem = self->elems; elem; elem = elem->next) {
        elem->out = self->out;
        elem->format = KND_FORMAT_GSP;
        err = elem->export(elem);
        if (err) {
            knd_log("-- export of \"%s\" elem failed: %d :(",
                    elem->attr->name, err);
            return err;
        }
    }

    if (self->type == KND_OBJ_ADDR) {
        /* rels */
        if (self->entry && self->entry->rels) {
            err = export_rels_GSP(self);                                          RET_ERR();
        }

        err = self->out->writec(self->out, '}');
        if (err) return err;

        self->frozen_size = self->out->buf_size - start_size;
    }

    return knd_OK;
}


/* export object */
static int 
kndObject_export(struct kndObject *self)
{
    int err;
    switch(self->format) {
    case KND_FORMAT_JSON:
        err = kndObject_export_JSON(self);
        if (err) return err;
        break;
        /*case KND_FORMAT_HTML:
        err = kndObject_export_HTML(self, is_concise);
        if (err) return err;
        break;*/
    case KND_FORMAT_GSP:
        err = kndObject_export_GSP(self);
        if (err) return err;
        break;
    default:
        break;
    }
    return knd_OK;
}

static gsl_err_t run_set_name(void *obj, const char *name, size_t name_size)
{
    struct kndObject *self = (struct kndObject*)obj;
    struct kndConcept *conc;
    struct kndObjEntry *entry;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    /* check name doublets */
    conc = self->conc;
    if (conc->dir && conc->dir->obj_name_idx) {
        entry = conc->dir->obj_name_idx->get(conc->dir->obj_name_idx,
                                             name, name_size);
        if (entry) {
            if (entry->obj && entry->obj->state->phase == KND_REMOVED) {
                knd_log("-- this obj has been removed lately: %.*s :(",
                    name_size, name);
                goto assign_name;
            }
            knd_log("-- obj name doublet found: %.*s:(",
                    name_size, name);
            return make_gsl_err(gsl_EXISTS);
        }
    }

 assign_name:
    self->name = name;
    self->name_size = name_size;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log("++ OBJ NAME: \"%.*s\"",
                self->name_size, self->name);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t find_obj_rel(void *obj, const char *name, size_t name_size)
{
    struct kndObject *self = obj;
    struct kndRelRef *relref = NULL;
    struct kndRel *rel;
    size_t name_hash = 0;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    for (size_t i = 0; i < name_size; i++)
        name_hash = (name_hash << 1) ^ name[i];

    if (DEBUG_OBJ_LEVEL_TMP)
        knd_log("++ find Rel Ref: \"%.*s\"", name_size, name);

    for (relref = self->entry->rels; relref; relref = relref->next) {
        rel = relref->rel;

        if (!memcmp(rel->name, name, name_size)) {
            knd_log("++ got REL: %.*s", rel->name_size, rel->name);
            self->curr_rel = relref;
            return make_gsl_err(gsl_OK);
        }
    }

    return make_gsl_err(gsl_NO_MATCH);
}

static gsl_err_t present_rels(void *data,
                              const char *val __attribute__((unused)),
                              size_t val_size __attribute__((unused)))
{
    struct kndObject *self = data;
    int err;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log(".. present obj rels..");

    self->expand_depth = 1;
    err = export_rel_dir_JSON(self);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t present_rel(void *data,
                             const char *val __attribute__((unused)),
                             size_t val_size __attribute__((unused)))
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndObject *self = data;
    struct kndRelRef *relref;
    struct kndRel *rel;
    struct glbOutput *out;
    int err;

    if (!self->curr_rel) {
        if (DEBUG_OBJ_LEVEL_1)
            knd_log(".. presenting all obj rels.. %p", self->out);

        self->expand_depth = 1;
        err = export_rel_dir_JSON(self);
        if (err) return make_gsl_err_external(err);
        return make_gsl_err(gsl_OK);
    }

    out = self->out;
    relref = self->curr_rel;
    rel = relref->rel;

    err = out->write(out, "{\"n\":\"", strlen("{\"n\":\""));
    if (err) return make_gsl_err_external(err);
    err = out->write(out, rel->name, rel->name_size);
    if (err) return make_gsl_err_external(err);
    err = out->write(out, "\"", 1);
    if (err) return make_gsl_err_external(err);

    err = out->write(out, ",\"num_instances\":", strlen(",\"num_instances\":"));
    if (err) return make_gsl_err_external(err);
    buf_size = snprintf(buf, KND_NAME_SIZE, "%zu", relref->num_insts);
    err = out->write(out, buf, buf_size);
    if (err) return make_gsl_err_external(err);

    err = out->write(out, ",\"instances\":", strlen(",\"instances\":"));
    if (err) return make_gsl_err_external(err);

    rel->out = self->out;
    err = rel->export_insts(rel, relref->insts);
    if (err) return make_gsl_err_external(err);

    err = out->write(out, "}", 1);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_obj_import(void *data,
                                    const char *val __attribute__((unused)),
                                    size_t val_size __attribute__((unused)))
{
    struct kndObject *self = data;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log(".. confirm obj import.. %p", self);

    return make_gsl_err(gsl_OK);
}

static int
kndObject_validate_attr(struct kndObject *self,
                        const char *name,
                        size_t name_size,
                        struct kndAttr **result,
                        struct kndElem **result_elem)
{
    struct kndConcept *conc;
    struct kndAttr *attr = NULL;
    struct kndElem *elem = NULL;
    int err, e;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log(".. \"%.*s\" to validate elem: \"%.*s\"",
                self->name_size, self->name, name_size, name);

    /* check existing elems */
    for (elem = self->elems; elem; elem = elem->next) {
        if (!memcmp(elem->attr->name, name, name_size)) {
            if (DEBUG_OBJ_LEVEL_2)
                knd_log("++ ELEM \"%.*s\" is already set!", name_size, name);
            *result_elem = elem;
            return knd_OK;
        }
    }
    
    conc = self->conc;
    err = conc->get_attr(conc, name, name_size, &attr);
    if (err) {
        knd_log("  -- \"%.*s\" attr is not approved :(", name_size, name);
        self->log->reset(self->log);
        e = self->log->write(self->log, name, name_size);
        if (e) return e;
        e = self->log->write(self->log, " attr not confirmed",
                               strlen(" attr not confirmed"));
        if (e) return e;
        return err;
    }

    if (DEBUG_OBJ_LEVEL_2) {
        const char *type_name = knd_attr_names[attr->type];
        knd_log("++ \"%.*s\" ELEM \"%s\" attr type: \"%s\"",
                name_size, name, attr->name, type_name);
    }

    *result = attr;
    return knd_OK;
}

static gsl_err_t parse_elem(void *data,
                            const char *name, size_t name_size,
                            const char *rec, size_t *total_size)
{
    struct kndObject *self = data;
    if (self->curr_obj) {
        self = self->curr_obj;
    }
    struct kndConcept *root_class;
    struct kndConcept *c;
    struct kndObject *obj;
    struct kndElem *elem = NULL;
    struct kndAttr *attr = NULL;
    struct kndRef *ref = NULL;
    struct kndNum *num = NULL;
    struct kndText *text = NULL;
    int err;

    if (DEBUG_OBJ_LEVEL_2) {
        knd_log("..  obj: id:%.*s name:%.*s to validate \"%.*s\" elem,   REC: \"%.*s\"",
                self->id_size, self->id,
                self->name_size, self->name,
               name_size, name, 32, rec);
    }

    err = kndObject_validate_attr(self, name, name_size, &attr, &elem);
    if (err) return make_gsl_err_external(err);


    if (elem) {
        switch (elem->attr->type) {
        case KND_ATTR_AGGR:
            err = elem->aggr->parse(elem->aggr, rec, total_size);
            if (err) return make_gsl_err_external(err);
            break;
        case KND_ATTR_NUM:
            knd_log(".. set numeric elem");
            num = elem->num;
            err = num->parse(num, rec, total_size);
            if (err) return make_gsl_err_external(err);
            break;
        default:
            break;
        }

        return make_gsl_err(gsl_OK);
    }        

    err = self->mempool->new_obj_elem(self->mempool, &elem);
    if (err) return make_gsl_err_external(err);
    elem->obj = self;
    elem->root = self->root ? self->root : self;
    elem->attr = attr;
    elem->out = self->out;
 
    if (DEBUG_OBJ_LEVEL_2)
        knd_log("   == basic elem type: %s",
                knd_attr_names[attr->type]);

    switch (attr->type) {
    case KND_ATTR_AGGR:
        err = self->mempool->new_obj(self->mempool, &obj);
        if (err) return make_gsl_err_external(err);
        err = self->mempool->new_state(self->mempool, &obj->state);
        if (err) return make_gsl_err_external(err);
        obj->state->phase = self->state->phase;

        obj->type = KND_OBJ_AGGR;
        if (!attr->conc) {
            if (self->state->phase == KND_FROZEN || self->state->phase == KND_SUBMITTED) {
                if (DEBUG_OBJ_LEVEL_2) {
                    knd_log(".. resolve attr \"%.*s\": \"%.*s\"..",
                            attr->name_size, attr->name,
                            attr->ref_classname_size, attr->ref_classname);
                }
                root_class = self->conc->root_class;
                err = root_class->get(root_class,
                                      attr->ref_classname, attr->ref_classname_size,
                                      &c);
                if (err) return make_gsl_err_external(err);
                attr->conc = c;
            }
            else {
                knd_log("-- couldn't resolve the %.*s attr :(",
                        attr->name_size, attr->name);
                return make_gsl_err(gsl_FAIL);
            }
        }

        obj->conc = attr->conc;
        obj->out = self->out;
        obj->log = self->log;
        obj->mempool = self->mempool;

        obj->root = self->root ? self->root : self;
        err = obj->parse(obj, rec, total_size);
        if (err) return make_gsl_err_external(err);

        elem->aggr = obj;
        obj->parent = elem;
        goto append_elem;
    case KND_ATTR_NUM:
        err = kndNum_new(&num);
        if (err) return make_gsl_err_external(err);
        num->elem = elem;
        num->out = self->out;
        err = num->parse(num, rec, total_size);
        if (err) goto final;

        elem->num = num;
        goto append_elem;
    case KND_ATTR_REF:
        err = kndRef_new(&ref);
        if (err) return make_gsl_err_external(err);
        ref->elem = elem;
        ref->out = self->out;
        err = ref->parse(ref, rec, total_size);
        if (err) goto final;

        elem->ref = ref;
        goto append_elem;
    case KND_ATTR_TEXT:
        err = kndText_new(&text);
        if (err) return make_gsl_err_external(err);

        text->elem = elem;
        text->out = self->out;

        err = text->parse(text, rec, total_size);
        if (err) return make_gsl_err_external(err);
        
        elem->text = text;
        goto append_elem;
    default:
        break;
    }

    err = elem->parse(elem, rec, total_size);
    if (err) goto final;

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

    if (DEBUG_OBJ_LEVEL_3)
        knd_log("++ elem %.*s parsing OK!",
                elem->attr->name_size, elem->attr->name);

    return make_gsl_err(gsl_OK);

 final:

    knd_log("-- validation of \"%s\" elem failed :(", name);

    elem->del(elem);
    return make_gsl_err_external(err);
}

static gsl_err_t resolve_relref(void *obj, const char *name, size_t name_size)
{
    struct kndRelRef *self = obj;
    struct kndRel *root_rel;
    struct kndRel *rel;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    root_rel = self->rel;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log(".. resolving Rel Ref: \"%.*s\"   root rel: %p",
                name_size, name, root_rel);

    err = root_rel->get_rel(root_rel, name, name_size, &rel);
    if (err) return make_gsl_err_external(err);
    self->rel = rel;

    return make_gsl_err(gsl_OK);
}



static gsl_err_t rel_inst_append(void *accu,
                                 void *item)
{
    struct kndRelRef *self = accu;
    struct kndRelInstance *inst = item;
    struct kndRelArgInstance *rel_arg_inst = NULL;
    struct kndRelArgInstRef *rel_arg_inst_ref = NULL;
    int err;
    err = self->rel->mempool->new_rel_arg_inst_ref(self->rel->mempool,
                                                   &rel_arg_inst_ref);
    if (err) return make_gsl_err_external(err);

    rel_arg_inst = inst->args;

    rel_arg_inst_ref->inst = rel_arg_inst;
    rel_arg_inst_ref->next = self->insts;
    self->insts = rel_arg_inst_ref;
    self->num_insts++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t rel_inst_alloc(void *obj,
                                const char *name,
                                size_t name_size,
                                size_t count,
                                void **item)
{
    struct kndRelRef *self = obj;
    struct kndRelInstance *rel_inst = NULL;
    int err;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log(".. %.*s Rel Ref to alloc rel inst \"%.*s\" count:%zu..",
                self->rel->name_size, self->rel->name,
                name_size, name, count);

    err = self->rel->mempool->new_rel_inst(self->rel->mempool, &rel_inst);
    if (err) return make_gsl_err_external(err);

    rel_inst->rel = self->rel;

    *item = (void*)rel_inst;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t rel_inst_read(void *obj,
                               const char *rec,
                               size_t *total_size)
{
    struct kndRelInstance *inst = obj;
    struct kndRel *rel;
    int err;
    rel = inst->rel;
    err = rel->read_inst(rel, inst, rec, total_size);
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
        .alloc = rel_inst_alloc,
        .append = rel_inst_append,
        .parse = rel_inst_read
    };

    if (DEBUG_OBJ_LEVEL_TMP)
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
        { .is_implied = true,
          .run = resolve_relref,
          .obj = relref
        },
        { .is_list = true,
          .name = "inst",
          .name_size = strlen("inst"),
          .parse = read_rel_insts,
          .obj = relref
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t rel_append(void *accu,
                            void *item)
{
    struct kndObject *self = accu;
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
    struct kndObject *self = obj;
    struct kndRelRef *relref = NULL;
    struct kndRel *root_rel;
    int err;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log(".. %.*s OBJ to alloc rel ref %.*s count:%zu..",
                self->name_size, self->name,
                name_size, name, count);

    err = self->mempool->new_rel_ref(self->mempool, &relref);
    if (err) return make_gsl_err_external(err);

    root_rel = self->conc->root_class->rel;
    relref->rel = root_rel;
    *item = (void*)relref;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t select_rel(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndObject *self = obj;

    if (!self->curr_obj) {
        knd_log("-- no obj selected :(");
        return make_gsl_err(gsl_FAIL);
    }

    /* reset rel selection */
    self->curr_obj->curr_rel = NULL;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = find_obj_rel,
          .obj = self->curr_obj
        },
        { .is_default = true,
          .run = present_rel,
          .obj = self->curr_obj
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t remove_obj(void *data, const char *name __attribute__((unused)), size_t name_size __attribute__((unused)))
{
    struct kndObject *self = data;
    struct kndConcept *conc = self->conc;
    struct kndConcept *root_class = conc->root_class;
    struct kndObject *obj;
    struct kndState *state;
    int err;

    if (!self->curr_obj) {
        knd_log("-- remove operation: no obj selected :(");
        return make_gsl_err(gsl_FAIL);
    }

    obj = self->curr_obj;

    if (DEBUG_OBJ_LEVEL_TMP)
        knd_log("== obj to remove: \"%.*s\"", obj->name_size, obj->name);

    err = self->mempool->new_state(self->mempool, &state);
    if (err) return make_gsl_err_external(err);

    state->phase = KND_REMOVED;
    state->next = obj->state;
    obj->state = state;

    self->log->reset(self->log);
    err = self->log->write(self->log, self->name, self->name_size);
    if (err) return make_gsl_err_external(err);
    err = self->log->write(self->log, " obj removed", strlen(" obj removed"));
    if (err) return make_gsl_err_external(err);
    self->task->type = KND_UPDATE_STATE;
    obj->next = conc->obj_inbox;
    conc->obj_inbox = obj;
    conc->obj_inbox_size++;

    conc->next = root_class->inbox;
    root_class->inbox = conc;
    root_class->inbox_size++;
    return make_gsl_err(gsl_OK);
}


 
static gsl_err_t parse_rels(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndObject *self = obj;
    struct gslTaskSpec rel_spec = {
        .is_list_item = true,
        .accu = self,
        .alloc = rel_alloc,
        .append = rel_append,
        .parse = rel_read
    };

    
    if (DEBUG_OBJ_LEVEL_TMP)
        knd_log(".. reading rels of obj %.*s..",
                self->name_size, self->name);

    return gsl_parse_array(&rel_spec, rec, total_size);
}

 
/* parse object */
static int parse_GSL(struct kndObject *self,
                     const char *rec,
                     size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = self
        },
        { .type = GSL_CHANGE_STATE,
          .is_validator = true,
          .validate = parse_elem,
          .obj = self
        },
        { .is_validator = true,
          .validate = parse_elem,
          .obj = self
        },
        { .is_list = true,
          .name = "_rel",
          .name_size = strlen("_rel"),
          .parse = parse_rels,
          .obj = self
        },
        { .is_default = true,
          .run = confirm_obj_import,
          .obj = self
        }
    };
    gsl_err_t parser_err;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log(".. parsing obj REC: %.*s", 128, rec);
 
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}

static int select_rels(struct kndObject *self,
                       const char *rec,
                       size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        { .name = "rel",
          .name_size = strlen("rel"),
          .parse = select_rel,
          .obj = self
        },
        { .is_default = true,
          .run = present_rels,
          .obj = self
        }
    };
    gsl_err_t parser_err;
 
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}

static gsl_err_t run_present_obj(void *data, const char *val __attribute__((unused)),
                                 size_t val_size __attribute__((unused)))
{
    struct kndObject *self = data;
    struct kndConcept *conc = self->conc;
    struct kndObject *obj;
    int err;

    if (!self->curr_obj) {
        knd_log("-- no obj selected :(");
        return make_gsl_err(gsl_FAIL);
    }

    obj = self->curr_obj;
    obj->out = conc->out;
    obj->out->reset(obj->out);

    obj->log = conc->log;
    obj->task = conc->task;

    obj->format = KND_FORMAT_JSON;
    err = obj->export(obj);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_OBJ_LEVEL_TMP)
        knd_log("++ obj export: \"%.*s\"",
                obj->out->buf_size, obj->out->buf);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_get_obj(void *obj, const char *name, size_t name_size)
{
    struct kndObject *self = obj;
    struct kndConcept *conc = self->conc;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    self->curr_obj = NULL;
    err = conc->get_obj(conc, name, name_size, &self->curr_obj);
    if (err) {
        knd_log("-- failed to get obj: %.*s :(", name_size, name);
        return make_gsl_err_external(err);
    }

    self->curr_obj->mempool = self->mempool;
    self->curr_obj->out = self->out;

    if (DEBUG_OBJ_LEVEL_2) {
        knd_log("++ got obj: \"%.*s\" %p!", name_size, name, self->curr_obj);
        self->curr_obj->str(self->curr_obj);
    }
    
    return make_gsl_err(gsl_OK);
}

static int parse_select_obj(struct kndObject *self,
                            const char *rec,
                            size_t *total_size)
{
    int err;
    gsl_err_t parser_err;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log(".. parsing OBJ select rec: \"%.*s\"", 32, rec);
    self->curr_obj = NULL;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_obj,
          .obj = self
        },
        { .type = GSL_CHANGE_STATE,
          .is_validator = true,
          .validate = parse_elem,
          .obj = self
        },
        { .name = "_rel",
          .name_size = strlen("_rel"),
          .parse = select_rel,
          .obj = self
        },
        { .type = GSL_CHANGE_STATE,
          .name = "_rm",
          .name_size = strlen("_rm"),
          .run = remove_obj,
          .obj = self
        },
        { .is_default = true,
          .run = run_present_obj,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- obj select parse error: \"%.*s\"",
                self->log->buf_size, self->log->buf);
        if (!self->log->buf_size) {
            err = self->log->write(self->log, "obj select parse failure",
                                 strlen("obj select parse failure"));
            if (err) return err;
        }
        return gsl_err_to_knd_err_codes(parser_err);
    }

    return knd_OK;
}


static int 
kndObject_resolve(struct kndObject *self)
{
    struct kndElem *elem;
    int err;

    if (DEBUG_OBJ_LEVEL_2) {
        if (self->type == KND_OBJ_ADDR) {
            knd_log(".. resolve OBJ %.*s::%.*s [%.*s]",
                    self->conc->name_size, self->conc->name,
                    self->name_size, self->name,
                    KND_ID_SIZE, self->id);
        } else {
            knd_log(".. resolve aggr elem \"%.*s\" %.*s..",
                    self->parent->attr->name_size, self->parent->attr->name,
                    self->conc->name_size, self->conc->name);
        }
    }

    for (elem = self->elems; elem; elem = elem->next) {
        elem->log = self->log;
        err = elem->resolve(elem);                                                RET_ERR();
    }
    
    return knd_OK;
}

extern void kndObjEntry_init(struct kndObjEntry *self)
{
    memset(self, 0, sizeof(struct kndObjEntry));
}

extern void
kndObject_init(struct kndObject *self)
{
    self->name_size = 0;
    self->del = del;
    self->str = str;
    self->parse = parse_GSL;
    self->read = parse_GSL;
    self->resolve = kndObject_resolve;
    self->export = kndObject_export;
    self->select = parse_select_obj;
    self->select_rels = select_rels;
}

extern int
kndObject_new(struct kndObject **obj)
{
    struct kndObject *self;

    self = malloc(sizeof(struct kndObject));
    if (!self) return knd_NOMEM;
    memset(self, 0, sizeof(struct kndObject));
    kndObject_init(self);
    *obj = self;

    return knd_OK;
}
