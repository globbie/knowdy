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

static int export_inst_rels_JSON(struct kndObject *self);
static int export_inst_rel_JSON(struct kndObject *self,
                                struct kndRelRef *relref);
static int export_rel_inst_JSON(void *obj,
                                const char *elem_id, size_t elem_id_size,
                                size_t count,
                                void *elem __attribute__((unused)));

static void del(struct kndObject *self)
{
    self->states->phase = KND_FREED;
}

static void str(struct kndObject *self)
{
    struct kndElem *elem;

    if (self->type == KND_OBJ_ADDR) {
        knd_log("\n%*sOBJ %.*s::%.*s  numid:%zu",
                self->depth * KND_OFFSET_SIZE, "",
                self->base->name_size, self->base->name,
                self->name_size, self->name,
                self->entry->numid);
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
    struct glbOutput *out = self->base->entry->repo->out;
    int err;

    /* anonymous obj */
    err = out->write(out, "{", 1);
    if (err) return err;

    elem = self->elems;
    while (elem) {
        elem->out = out;
        elem->format = KND_FORMAT_JSON;
        err = elem->export(elem);
        if (err) return err;
        
        if (elem->next) {
            err = out->write(out, ",", 1);
            if (err) return err;
        }
        elem = elem->next;
    }

    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}

static int export_inst_relref_JSON(struct kndObject *self,
                                   struct kndRelRef *relref)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct glbOutput *out = self->base->entry->repo->out;
    struct kndRel *rel = relref->rel;
    struct kndUpdate *update;
    struct kndSet *set;
    struct tm tm_info;
    int err;

    err = out->write(out, "{\"_name\":\"", strlen("{\"_name\":\""));              RET_ERR();
    err = out->write(out, rel->entry->name, rel->entry->name_size);               RET_ERR();
    err = out->write(out, "\"", 1);                                               RET_ERR();

    /* show the latest state */
    if (relref->num_states) {
        err = out->write(out, ",\"_state\":", strlen(",\"_state\":"));            RET_ERR();
        err = out->writef(out, "%zu", relref->num_states);                        RET_ERR();

        update = relref->states->update;
        time(&update->timestamp);
        localtime_r(&update->timestamp, &tm_info);
        buf_size = strftime(buf, KND_NAME_SIZE,
                            ",\"_timestamp\":\"%Y-%m-%d %H:%M:%S\"", &tm_info);
        err = out->write(out, buf, buf_size);                                     RET_ERR();
    }

    set = relref->idx;
    err = out->write(out, ",\"num_instances\":",
                     strlen(",\"num_instances\":"));                          RET_ERR();
    buf_size = snprintf(buf, KND_NAME_SIZE, "%zu", set->num_elems);
    err = out->write(out, buf, buf_size);                                     RET_ERR();

    if (self->max_depth) {
        err = out->write(out, ",\"instances\":",
                         strlen(",\"instances\":"));                          RET_ERR();
        err = out->writec(out, '[');                                          RET_ERR();
        err = set->map(set, export_rel_inst_JSON, (void*)out);
        if (err) return err;
        err = out->writec(out, ']');                                          RET_ERR();
    }
    err = out->writec(out, '}');                                              RET_ERR();
    return knd_OK;
}

static int export_inst_relrefs_JSON(struct kndObject *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct kndRelRef *relref;
    struct glbOutput *out = self->base->entry->repo->out;
    int err;

    err = out->writec(out, '[');                                                  RET_ERR();

    for (relref = self->entry->rels; relref; relref = relref->next) {
        if (!relref->idx) continue;

        err = export_inst_relref_JSON(self, relref);                              RET_ERR();
    }
    err = out->writec(out, ']');                                                  RET_ERR();

    return knd_OK;
}

