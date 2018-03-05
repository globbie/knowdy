#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_rel_arg.h"
#include "knd_rel.h"
#include "knd_task.h"
#include "knd_concept.h"
#include "knd_output.h"
#include "knd_text.h"
#include "knd_mempool.h"

#include <gsl-parser.h>

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
            knd_log("LANG: %s == CURR LOCALE: %s [%zu] => %s",
                    tr->locale, self->locale, self->locale_size, tr->val);

        if (strncmp(self->locale, tr->locale, tr->locale_size)) {
            goto next_tr;
        }
        err = out->write(out, ",\"gloss\":\"", strlen(",\"gloss\":\""));          RET_ERR();
        err = out->write(out, tr->val,  tr->val_size);                            RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();
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

static int export_inst_GSP(struct kndRelArg *self,
                           struct kndRelArgInstance *inst)
{
    struct kndOutput *out;
    int err;

    out = self->out;
    err = out->write(out, "{class ", strlen("{class "));                          RET_ERR();
    err = out->write(out, inst->classname, inst->classname_size);                 RET_ERR();
    err = out->write(out, "{obj ", strlen("{obj "));                              RET_ERR();
    err = out->write(out, inst->objname, inst->objname_size);                     RET_ERR();
    err = out->write(out, "}}", strlen("}}"));                                    RET_ERR();

    return knd_OK;
}

static int export_inst_JSON(struct kndRelArg *self,
                            struct kndRelArgInstance *inst)
{
    struct kndOutput *out = self->out;
    /*const char *type_name = knd_relarg_names[self->type];
      size_t type_name_size = strlen(knd_relarg_names[self->type]); */
    int err;

    err = out->write(out, "{", 1);                                               RET_ERR();
    err = out->write(out, "\"class\":\"", strlen("\"class\":\""));               RET_ERR();
    err = out->write(out, inst->classname, inst->classname_size);                RET_ERR();
    err = out->write(out, "\"", 1);                                              RET_ERR();

    err = out->write(out, ",\"obj\":\"", strlen(",\"obj\":\""));                 RET_ERR();
    err = out->write(out, inst->objname, inst->objname_size);                    RET_ERR();
    err = out->write(out, "\"", 1);                                              RET_ERR();

    err = out->write(out, "}", 1);                                               RET_ERR();

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

static int export_inst(struct kndRelArg *self,
                       struct kndRelArgInstance *inst)
{
    int err = knd_FAIL;

    switch (self->format) {
    case KND_FORMAT_JSON:
        err = export_inst_JSON(self, inst);
        if (err) goto final;
        break;
    case KND_FORMAT_GSP:
        err = export_inst_GSP(self, inst);
        if (err) goto final;
        break;
    default:
        break;
    }

