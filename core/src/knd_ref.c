#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_ref.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_output.h"
#include "knd_conc.h"
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

    struct kndRefState *curr_state;
  
    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    if (self->user_name_size) {
        knd_log("%sUSER REF: %s [%p]", offset, self->user_name, self->user);
    }

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




static int
kndRef_run_get_user(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndRef *self;
    struct kndUser *user;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;
    
    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "un", strlen("un"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }

    if (!name_size) return knd_FAIL;

    self = (struct kndRef*)obj;
    
    if (DEBUG_REF_LEVEL_TMP) {
        knd_log(".. get user: \"%s\"",
                name);
    }
    
    
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



static int 
export(struct kndRef *self,
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

    self->user_name_size = KND_NAME_SIZE;
    
    struct kndTaskSpec specs[] = {
        { .name = "un",
          .name_size = strlen("un"),
          .buf = self->user_name,
          .buf_size = &self->user_name_size,
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
    self->parse = parse_GSL;
    self->index = kndRef_index;

    *ref = self;
    return knd_OK;
}
