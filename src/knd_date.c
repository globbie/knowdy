#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>
#include <glb-lib/output.h>

#include "knd_date.h"
#include "knd_repo.h"
#include "knd_elem.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_user.h"

#define DEBUG_DATE_LEVEL_0 0
#define DEBUG_DATE_LEVEL_1 0
#define DEBUG_DATE_LEVEL_2 0
#define DEBUG_DATE_LEVEL_3 0
#define DEBUG_DATE_LEVEL_TMP 1

static void
kndDate_del(struct kndDate *self)
{
    free(self);
}

static void str(struct kndDate *self)
{
    knd_log("%*s%.*s => %.*s",
                self->depth * KND_OFFSET_SIZE, "",
            self->elem->attr->name_size, self->elem->attr->name,
            self->states->val_size, self->states->val);
}

static int 
kndDate_index(struct kndDate *unused_var(self))
{
    //char buf[KND_LARGE_BUF_SIZE];
    //size_t buf_size;

    //struct kndClassInst *obj;
    //struct kndDateState *curr_state;
    
    //int err = knd_FAIL;

    //obj = self->elem->obj;

    //curr_state = self->states;

    return knd_OK;
}


static int 
kndDate_export(struct kndDate *unused_var(self), knd_format unused_var(format))
{
    //char buf[KND_LARGE_BUF_SIZE];
    //size_t buf_size;

    //struct kndDateState *curr_state;

    //struct kndClassInst *obj;
    int err = knd_FAIL;

    //obj = self->elem->obj;

    //curr_state = self->states;

    //err = knd_OK;
    
    return err;
}

static gsl_err_t run_set_val(void *obj, const char *val, size_t val_size)
{
    struct kndDate *self = obj;
    struct kndDateState *state;
    int err;

    if (DEBUG_DATE_LEVEL_1)
        knd_log(".. run set date val..");

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size >= KND_VAL_SIZE) return make_gsl_err(gsl_LIMIT);

    state = malloc(sizeof(struct kndDateState));
    if (!state) return make_gsl_err_external(knd_NOMEM);
    memset(state, 0, sizeof(struct kndDateState));
    self->states = state;
    self->date_states = 1;

    err = knd_parse_date((const char*)val, &state->dateval);
    if (err) return make_gsl_err_external(err);

    memcpy(state->val, val, val_size);
    state->val[val_size] = '\0';
    state->val_size = val_size;
 
    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_GSL(struct kndDate *self,
                           const char *rec,
                           size_t *total_size)
{
   if (DEBUG_DATE_LEVEL_1)
       knd_log(".. parse DATE field: \"%.*s\"..", 16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_val,
          .obj = self
        }
    };
    
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

extern int 
kndDate_new(struct kndDate **date)
{
    struct kndDate *self;
    
    self = malloc(sizeof(struct kndDate));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndDate));

    self->del = kndDate_del;
    self->str = str;
    self->export = kndDate_export;
    self->parse = parse_GSL;
    self->index = kndDate_index;

    *date = self;

    return knd_OK;
}
