#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_task.h"
#include "knd_state.h"
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
    struct kndTranslation *tr;
    struct kndProcArg *arg;

    knd_log("PROC: %.*s", self->name_size, self->name);

    for (tr = self->tr; tr; tr = tr->next) {
        knd_log("%*s~ %s %.*s", (self->depth + 1) * KND_OFFSET_SIZE, "",
                tr->locale, tr->val_size, tr->val);
    }

    for (arg = self->args; arg; arg = arg->next) {
	arg->str(arg);
    }
}

static int run_set_translation_text(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndTranslation *tr = obj;
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


static int export_GSP(struct kndProc *self)
{
    struct kndOutput *out = self->out;
    int err = 0;

    if (DEBUG_PROC_LEVEL_TMP)
	knd_log("%.*s : %d", out->buf_size, out->buf, err);

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
          .buf = self->name,
          .buf_size = &self->name_size,
          .max_buf_size = KND_NAME_SIZE
        }
    };
    int err;
    
    err = knd_parse_task(rec, total_size, specs,
			 sizeof(specs) / sizeof(struct kndTaskSpec));             RET_ERR();

    knd_log("PROC: %.*s", self->name_size, self->name);

    return knd_OK;
}

static int read_gloss(void *obj,
                      const char *rec,
                      size_t *total_size)
{
    struct kndTranslation *tr = obj;
    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .buf = tr->val,
          .buf_size = &tr->val_size,
          .max_buf_size = KND_NAME_SIZE
        }
    };
    int err;

    err = knd_parse_task(rec, total_size, specs,
			 sizeof(specs) / sizeof(struct kndTaskSpec));             RET_ERR();
    
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

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. alloc gloss.. %zu", count);

    /* TODO: mempool alloc */
    //self->mempool->new_text_seq();
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

static int parse_arg(void *data,
		     const char *rec,
		     size_t *total_size)
{
    struct kndProc *self = data;
    struct kndProcArg *arg;
    int err;
    err = self->mempool->new_proc_arg(self->mempool, &arg);                       RET_ERR();
    arg->task = self->task;
    err = arg->parse(arg, rec, total_size);                                       PARSE_ERR();

    arg->parent = self;
    arg->next = self->args;
    self->args = arg;
    self->num_args++;
    return knd_OK;
}

static int import_proc(struct kndProc *self,
		       const char *rec,
		       size_t *total_size)
{
    struct kndProc *proc;
    struct kndProcDir *dir;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. import Proc: \"%.*s\"..", 32, rec);

    err  = self->mempool->new_proc(self->mempool, &proc);                         RET_ERR();
    proc->out = self->out;
    proc->log = self->log;
    proc->task = self->task;
    proc->mempool = self->mempool;
    proc->proc_idx = self->proc_idx;
    proc->class_idx = self->class_idx;

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .buf = proc->name,
          .buf_size = &proc->name_size,
          .max_buf_size = KND_NAME_SIZE
        },
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
        },
        { .type = KND_CHANGE_STATE,
          .name = "arg",
          .name_size = strlen("arg"),
          .parse = parse_arg,
          .obj = proc
        }
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

    err = self->mempool->new_proc_dir(self->mempool, &dir);                       RET_ERR();
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
    struct kndProcArg *arg = NULL;
    int err;

    if (DEBUG_PROC_LEVEL_TMP)
        knd_log(".. resolving PROC: %.*s",
                self->name_size, self->name);

    for (arg = self->args; arg; arg = arg->next) {
        err = arg->resolve(arg);                                                  RET_ERR();
    }

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

    /* display all procs */
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


static int update_state(struct kndProc *self,
			struct kndUpdate *update)
{
    struct kndProc *proc;
    struct kndProcUpdate *proc_update;
    struct kndProcUpdate **proc_updates;
    int err;

    /* create index of PROC updates */
    proc_updates = realloc(update->procs,
                          (self->inbox_size * sizeof(struct kndProcUpdate*)));
    if (!proc_updates) return knd_NOMEM;
    update->procs = proc_updates;

    for (proc = self->inbox; proc; proc = proc->next) {
        err = proc->resolve(proc);                                                  RET_ERR();
        err = self->mempool->new_proc_update(self->mempool, &proc_update);          RET_ERR();

        /*self->next_id++;
        proc->id = self->next_id;
	*/

        proc_update->proc = proc;

        update->procs[update->num_procs] = proc_update;
        update->num_procs++;
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
    self->update = update_state;
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
