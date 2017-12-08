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
export_reverse_rels_JSON(struct kndObject *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndRelRef *relref;
    struct kndRel *rel;
    struct kndRelArgInstance *rel_arg_inst;
    struct kndRelArgInstRef *rel_arg_inst_ref;

    struct kndOutput *out = self->out;
    int err;

    /* sort refs by class */
    if (DEBUG_OBJ_LEVEL_TMP)
        knd_log("..export reverse_rels of %.*s..",
		self->name_size, self->name);

    err = out->write(out, ",\"_rels\":[", strlen(",\"_rels\":["));
    if (err) return err;

    /* class conc */
    for (relref = self->entry->reverse_rels; relref; relref = relref->next) {
        err = out->write(out, "{\"n\":\"", strlen("{\"n\":\""));
        if (err) return err;

	/* TODO: defreeze */
        if (!relref->rel) return knd_FAIL;

	rel = relref->rel;
	err = out->write(out, rel->name, rel->name_size);
	if (err) return err;

        err = out->write(out, "\"", 1);
        if (err) return err;

        err = out->write(out, ",\"instances\":[", strlen(",\"instances\":["));
        if (err) return err;

        for (rel_arg_inst_ref = relref->insts;
	     rel_arg_inst_ref;
	     rel_arg_inst_ref = rel_arg_inst_ref->next) {

	    rel_arg_inst = rel_arg_inst_ref->inst;

	    buf_size = snprintf(buf, KND_NAME_SIZE, "\"%p\"", (void*)rel_arg_inst);
	    err = out->write(out, buf, buf_size);
	    if (err) return err;

            /*for (ref = reltype->refs; ref; ref = ref->next) {
                ref->out = out;
                ref->log = self->log;
                err = ref->export_reverse_rel(ref);
                if (err) return err;

                if (ref->next) {
                    err = out->write(out, ",", 1);
                    if (err) return err;
                }
		}*/
	    if (rel_arg_inst_ref->next) {
		err = out->write(out, ",", 1);
		if (err) return err;
	    }
	}
	err = out->write(out, "]", 1);
	if (err) return err;
	err = out->write(out, "}", 1);
	if (err) return err;

        if (relref->next) {
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
    if (self->entry->reverse_rels) {
        err = export_reverse_rels_JSON(self);
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
kndObject_export_GSP(struct kndObject *self)
{
    struct kndElem *elem;
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
            knd_log("-- export of \"%s\" elem failed: %d :(",
		    elem->attr->name, err);
            return err;
        }
    }

    /* reverse_rels */
    /*if (self->reverse_rel_classes) {
        err = kndObject_export_reverse_rels_GSP(self);
        if (err) return err;
    }
    */

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
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;
    size_t name_hash = 0;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val_ref;
            name_size = arg->val_size;
            name_hash = arg->hash_val;
        }
    }
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE) return knd_LIMIT;

    /* check name doublets */
    conc = self->conc;
    if (conc->dir && conc->dir->obj_idx) {
        if (conc->dir->obj_idx->exists(conc->dir->obj_idx, name, name_size)) {
            knd_log("-- obj name doublet found: %.*s:(",
                    name_size, name);
            return knd_EXISTS;
        }
    }

    self->name = name;
    self->name_size = name_size;
    self->name_hash = name_hash;

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

    if (DEBUG_OBJ_LEVEL_2)
        knd_log(".. present obj..");

    err = kndObject_export(self);
    if (err) return err;

    return knd_OK;
}

static int confirm_obj_import(void *data,
                              struct kndTaskArg *args __attribute__((unused)),
                              size_t num_args __attribute__((unused)))
{
    struct kndObject *self = data;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log(".. confirm obj import.. %p", self);

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

    if (DEBUG_OBJ_LEVEL_2)
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

    if (DEBUG_OBJ_LEVEL_2) {
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
        knd_log("..  validating \"%.*s\" elem,   REC: \"%.*s\"\n",
                name_size, name, 32, rec);
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
        knd_log("   == basic elem type: %s   obj phase: %d mempool:%p",
                knd_attr_names[attr->type], self->phase, self->mempool);

    switch (attr->type) {
    case KND_ATTR_AGGR:
        err = self->mempool->new_obj(self->mempool, &obj);
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

    if (DEBUG_OBJ_LEVEL_3)
        knd_log("++ elem %.*s parsing OK!",
                elem->attr->name_size, elem->attr->name);
    return knd_OK;

 final:

    knd_log("-- validation of \"%s\" elem failed :(", name);

    elem->del(elem);
    return err;
}

/*
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
*/

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

/*static int objref_append(void *accu,
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
        knd_log(".. create objref: %.*s count: %zu %p",
                name_size, name, count, self);
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

    memcpy(reltype->name, name, name_size);
    reltype->name_size = name_size;
    reltype->name[name_size] = '\0';

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
        knd_log(".. create rev_rel: %.*s count: %zu  %p",
                name_size, name, count, self);
    if (name_size > KND_ID_SIZE) return knd_LIMIT;

    relc = malloc(sizeof(struct kndRelClass));
    if (!relc) return knd_NOMEM;

    memset(relc, 0, sizeof(struct kndRelClass));
    memcpy(relc->id, name, name_size);

    *item = relc;

    return knd_OK;
}
*/

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
        }/*,
        { .is_list = true,
          .name = "_rev",
          .name_size = strlen("_rev"),
          .accu = self,
          .alloc = rev_rel_alloc,
          .append = rev_rel_append,
          .parse = rev_rel_read
	  }*/,
        { .name = "default",
          .name_size = strlen("default"),
          .is_default = true,
          .run = run_present_obj,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "default",
          .name_size = strlen("default"),
          .is_default = true,
          .run = confirm_obj_import,
          .obj = self
        }
    };
    int err;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log(".. parsing obj REC: %.*s", 64, rec);
 
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

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
        err = elem->resolve(elem);
        if (err) return err;
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