 final:
    return err;
}


static gsl_err_t run_set_name(void *obj, const char *name, size_t name_size)
{
    struct kndRelArg *self = (struct kndRelArg*)obj;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    memcpy(self->name, name, name_size);
    self->name_size = name_size;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t run_set_translation_text(void *obj, const char *val, size_t val_size)
{
    struct kndTranslation *tr = obj;

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    if (DEBUG_RELARG_LEVEL_2)
        knd_log(".. run set translation text: %s\n", val);

    memcpy(tr->val, val, val_size);
    tr->val[val_size] = '\0';
    tr->val_size = val_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_gloss(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndTranslation *tr = obj;
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_translation_text,
          .obj = tr
        }
    };

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. reading gloss translation: \"%.*s\"",
                tr->locale_size, tr->locale);

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t gloss_append(void *accu,
                              void *item)
{
    struct kndRelArg *self = accu;
    struct kndTranslation *tr = item;

    tr->next = self->tr;
    self->tr = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t gloss_alloc(void *obj,
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

    if (name_size > KND_LOCALE_SIZE) return make_gsl_err(gsl_LIMIT);

    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return make_gsl_err_external(knd_NOMEM);

    memset(tr, 0, sizeof(struct kndTranslation));
    memcpy(tr->curr_locale, name, name_size);
    tr->curr_locale_size = name_size;

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;
    *item = tr;

    return make_gsl_err(gsl_OK);
}


static int parse_GSL(struct kndRelArg *self,
                     const char *rec,
                     size_t *total_size)
{
    if (DEBUG_RELARG_LEVEL_2)
        knd_log(".. Rel Arg parsing: \"%.*s\"..", 32, rec);

    struct gslTaskSpec specs[] = {
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
        { .is_list = true,
          .name = "_g",
          .name_size = strlen("_g"),
          .accu = self,
          .alloc = gloss_alloc,
          .append = gloss_append,
          .parse = read_gloss
        },
        { .type = GSL_CHANGE_STATE,
          .name = "c",
          .name_size = strlen("c"),
          .buf = self->classname,
          .buf_size = &self->classname_size,
          .max_buf_size = KND_NAME_SIZE
        },
        { .name = "c",
          .name_size = strlen("c"),
          .buf = self->classname,
          .buf_size = &self->classname_size,
          .max_buf_size = KND_NAME_SIZE
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}

static gsl_err_t set_inst_classname(void *obj, const char *name, size_t name_size)
{
    struct kndRelArgInstance *self = obj;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    self->classname = name;
    self->classname_size = name_size;

    if (DEBUG_RELARG_LEVEL_2)
        knd_log("++ INST ARG CLASS NAME: \"%.*s\"",
                self->classname_size, self->classname);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_inst_objname(void *obj, const char *name, size_t name_size)
{
    struct kndRelArgInstance *self = obj;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    self->objname = name;
    self->objname_size = name_size;

    if (DEBUG_RELARG_LEVEL_2)
        knd_log("++ INST ARG OBJ NAME: \"%.*s\"",
                self->objname_size, self->objname);

    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_inst_obj(void *data,
                                const char *rec,
                                size_t *total_size)
{
    struct kndRelArgInstance *inst = data;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_inst_objname,
          .obj = inst
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_inst_class(void *data,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndRelArgInstance *self = data;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_inst_classname,
          .obj = self
        },
        { .name = "obj",
          .name_size = strlen("obj"),
          .parse = parse_inst_obj,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int parse_inst_GSL(struct kndRelArg *self,
                          struct kndRelArgInstance *inst,
                          const char *rec,
                          size_t *total_size)
{
    if (DEBUG_RELARG_LEVEL_2)
        knd_log(".. %.*s Rel Arg instance parsing: \"%.*s\"..",
                self->name_size, self->name, 32, rec);

    struct gslTaskSpec specs[] = {
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_inst_class,
          .obj = inst
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}

static int link_rel(struct kndRelArg *self,
                    struct kndRelArgInstance *inst,
                    struct kndObjEntry *obj_entry)
{
    struct kndRel *rel = self->rel;
    struct kndRelRef *ref = NULL;
    struct kndRelArgInstRef *rel_arg_inst_ref = NULL;
    int err;

    if (DEBUG_RELARG_LEVEL_2)
        knd_log(".. %.*s OBJ to link rel %.*s..",
                obj_entry->name_size, obj_entry->name,
                rel->name_size, rel->name);

    for (ref = obj_entry->rels; ref; ref = ref->next) {
        if (ref->rel == rel) break;
    }

    if (!ref) {
        err = rel->mempool->new_rel_ref(rel->mempool, &ref);                      RET_ERR();
        ref->rel = rel;
        ref->next = obj_entry->rels;
        obj_entry->rels = ref;
    }

    err = rel->mempool->new_rel_arg_inst_ref(rel->mempool, &rel_arg_inst_ref);    RET_ERR();
    rel_arg_inst_ref->inst = inst;
    rel_arg_inst_ref->next = ref->insts;
    ref->insts = rel_arg_inst_ref;
    ref->num_insts++;

    return knd_OK;
}


static int resolve(struct kndRelArg *self)
{
    struct kndConcDir *dir;

    dir = self->rel->class_name_idx->get(self->rel->class_name_idx,
                                  self->classname, self->classname_size);
    if (!dir) {
        knd_log("-- Rel Arg resolve failed: no such class: %.*s :(", self->classname_size, self->classname);
        return knd_FAIL;
    }
    self->conc_dir = dir;

    return knd_OK;
}

static int resolve_inst(struct kndRelArg *self,
                        struct kndRelArgInstance *inst)
{
    struct kndConcDir *dir;
    struct kndObjEntry *obj;
    int err;


    dir = self->rel->class_name_idx->get(self->rel->class_name_idx,
                                    inst->classname, inst->classname_size);
    if (!dir) {
        knd_log("-- no such class: %.*s :(", inst->classname_size, inst->classname);
        return knd_FAIL;
    }

    /* TODO: check inheritance or role */

    inst->conc_dir = dir;

    /* resolve obj ref */
    if (inst->objname_size) {
        if (dir->obj_idx) {
            obj = dir->obj_idx->get(dir->obj_idx,
                                    inst->objname, inst->objname_size);
            if (!obj) {
                knd_log("-- no such obj: %.*s :(", inst->objname_size, inst->objname);
                return knd_FAIL;
            }
            err = link_rel(self, inst, obj);        RET_ERR();
            inst->obj = obj;

            if (DEBUG_RELARG_LEVEL_2)
                knd_log("++ obj resolved: \"%.*s\"!",  inst->objname_size, inst->objname);
        }
    }

    if (DEBUG_RELARG_LEVEL_TMP)
        knd_log("++ Rel Arg instance resolved: \"%.*s\"!",
                inst->classname_size, inst->classname);

    return knd_OK;
}

extern void kndRelArgInstance_init(struct kndRelArgInstance *self)
{
    memset(self, 0, sizeof(struct kndRelArgInstance));
}
extern void kndRelArgInstRef_init(struct kndRelArgInstRef *self)
{
    memset(self, 0, sizeof(struct kndRelArgInstRef));
}

/*  RelArg Initializer */
static void init(struct kndRelArg *self)
{
    /* binding our methods */
    self->init = init;
    self->del = del;
    self->str = str;
    self->parse = parse_GSL;
    self->resolve = resolve;
    self->export = export;
    self->parse_inst = parse_inst_GSL;
    self->resolve_inst = resolve_inst;
    self->export_inst = export_inst;
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
