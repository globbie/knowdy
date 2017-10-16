#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_concept.h"
#include "knd_attr.h"
#include "knd_elem.h"
#include "knd_repo.h"
#include "knd_object.h"

#include "knd_text.h"
#include "knd_num.h"
#include "knd_ref.h"

#include "knd_refset.h"
#include "knd_sorttag.h"
#include "knd_parser.h"

#include "knd_output.h"
#include "knd_user.h"

#define DEBUG_OBJ_LEVEL_1 0
#define DEBUG_OBJ_LEVEL_2 0
#define DEBUG_OBJ_LEVEL_3 0
#define DEBUG_OBJ_LEVEL_4 0
#define DEBUG_OBJ_LEVEL_TMP 1

static void del(struct kndObject *self)
{
    struct kndElem *elem, *next_elem;

    elem = self->elems;
    while (elem) {
        next_elem = elem->next;
        elem->del(elem);
        elem = next_elem;
    }

    self->phase = KND_FREED;
}

static void str(struct kndObject *self)
{
    struct kndElem *elem;
    if (self->type == KND_OBJ_ADDR) {
        knd_log("\n%*sOBJ %.*s::%.*s  id:%.*s  numid:%zu  state:%.*s  phase:%d\n",
                self->depth * KND_OFFSET_SIZE, "", self->conc->name_size, self->conc->name,
                self->name_size, self->name,
                KND_ID_SIZE, self->id, self->numid, KND_STATE_SIZE, self->state, self->phase);
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
kndObject_export_reverse_rels_JSON(struct kndObject *self)
{
    struct kndRelClass *relc;
    struct kndRelType *reltype;
    struct kndRef *ref;
    struct kndOutput *out = self->out;
    int err;

    /* sort refs by class */
    if (DEBUG_OBJ_LEVEL_2)
        knd_log("..export reverse_rels of %.*s..", self->name_size, self->name);

    err = out->write(out, ",\"_reverse_rels\":[", strlen(",\"_reverse_rels\":["));
    if (err) return err;

    /* class conc */
    for (relc = self->reverse_rel_classes; relc; relc = relc->next) {
        err = out->write(out, "{\"c\":\"", strlen("{\"c\":\""));
        if (err) return err;

        if (relc->conc) {
            err = out->write(out, relc->conc->name, relc->conc->name_size);
            if (err) return err;
        }
        else {
            err = out->write(out, relc->name, relc->name_size);
            if (err) return err;
        }
        err = out->write(out, "\"", 1);
        if (err) return err;

        err = out->write(out, ",\"attrs\":[", strlen(",\"attrs\":["));
        if (err) return err;

        /* attr type */
        for (reltype = relc->rel_types; reltype; reltype = reltype->next) {
            err = out->write(out, "{\"n\":\"", strlen("{\"n\":\""));
            if (err) return err;

            if (reltype->attr) {
                err = out->write(out, reltype->attr->name, reltype->attr->name_size);
                if (err) return err;
            }
            else {
                err = out->write(out, reltype->name, reltype->name_size);
                if (err) return err;
            }
            err = out->write(out, "\"", 1);
            if (err) return err;

            err = out->write(out, ",\"refs\":[", strlen(",\"refs\":["));
            if (err) return err;

            for (ref = reltype->refs; ref; ref = ref->next) {
                ref->out = out;
                ref->log = self->log;
                err = ref->export_reverse_rel(ref);
                if (err) return err;

                if (ref->next) {
                    err = out->write(out, ",", 1);
                    if (err) return err;
                }
            }
            err = out->write(out, "]", 1);
            if (err) return err;
            err = out->write(out, "}", 1);
            if (err) return err;

            if (reltype->next) {
                err = out->write(out, ",", 1);
                if (err) return err;
            }
        }
        err = out->write(out, "]", 1);
        if (err) return err;

        err = out->write(out, "}", 1);
        if (err) return err;

        if (relc->next) {
            err = out->write(out, ",", 1);
            if (err) return err;
        }
    }
    
    err = out->write(out, "]", 1);
    if (err) return err;

    return knd_OK;
}

static int
kndObject_export_JSON(struct kndObject *self)
{
    struct kndElem *elem;
    struct kndObject *obj;
    struct kndOutput *out = self->out;
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
    
    err = out->write(out, "{\"n\":\"", strlen("{\"n\":\""));
    if (err) return err;
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

    /*if (self->elems) {
        err = out->write(out, "}", 1);
        if (err) goto final;
    }*/

    /* reverse_rels */
    if (self->reverse_rel_classes) {
        err = kndObject_export_reverse_rels_JSON(self);
        if (err) return err;
    }
    
    if (is_concise) goto closing;

    /* skip reverse_rels */
    //if (self->depth) goto closing;

    
 closing:

    err = out->write(out, "}", 1);
    if (err) return err;
    
    return err;
}


static int
kndObject_export_reverse_rels_GSP(struct kndObject *self)
{
    struct kndRelClass *relc;
    struct kndRelType *reltype;
    struct kndRef *ref;
    struct kndOutput *out = self->out;
    int err;

    /* sort refs by class */
    if (DEBUG_OBJ_LEVEL_2)
        knd_log(".. GSP export reverse_rels of %.*s..", self->name_size, self->name);

    err = out->write(out, "[_rev", strlen("[_rev"));
    if (err) return err;

    /* class conc */
    for (relc = self->reverse_rel_classes; relc; relc = relc->next) {
        err = out->write(out, "{", 1);
        if (err) return err;
        err = out->write(out, relc->conc->id, KND_ID_SIZE);
        if (err) return err;
        err = out->write(out, " ", 1);
        if (err) return err;
        err = out->write(out, relc->conc->name, relc->conc->name_size);
        if (err) return err;

        /* attr type */
        for (reltype = relc->rel_types; reltype; reltype = reltype->next) {
            err = out->write(out, "{", 1);
            if (err) return err;
            err = out->write(out, reltype->attr->name, reltype->attr->name_size);
            if (err) return err;

            err = out->write(out, "[ref", strlen("[ref"));
            if (err) return err;

            for (ref = reltype->refs; ref; ref = ref->next) {
                ref->format = KND_FORMAT_GSP;
                err = ref->export_reverse_rel(ref);
                if (err) return err;
            }
            err = out->write(out, "]", 1);
            if (err) return err;
            err = out->write(out, "}", 1);
            if (err) return err;
        }
        err = out->write(out, "}", 1);
        if (err) return err;
    }
    
    err = out->write(out, "]", 1);
    if (err) return err;

    return knd_OK;
}


static int 
kndObject_export_GSP(struct kndObject *self)
{
    bool got_elem = false;
    struct kndElem *elem;
    bool is_concise = true;
    size_t start_size = 0;
    int err;

    if (self->type == KND_OBJ_ADDR) {
        start_size = self->out->buf_size;
        if (DEBUG_OBJ_LEVEL_2)
            knd_log("%*s.. export GSP obj \"%.*s\" [id: %.*s]..",
                    self->depth *  KND_OFFSET_SIZE, "",
                    self->name_size, self->name, KND_ID_SIZE, self->id);

        err = self->out->write(self->out, "{", 1);
        if (err) return err;

        err = self->out->write(self->out, self->name, self->name_size);
        if (err) return err;
    }

    /* ELEMS */
    for (elem = self->elems; elem; elem = elem->next) {
        elem->out = self->out;
        elem->format = KND_FORMAT_GSP;
        err = elem->export(elem);
        if (err) {
            knd_log("-- export of \"%s\" elem failed: %d :(", elem->attr->name, err);
            return err;
        }
    }

    /* reverse_rels */
    if (self->reverse_rel_classes) {
        err = kndObject_export_reverse_rels_GSP(self);
        if (err) return err;
    }

    if (self->type == KND_OBJ_ADDR) {
        err = self->out->write(self->out, "}", 1);
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

static int run_set_name(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndObject *self = (struct kndObject*)obj;
    struct kndConcept *conc;
    struct kndObjEntry *entry;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE) return knd_LIMIT;

    /* check name doublets */
    conc = self->conc;
    if (conc->dir && conc->dir->obj_idx) {
        entry = conc->dir->obj_idx->get(conc->dir->obj_idx, name);
        if (entry) {
            knd_log("-- obj name doublet found: %.*s %p :(",
                    name_size, name, entry->obj);
            entry->obj->str(entry->obj);
            return knd_FAIL;
        }
    }

    memcpy(self->name, name, name_size);
    self->name_size = name_size;
    self->name[name_size] = '\0';

    if (DEBUG_OBJ_LEVEL_2)
        knd_log("++ OBJ NAME: \"%.*s\"",
                self->name_size, self->name);

    return knd_OK;
}

static int run_present_obj(void *data,
                           struct kndTaskArg *args __attribute__((unused)),
                           size_t num_args __attribute__((unused)))
{
    struct kndObject *self = data;
    int err;

    if (DEBUG_OBJ_LEVEL_TMP)
        knd_log(".. present obj..");

    err = kndObject_export(self);
    if (err) return err;

    return knd_OK;
}

static int
kndObject_validate_attr(struct kndObject *self,
                        const char *name,
                        size_t name_size,
                        struct kndAttr **result)
{
    struct kndConcept *conc;
    struct kndAttr *attr = NULL;
    int err, e;

    if (DEBUG_OBJ_LEVEL_1)
        knd_log(".. \"%.*s\" to validate elem: \"%.*s\" conc: %p",
                self->name_size, self->name, name_size, name, self->conc);
    conc = self->conc;

    err = conc->get_attr(conc, name, name_size, &attr);
    if (err) {
        knd_log("  -- ELEM \"%.*s\" not approved :(\n", name_size, name);
        self->log->reset(self->log);
        e = self->log->write(self->log, name, name_size);
        if (e) return e;
        e = self->log->write(self->log, " elem not confirmed",
                               strlen(" elem not confirmed"));
        if (e) return e;
        return err;
    }

    if (DEBUG_OBJ_LEVEL_1) {
        const char *type_name = knd_attr_names[attr->type];
        knd_log("++ \"%.*s\" ELEM \"%s\" attr type: \"%s\"",
                name_size, name, attr->name, type_name);
    }

    *result = attr;
    return knd_OK;
}

static int parse_elem(void *data,
                      const char *name, size_t name_size,
                      const char *rec, size_t *total_size)
{
    struct kndObject *self = data;
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
        knd_log("..  %s  needs to validate \"%s\" elem,   REC: \"%.*s\"\n",
                obj->name, name, 32, rec);
    }
    err = kndObject_validate_attr(self, name, name_size, &attr);
    if (err) return err;

    err = kndElem_new(&elem);
    if (err) return err;
    elem->obj = self;
    elem->root = self->root ? self->root : self;
    elem->attr = attr;
    elem->out = self->out;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log("   == basic elem type: %s   obj phase: %d root:%p",
                knd_attr_names[attr->type], self->phase, self->conc->root_class);

    switch (attr->type) {
    case KND_ATTR_AGGR:
        err = kndConcept_alloc_obj(self->conc->root_class, &obj);
        if (err) return err;
        
        obj->type = KND_OBJ_AGGR;
        if (!attr->conc) {
            
            if (self->phase == KND_FROZEN || self->phase == KND_SUBMITTED) {
                if (DEBUG_OBJ_LEVEL_2) {
                    knd_log(".. resolve attr \"%.*s\": \"%.*s\"..",
                            attr->name_size, attr->name,
                            attr->ref_classname_size, attr->ref_classname);
                }
                root_class = self->conc->root_class;
                err = root_class->get(root_class,
                                      attr->ref_classname, attr->ref_classname_size,
                                      &c);
                if (err) return err;
                attr->conc = c;
            }
            else {
                knd_log("-- couldn't resolve the %.*s attr :(",
                        attr->name_size, attr->name);
                return knd_FAIL;
            }
        }

        obj->conc = attr->conc;
        obj->out = self->out;
        obj->log = self->log;
        
        obj->root = self->root ? self->root : self;
        err = obj->parse(obj, rec, total_size);
        if (err) return err;

        elem->aggr = obj;
        obj->parent = elem;
        goto append_elem;
    case KND_ATTR_NUM:
        err = kndNum_new(&num);
        if (err) return err;
        num->elem = elem;
        num->out = self->out;
        err = num->parse(num, rec, total_size);
        if (err) goto final;

        elem->num = num;
        goto append_elem;
    case KND_ATTR_REF:
        err = kndRef_new(&ref);
        if (err) return err;
        ref->elem = elem;
        ref->out = self->out;
        err = ref->parse(ref, rec, total_size);
        if (err) goto final;

        elem->ref = ref;
        goto append_elem;
    case KND_ATTR_TEXT:
        err = kndText_new(&text);
        if (err) return err;

        text->elem = elem;
        text->out = self->out;

        err = text->parse(text, rec, total_size);
        if (err) return err;
        
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

    if (DEBUG_OBJ_LEVEL_2)
        knd_log("++ elem %s parsing OK!", elem->attr->name);
    return knd_OK;

 final:

    if (DEBUG_OBJ_LEVEL_TMP)
        knd_log("-- validation of \"%s\" elem failed :(", name);

    elem->del(elem);
    return err;
}


static int run_set_relclass_name(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndRelClass *self = obj;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE) return knd_LIMIT;

    memcpy(self->name, name, name_size);
    self->name_size = name_size;
    self->name[name_size] = '\0';

    if (DEBUG_OBJ_LEVEL_2)
        knd_log("++ Rel Class name: \"%.*s\"",
                self->name_size, self->name);

    return knd_OK;
}

static int run_set_objref_name(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndRef *self = obj;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE) return knd_LIMIT;

    memcpy(self->name, name, name_size);
    self->name_size = name_size;
    self->name[name_size] = '\0';

    if (DEBUG_OBJ_LEVEL_2)
        knd_log("++ OBJ REF NAME: \"%.*s\"",
                self->name_size, self->name);

    return knd_OK;
}

static int objref_read(void *obj,
                       const char *rec,
                       size_t *total_size)
{
    struct kndRef *ref = obj;
    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_objref_name,
          .obj = ref
        }
    };
    int err;

    if (DEBUG_OBJ_LEVEL_1)
        knd_log(".. reading obj ref \"%.*s\"..",
                KND_ID_SIZE, ref->id);

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}