static int kndObject_export_JSON(struct kndObject *self)
{
    struct kndElem *elem;
    struct kndObject *obj;
    struct glbOutput *out = self->base->entry->repo->out;
    struct kndTask *task = self->base->entry->repo->task;
    struct kndState *state = self->states;
    bool is_concise = true;
    bool need_separ;
    int err;

    if (DEBUG_INST_LEVEL_TMP)
        knd_log("   .. export OBJ \"%.*s\"  (class: %.*s)  entry:%p",
                self->name_size, self->name,
                self->base->name_size, self->base->name, self->entry);

    if (self->type == KND_OBJ_AGGR) {
        err = kndObject_export_aggr_JSON(self);
        return err;
    }

    err = out->write(out, "{\"_name\":\"", strlen("{\"_name\":\""));              RET_ERR();
    err = out->write(out, self->name, self->name_size);
    if (err) return err;
    err = out->write(out, "\"", 1);
    if (err) return err;

    err = out->write(out, ",\"_id\":", strlen(",\"_id\":"));                      RET_ERR();
    err = out->writef(out, "%zu", self->entry->numid);                            RET_ERR();

    if (state) {
        err = out->write(out, ",\"_state\":", strlen(",\"_state\":"));            RET_ERR();
        err = out->writef(out, "%zu", state->update->numid);                      RET_ERR();

        switch(state->phase) {
        case KND_REMOVED:
            err = out->write(out,   ",\"_phase\":\"del\"",
                             strlen(",\"_phase\":\"del\""));                      RET_ERR();
            // NB: no more details
            err = out->write(out, "}", 1);
            if (err) return err;
            return knd_OK;
            
        case KND_UPDATED:
            err = out->write(out,   ",\"_phase\":\"upd\"",
                             strlen(",\"_phase\":\"upd\""));                      RET_ERR();
            break;
        case KND_CREATED:
            err = out->write(out,   ",\"_phase\":\"new\"",
                             strlen(",\"_phase\":\"new\""));                      RET_ERR();
            break;
        default:
            break;
        }
        
    }

    err = out->write(out, ",\"_class\":\"", strlen(",\"_class\":\""));
    if (err) return err;

    err = out->write(out, self->base->name, self->base->name_size);
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

                /*if (need_separ) {*/
                err = out->write(out, ",", 1);
                if (err) return err;

                err = out->write(out, "\"", 1);
                if (err) return err;
                err = out->write(out,
                                 elem->attr->name,
                                 elem->attr->name_size);
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

            if (DEBUG_INST_LEVEL_2)
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
        err = out->write(out, ",\"_rel\":", strlen(",\"_rel\":"));                RET_ERR();
        err = export_inst_relrefs_JSON(self);                                     RET_ERR();
    }

    err = out->write(out, "}", 1);
    if (err) return err;
    
    return err;
}

static int export_rel_inst_id(void *obj,
                              const char *elem_id __attribute__((unused)),
                              size_t elem_id_size __attribute__((unused)),
                              size_t count __attribute__((unused)),
                              void *elem)
{
    struct glbOutput *out = obj;
    struct kndRelInstEntry *entry = elem;
    int err;

    err = out->writec(out, ' ');                                                  RET_ERR();
    err = out->write(out, entry->id, entry->id_size);                             RET_ERR();
    if (err) return err;

    return knd_OK;
}

static int export_rel_inst_JSON(void *obj,
                                const char *elem_id __attribute__((unused)),
                                size_t elem_id_size __attribute__((unused)),
                                size_t count __attribute__((unused)),
                                void *elem)
{
    struct glbOutput *out = obj;
    struct kndRelInstance *inst = elem;
    struct kndRel *rel = inst->rel;
    int err;

    /* separator */
    if (count) {
        err = out->writec(out, ',');                                          RET_ERR();
    }
    rel->out = out;
    rel->format = KND_FORMAT_JSON;
    err = rel->export_inst(rel, inst);
    if (err) return err;

    return knd_OK;
}

