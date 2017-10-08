#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "knd_ref.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_output.h"
#include "knd_elem.h"
#include "knd_object.h"
#include "knd_objref.h"
#include "knd_utils.h"
#include "knd_concept.h"
#include "knd_attr.h"
#include "knd_parser.h"

#define DEBUG_REF_LEVEL_0 0
#define DEBUG_REF_LEVEL_1 0
#define DEBUG_REF_LEVEL_2 0
#define DEBUG_REF_LEVEL_3 0
#define DEBUG_REF_LEVEL_TMP 1

static void
del(struct kndRef *self)
{
    free(self);
}

static void str(struct kndRef *self)
{
    if (self->states) {
        knd_log("%*s%.*s -> \"%.*s\"", self->depth * KND_OFFSET_SIZE, "",
                self->elem->attr->name_size, self->elem->attr->name,
                self->states->val_size, self->states->val);
    }
}

static int
kndRef_set_reverse_rel(struct kndRef *self,
                   struct kndObject *obj)
{
    struct kndRelClass *relc;
    struct kndRelType *reltype;
    struct kndConcept *conc;

    conc = self->elem->attr->parent_conc;
    if (DEBUG_REF_LEVEL_2)
        knd_log(".. set REF from %s => %s",
                conc->name,
                obj->conc->name);
    for (relc = obj->reverse_rel_classes; relc; relc = relc->next) {
        if (relc->conc == conc) break;
    }

    /* add a relclass */
    if (!relc) {
        relc = malloc(sizeof(struct kndRelClass));
        if (!relc) return knd_NOMEM;
        memset(relc, 0, sizeof(struct kndRelClass));
        relc->conc = conc;
        relc->next = obj->reverse_rel_classes;
        obj->reverse_rel_classes = relc;
    }
    for (reltype = relc->rel_types; reltype; reltype = reltype->next) {
        if (reltype->attr == self->elem->attr) break;
    }

    /* add a reltype */
    if (!reltype) {
        reltype = malloc(sizeof(struct kndRelType));
        if (!reltype) return knd_NOMEM;
        memset(reltype, 0, sizeof(struct kndRelType));
        reltype->attr = self->elem->attr;
        reltype->next = relc->rel_types;
        relc->rel_types = reltype;
    }

    /*err = kndObjRef_new(&r);
    if (err) return knd_NOMEM;

    memcpy(r->obj_id, obj_id, KND_ID_SIZE);
    r->obj_id_size = KND_ID_SIZE;
    */
    
    if (!reltype->tail) {
        reltype->tail = self;
        reltype->refs = self;
    }
    else {
        reltype->tail->next = self;
        reltype->tail = self;
    }

    reltype->num_refs++;
    
    return knd_OK;
}


static int kndRef_resolve(struct kndRef *self)
{
    struct kndConcept *conc;
    struct kndObject *obj;
    const char *obj_name;
    int err, e;
    
    if (!self->states) return knd_FAIL;
    conc = self->elem->attr->conc;
     
    if (DEBUG_REF_LEVEL_2)
        knd_log(".. resolve REF: %s (%s) => %s",
                self->elem->attr->name,
                conc->name, self->states->val);

    obj_name = self->states->val;

    obj = (struct kndObject*)conc->dir->obj_idx->get(conc->dir->obj_idx, obj_name);
    if (!obj) {
        knd_log("-- no such obj: \"%s\" :(", obj_name);
        e = self->log->write(self->log, "no such obj: ", strlen("no such obj: "));
        if (e) return e;
        e = self->log->write(self->log, obj_name, self->states->val_size);
        if (e) return e;
        return knd_FAIL;
    }

    self->states->obj = obj;

    /* set reverse_rel */
    err = kndRef_set_reverse_rel(self, obj);
    if (err) return err;
    
    /*if (obj->num_reverse_rels >= KND_MAX_REVERSE_RELS) {
        knd_log("-- reverse_rels limit reached in %.*s :(", obj->name_size, obj->name);
        return knd_LIMIT;
    }
    
    obj->reverse_rels[obj->num_reverse_rels] = self;
    obj->num_reverse_rels++;

    knd_log("== %.*s obj num reverse_rels: %lu", obj->name_size, obj->name,
            (unsigned long)obj->num_reverse_rels);
    */
    
    return knd_OK;
}