static int objref_append(void *accu,
                         void *item)
{
    struct kndRelType *self = accu;
    struct kndRef *ref = item;

    ref->next = self->refs;
    self->refs = ref;

    return knd_OK;
}

static int objref_alloc(void *obj,
                       const char *name,
                       size_t name_size,
                       size_t count,
                       void **item)
{
    struct kndRelType *self = obj;
    struct kndRef *ref;
    int err;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log(".. create objref: %.*s count: %zu",
                name_size, name, count);
    if (name_size > KND_ID_SIZE) return knd_LIMIT;

    err = kndRef_new(&ref);
    if (err) return err;

    memcpy(ref->id, name, name_size);
    *item = ref;

    return knd_OK;
}

static int reltype_read(void *data,
                        const char *name, size_t name_size,
                        const char *rec, size_t *total_size)
{
    struct kndRelClass *self = data;
    struct kndRelType *reltype;
    int err;

    if (DEBUG_OBJ_LEVEL_2) {
        knd_log("..  reltype validation of \"%.*s\" REC: \"%.*s\"\n",
                name_size, name, 16, rec);
    }
    
    reltype = malloc(sizeof(struct kndRelType));
    if (!reltype) return knd_NOMEM;
    memset(reltype, 0, sizeof(struct kndRelType));

    reltype->next = self->rel_types;
    self->rel_types = reltype;
    
    struct kndTaskSpec specs[] = {
        { .is_list = true,
          .name = "ref",
          .name_size = strlen("ref"),
          .accu = reltype,
          .alloc = objref_alloc,
          .append = objref_append,
          .parse = objref_read
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}

static int rev_rel_read(void *obj,
                        const char *rec,
                        size_t *total_size)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size;
    struct kndRelClass *relc = obj;
    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_relclass_name,
          .obj = relc
        },
        { .is_validator = true,
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_SHORT_NAME_SIZE,
          .validate = reltype_read,
          .obj = relc
        }
    };
    int err;

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}

