#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_rel_arg.h"
#include "knd_rel.h"
#include "knd_task.h"
#include "knd_concept.h"
#include "knd_output.h"
#include "knd_text.h"
#include "knd_parser.h"

#define DEBUG_RELARG_LEVEL_1 0
#define DEBUG_RELARG_LEVEL_2 0
#define DEBUG_RELARG_LEVEL_3 0
#define DEBUG_RELARG_LEVEL_4 0
#define DEBUG_RELARG_LEVEL_5 0
#define DEBUG_RELARG_LEVEL_TMP 1

static void del(struct kndRelArg *self)
{
    free(self);
}

static void str(struct kndRelArg *self)
{
    struct kndTranslation *tr;

    const char *type_name = knd_relarg_names[self->type];

    knd_log("\n%*s{%s %.*s class:%.*s", self->depth * KND_OFFSET_SIZE, "", type_name,
            self->name_size, self->name,
            self->classname_size, self->classname);
    
    tr = self->tr;
    while (tr) {
        knd_log("%*s   ~ %s %s", self->depth * KND_OFFSET_SIZE, "", tr->locale, tr->val);
        tr = tr->next;
    }
    
}

/**
 *  EXPORT
 */
static int export_JSON(struct kndRelArg *self)
{
    struct kndOutput *out;
    struct kndTranslation *tr;
    
    const char *type_name = knd_relarg_names[self->type];
    size_t type_name_size = strlen(knd_relarg_names[self->type]);
    int err;

    out = self->out;

    err = out->write(out, "\"", 1);
    if (err) return err;
    err = out->write(out, self->name, self->name_size);
    if (err) return err;
    err = out->write(out, "\":{\"type\":\"", strlen("\":{\"type\":\""));
    if (err) return err;
    
    err = out->write(out, type_name, type_name_size);
    if (err) return err;

    err = out->write(out, "\"", 1);
    if (err) return err;
    
    /* choose gloss */
    tr = self->tr;
    while (tr) {
        if (DEBUG_RELARG_LEVEL_2)
            knd_log("LANG: %s == CURR LOCALE: %s [%lu] => %s",
                    tr->locale, self->locale, (unsigned long)self->locale_size, tr->val);

        if (strncmp(self->locale, tr->locale, tr->locale_size)) {
            goto next_tr;
        }
        
        err = out->write(out,
                         ",\"gloss\":\"", strlen(",\"gloss\":\""));
        if (err) return err;

        err = out->write(out, tr->val,  tr->val_size);
        if (err) return err;

        err = out->write(out, "\"", 1);
        if (err) return err;
        break;

    next_tr:
        tr = tr->next;
    }
    
    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}

static int export_GSP(struct kndRelArg *self)
{
    struct kndOutput *out;
    struct kndTranslation *tr;
    
    const char *type_name = knd_relarg_names[self->type];
    size_t type_name_size = strlen(knd_relarg_names[self->type]);
    int err;

    out = self->out;

    err = out->write(out, "{", 1);
    if (err) return err;
    err = out->write(out, type_name, type_name_size);
    if (err) return err;
    err = out->write(out, " ", 1);
    if (err) return err;
    err = out->write(out, self->name, self->name_size);
    if (err) return err;

    err = out->write(out, "{c ", strlen("{c "));
    if (err) return err;
    err = out->write(out, self->classname, self->classname_size);
    if (err) return err;
    err = out->write(out, "}", 1);
    if (err) return err;

    if (self->tr) {
        err = out->write(out,
                         "[_g", strlen("[_g"));
        if (err) return err;
    }
    
    for (tr = self->tr; tr; tr = tr->next) {
        err = out->write(out, "{", 1);
        if (err) return err;
        err = out->write(out, tr->locale,  tr->locale_size);
        if (err) return err;
        err = out->write(out, " ", 1);
        if (err) return err;
        err = out->write(out, tr->val,  tr->val_size);
        if (err) return err;
        err = out->write(out, "}", 1);
        if (err) return err;
    }
    if (self->tr) {
        err = out->write(out, "]", 1);
        if (err) return err;
    }
    
    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}

static int export(struct kndRelArg *self)
{
    int err = knd_FAIL;

    switch (self->format) {
    case KND_FORMAT_JSON:
        err = export_JSON(self);
        if (err) goto final;
        break;
    case KND_FORMAT_GSP:
        err = export_GSP(self);
        if (err) goto final;
        break;
    default:
        break;
    }

 final:
    return err;
}


static int run_set_name(void *obj,
                        struct kndTaskArg *args, size_t num_args)
{
    struct kndRelArg *self = (struct kndRelArg*)obj;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;
    
    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
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


static int run_set_translation_text(void *obj,
                                    struct kndTaskArg *args, size_t num_args)
{
    struct kndTranslation *tr = obj;
    struct kndTaskArg *arg;
    const char *val = NULL;
    size_t val_size = 0;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            val = arg->val;
            val_size = arg->val_size;
        }
    }
    if (!val_size) return knd_FAIL;
    if (val_size >= KND_NAME_SIZE) return knd_LIMIT;

    if (DEBUG_RELARG_LEVEL_2)
        knd_log(".. run set translation text: %s\n", val);

    memcpy(tr->val, val, val_size);
    tr->val[val_size] = '\0';
    tr->val_size = val_size;

    return knd_OK;
}

static int read_gloss(void *obj,
                      const char *rec,
                      size_t *total_size)
{
    struct kndTranslation *tr = obj;
    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_translation_text,
          .obj = tr
        }
    };
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. reading gloss translation: \"%.*s\"",
                tr->locale_size, tr->locale);

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
   
    return knd_OK;
}

static int gloss_append(void *accu,
                        void *item)
{
    struct kndRelArg *self = accu;
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
    struct kndRelArg *self = obj;
    struct kndTranslation *tr;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. %.*s: create gloss: %.*s count: %zu",
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


static int parse_GSL(struct kndRelArg *self,
                     const char *rec,
                     size_t *total_size)
{
    if (DEBUG_RELARG_LEVEL_2)
        knd_log(".. Rel Arg parsing: \"%.*s\"..", 32, rec);

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = self
        },
        { .is_list = true,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .accu = self,
          .alloc = gloss_alloc,
          .append = gloss_append,
          .parse = read_gloss
        },
        { .type = KND_CHANGE_STATE,
          .name = "c",
          .name_size = strlen("c"),
          .buf = self->classname,
          .buf_size = &self->classname_size,
          .max_buf_size = KND_NAME_SIZE
        }
    };
    int err;
    
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}

static int resolve(struct kndRelArg *self)
{
    struct kndRel *rel;
    struct kndConcept *c;
    
    c = self->rel->class_idx->get(self->rel->class_idx,
                                  self->classname, self->classname_size);
    if (!c) {
        knd_log("-- no such class: %.*s :(", self->classname_size, self->classname);
        return knd_FAIL;
    }

    self->conc = c;

    if (DEBUG_RELARG_LEVEL_1)
        knd_log("++ Rel Arg resolved: \"%.*s\"!",
                self->classname_size, self->classname);
  
    return knd_OK;
}


/*  RelArg Initializer */
static void init(struct kndRelArg *self)
{
    /* binding our methods */
    self->init = init;
    self->del = del;
    self->str = str;
    self->parse = parse_GSL;
    self->export = export;
    self->resolve = resolve;
}


extern int
kndRelArg_new(struct kndRelArg **c)
{
    struct kndRelArg *self;

    self = malloc(sizeof(struct kndRelArg));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndRelArg));

    init(self);
    *c = self;

    return knd_OK;
}