static int run_set_val(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndRef *self = (struct kndRef*)obj;
    struct kndTaskArg *arg;
    struct kndRefState *state;
    const char *val = NULL;
    size_t val_size = 0;

    if (DEBUG_REF_LEVEL_2)
        knd_log(".. run set ref val..");

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            val = arg->val;
            val_size = arg->val_size;
        }
    }

    if (!val_size) return knd_FAIL;
    if (val_size >= KND_NAME_SIZE)
        return knd_LIMIT;

    state = malloc(sizeof(struct kndRefState));
    if (!state) return knd_NOMEM;
    memset(state, 0, sizeof(struct kndRefState));
    self->states = state;
    self->num_states = 1;

    memcpy(state->val, val, val_size);
    state->val[val_size] = '\0';
    state->val_size = val_size;

    return knd_OK;
}



static int 
export_reverse_rel_JSON(struct kndRef *self)
{
    struct kndObject *obj;
    struct kndOutput *out = self->out;
    int err = knd_FAIL;

    if (!self->elem) {
        err = out->write(out, "\"", 1);
        if (err) return err;
        err = out->write(out, self->name, self->name_size);
        if (err) return err;
        err = out->write(out, "\"", 1);
        if (err) return err;
        return knd_OK;
    }

    obj = self->elem->root;
    if (DEBUG_REF_LEVEL_2)
        knd_log(".. export reverse_rel to JSON..");

    obj->out = out;
    obj->depth = 0;
    err = obj->export(obj);
    if (err) return err;

    /*err = out->write(out, "\"", 1);
    if (err) return err;
    err = out->write(out, obj->name, obj->name_size);
    if (err) return err;

    err = out->write(out, "\"", 1);
    if (err) return err;
    */
    
    return knd_OK;
}

static int 
export_GSC(struct kndRef *self)
{
    /*    struct kndObject *obj;
    struct kndRefState *curr_state;
    struct kndTranslation *tr;
    
    struct kndRefSelect *sel;
    */
    
    if (DEBUG_REF_LEVEL_2)
        knd_log(".. export ref obj: %p  states: %p..", self->elem->obj, self->states);
    if (DEBUG_REF_LEVEL_2)
        knd_log("++ ref export OK!");

    return knd_OK;
}

static int 
export_reverse_rel_GSP(struct kndRef *self)
{
    struct kndObject *obj;
    struct kndOutput *out;
    int err = knd_FAIL;

    obj = self->elem->root;
    out = self->out;

    if (DEBUG_REF_LEVEL_2)
        knd_log(".. export reverse_rel to JSON..");

    obj->out = out;
    obj->depth = 0;
    /*err = obj->export(obj);
    if (err) return err;
    */

    err = out->write(out, "{", 1);
    if (err) return err;
    err = out->write(out, obj->id, KND_ID_SIZE);
    if (err) return err;

    err = out->write(out, " ", 1);
    if (err) return err;

    err = out->write(out, obj->name, obj->name_size);
    if (err) return err;

    err = out->write(out, "}", 1);
    if (err) return err;
    
    return knd_OK;
}

static int export(struct kndRef *self)
{
    int err;

    switch (self->format) {
    case KND_FORMAT_JSON:
        /*err = export_JSON(self);
          if (err) return err; */
        break;
    case KND_FORMAT_GSC:
        err = export_GSC(self);
        if (err) return err;
        break;
    default:
        break;
    }
    
    return knd_OK;
}

static int export_reverse_rel(struct kndRef *self)
{
    int err;

    switch (self->format) {
    case KND_FORMAT_JSON:
        err = export_reverse_rel_JSON(self);
        if (err) return err;
        break;
    case KND_FORMAT_GSP:
        err = export_reverse_rel_GSP(self);
        if (err) return err;
        break;
    default:
        break;
    }
    
    return knd_OK;
}


static int
parse_GSL(struct kndRef *self,
          const char *rec,
          size_t *total_size)
{
    if (DEBUG_REF_LEVEL_1)
        knd_log(".. parse REF field: \"%s\"..", rec);
    
    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_val,
          .obj = self
        }
    };
    int err;
    
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}

extern int 
kndRef_new(struct kndRef **ref)
{
    struct kndRef *self;
    
    self = malloc(sizeof(struct kndRef));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndRef));

    self->del = del;
    self->str = str;
    self->export = export;
    self->export_reverse_rel = export_reverse_rel;

    self->resolve = kndRef_resolve;
    self->parse = parse_GSL;
    //self->index = kndRef_index;

    *ref = self;
    return knd_OK;
}
