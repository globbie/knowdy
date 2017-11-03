#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_rel.h"
#include "knd_rel_arg.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_output.h"
#include "knd_object.h"
#include "knd_utils.h"
#include "knd_concept.h"
#include "knd_mempool.h"
#include "knd_parser.h"
#include "knd_text.h"

#define DEBUG_REL_LEVEL_0 0
#define DEBUG_REL_LEVEL_1 0
#define DEBUG_REL_LEVEL_2 0
#define DEBUG_REL_LEVEL_3 0
#define DEBUG_REL_LEVEL_TMP 1

static void
del(struct kndRel *self __attribute__((unused)))
{
}

static void str(struct kndRel *self)
{
    struct kndRelArg *arg;

    knd_log("\n%*sREL: %.*s", self->depth * KND_OFFSET_SIZE, "", self->name_size, self->name);

    for (arg = self->args; arg; arg = arg->next) {
        arg->depth = self->depth + 1;
        arg->str(arg);
    }
    
}

static int run_set_translation_text(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndTranslation *tr = (struct kndTranslation*)obj;
    struct kndTaskArg *arg;
    const char *val = NULL;
    size_t val_size = 0;

    if (DEBUG_REL_LEVEL_2)
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

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. run set translation text: %.*s [%lu]\n", val_size, val,
                (unsigned long)val_size);

    memcpy(tr->val, val, val_size);
    tr->val_size = val_size;

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

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. reading gloss translation: \"%.*s\"",
                tr->locale_size, tr->locale);

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}

static int gloss_append(void *accu,
                        void *item)
{
    struct kndRel *self = accu;
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
    struct kndRel *self = obj;
    struct kndTranslation *tr;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. %.*s to create gloss: %.*s count: %zu",
                self->name_size, self->name, name_size, name, count);

    if (name_size > KND_LOCALE_SIZE) return knd_LIMIT;

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

static int run_set_name(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndRel *self = obj;
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

static int run_set_val(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndRel *self = (struct kndRel*)obj;
    struct kndTaskArg *arg;
    struct kndRelState *state;
    const char *val = NULL;
    size_t val_size = 0;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. run set rel val..");

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

    state = malloc(sizeof(struct kndRelState));
    if (!state) return knd_NOMEM;
    memset(state, 0, sizeof(struct kndRelState));
    self->states = state;
    self->num_states = 1;

    memcpy(state->val, val, val_size);
    state->val[val_size] = '\0';
    state->val_size = val_size;

    return knd_OK;
}

static int export_GSP(struct kndRel *self)
{
    struct kndObject *obj;
    struct kndOutput *out = self->out;
    int err;

    /*if (!self->states) return knd_FAIL;

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
    */
    return knd_OK;
}

static int 
export_reverse_rel_GSP(struct kndRel *self)
{
    struct kndObject *obj;
    struct kndOutput *out;
    int err = knd_FAIL;

    /* obj = self->elem->root;
    out = self->out;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. export reverse_rel to JSON..");

    obj->out = out;
    obj->depth = 0;

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
    */
    
    return knd_OK;
}

static int export(struct kndRel *self)
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

static int export_reverse_rel(struct kndRel *self)
{
    int err;

    switch (self->format) {
    case KND_FORMAT_JSON:
        /*err = export_reverse_rel_JSON(self);
          if (err) return err; */
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

static int validate_rel_arg(void *obj,
                            const char *name, size_t name_size,
                            const char *rec,
                            size_t *total_size)
{
    struct kndRel *self = obj;
    struct kndRelArg *arg;
    int err;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. parsing the \"%.*s\" rel arg, rec:\"%.*s\"", name_size, name, 32, rec);

    err = kndRelArg_new(&arg);
    if (err) return err;
    arg->rel = self;

    if (!strncmp(name, "subj", strlen("subj"))) {
        arg->type = KND_RELARG_SUBJ;
    } else if (!strncmp(name, "obj", strlen("obj"))) {
        arg->type = KND_RELARG_OBJ;
    } else if (!strncmp(name, "ins", strlen("ins"))) {
        arg->type = KND_RELARG_INS;
    }

    err = arg->parse(arg, rec, total_size);
    if (err) {
        if (DEBUG_REL_LEVEL_TMP)
            knd_log("-- failed to parse rel arg: %d", err);
        return err;
    }

    if (!self->tail_arg) {
        self->tail_arg = arg;
        self->args = arg;
    }
    else {
        self->tail_arg->next = arg;
        self->tail_arg = arg;
    }

    if (DEBUG_REL_LEVEL_2)
        arg->str(arg);

    return knd_OK;
}

static int import_rel(struct kndRel *self,
                      const char *rec,
                      size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndRel *rel;
    struct kndRelDir *dir;
    int err;

    if (DEBUG_REL_LEVEL_2)
        knd_log(".. import Rel: \"%.*s\"..", 32, rec);

    err  = self->mempool->new_rel(self->mempool, &rel);
    if (err) return err;

    rel->out = self->out;
    rel->log = self->log;
    rel->task = self->task;
    rel->mempool = self->mempool;
    rel->rel_idx = self->rel_idx;

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = rel
        }/*,
        { .type = KND_CHANGE_STATE,
          .name = "base",
          .name_size = strlen("base"),
          .parse = parse_baseclass,
          .obj = rel
          }*/,
        { .type = KND_CHANGE_STATE,
          .is_list = true,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .accu = rel,
          .alloc = gloss_alloc,
          .append = gloss_append,
          .parse = read_gloss
        },
        { .is_list = true,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .accu = rel,
          .alloc = gloss_alloc,
          .append = gloss_append,
          .parse = read_gloss
        },
        { .type = KND_CHANGE_STATE,
          .name = "arg",
          .name_size = strlen("arg"),
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_NAME_SIZE,
          .is_validator = true,
          .validate = validate_rel_arg,
          .obj = rel
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) goto final;

    if (!rel->name_size) {
        err = knd_FAIL;
        goto final;
    }

    dir = (struct kndRelDir*)self->rel_idx->get(self->rel_idx,
                                                rel->name, rel->name_size);
    if (dir) {
        knd_log("-- %s relation name doublet found :(", rel->name);
        self->log->reset(self->log);
        err = self->log->write(self->log,
                               rel->name,
                               rel->name_size);
        if (err) goto final;
        err = self->log->write(self->log,
                               " relation name already exists",
                               strlen(" relation name already exists"));
        if (err) goto final;
        err = knd_FAIL;
        goto final;
    }

    if (!self->batch_mode) {
        rel->next = self->inbox;
        self->inbox = rel;
        self->inbox_size++;
    }

    dir = malloc(sizeof(struct kndRelDir));
    memset(dir, 0, sizeof(struct kndRelDir));
    dir->rel = rel;
    rel->dir = dir;
    err = self->rel_idx->set(self->rel_idx,
                             rel->name, rel->name_size, (void*)dir);
    if (err) goto final;

    if (DEBUG_REL_LEVEL_TMP)
        rel->str(rel);

    return knd_OK;
 final:
    
    rel->del(rel);
    return err;
}


extern void 
kndRel_init(struct kndRel *self)
{
    self->del = del;
    self->str = str;
    self->export = export;
    //self->resolve = kndRel_resolve;
    //self->parse = parse_GSL;
    self->import = import_rel;
}

extern int 
kndRel_new(struct kndRel **rel)
{
    struct kndRel *self;

    self = malloc(sizeof(struct kndRel));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndRel));
    
    kndRel_init(self);
    *rel = self;
    return knd_OK;
}
