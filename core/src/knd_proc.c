#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_proc.h"
#include "knd_task.h"
#include "knd_mempool.h"
#include "knd_output.h"
#include "knd_utils.h"
#include "knd_text.h"
#include "knd_dict.h"
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
    knd_log("PROC: %.*s", self->name_size, self->name);
}

static int run_set_name(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndProc *self = obj;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE) return knd_LIMIT;

    memcpy(self->name, name, name_size);
    self->name_size = name_size;

    return knd_OK;
}

static int run_set_translation_text(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndTranslation *tr = (struct kndTranslation*)obj;
    struct kndTaskArg *arg;
    const char *val = NULL;
    size_t val_size = 0;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. run set translation text..");

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            val = arg->val;
            val_size = arg->val_size;
        }
    }
    if (!val_size) return knd_FAIL;
    if (val_size >= KND_NAME_SIZE) return knd_LIMIT;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. run set translation text: %.*s [%lu]\n", val_size, val,
                (unsigned long)val_size);

    memcpy(tr->val, val, val_size);
    tr->val_size = val_size;

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

static int read_gloss(void *obj,
                      const char *rec,
                      size_t *total_size)
{
    struct kndTranslation *tr = (struct kndTranslation*)obj;
    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_translation_text,
          .obj = tr
        }
    };
    int err;

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}

static int gloss_append(void *accu,
                        void *item)
{
    struct kndProc *self = accu;
    struct kndTranslation *tr = item;

    tr->next = self->tr;
    self->tr = tr;
   
    return knd_OK;
}

static int gloss_alloc(void *obj,
                       const char *name,
                       size_t name_size,
                       size_t count,
                       void **item)
{
    struct kndProc *self = obj;
    struct kndTranslation *tr;

    if (name_size > KND_LOCALE_SIZE) return knd_LIMIT;

    /* TODO: mempool alloc */
    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return knd_NOMEM;

    memset(tr, 0, sizeof(struct kndTranslation));
    memcpy(tr->curr_locale, name, name_size);
    tr->curr_locale_size = name_size;

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;
    *item = tr;

    return knd_OK;
}

static int import_proc(struct kndProc *self,
                      const char *rec,
                      size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndProc *proc;
    struct kndProcDir *dir;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. import Proc: \"%.*s\"..", 32, rec);

    err  = self->mempool->new_proc(self->mempool, &proc);
    if (err) return err;

    proc->out = self->out;
    proc->log = self->log;
    proc->task = self->task;
    proc->mempool = self->mempool;
    proc->proc_idx = self->proc_idx;
    proc->class_idx = self->class_idx;

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = proc
        }/*,
        { .type = KND_CHANGE_STATE,
          .name = "base",
          .name_size = strlen("base"),
          .parse = parse_baseclass,
          .obj = proc
          }*/,
        { .type = KND_CHANGE_STATE,
          .is_list = true,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .accu = proc,
          .alloc = gloss_alloc,
          .append = gloss_append,
          .parse = read_gloss
        },
        { .is_list = true,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .accu = proc,
          .alloc = gloss_alloc,
          .append = gloss_append,
          .parse = read_gloss
        }/*,
        { .type = KND_CHANGE_STATE,
          .name = "if",
          .name_size = strlen("if"),
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_NAME_SIZE,
          .is_validator = true,
          .validate = validate_proc_cond,
          .obj = proc
          }*/
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) goto final;

    if (!proc->name_size) {
        err = knd_FAIL;
        goto final;
    }

    dir = (struct kndProcDir*)self->proc_idx->get(self->proc_idx,
                                                  proc->name, proc->name_size);
    if (dir) {
        knd_log("-- %s proc name doublet found :(", proc->name);
        self->log->reset(self->log);
        err = self->log->write(self->log,
                               proc->name,
                               proc->name_size);
        if (err) goto final;
        err = self->log->write(self->log,
                               " proc name already exists",
                               strlen(" proc name already exists"));
        if (err) goto final;
        err = knd_FAIL;
        goto final;
    }

    if (!self->batch_mode) {
        proc->next = self->inbox;
        self->inbox = proc;
        self->inbox_size++;
    }

    dir = malloc(sizeof(struct kndProcDir));
    memset(dir, 0, sizeof(struct kndProcDir));
    dir->proc = proc;
    proc->dir = dir;
    err = self->proc_idx->set(self->proc_idx,
                              proc->name, proc->name_size, (void*)dir);
    if (err) goto final;

    if (DEBUG_PROC_LEVEL_TMP)
        proc->str(proc);

    return knd_OK;
 final:
    
    proc->del(proc);
    return err;
}

static int kndProc_resolve(struct kndProc *self)
{
    struct kndProcArg *arg;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. resolving PROC: %.*s",
                self->name_size, self->name);

    /*   for (arg = self->args; arg; arg = arg->next) {
        err = arg->resolve(arg);
        if (err) return err;
    }
    */
    return knd_OK;
}

static int resolve_procs(struct kndProc *self)
{
    struct kndProc *proc;
    struct kndProcDir *dir;
    const char *key;
    void *val;
    int err;

    if (DEBUG_PROC_LEVEL_TMP)
        knd_log(".. resolving procs by \"%.*s\"",
                self->name_size, self->name);
    key = NULL;
    self->proc_idx->rewind(self->proc_idx);
    do {
        self->proc_idx->next_item(self->proc_idx, &key, &val);
        if (!key) break;

        dir = (struct kndProcDir*)val;
        proc = dir->proc;
        if (proc->is_resolved) continue;

        err = proc->resolve(proc);
        if (err) {
            knd_log("-- couldn't resolve the \"%s\" proc :(", proc->name);
            return err;
        }
    } while (key);

    return knd_OK;
}

static int kndProc_coordinate(struct kndProc *self)
{
    struct kndProc *proc;
    struct kndProcDir *dir;
    const char *key;
    void *val;
    int err;

    if (DEBUG_PROC_LEVEL_TMP)
        knd_log(".. proc coordination in progress ..");

    err = resolve_procs(self);
    if (err) return err;

    /* assign ids */
    key = NULL;
    self->proc_idx->rewind(self->proc_idx);
    do {
        self->proc_idx->next_item(self->proc_idx, &key, &val);
        if (!key) break;

        dir = (struct kndProcDir*)val;
        proc = dir->proc;

        /* assign id */
        err = knd_next_state(self->mempool->next_proc_id);
        if (err) return err;
        
        memcpy(proc->id, self->mempool->next_proc_id, KND_ID_SIZE);
        proc->phase = KND_CREATED;
    } while (key);

    /* display all proces */
    if (DEBUG_PROC_LEVEL_TMP) {
        key = NULL;
        self->proc_idx->rewind(self->proc_idx);
        do {
            self->proc_idx->next_item(self->proc_idx, &key, &val);
            if (!key) break;
            dir = (struct kndProcDir*)val;
            proc = dir->proc;
            proc->depth = self->depth + 1;
            proc->str(proc);
        } while (key);
    }

    return knd_OK;
}


extern void 
kndProc_init(struct kndProc *self)
{
    self->del = del;
    self->str = str;
    self->export = export;
    self->import = import_proc;
    self->resolve = kndProc_resolve;
    self->coordinate = kndProc_coordinate;
    self->parse = parse_GSL;
    self->import = import_proc;
}

extern int 
kndProc_new(struct kndProc **proc)
{
    struct kndProc *self;

    self = malloc(sizeof(struct kndProc));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndProc));

    kndProc_init(self);

    *proc = self;
    return knd_OK;
}
