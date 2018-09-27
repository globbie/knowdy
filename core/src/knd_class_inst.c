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
                                void *elem __attribute__((unused)));

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

static int export_inner_JSON(struct kndClassInst *self,
                            struct glbOutput *out)
{
    struct kndElem *elem;
    int err;

    /* anonymous obj */
    err = out->write(out, "{", 1);
    if (err) return err;

    elem = self->elems;
    while (elem) {
        err = knd_elem_export(elem, KND_FORMAT_JSON, out);
        if (err) {
            knd_log("-- inst elem export failed: %.*s",
                    elem->attr->name_size, elem->attr->name);
            return err;
        }

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

static int export_inner_GSP(struct kndClassInst *self,
                           struct glbOutput *out)
{
    struct kndElem *elem;
    int err;

    /* anonymous obj */
    //err = out->writec(out, '{');   RET_ERR();

    elem = self->elems;
    while (elem) {
        err = knd_elem_export(elem, KND_FORMAT_GSP, out);
        if (err) {
            knd_log("-- inst elem GSP export failed: %.*s",
                    elem->attr->name_size, elem->attr->name);
            return err;
        }
        elem = elem->next;
    }
    //err = out->writec(out, '}');   RET_ERR();

    return knd_OK;
}

static int export_inst_relref_JSON(struct kndClassInst *self,
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
                            ",\"_modif\":\"%Y-%m-%d %H:%M:%S\"", &tm_info);
        err = out->write(out, buf, buf_size);                                     RET_ERR();
    }

    set = relref->idx;
    err = out->write(out, ",\"num_instances\":",
                     strlen(",\"num_instances\":"));                              RET_ERR();
    err = out->writef(out, "%zu", relref->num_insts);                             RET_ERR();

    if (self->max_depth) {
        err = out->write(out, ",\"instances\":",
                         strlen(",\"instances\":"));                              RET_ERR();
        err = out->writec(out, '[');                                              RET_ERR();
        err = set->map(set, export_rel_inst_JSON, (void*)out);
        if (err) return err;
        err = out->writec(out, ']');                                              RET_ERR();
    }
    err = out->writec(out, '}');                                                  RET_ERR();
    return knd_OK;
}

static int export_inst_relrefs_JSON(struct kndClassInst *self)
{
    struct kndRelRef *relref;
    int err;

    for (relref = self->entry->rels; relref; relref = relref->next) {
        if (!relref->idx) continue;
        err = export_inst_relref_JSON(self, relref);                              RET_ERR();
    }
    return knd_OK;
}

static int export_concise_JSON(struct kndClassInst *self,
                               struct glbOutput *out)
{
    struct kndClassInst *obj;
    struct kndElem *elem;
    bool is_concise = true;
    int err;

    err = out->write(out, ",\"_class\":\"", strlen(",\"_class\":\""));
    if (err) return err;

    err = out->write(out, self->base->name, self->base->name_size);
    if (err) return err;

    err = out->write(out, "\"", 1);
    if (err) return err;