static int rev_rel_append(void *accu,
                          void *item)
{
    struct kndObject *self = accu;
    struct kndRelClass *relc = item;
    
    relc->next = self->reverse_rel_classes;
    self->reverse_rel_classes = relc;

    return knd_OK;
}

static int rev_rel_alloc(void *obj,
                       const char *name,
                       size_t name_size,
                       size_t count,
                       void **item)
{
    struct kndObject *self = obj;
    struct kndRelClass *relc;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log(".. create rev_rel: %.*s count: %zu",
                name_size, name, count);
    if (name_size > KND_ID_SIZE) return knd_LIMIT;

    relc = malloc(sizeof(struct kndRelClass));
    if (!relc) return knd_NOMEM;

    memset(relc, 0, sizeof(struct kndRelClass));
    memcpy(relc->id, name, name_size);

    *item = relc;

    return knd_OK;
}


/* parse object */
static int parse_GSL(struct kndObject *self,
                     const char *rec,
                     size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "elem",
          .name_size = strlen("elem"),
          .is_validator = true,
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_NAME_SIZE,
          .validate = parse_elem,
          .obj = self
        },
        { .name = "elem",
          .name_size = strlen("elem"),
          .is_validator = true,
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_NAME_SIZE,
          .validate = parse_elem,
          .obj = self
        },
        { .is_list = true,
          .name = "_rev",
          .name_size = strlen("_rev"),
          .accu = self,
          .alloc = rev_rel_alloc,
          .append = rev_rel_append,
          .parse = rev_rel_read
        },
        { .name = "default",
          .name_size = strlen("default"),
          .is_default = true,
          .run = run_present_obj,
          .obj = self
        }
    };
    int err;
    
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}

