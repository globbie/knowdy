#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>
#include <glb-lib/output.h>

#include "knd_num.h"
#include "knd_repo.h"
#include "knd_elem.h"
#include "knd_attr.h"
#include "knd_object.h"
#include "knd_task.h"
#include "knd_user.h"

#define DEBUG_NUM_LEVEL_0 0
#define DEBUG_NUM_LEVEL_1 0
#define DEBUG_NUM_LEVEL_2 0
#define DEBUG_NUM_LEVEL_3 0
#define DEBUG_NUM_LEVEL_TMP 1

static int 
kndNum_del(struct kndNum *self)
{

    free(self);

    return knd_OK;
}

static void str(struct kndNum *self)
{
    knd_log("%*s%.*s = %.*s",
                self->depth * KND_OFFSET_SIZE, "",
            self->elem->attr->name_size, self->elem->attr->name,
            self->states->val_size, self->states->val);
}

static int 
kndNum_index(struct kndNum *self __attribute__((unused)))
{
    //char buf[KND_LARGE_BUF_SIZE];
    //size_t buf_size;

    //struct kndObject *obj;
    //struct kndNumState *curr_state;
    
    //int err = knd_FAIL;

    //obj = self->elem->obj;

    //curr_state = self->states;

    return knd_OK;
}


static int 
kndNum_export(struct kndNum *self __attribute__((unused)), knd_format format __attribute__((unused)))
{
    //char buf[KND_LARGE_BUF_SIZE];
    //size_t buf_size;

    //struct kndNumState *curr_state;

    //struct kndObject *obj;
    int err = knd_FAIL;

    //obj = self->elem->obj;

    //curr_state = self->states;

    //err = knd_OK;
    
    return err;
}

static gsl_err_t run_set_val(void *obj, const char *val, size_t val_size)
{
    struct kndNum *self = (struct kndNum*)obj;
    struct kndNumState *state;
    int err;

    if (DEBUG_NUM_LEVEL_1)
        knd_log(".. run set num val..");

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size >= KND_VAL_SIZE) return make_gsl_err(gsl_LIMIT);

    state = malloc(sizeof(struct kndNumState));
    if (!state) return make_gsl_err_external(knd_NOMEM);
    memset(state, 0, sizeof(struct kndNumState));
    self->states = state;
    self->num_states = 1;

    err = knd_parse_num((const char*)val, &state->numval);
    if (err) return make_gsl_err_external(err);

    memcpy(state->val, val, val_size);
    state->val[val_size] = '\0';
    state->val_size = val_size;
 
    return make_gsl_err(gsl_OK);
}


static int parse_GSL(struct kndNum *self,
                     const char *rec,
                     size_t *total_size)
{
   gsl_err_t parser_err;

   if (DEBUG_NUM_LEVEL_1)
       knd_log(".. parse NUM field: \"%.*s\"..", 16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_val,
          .obj = self
        }
    };
    
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);
    
    return knd_OK;
}

extern int 
kndNum_new(struct kndNum **num)
{
    struct kndNum *self;
    
    self = malloc(sizeof(struct kndNum));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndNum));

    self->del = kndNum_del;
    self->str = str;
    self->export = kndNum_export;
    self->parse = parse_GSL;
    self->index = kndNum_index;

    *num = self;

    return knd_OK;
}