    for (elem = self->elems; elem; elem = elem->next) {
        /* NB: restricted attr */
        if (elem->attr->access_type == KND_ATTR_ACCESS_RESTRICTED)
            continue;

        if (DEBUG_INST_LEVEL_3)
            knd_log(".. export elem: %.*s",
                    elem->attr->name_size, elem->attr->name);

        /* filter out detailed presentation */
        if (is_concise) {
            /* inner obj? */
            if (elem->inner) {
                obj = elem->inner;

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
                
                err = obj->export(obj, KND_FORMAT_JSON, out);
                if (err) return err;

                //need_separ = true;
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
        err = knd_elem_export(elem, KND_FORMAT_JSON, out);
        if (err) {
            knd_log("-- elem not exported: %s", elem->attr->name);
            return err;
        }
        //need_separ = true;
    }
    return knd_OK;
}

static int export_JSON(struct kndClassInst *self,
                       struct glbOutput *out)
{
    struct kndElem *elem;
    struct kndClassInst *obj;
    struct kndState *state = self->states;
    bool is_concise = true;
    int err;

    if (DEBUG_INST_LEVEL_2) {
        knd_log(".. JSON export class inst \"%.*s\"",
                self->name_size, self->name);
        if (self->base) {
            knd_log("   (class: %.*s)",
                    self->base->name_size, self->base->name);
        }
    }

    if (self->type == KND_OBJ_INNER) {
        err = export_inner_JSON(self, out);
        if (err) {
            knd_log("-- inner obj JSON export failed");
            return err;
        }
        return knd_OK;
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
        err = out->writef(out, "%zu", state->numid);                              RET_ERR();

        switch (state->phase) {
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

    if (self->depth >= self->max_depth) {
        /* any concise fields? */
        err = export_concise_JSON(self, out);                                     RET_ERR();
        goto final;
    }

    err = out->write(out, ",\"_class\":\"", strlen(",\"_class\":\""));
    if (err) return err;

    err = out->write(out, self->base->name, self->base->name_size);
    if (err) return err;

    err = out->write(out, "\"", 1);
    if (err) return err;

    /* TODO: id */

    for (elem = self->elems; elem; elem = elem->next) {
        /* NB: restricted attr */
        if (elem->attr->access_type == KND_ATTR_ACCESS_RESTRICTED)
            continue;

        if (DEBUG_INST_LEVEL_3)
            knd_log(".. export elem: %.*s",
                    elem->attr->name_size, elem->attr->name);

        /* filter out detailed presentation */
        if (is_concise) {
            /* inner obj? */
            if (elem->inner) {
                obj = elem->inner;

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
                
                err = obj->export(obj, KND_FORMAT_JSON, out);
                if (err) return err;

                //need_separ = true;
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
        err = knd_elem_export(elem, KND_FORMAT_JSON, out);
        if (err) {
            knd_log("-- elem not exported: %s", elem->attr->name);
            return err;
        }
        //need_separ = true;
    }

    if (self->entry->rels) {
        err = out->write(out, ",\"_rel\":", strlen(",\"_rel\":"));                RET_ERR();
        err = out->writec(out, '[');                                              RET_ERR();
        err = export_inst_relrefs_JSON(self);                                     RET_ERR();
        err = out->writec(out, ']');                                              RET_ERR();
    }

 final:
    err = out->write(out, "}", 1);
    if (err) return err;

    return err;
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

/*static int export_state_GSP(struct kndClassInst *self,
                            struct glbOutput *out)
{
    struct kndElem *elem;
    int err;

    err = out->write(out, "{_st ", strlen("{_st "));                            RET_ERR();

    for (elem = self->elems; elem; elem = elem->next) {
        err = knd_elem_export(elem, KND_FORMAT_GSP, out);
        if (err) {
            knd_log("-- export of \"%s\" elem failed: %d :(",
                    elem->attr->name, err);
            return err;
        }
    }
    err = out->writec(out, '}');                                              RET_ERR();

    return knd_OK;
}
*/

static int export_GSP(struct kndClassInst *self,
                      struct glbOutput *out)
{
    struct kndElem *elem;
    int err;

    if (self->type == KND_OBJ_INNER) {
        err = export_inner_GSP(self, out);
        if (err) {
            knd_log("-- inner obj GSP export failed");
            return err;
        }
        return knd_OK;
    }

    /* elems */
    for (elem = self->elems; elem; elem = elem->next) {
        err = knd_elem_export(elem, KND_FORMAT_GSP, out);
        if (err) {
            knd_log("-- export of \"%s\" elem failed: %d :(",
                    elem->attr->name, err);
            return err;
        }
    }

    return knd_OK;
}

/*static int export_state(struct kndClassInst *self,
                        knd_format format,
                        struct glbOutput *out)
{
    int err;
    switch (format) {
    case KND_FORMAT_GSP:
        err = export_state_GSP(self, out);
        if (err) return err;
        break;
    default:
        break;
    }

    return knd_OK;
    }*/

/* export object */
static int kndClassInst_export(struct kndClassInst *self,
                               knd_format format,
                               struct glbOutput *out)
{
    int err;
    switch (format) {
    case KND_FORMAT_JSON:
        err = export_JSON(self, out);
        if (err) return err;
        break;
    case KND_FORMAT_GSP:
        err = export_GSP(self, out);
        if (err) return err;
        break;
    default:
        break;
    }

    return knd_OK;
}

static gsl_err_t run_set_name(void *obj, const char *name, size_t name_size)
{
    struct kndClassInst *self = obj;
    struct kndClass *c = self->base;
    struct kndClassInstEntry *entry;
    struct kndRepo *repo = c->entry->repo;
    struct ooDict *name_idx = repo->class_inst_name_idx;
    struct glbOutput *log = repo->log;
    struct kndTask *task = repo->task;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
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

static gsl_err_t find_obj_rel(void *obj, const char *name, size_t name_size)
{
    struct kndClassInst *self = obj;
    struct kndRelRef *relref = NULL;
    struct kndRel *rel;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    for (relref = self->entry->rels; relref; relref = relref->next) {
        rel = relref->rel;

        if (DEBUG_INST_LEVEL_2)
            knd_log("== REL: %.*s",
                    rel->entry->name_size, rel->entry->name);

        if (rel->entry->name_size != name_size) continue;

        if (!memcmp(rel->entry->name, name, name_size)) {
            if (DEBUG_INST_LEVEL_2)
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
    struct kndClassInst *self = data;
    struct glbOutput *out = self->base->entry->repo->out;
    struct kndTask *task = self->base->entry->repo->task;
    struct kndSet *set;
    struct kndRel *rel;
    int err;

    if (task->type == KND_SELECT_STATE) {

        /* no sets found? */
        if (!task->num_sets) {
            err = out->write(out, "{}", strlen("{}"));
            if (err) return make_gsl_err_external(err);
            return make_gsl_err(gsl_OK);
        }
        /* TODO: intersection cache lookup  */
        set = task->sets[0];
        if (!set->num_elems) {
            err = out->write(out, "{}", strlen("{}"));
            if (err) return make_gsl_err_external(err);
            return make_gsl_err(gsl_OK);
        }

        rel = self->curr_rel->rel;

        err = rel->export_inst_set(rel, set);
        if (err) return make_gsl_err_external(err);

        return make_gsl_err(gsl_OK);
    }
    
    /* display all relrefs */
    if (!self->curr_rel) {
        self->max_depth = 1;
        err = out->writec(out, '[');
        if (err) return make_gsl_err_external(err);

        err = export_inst_relrefs_JSON(self);
        if (err) return make_gsl_err_external(err);

        err = out->writec(out, ']'); 
        if (err) return make_gsl_err_external(err);
        return make_gsl_err(gsl_OK);
    }

    err = export_inst_relref_JSON(self, self->curr_rel);
    if (err) return make_gsl_err_external(err);

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

static gsl_err_t parse_select_elem(void *data,
                                   const char *name, size_t name_size,
                                   const char *rec, size_t *total_size)
{
    struct kndClassInst *self = data;
    struct kndClassInst *inst;
    struct kndElem *elem = NULL;
    struct kndAttr *attr = NULL;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. parsing elem \"%.*s\" select REC: %.*s",
                name_size, name, 128, rec);

    inst = self->curr_inst;
    if (self->parent) {
        inst = self;
    }

    if (!inst) {
        knd_log("-- no inst selected");
        return *total_size = 0, make_gsl_err(gsl_FAIL);
    }

    err = kndClassInst_validate_attr(inst, name, name_size, &attr, &elem);
    if (err) {
        knd_log("-- \"%.*s\" attr not validated", name_size, name);
        return *total_size = 0, make_gsl_err_external(err);
    }

    if (!elem) {
        knd_log("-- elem not set");
        return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }

    switch (elem->attr->type) {
    case KND_ATTR_INNER:
        parser_err = knd_parse_select_inst(elem->inner, rec, total_size);
        if (parser_err.code) return parser_err;
        break;
    default:
        parser_err = knd_elem_parse_select(elem, rec, total_size);
        if (parser_err.code) return parser_err;
        break;
    }

    if (DEBUG_INST_LEVEL_2)
        knd_log("++ elem %.*s select parsing OK!",
                elem->attr->name_size, elem->attr->name);

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

    err = mempool->new_rel_ref(mempool, &relref);
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

static gsl_err_t remove_inst(void *data,
                             const char *name __attribute__((unused)),
                             size_t name_size __attribute__((unused)))
{
    struct kndClassInst *self       = data;

    if (!self->curr_inst) {
        knd_log("-- remove operation: no class instance selected");
        return make_gsl_err(gsl_FAIL);
    }

    struct kndClass  *base       = self->base;
    struct kndClassInst *obj;
    struct kndState  *state;
    struct kndStateRef  *state_ref;
    struct kndRepo *repo = base->entry->repo;
    struct glbOutput *log        = repo->log;
    struct kndMemPool *mempool   = repo->mempool;
    struct kndTask *task         = repo->task;
    struct kndSet *set;
    int err;

    obj = self->curr_inst;
    if (DEBUG_INST_LEVEL_2)
        knd_log("== class inst to remove: \"%.*s\"",
                obj->name_size, obj->name);

    err = knd_state_new(mempool, &state);
    if (err) return make_gsl_err_external(err);
    err = knd_state_ref_new(mempool, &state_ref);
    if (err) return make_gsl_err_external(err);
    state_ref->state = state;

    state->phase = KND_REMOVED;
    state->next = obj->states;
    obj->states = state;
    obj->num_states++;
    state->numid = obj->num_states;

    log->reset(log);
    err = log->write(log, self->name, self->name_size);
    if (err) return make_gsl_err_external(err);
    err = log->write(log, " obj removed", strlen(" obj removed"));
    if (err) return make_gsl_err_external(err);

    task->type = KND_UPDATE_STATE;
    
    state_ref->next = base->inst_state_refs;
    base->inst_state_refs = state_ref;

    return make_gsl_err(gsl_OK);
}

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

    if (DEBUG_INST_LEVEL_TMP)
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
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_rel",
          .name_size = strlen("_rel"),
          .parse = parse_rels,
          .obj = self
        }
    };
 
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t present_inst_rels_state(void *obj,
                                         const char *name  __attribute__((unused)),
                                         size_t name_size  __attribute__((unused)))
{
    struct kndClassInst *self = obj;
    struct glbOutput *log = self->base->entry->repo->log;
    struct kndTask *task = self->base->entry->repo->task;
    struct kndRelRef *relref = self->curr_rel;
    int e, err;

    if (!self) return make_gsl_err_external(knd_FAIL);
    
    if (DEBUG_INST_LEVEL_2)
        knd_log(".. get state of inst rels \"%.*s\"..",
                self->name_size, self->name);

    if (!relref) {
        knd_log("-- no rel ref selected");
        log->reset(log);
        e = log->write(log, "Relation name not specified",
                       strlen("Relation name not specified"));
        if (e) return make_gsl_err_external(e);
        task->http_code = HTTP_BAD_REQUEST;
        return make_gsl_err_external(knd_FAIL);
    }

    self->max_depth = 0;
    err = export_inst_relref_JSON(self, relref);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static int select_inst_rel_delta(struct kndClassInst *self,
                                 const char *rec,
                                 size_t *total_size)
{
    struct kndClass *base = self->base;
    struct kndTask *task = base->entry->repo->task;
    struct kndMemPool *mempool = base->entry->repo->mempool;
    struct glbOutput *log = base->entry->repo->log;
    struct kndState *state;
    struct kndSet *set;
    //struct kndRelInstance *inst;
    //struct kndRelInstanceUpdate *rel_inst_upd;
    struct kndRelRef *relref = self->curr_rel;
    int e, err;
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .name = "eq",
          .name_size = strlen("eq"),
          .parse = gsl_parse_size_t,
          .obj = &task->state_eq
        },
        { .name = "gt",
          .name_size = strlen("gt"),
          .parse = gsl_parse_size_t,
          .obj = &task->state_gt
        },
        { .name = "lt",
          .name_size = strlen("lt"),
          .parse = gsl_parse_size_t,
          .obj = &task->state_lt
        },
        { .is_default = true,
          .run = present_rel,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    if (DEBUG_INST_LEVEL_TMP)
        knd_log(".. select inst rels delta:  gt %zu  lt %zu ..",
                task->state_gt, task->state_lt);

    task->type = KND_SELECT_STATE;

    // TODO error logs

    if (!relref->num_states) {
        log->reset(log);
        e = log->write(log, "no states available",
                       strlen("no states available"));
        if (e) return e;
        task->http_code = HTTP_BAD_REQUEST;
        return knd_FAIL;
    }

    if (task->state_gt >= relref->num_states) {
        log->reset(log);
        e = log->write(log, "invalid state range given",
                       strlen("invalid state range given"));
        if (e) return e;
        task->http_code = HTTP_BAD_REQUEST;
        return knd_FAIL;
    }

    if (task->state_lt && task->state_lt < task->state_gt) {
        log->reset(log);
        e = log->write(log, "invalid state range given",
                       strlen("invalid state range given"));
        if (e) return e;
        task->http_code = HTTP_BAD_REQUEST;
        return knd_FAIL;
    }

    err = knd_set_new(mempool, &set);                                        MEMPOOL_ERR(kndSet);
    set->mempool = mempool;

    if (!task->state_lt)
        task->state_lt = relref->init_state + relref->states->update->numid + 1;
    
    for (state = relref->states; state; state = state->next) {
        if (task->state_lt <= state->numid) continue;
        if (task->state_gt >= state->numid) continue;

        // iterate over rel insts
        /*for (rel_inst_upd = state->val; rel_inst_upd; rel_inst_upd = rel_inst_upd->next) {
            inst = rel_inst_upd->inst;
            if (DEBUG_INST_LEVEL_TMP)
                knd_log("* rel inst id:%.*s",
                        inst->id_size, inst->id);

            err = set->add(set, inst->id, inst->id_size,
                           (void*)inst);                                          RET_ERR();
                           }*/
    }

    task->sets[0] = set;
    task->num_sets = 1;
    task->curr_inst = self;

    return knd_OK;
}

/*static gsl_err_t parse_select_inst_rel_delta(void *data,
                                             const char *rec,
                                             size_t *total_size)
{
    struct kndClassInst *self = data;
    struct glbOutput *log = self->base->entry->repo->log;
    struct kndTask *task = self->base->entry->repo->task;
    int e, err;

    if (!self->curr_rel) {
        knd_log("-- no rel selected :(");
        log->reset(log);
        e = log->write(log, "Relation name not specified",
                       strlen("Relation name not specified"));
        if (e) return make_gsl_err_external(e);
        task->http_code = HTTP_BAD_REQUEST;
        return make_gsl_err_external(knd_FAIL);
    }

    err = select_inst_rel_delta(self, rec, total_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}
*/

 /*static gsl_err_t select_rels(struct kndClassInst *self,
                             const char *rec,
                             size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = find_obj_rel,
          .obj = self
        },
        { .name = "_state",
          .name_size = strlen("_state"),
          .run = present_inst_rels_state,
          .obj = self
        },
        { .name = "_delta",
          .name_size = strlen("_delta"),
          .is_selector = true,
          .parse = parse_select_inst_rel_delta,
          .obj = self
        },
        { .is_default = true,
          .run = present_rel,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}
*/

static int export_inst_JSON(void *obj,
                            const char *elem_id  __attribute__((unused)),
                            size_t elem_id_size  __attribute__((unused)),
                            size_t count,
                            void *elem)
{
    struct kndClass *self = obj;
    struct kndTask *task = self->entry->repo->task;
    if (count < task->start_from) return knd_OK;
    if (task->batch_size >= task->batch_max) return knd_RANGE;
    struct glbOutput *out = self->entry->repo->out;
    struct kndClassInstEntry *entry = elem;
    struct kndClassInst *inst = entry->inst;
    struct kndState *state;
    int err;

    if (DEBUG_INST_LEVEL_TMP) {
        knd_class_inst_str(inst, 0);
    }

    /*if (!task->show_removed_objs) {
        state = inst->states;
        if (state && state->phase == KND_REMOVED)
            return knd_OK;
            }*/

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
    err = kndClassInst_export(inst, KND_FORMAT_JSON, out);                        RET_ERR();
    task->batch_size++;

    return knd_OK;
}

static int export_inst_set_JSON(struct kndClass *self,
                                struct kndSet *set,
                                struct glbOutput *out)
{
    struct kndTask *task = self->entry->repo->task;
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
    struct kndClassInst *self = data;
    struct kndClassInst *inst;
    struct kndClass *base = self->base;
    struct kndTask *task = base->entry->repo->task;
    struct glbOutput *out = base->entry->repo->out;
    struct kndSet *set;
    int err;

    if (DEBUG_INST_LEVEL_TMP)
        knd_log(".. class \"%.*s\" (repo:%.*s) inst selection: task type:%d num sets:%zu",
                base->name_size, base->name,
                base->entry->repo->name_size, base->entry->repo->name,
                task->type, task->num_sets);

    out->reset(out);
    if (task->type == KND_SELECT_STATE) {
        /* no sets found? */
        if (!task->num_sets) {
            if (base->entry->inst_idx) {
                set = base->entry->inst_idx;

                task->show_removed_objs = false;

                err = export_inst_set_JSON(self->base, set, out);
                if (err) return make_gsl_err_external(err);
                return make_gsl_err(gsl_OK);
            }

            err = out->write(out, "{}", strlen("{}"));
            if (err) return make_gsl_err_external(err);
            return make_gsl_err(gsl_OK);
        }

        /* TODO: intersection cache lookup  */
        set = task->sets[0];

        /* intersection required */
        /*if (task->num_sets > 1) {
            err = knd_set_new(mempool, &set);
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
        task->show_removed_objs = false;
        err = export_inst_set_JSON(self->base, set, out);
        if (err) return make_gsl_err_external(err);

        return make_gsl_err(gsl_OK);
    }
    
    if (!self->curr_inst) {
        knd_log("-- no class inst selected :(");
        return make_gsl_err(gsl_FAIL);
    }

    inst = self->curr_inst;
    inst->max_depth = self->max_depth;

    err = inst->export(inst, KND_FORMAT_JSON, out);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_get_inst(void *obj, const char *name, size_t name_size)
{
    struct kndClassInst *self = obj;
    struct kndClass *c = self->base;
    struct kndTask *task = c->entry->repo->task;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    self->curr_inst = NULL;
    err = knd_get_class_inst(c, name, name_size, &self->curr_inst);
    if (err) {
        knd_log("-- failed to get class inst: %.*s", name_size, name);
        return make_gsl_err_external(err);
    }
    self->curr_inst->max_depth = 0;

    /* to return a single object */
    task->type = KND_GET_STATE;
    self->curr_inst->elem_state_refs = NULL;

    if (DEBUG_INST_LEVEL_TMP) {
        knd_class_inst_str(self->curr_inst, 0);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t present_state(void *obj,
                               const char *name  __attribute__((unused)),
                               size_t name_size  __attribute__((unused)))
{
    struct kndClass *self = obj;
    struct kndTask *task = self->entry->repo->task;
    struct glbOutput *out = self->entry->repo->out;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    struct kndSet *set;
    int err;

    if (DEBUG_INST_LEVEL_TMP) {
        knd_log(".. select delta:  gt %zu  lt %zu  eq:%zu..",
                task->state_gt, task->state_lt, task->state_eq);
    }

    task->type = KND_SELECT_STATE;

    if (DEBUG_INST_LEVEL_TMP) {
        self->str(self);   
    }

    if (task->state_gt  >= self->num_inst_states) goto JSON_state;
    //if (task->gte >= base->num_inst_states) goto JSON_state;

    if (task->state_lt && task->state_lt < task->state_gt) goto JSON_state;

    err = knd_set_new(mempool, &set);
    if (err) return make_gsl_err_external(err);
    set->mempool = mempool;

    err = knd_class_get_inst_updates(self,
                                     task->state_gt, task->state_lt,
                                     task->state_eq, set);
    if (err) return make_gsl_err_external(err);


    task->show_removed_objs = true;

    err = export_inst_set_JSON(self, set, out);
    if (err) return make_gsl_err_external(err);
    
    return make_gsl_err(gsl_OK);

 JSON_state:
    err = out->writec(out, '{');
    if (err) return make_gsl_err_external(err);

    err = knd_export_class_inst_state_JSON(self);
    if (err) return make_gsl_err_external(err);

    err = out->writec(out, '}');
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_select_state(void *data,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndClassInst *self = data;
    struct kndClass *base = self->base;
    struct kndTask *task = base->entry->repo->task;

    struct gslTaskSpec specs[] = {
        { .name = "eq",
          .name_size = strlen("eq"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_eq
        },
        { .name = "gt",
          .name_size = strlen("gt"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_gt
        },
        { .name = "lt",
          .name_size = strlen("lt"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_lt
        },
        { .name = "gte",
          .name_size = strlen("gte"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_gte
        },
        { .name = "lte",
          .name_size = strlen("lte"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_lte
        },
        { .is_default = true,
          .run = present_state,
          .obj = base
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int update_elem_states(struct kndClassInst *self)
{
    struct kndStateRef *ref;
    struct kndState *state;
    struct kndClassInst *inst;
    struct kndElem *elem;
    struct kndClass *c;
    struct kndMemPool *mempool = self->base->entry->repo->mempool;
    int err;

    if (DEBUG_INST_LEVEL_TMP)
        knd_log("\n++ \"%.*s\" class inst updates happened:",
                self->name_size, self->name);

    for (ref = self->elem_state_refs; ref; ref = ref->next) {
        state = ref->state;
        if (state->val) {
            elem = state->val->obj;
            knd_log("== elem %.*s updated!",
                    elem->attr->name_size, elem->attr->name);
        }
        if (state->children) {
            knd_log("== child elem updated: %zu", state->numid);
        }
    }
    
    err = knd_state_new(mempool, &state);
    if (err) {
        knd_log("-- class inst state alloc failed");
        return err;
    }
    err = knd_state_ref_new(mempool, &ref);
    if (err) {
        knd_log("-- state ref alloc failed :(");
        return err;
    }
    ref->state = state;
    state->phase = KND_UPDATED;
    self->num_states++;
    state->numid = self->num_states;
    state->children = self->elem_state_refs;
    self->elem_state_refs = NULL;
    self->states = state;

    /* inform your immediate parent or baseclass */
    if (self->parent) {
        elem = self->parent;
        inst = elem->parent;

        knd_log(".. inst \"%.*s\" to get new updates..",
                inst->name_size, inst->name);

        ref->next = inst->elem_state_refs;
        inst->elem_state_refs = ref;
    } else {
        c = self->base;

        knd_log(".. class \"%.*s\" (repo:%.*s) to get new updates..",
                c->name_size, c->name,
                c->entry->repo->name_size, c->entry->repo->name);

        ref->next = c->inst_state_refs;
        c->inst_state_refs = ref;
    }
    return knd_OK;
}

extern gsl_err_t knd_parse_select_inst(struct kndClassInst *self,
                                       const char *rec,
                                       size_t *total_size)
{
    struct kndRepo *repo = self->base->entry->repo;
    struct kndTask *task = repo->task;
    struct kndClassInst *inst;

    int err;
    gsl_err_t parser_err;

    if (DEBUG_INST_LEVEL_2)
        knd_log(".. class instance parsing: \"%.*s\"", 64, rec);

    task->type = KND_SELECT_STATE;
    self->curr_inst = NULL;
    if (self->parent) {
        self->elem_state_refs = NULL;
    }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_inst,
          .obj = self
        },
        { .name = "_state",
          .name_size = strlen("_state"),
          .parse = parse_select_state,
          .obj = self
        },
        { .is_validator = true,
          .validate = parse_select_elem,
          .obj = self
        }/*,
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = parse_import_elem,
          .obj = self->
          }*/,
        {  .name = "_depth",
           .name_size = strlen("_depth"),
           .parse = gsl_parse_size_t,
           .is_selector = true,
           .obj = &self->max_depth
        },
        { .name = "_rm",
          .name_size = strlen("_rm"),
          .run = remove_inst,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "_rm",
          .name_size = strlen("_rm"),
          .run = remove_inst,
          .obj = self
        },
        { .is_default = true,
          .run = present_inst_selection,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        struct glbOutput *log = repo->log;
        knd_log("-- obj select parse error: \"%.*s\"",
                log->buf_size, log->buf);
        if (!log->buf_size) {
            err = log->write(log, "class instance selection failure",
                             strlen("class instance selection failure"));
            if (err) return make_gsl_err_external(err);
        }
        return parser_err;
    }

    /* check elem updates */
    inst = self->curr_inst;
    if (self->parent) inst = self;
    if (!inst) return make_gsl_err(gsl_OK);

    if (inst->elem_state_refs) {
        err = update_elem_states(inst);
        if (err) return make_gsl_err_external(err);
    }

    return make_gsl_err(gsl_OK);
}

static int kndClassInst_resolve(struct kndClassInst *self)
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
            knd_log(".. resolve inner elem \"%.*s\" %.*s..",
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

extern void kndClassInst_init(struct kndClassInst *self)
{
    self->parse = parse_import_inst;
    self->read = parse_import_inst;
    self->read_state  = read_state;
    self->resolve = kndClassInst_resolve;
    self->export = kndClassInst_export;
}

extern int kndClassInst_new(struct kndClassInst **obj)
{
    struct kndClassInst *self;
    self = malloc(sizeof(struct kndClassInst));
    if (!self) return knd_NOMEM;
    memset(self, 0, sizeof(struct kndClassInst));
    kndClassInst_init(self);
    *obj = self;
    return knd_OK;
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
