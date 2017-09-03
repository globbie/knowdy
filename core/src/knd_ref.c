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

static void str(struct kndRef *self, size_t depth)
{
    size_t offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;
    char *offset = malloc(offset_size + 1);
    if (!offset) return;
    
    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    if (self->states) {
        knd_log("%s%s -> \"%s\"", offset,
                self->elem->attr->name, self->states->val);
    }
    free(offset);
}


static int kndRef_resolve(struct kndRef *self)
{
    struct kndConcept *conc;
    struct kndObject *obj;
    const char *obj_name;
    int e;
    
    if (!self->states) return knd_FAIL;
    conc = self->elem->attr->conc;
     
    if (DEBUG_REF_LEVEL_TMP)
        knd_log(".. resolve REF: %s (%s) => %s",
                self->elem->attr->name,
                conc->name, self->states->val);

    obj_name = self->states->val;
    
    obj = (struct kndObject*)conc->obj_idx->get(conc->obj_idx, obj_name);
    if (!obj) {
        knd_log("-- no such obj: \"%s\" :(", obj_name);
        e = self->log->write(self->log, "no such obj: ", strlen("no such obj: "));
        if (e) return e;
        e = self->log->write(self->log, obj_name, self->states->val_size);
        if (e) return e;
        return knd_FAIL;
    }

    self->states->obj = obj;

    /* set backref */
    if (obj->num_backrefs >= KND_MAX_BACKREFS) {
        knd_log("-- backrefs limit reached in %.*s :(", obj->name_size, obj->name);
        return knd_LIMIT;
    }
    
    obj->backrefs[obj->num_backrefs] = self;
    obj->num_backrefs++;

    knd_log("== %.*s obj num backrefs: %lu", obj->name_size, obj->name,
            (unsigned long)obj->num_backrefs);

    return knd_OK;
}

static int kndRef_index(struct kndRef *self)
{
    /*    char buf[KND_LARGE_BUF_SIZE];
    size_t buf_size;

    struct kndObject *obj;
    struct kndRefState *curr_state;
    struct kndTranslation *tr;
    int err = knd_FAIL;

    obj = self->elem->obj;
    curr_state = self->states;
    
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
    int err;

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
export_JSON(struct kndRef *self)
{
    /*    char buf[KND_NAME_SIZE];
    size_t buf_size;

    struct kndObject *obj;
    struct kndRefState *curr_state;
    struct kndOutput *out;
    
    int num_trs = 0;

    int err = knd_FAIL;

    obj = self->elem->obj;
    out = self->out;

    if (DEBUG_REF_LEVEL_2)
        knd_log(".. export ref to JSON..");

    curr_state = self->states;
    */

    return knd_OK;
}


static int 
export_GSC(struct kndRef *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;

    struct kndObject *obj;
    struct kndRefState *curr_state;
    struct kndTranslation *tr;
    
    struct kndRefSelect *sel;
    size_t curr_size;
    
    int err = knd_FAIL;

    if (DEBUG_REF_LEVEL_2)
        knd_log(".. export ref obj: %p  states: %p..", self->elem->obj, self->states);
  
    if (DEBUG_REF_LEVEL_2)
        knd_log("++ ref export OK!");

    return knd_OK;
}

static int export(struct kndRef *self,
                  knd_format format)
{
    int err;

    switch(format) {
    case KND_FORMAT_JSON:
        err = export_JSON(self);
        if (err) return err;
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

    self->resolve = kndRef_resolve;
    self->parse = parse_GSL;
    self->index = kndRef_index;

    *ref = self;
    return knd_OK;
}