static int
export_rels_GSP(struct kndObject *self)
{
    struct kndRel *rel;
    struct kndRelRef *relref;
    struct glbOutput *out = self->base->entry->repo->out;
    int err;

    /* sort refs by class */
    if (DEBUG_INST_LEVEL_2)
        knd_log(".. GSP export rels of %.*s..",
                self->name_size, self->name);

    err = out->write(out, "[_rel", strlen("[_rel"));                              RET_ERR();
    for (relref = self->entry->rels; relref; relref = relref->next) {
        rel = relref->rel;
        err = out->writec(out, '{');                                              RET_ERR();
        err = out->write(out, rel->id, rel->id_size);                             RET_ERR();

        err = out->write(out, "[i", strlen("[i"));                                RET_ERR();

        err = relref->idx->map(relref->idx, export_rel_inst_id, (void*)out);
        if (err) return err;

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
    struct glbOutput *out = self->base->entry->repo->out;
    size_t start_size = 0;
    int err;

    if (self->type == KND_OBJ_ADDR) {
        start_size = out->buf_size;
        if (DEBUG_INST_LEVEL_2)
            knd_log("%*s.. export GSP obj \"%.*s\" [id: %.*s]..",
                    self->depth *  KND_OFFSET_SIZE, "",
                    self->name_size, self->name,
                    self->entry->id_size, self->entry->id);

        err = out->writec(out, '{');
        if (err) return err;
        err = out->write(out, self->entry->id, self->entry->id_size);
        if (err) return err;

        err = out->writec(out, ' ');
        if (err) return err;

        err = out->write(out, self->name, self->name_size);
        if (err) return err;
    }

    /* elems */
    for (elem = self->elems; elem; elem = elem->next) {
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

        err = out->writec(out, '}');
        if (err) return err;

        self->frozen_size = out->buf_size - start_size;
    }

    return knd_OK;
}


/* export object */
static int kndObject_export(struct kndObject *self)
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
    struct kndObject *self = obj;
    struct kndClass *c = self->base;
    struct kndObjEntry *entry;
    struct ooDict *name_idx = c->entry->obj_name_idx;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    /* check name doublets */
    if (name_idx) {
        entry = name_idx->get(name_idx,
                              name, name_size);
        if (entry) {
            if (entry->obj && entry->obj->states->phase == KND_REMOVED) {
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

    if (DEBUG_INST_LEVEL_2)
        knd_log("++ class inst name: \"%.*s\"",
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

    if (DEBUG_INST_LEVEL_TMP)
        knd_log("++ find Rel Ref: \"%.*s\"", name_size, name);

    for (relref = self->entry->rels; relref; relref = relref->next) {
        rel = relref->rel;

        if (DEBUG_INST_LEVEL_TMP)
            knd_log("== REL: %.*s",
                    rel->entry->name_size, rel->entry->name);

        if (rel->entry->name_size != name_size) continue;

        if (!memcmp(rel->entry->name, name, name_size)) {
            if (DEBUG_INST_LEVEL_TMP)
                knd_log("++ got REL: %.*s",
                        rel->entry->name_size, rel->entry->name);

            // TODO
            self->max_depth = 1;
            self->curr_rel = relref;
            return make_gsl_err(gsl_OK);
        }
    }

    return make_gsl_err(gsl_NO_MATCH);
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
    struct glbOutput *out = self->base->entry->repo->out;
    int err;

    if (!self->curr_rel) {
        self->max_depth = 1;
        err = export_inst_relrefs_JSON(self);
        if (err) return make_gsl_err_external(err);
        return make_gsl_err(gsl_OK);
    }

    relref = self->curr_rel;
    rel = relref->rel;

    err = export_inst_relref_JSON(self, relref);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_obj_import(void *data,
                                    const char *val __attribute__((unused)),
                                    size_t val_size __attribute__((unused)))
{
    struct kndObject *self = data;

    if (DEBUG_INST_LEVEL_2)
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
    struct kndClass *conc;
    struct kndAttr *attr = NULL;
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
    err = conc->get_attr(conc, name, name_size, &attr);
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

    if (DEBUG_INST_LEVEL_2) {
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
    struct kndClass *root_class;
    struct kndMemPool *mempool = self->base->entry->repo->mempool;
    struct kndClass *c;
    struct kndObject *obj;
    struct kndElem *elem = NULL;
    struct kndAttr *attr = NULL;
    struct kndRef *ref = NULL;
    struct kndNum *num = NULL;
    struct kndText *text = NULL;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_INST_LEVEL_2) {
        knd_log("..  obj: id:%.*s name:%.*s "
                " to validate \"%.*s\" elem,"
                " REC: \"%.*s\"",
                self->entry->id_size, self->entry->id,
                self->name_size, self->name,
                name_size, name, 32, rec);
    }

    err = kndObject_validate_attr(self, name, name_size, &attr, &elem);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    if (elem) {
        switch (elem->attr->type) {
        case KND_ATTR_AGGR:
            parser_err = elem->aggr->parse(elem->aggr, rec, total_size);
            if (parser_err.code) return parser_err;
            break;
        case KND_ATTR_NUM:
            num = elem->num;
            parser_err = num->parse(num, rec, total_size);
            if (parser_err.code) return parser_err;
            break;
        default:
            break;
        }
        return *total_size = 0, make_gsl_err(gsl_OK);
    }        

    err = mempool->new_obj_elem(mempool, &elem);
    if (err) {
        knd_log("-- elem alloc failed :(");
        return *total_size = 0, make_gsl_err_external(err);
    }
    elem->obj = self;
    elem->root = self->root ? self->root : self;
    elem->attr = attr;
 
    if (DEBUG_INST_LEVEL_2)
        knd_log("   == basic elem type: %s",
                knd_attr_names[attr->type]);

    switch (attr->type) {
    case KND_ATTR_AGGR:
        err = mempool->new_obj(mempool, &obj);
        if (err) {
            knd_log("-- aggr class inst alloc failed :(");
            return *total_size = 0, make_gsl_err_external(err);
        }
        err = mempool->new_state(mempool, &obj->states);
        if (err) {
            knd_log("-- state alloc failed :(");
            return *total_size = 0, make_gsl_err_external(err);
        }
        obj->states->phase = self->states->phase;

        obj->type = KND_OBJ_AGGR;
        if (!attr->conc) {
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
                attr->conc = c;
            }
            else {
                knd_log("-- couldn't resolve the %.*s attr :(",
                        attr->name_size, attr->name);
                return *total_size = 0, make_gsl_err(gsl_FAIL);
            }
        }

        obj->base = attr->conc;
        obj->root = self->root ? self->root : self;

        parser_err = obj->parse(obj, rec, total_size);
        if (parser_err.code) return parser_err;

        elem->aggr = obj;
        obj->parent = elem;
        goto append_elem;
    case KND_ATTR_NUM:
        err = kndNum_new(&num);
        if (err) return *total_size = 0, make_gsl_err_external(err);
        num->elem = elem;
        parser_err = num->parse(num, rec, total_size);
        if (parser_err.code) goto final;

        elem->num = num;
        goto append_elem;
    case KND_ATTR_REF:
        err = kndRef_new(&ref);
        if (err) return *total_size = 0, make_gsl_err_external(err);
        ref->elem = elem;
        parser_err = ref->parse(ref, rec, total_size);
        if (parser_err.code) goto final;

        elem->ref = ref;
        goto append_elem;
    case KND_ATTR_TEXT:
        err = kndText_new(&text);
        if (err) return *total_size = 0, make_gsl_err_external(err);

        text->elem = elem;
        parser_err = text->parse(text, rec, total_size);
        if (parser_err.code) goto final;
        
        elem->text = text;
        goto append_elem;
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

    if (DEBUG_INST_LEVEL_3)
        knd_log("++ elem %.*s parsing OK!",
                elem->attr->name_size, elem->attr->name);

    return make_gsl_err(gsl_OK);

 final:

    knd_log("-- validation of \"%.*s\" elem failed :(", name_size, name);

    elem->del(elem);
    return parser_err;
}

static gsl_err_t resolve_relref(void *obj, const char *name, size_t name_size)
{
    struct kndRelRef *self = obj;
    struct kndRel *root_rel;
    struct kndRelEntry *entry;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    root_rel = self->rel;

    if (DEBUG_INST_LEVEL_1)
        knd_log(".. resolving Rel Ref by id \"%.*s\"",
                name_size, name);

    entry = root_rel->rel_idx->get(root_rel->rel_idx, name, name_size);
    if (!entry) {
        knd_log("-- no such Rel: \"%.*s\"",
                name_size, name);
        return make_gsl_err(gsl_NO_MATCH);
    }

    if (DEBUG_INST_LEVEL_2)
        knd_log("== Rel name: \"%.*s\" id:%.*s rel:%p",
                entry->name_size, entry->name,
                entry->id_size, entry->id, entry->rel);

    if (!entry->rel) {
        err = root_rel->get_rel(root_rel, entry->name, entry->name_size, &entry->rel);
        if (err) {
            knd_log("-- no such Rel..");
            return make_gsl_err(gsl_NO_MATCH);
        }
    }

    self->rel = entry->rel;

    return make_gsl_err(gsl_OK);
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

    if (DEBUG_INST_LEVEL_TMP)
        knd_log("== Rel Instance entry:\"%.*s\"",
                entry->id_size, entry->id);

    set = self->idx;
    if (!set) {
        err = mempool->new_set(mempool, &set);
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
                                 size_t count __attribute__((unused)),
                                 void **item)
{
    struct kndRelRef *self = obj;
    struct kndSet *set;
    void *elem;
    int err;

    if (DEBUG_INST_LEVEL_TMP)
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
        { .is_implied = true,
          .run = resolve_relref,
          .obj = relref
        },
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
    struct kndMemPool *mempool = self->base->entry->repo->mempool;
    int err;

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. %.*s OBJ to alloc rel ref %.*s count:%zu..",
                self->name_size, self->name,
                name_size, name, count);

    err = mempool->new_rel_ref(mempool, &relref);
    if (err) return make_gsl_err_external(err);

    root_rel = self->base->root_class->rel;
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
        return *total_size = 0, make_gsl_err(gsl_FAIL);
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

static gsl_err_t remove_obj(void *data,
                            const char *name __attribute__((unused)),
                            size_t name_size __attribute__((unused)))
{
    struct kndObject *self      = data;
    struct kndClass *conc       = self->base;
    struct kndClass *root_class = conc->root_class;
    struct kndObject *obj;
    struct kndState *state;
    struct glbOutput *log      = self->base->entry->repo->log;
    struct kndMemPool *mempool = self->base->entry->repo->mempool;
    int err;

    if (!self->curr_obj) {
        knd_log("-- remove operation: no obj selected :(");
        return make_gsl_err(gsl_FAIL);
    }

    obj = self->curr_obj;

    if (DEBUG_INST_LEVEL_TMP)
        knd_log("== obj to remove: \"%.*s\"", obj->name_size, obj->name);

    err = mempool->new_state(mempool, &state);
    if (err) return make_gsl_err_external(err);

    state->phase = KND_REMOVED;
    state->next = obj->states;
    obj->states = state;

    log->reset(log);
    err = log->write(log, self->name, self->name_size);
    if (err) return make_gsl_err_external(err);
    err = log->write(log, " obj removed", strlen(" obj removed"));
    if (err) return make_gsl_err_external(err);

    self->base->entry->repo->task->type = KND_UPDATE_STATE;
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
    
    if (DEBUG_INST_LEVEL_2)
        knd_log(".. reading rels of obj %.*s..",
                self->name_size, self->name);

    return gsl_parse_array(&rel_spec, rec, total_size);
}

 
/* parse object */
static gsl_err_t parse_import_inst(struct kndObject *self,
                                   const char *rec,
                                   size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = parse_elem,
          .obj = self
        },
        { .is_validator = true,
          .validate = parse_elem,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_rel",
          .name_size = strlen("_rel"),
          .parse = parse_rels,
          .obj = self
        }/*,
        { .name = "_rel",
          .name_size = strlen("_rel"),
          .parse = select_rel,
          .obj = self
          }*/,
        { .is_default = true,
          .run = confirm_obj_import,
          .obj = self
        }
    };

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. parsing obj REC: %.*s", 128, rec);
 
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t select_rels(struct kndObject *self,
                             const char *rec,
                             size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = find_obj_rel,
          .obj = self
        },
        { .is_default = true,
          .run = present_rel,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int export_inst_JSON(void *obj,
                            const char *elem_id,
                            size_t elem_id_size,
                            size_t count,
                            void *elem)
{
    struct kndObject *self = obj;
    struct kndClass *base = self->base;
    struct kndTask *task = base->entry->repo->task;
    if (count < task->start_from) return knd_OK;
    if (task->batch_size >= task->batch_max) return knd_RANGE;
    struct glbOutput *out = base->entry->repo->out;
    struct kndObject *inst = elem;
    int err;

    if (DEBUG_INST_LEVEL_2)
        knd_log("..export inst elem: %.*s", elem_id_size, elem_id);

    // TODO unfreeze

    /* separator */
    if (task->batch_size) {
        err = out->writec(out, ',');                                              RET_ERR();
    }

    inst->depth = 0;
    inst->max_depth = 0;
    if (self->max_depth) {
        inst->max_depth = self->max_depth;
    }

    err = kndObject_export(inst);                                                 RET_ERR();

    task->batch_size++;

    return knd_OK;
}

static int export_inst_set_JSON(struct kndObject *self,
                                struct kndSet *set)
{
    struct glbOutput *out = self->base->entry->repo->out;
    struct kndTask *task = self->base->entry->repo->task;
    int err;

    err = out->write(out, "{\"_set\":{",
                     strlen("{\"_set\":{"));                                      RET_ERR();

    err = out->writef(out, "\"total\":%lu",
                      (unsigned long)set->num_elems);                             RET_ERR();

    err = out->write(out, ",\"batch\":[",
                     strlen(",\"batch\":["));                                     RET_ERR();

    err = set->map(set, export_inst_JSON, (void*)self);
    if (err && err != knd_RANGE) return err;

    err = out->writec(out, ']');                                                  RET_ERR();

    err = out->writef(out, ",\"batch_max\":%lu",
                      (unsigned long)task->batch_max);                            RET_ERR();

    err = out->writef(out, ",\"batch_size\":%lu",
                      (unsigned long)task->batch_size);                           RET_ERR();

    err = out->writef(out, ",\"batch_from\":%lu",
                      (unsigned long)task->batch_from);                           RET_ERR();

    err = out->writec(out, '}');                                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

static gsl_err_t present_inst_selection(void *data, const char *val __attribute__((unused)),
                                        size_t val_size __attribute__((unused)))
{
    struct kndObject *self = data;
    struct kndObject *obj;
    struct kndClass *base = self->base;
    struct kndTask *task = base->entry->repo->task;
    struct glbOutput *out = base->entry->repo->out;
    struct kndMemPool *mempool = base->entry->repo->mempool;
    struct kndSet *set;
    int err;

    if (task->type == KND_SELECT_STATE) {
        if (DEBUG_INST_LEVEL_TMP)
            knd_log(".. inst selection..");
        /* no sets found? */
        if (!task->num_sets) {
            /*if (self->curr_baseclass && self->curr_baseclass->entry->descendants) {
                set = self->curr_baseclass->entry->descendants;

                err = export_set_JSON(self, set);
                if (err) return make_gsl_err_external(err);

                return make_gsl_err(gsl_OK);
                }*/
            err = out->write(out, "{}", strlen("{}"));
            if (err) return make_gsl_err_external(err);
            return make_gsl_err(gsl_OK);
        }

        /* TODO: intersection cache lookup  */
        set = task->sets[0];

        /* intersection required */
        /*if (task->num_sets > 1) {
            err = mempool->new_set(mempool, &set);
            if (err) return make_gsl_err_external(err);

            set->type = KND_SET_CLASS;
            set->mempool = mempool;
            set->base = task->sets[0]->base;

            err = set->intersect(set, task->sets, task->num_sets);
            if (err) return make_gsl_err_external(err);
            }*/

        if (!set->num_elems) {
            err = out->write(out, "{}", strlen("{}"));
            if (err) return make_gsl_err_external(err);
            return make_gsl_err(gsl_OK);
        }

        /* final presentation in JSON 
           TODO: choose output format */
        err = export_inst_set_JSON(self, set);
        if (err) return make_gsl_err_external(err);

        return make_gsl_err(gsl_OK);
    }

    
    if (!self->curr_obj) {
        knd_log("-- no obj selected :(");
        return make_gsl_err(gsl_FAIL);
    }

    obj = self->curr_obj;
    obj->max_depth = self->max_depth;

    err = obj->export(obj);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_get_obj(void *obj, const char *name, size_t name_size)
{
    struct kndObject *self = obj;
    struct kndClass *conc = self->base;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    self->curr_obj = NULL;
    err = conc->get_obj(conc, name, name_size, &self->curr_obj);
    if (err) {
        knd_log("-- failed to get obj: %.*s :(", name_size, name);
        return make_gsl_err_external(err);
    }

    self->curr_obj->max_depth = 0;
    if (DEBUG_INST_LEVEL_2) {
        knd_log("++ got obj: \"%.*s\" %p!", name_size, name, self->curr_obj);
        self->curr_obj->str(self->curr_obj);
    }
    
    return make_gsl_err(gsl_OK);
}

static int select_delta(struct kndObject *self,
                        const char *rec,
                        size_t *total_size)
{
    struct kndClass *base = self->base;
    struct kndTask *task = base->entry->repo->task;
    struct kndStateControl *state_ctrl = base->entry->repo->state_ctrl;
    struct kndMemPool *mempool = base->entry->repo->mempool;
    struct kndUpdate *update;
    struct kndState *state;
    struct kndSet *set;
    struct kndClassUpdate *class_update;
    struct kndObject *inst;
    size_t gt = 0;
    size_t lt = 0;
    size_t eq = 0;
    int err;
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .name = "eq",
          .name_size = strlen("eq"),
          .parse = gsl_parse_size_t,
          .obj = &eq
        },
        { .name = "gt",
          .name_size = strlen("gt"),
          .parse = gsl_parse_size_t,
          .obj = &gt
        },
        { .name = "lt",
          .name_size = strlen("lt"),
          .parse = gsl_parse_size_t,
          .obj = &lt
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. select delta:  gt %zu  lt %zu ..", gt, lt);

    task->type = KND_SELECT_STATE;

    // TODO error logs
    if (!base->num_inst_states) return knd_FAIL;
    if (gt >= base->num_inst_states) return knd_FAIL;
    if (lt && lt < gt) return knd_FAIL;

    err = mempool->new_set(mempool, &set);  MEMPOOL_ERR(kndSet);
    set->mempool = mempool;

    if (!lt) lt = base->num_inst_states + 1;

    for (state = base->inst_states; state; state = state->next) {

        if (state->update->numid >= lt) continue;
        if (state->update->numid <= gt) continue;

        if (DEBUG_INST_LEVEL_2)
            knd_log("== export update: %zu", state->update->numid);

        class_update = state->val;

        for (size_t i = 0; i < class_update->num_insts; i++) {
            inst = class_update->insts[i];

            if (DEBUG_INST_LEVEL_3)
                knd_log("* inst id:%.*s",
                        inst->entry->id_size, inst->entry->id);

            err = set->add(set, inst->entry->id,
                           inst->entry->id_size,
                           (void*)inst);                                          RET_ERR();
        }
    }

    task->sets[0] = set;
    task->num_sets = 1;

    return knd_OK;
}

static gsl_err_t parse_select_inst_delta(void *data,
                                         const char *rec,
                                         size_t *total_size)
{
    struct kndObject *self = data;
    int err;

    err = select_delta(self, rec, total_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_select_inst(struct kndObject *self,
                                  const char *rec,
                                  size_t *total_size)
{
    struct glbOutput *log = self->base->entry->repo->log;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_INST_LEVEL_TMP)
        knd_log(".. parsing class instance select: \"%.*s\"", 32, rec);

    self->max_depth = 0;
    self->curr_obj = NULL;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_obj,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = parse_elem,
          .obj = self
        },
        {  .name = "_depth",
           .name_size = strlen("_depth"),
           .parse = gsl_parse_size_t,
           .is_selector = true,
           .obj = &self->max_depth
        },
        { .name = "_rel",
          .name_size = strlen("_rel"),
          .parse = select_rel,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "_rm",
          .name_size = strlen("_rm"),
          .run = remove_obj,
          .obj = self
        },
        { .name = "_delta",
          .name_size = strlen("_delta"),
          .is_selector = true,
          .parse = parse_select_inst_delta,
          .obj = self
        },
        { .is_default = true,
          .run = present_inst_selection,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- obj select parse error: \"%.*s\"",
                log->buf_size, log->buf);
        if (!log->buf_size) {
            err = log->write(log, "obj select parse failure",
                                 strlen("obj select parse failure"));
            if (err) return make_gsl_err_external(err);
        }
        return parser_err;
    }

    return make_gsl_err(gsl_OK);
}

static int 
kndObject_resolve(struct kndObject *self)
{
    struct kndElem *elem;
    int err;

    if (DEBUG_INST_LEVEL_2) {
        if (self->type == KND_OBJ_ADDR) {
            knd_log(".. resolve OBJ %.*s::%.*s [%.*s]",
                    self->base->entry->name_size, self->base->entry->name,
                    self->name_size, self->name,
                    self->entry->id_size, self->entry->id);
        } else {
            knd_log(".. resolve aggr elem \"%.*s\" %.*s..",
                    self->parent->attr->name_size, self->parent->attr->name,
                    self->base->name_size, self->base->name);
        }
    }

    for (elem = self->elems; elem; elem = elem->next) {
        if (DEBUG_INST_LEVEL_2)
            knd_log(".. resolving elem: \"%.*s\"",
                    elem->attr->name_size, elem->attr->name);
        err = elem->resolve(elem);                                                RET_ERR();

        if (DEBUG_INST_LEVEL_2)
            knd_log("++ elem resolved: %.*s!",
                    elem->attr->name_size, elem->attr->name);
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
    self->parse = parse_import_inst;
    self->read = parse_import_inst;
    self->resolve = kndObject_resolve;
    self->export = kndObject_export;
    self->select = parse_select_inst;
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
