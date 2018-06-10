#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_ref.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_elem.h"
#include "knd_object.h"
#include "knd_utils.h"
#include "knd_concept.h"
#include "knd_attr.h"


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

/*static int
kndRef_set_reverse_rel(struct kndRef *self,
                   struct kndObject *obj)
{
    struct kndRelClass *relc;
    struct kndRelType *reltype;
    struct kndClass *conc;

    conc = self->elem->attr->parent_conc;
    if (DEBUG_REF_LEVEL_2)
        knd_log(".. %.*s OBJ to get REF from %s => %s",
                obj->name_size, obj->name,
                conc->name,
                obj->conc->name);

    for (relc = obj->reverse_rel_classes; relc; relc = relc->next) {
        if (relc->conc == conc) break;
    }


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


    if (!reltype) {
        reltype = malloc(sizeof(struct kndRelType));
        if (!reltype) return knd_NOMEM;
        memset(reltype, 0, sizeof(struct kndRelType));
        reltype->attr = self->elem->attr;
        reltype->next = relc->rel_types;
        relc->rel_types = reltype;
    }

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
*/

 /*static int kndRef_resolve(struct kndRef *self)
{
    struct kndClass *conc;
    struct kndObjEntry *entry;
    struct kndObject *obj;
    const char *obj_name;
    int err, e;

    conc = self->elem->attr->conc;

    if (DEBUG_REF_LEVEL_2) {
        knd_log(".. resolve REF: %.*s  state:%p",
                conc->name_size, conc->name,
                self->states);

        knd_log(".. resolve REF: %.*s::%.*s => \"%.*s\" dir: %p",
                conc->name_size, conc->name,
                self->elem->attr->name_size, self->elem->attr->name,
                self->states->val_size, self->states->val,
                conc->dir);
    }

    if (!conc->dir || !conc->dir->obj_idx) {
        knd_log("-- \"%.*s\" class has no obj idx, unable to resolve ref: \"%.*s\" :(",
                conc->name_size, conc->name,
                self->elem->attr->name_size, self->elem->attr->name);
        return knd_FAIL;
    }

    obj_name = self->states->val;
    entry = conc->dir->obj_idx->get(conc->dir->obj_idx, obj_name, self->states->val_size);
    if (!entry) {
        knd_log("-- no such obj: \"%s\" :(", obj_name);

        e = self->log->write(self->log, "no such obj: ", strlen("no such obj: "));
        if (e) return e;
        e = self->log->write(self->log, obj_name, self->states->val_size);
        if (e) return e;
        return knd_FAIL;
    }

    obj = entry->obj;
    self->states->obj = obj;

    err = kndRef_set_reverse_rel(self, obj);
    if (err) return err;

    return knd_OK;
}
 */

static gsl_err_t run_set_val(void *obj, const char *val, size_t val_size)
{
    struct kndRef *self = (struct kndRef*)obj;
    struct kndRefState *state;

    if (DEBUG_REF_LEVEL_2)
        knd_log(".. run set ref val..");

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    state = malloc(sizeof(struct kndRefState));
    if (!state) return make_gsl_err_external(knd_NOMEM);
    memset(state, 0, sizeof(struct kndRefState));
    self->states = state;
    self->num_states = 1;

    memcpy(state->val, val, val_size);
    state->val[val_size] = '\0';
    state->val_size = val_size;

    return make_gsl_err(gsl_OK);
}

static int
export_reverse_rel_JSON(struct kndRef *self)
{
    struct kndObject *obj;
    struct glbOutput *out = self->out;
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

static int export_GSP(struct kndRef *self)
{
    struct kndObject *obj;
    struct glbOutput *out = self->out;
    int err;

    if (!self->states) return knd_FAIL;

    obj = self->elem->root;


    err = out->write(out, "{", 1);
    if (err) return err;
    err = out->write(out, self->states->val, self->states->val_size);
    if (err) return err;
    err = out->write(out, "{c ", strlen("{c "));
    if (err) return err;

    err = out->write(out, obj->conc->name, obj->conc->name_size);
    if (err) return err;

    err = out->write(out, "}}", strlen("}}"));
    if (err) return err;

    return knd_OK;
}

static int
export_reverse_rel_GSP(struct kndRef *self)
{
    struct kndObject *obj;
    struct glbOutput *out;
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
    case KND_FORMAT_GSP:
        err = export_GSP(self);
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
    if (DEBUG_REF_LEVEL_TMP)
        knd_log(".. parse REF field: \"%s\"..", rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_val,
          .obj = self
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

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
    self->parse = parse_GSL;

    *ref = self;
    return knd_OK;
}