static int 
kndObject_contribute(struct kndObject *self,
                     size_t  matchpoint_num,
                     size_t orig_pos)
{
    struct kndMatchPoint *mp;
    //struct kndMatchResult *res;
    //float score;
    int idx_pos, err;

    if (!self->num_matchpoints) return knd_OK;

    if ((matchpoint_num) > self->num_matchpoints) return knd_OK;
    
    /*if (self->cache->repo->match_state > self->match_state) {
        self->match_state = self->cache->repo->match_state;
        memset(self->matchpoints, 0, sizeof(struct kndMatchPoint) * self->num_matchpoints);
        self->match_score = 0;
        self->match_idx_pos = -1;
    }
    */
    
    mp = &self->matchpoints[matchpoint_num];

    if (mp->orig_pos) {
        knd_log("  .. this matchpoint was already covered by another unit?\n");
        err = knd_FAIL;
        goto final;
    }
    
    mp->score = KND_MATCH_MAX_SCORE;
    self->match_score += mp->score;
    mp->orig_pos = orig_pos;
    
    self->average_score = (float)self->match_score / (float)self->max_score;

    /*knd_log("   == \"%s\": matched in %lu!    SCORE: %.2f [%lu:%lu]\n",
            self->name,
            (unsigned long)matchpoint_num,
            self->average_score,
            (unsigned long)self->match_score,
            (unsigned long)self->max_score); */

    if (self->average_score >= KND_MATCH_SCORE_THRESHOLD) {

        knd_log("   ++ \"%s\": matching threshold reached: %.2f!\n",
                self->name, self->average_score);

        if (self->match_idx_pos >= 0)
            idx_pos = self->match_idx_pos;
        else {

            /*if (self->cache->num_matches > KND_MAX_MATCHES) {
                knd_log("  -- results buffer limit reached :(\n");
                return knd_FAIL;
            }
                
            idx_pos = self->cache->num_matches;
            self->match_idx_pos = idx_pos;
            self->cache->matches[idx_pos] = self;
            self->cache->num_matches++; */
        }

    }
    
    err = knd_OK;
 final:
    
    return err;
}


