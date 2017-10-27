#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_proc.h"
#include "knd_task.h"
#include "knd_output.h"
#include "knd_utils.h"
#include "knd_parser.h"

#define DEBUG_PROC_LEVEL_0 0
#define DEBUG_PROC_LEVEL_1 0
#define DEBUG_PROC_LEVEL_2 0
#define DEBUG_PROC_LEVEL_3 0
#define DEBUG_PROC_LEVEL_TMP 1

static void
del(struct kndProc *self)
{
    free(self);
}

static void str(struct kndProc *self)
{
}

static int kndProc_resolve(struct kndProc *self)
{
    
    return knd_OK;
}

static int run_set_val(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndProc *self = (struct kndProc*)obj;
    struct kndTaskArg *arg;
    struct kndProcState *state;
    const char *val = NULL;
    size_t val_size = 0;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. run set proc val..");

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

    state = malloc(sizeof(struct kndProcState));
    if (!state) return knd_NOMEM;
    memset(state, 0, sizeof(struct kndProcState));
    self->states = state;
    self->num_states = 1;

    memcpy(state->val, val, val_size);
    state->val[val_size] = '\0';
    state->val_size = val_size;

    return knd_OK;
}

static int export_GSP(struct kndProc *self)
{
    struct kndOutput *out = self->out;
    int err;


    return knd_OK;
}

static int export(struct kndProc *self)
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

static int
parse_GSL(struct kndProc *self,
          const char *rec,
          size_t *total_size)
{
    if (DEBUG_PROC_LEVEL_1)
        knd_log(".. parse PROC field: \"%s\"..", rec);
    
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
kndProc_new(struct kndProc **proc)
{
    struct kndProc *self;

    self = malloc(sizeof(struct kndProc));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndProc));

    self->del = del;
    self->str = str;
    self->export = export;
    self->resolve = kndProc_resolve;
    self->parse = parse_GSL;

    *proc = self;
    return knd_OK;
}