static int 
kndObject_resolve(struct kndObject *self)
{
    struct kndElem *elem;
    struct kndObject *obj;
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
        err = elem->resolve(elem);
        if (err) return err;
    } 
    
    return knd_OK;
}

static int 
kndObject_sync(struct kndObject *self)
{
    struct kndElem *elem;
    struct kndConcept *conc;
    struct kndObject *obj;
    struct kndRefSet *refset = NULL;
    struct kndSortTag *tag;
    struct kndSortAttr *attr;
    struct kndSortAttr *a;
    struct kndObjRef *ref;
    int err;

    /*if (DEBUG_OBJ_LEVEL_TMP) {
        if (!self->root) {
            knd_log("\n    !! syncing primary OBJ %s::%s\n",
                    self->id, self->name);
        }
        else {
            knd_log("    .. syncing aggr obj %s..\n",
                    self->parent->name);
        }
        }
    */
    
    elem = self->elems;
    while (elem) {
        /* resolve refs of aggr obj */
        if (elem->aggr) {
            obj = elem->aggr;
            
            knd_log("    .. syncing aggr obj in %s..\n",
                    elem->attr->name);

            while (obj) {
                err = obj->sync(obj);
                if (err) return err;
                obj = obj->next;
            }
            
            goto next_elem;
        }
        
        if (!elem->attr) goto next_elem;
        if (elem->attr->type != KND_ATTR_REF)
            goto next_elem;
        
        conc = elem->attr->conc;
        if (!conc) goto next_elem;

        /*
        if (elem->ref_class)
            conc = elem->ref_class;
        
        if (DEBUG_OBJ_LEVEL_TMP)
            knd_log("\n    .. sync expanding ELEM REF: %s::%s..\n",
                    conc->name,
                    elem->states->val);

        obj = elem->states->refobj;
        if (!obj) {
            err = self->cache->repo->get_cache(self->cache->repo, conc, &cache);
            if (err) return knd_FAIL;

            err = self->cache->name_idx->lookup_name(self->cache->name_idx,
                                                     self->name, self->name_size,
                                                     self->name, self->name_size, idbuf);
            if (err) {
                if (DEBUG_OBJ_LEVEL_TMP)
                    knd_log("  -- failed to sync expand ELEM REF: %s::%s :(\n",
                        conc->name,
                        elem->states->val);
                goto next_elem;
            }
            elem->states->refobj = obj;
        }
        */
        
        next_elem:
        elem = elem->next;
    }

    if (!self->tag) {
        if (DEBUG_OBJ_LEVEL_3)
            knd_log("    -- obj %s:%s is not meant for browsing\n",
                    self->id, self->name);
        return knd_OK;
    }

  
    for (size_t i = 0; i < self->tag->num_attrs; i++) {
        attr = self->tag->attrs[i];
        
        /*err = kndObject_get_idx(self,
                                (const char*)attr->name,
                                attr->name_size, &refset);
        if (err) {
            knd_log("  -- no refset %s :(\n", attr->name);
            return err;
        }
        */
        
        err = kndObjRef_new(&ref);
        if (err) return err;
        
        ref->obj = self;
        if (self->root)
            ref->obj = self->root;
        
        memcpy(ref->obj_id, ref->obj->id, KND_ID_SIZE);
        ref->obj_id_size = KND_ID_SIZE;
    
        memcpy(ref->name, ref->obj->name, ref->obj->name_size);
        ref->name_size = ref->obj->name_size;

        err = kndSortTag_new(&tag);
        if (err) return err;

        ref->sorttag = tag;

        a = malloc(sizeof(struct kndSortAttr));
        if (!a) return knd_NOMEM;
        memset(a, 0, sizeof(struct kndSortAttr));
        a->type = attr->type;
    
        memcpy(a->name, attr->name, attr->name_size);
        a->name_size = attr->name_size;
            
        memcpy(a->val, attr->val, attr->val_size);
        a->val_size = attr->val_size;

        tag->attrs[tag->num_attrs] = a;
        tag->num_attrs++;

        /* subordinate objs not included to AZ */
        if (self->is_subord) {
            if (!strcmp(attr->name, "AZ")) {
                if (DEBUG_OBJ_LEVEL_3)
                    knd_log("  -- %s:%s excluded from AZ\n", self->id, self->name);
                continue;
            }
        }
        
        /*if (DEBUG_OBJ_LEVEL_TMP)
            knd_log("  .. add ref: %s %p", ref->obj_id, refset);
        */
        
        err = refset->add_ref(refset, ref);
        if (DEBUG_OBJ_LEVEL_TMP)
            knd_log("  .. result: %d", err);
        if (err) {
            if (DEBUG_OBJ_LEVEL_TMP) {
                ref->str(ref, 1);
                knd_log("  -- ref \"%s\" not added to refset :(\n", ref->obj_id);
            }
            return err;
        }
        
    }
    
    
    return knd_OK;
}


extern void
kndObject_init(struct kndObject *self)
{
    self->del = del;
    self->str = str;

    //self->flatten = kndObject_flatten;
    //self->match = kndObject_match;
    self->contribute = kndObject_contribute;

    self->parse = parse_GSL;
    self->read = parse_GSL;
    self->resolve = kndObject_resolve;
    self->export = kndObject_export;
    self->sync = kndObject_sync;
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
