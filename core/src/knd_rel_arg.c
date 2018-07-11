#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_rel_arg.h"
#include "knd_rel.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_set.h"
#include "knd_class.h"
#include "knd_text.h"
#include "knd_mempool.h"

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
    knd_log("}");

}

/**
 *  EXPORT
 */
static int export_JSON(struct kndRelArg *self)
{
    struct glbOutput *out = self->rel->entry->repo->out;
    struct kndTranslation *tr;

    const char *type_name = knd_relarg_names[self->type];
    size_t type_name_size = strlen(knd_relarg_names[self->type]);
    int err;

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
    struct glbOutput *out;
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
        err = out->write(out, "{t ", 3);
        if (err) return err;
        err = out->write(out, tr->val,  tr->val_size);
        if (err) return err;
        err = out->write(out, "}}", 2);
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
    struct glbOutput *out;
    int err;

    out = self->out;
    err = out->write(out, "{c ", strlen("{c "));                                  RET_ERR();
    err = out->write(out, inst->class_entry->id, inst->class_entry->id_size);           RET_ERR();

    if (inst->objname_size) {
        err = out->write(out, "{o ", strlen("{o "));                              RET_ERR();
        err = out->write(out, inst->obj->id, inst->obj->id_size);                 RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
    }
    
    if (inst->val_size) {
        err = out->write(out, "{v ", strlen("{v "));                              RET_ERR();
        err = out->write(out, inst->val, inst->val_size);                         RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
    }

    err = out->writec(out, '}');                                                  RET_ERR();
    return knd_OK;
}

static int export_inst_JSON(struct kndRelArg *self,
                            struct kndRelArgInstance *inst)
{
    struct glbOutput *out = self->out;
    /*const char *type_name = knd_relarg_names[self->type];
      size_t type_name_size = strlen(knd_relarg_names[self->type]); */
    bool in_list = false;
    int err;

    err = out->writec(out, '{');                                                 RET_ERR();

    /* output class name only if it's 
       different from the default one */
    if (self->conc_entry != inst->class_entry) {
        err = out->write(out, "\"class\":\"", strlen("\"class\":\""));           RET_ERR();
        err = out->write(out, inst->classname, inst->classname_size);            RET_ERR();
        err = out->writec(out, '"');                                             RET_ERR();
        in_list = true;
    }

    if (inst->objname_size) {
        if (in_list) {
            err = out->writec(out, ',');                                         RET_ERR();
        }
        err = out->write(out, "\"obj\":\"", strlen("\"obj\":\""));             RET_ERR();
        err = out->write(out, inst->objname, inst->objname_size);                RET_ERR();
        err = out->writec(out, '"');                                             RET_ERR();
    }
    if (inst->val_size) {
        if (in_list) {
            err = out->writec(out, ',');                                         RET_ERR();
        }
        err = out->write(out, "\"val\":\"", strlen("\"val\":\""));               RET_ERR();
        err = out->write(out, inst->val, inst->val_size);                        RET_ERR();
        err = out->writec(out, '"');                                             RET_ERR();
    }

    err = out->writec(out, '}');                                                 RET_ERR();

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

static gsl_err_t alloc_gloss_item(void *obj,
                                  const char *name,
                                  size_t name_size,
                                  size_t count,
                                  void **item)
{
    struct kndRelArg *self = obj;
    struct kndTranslation *tr;

    assert(name == NULL && name_size == 0);

    if (DEBUG_RELARG_LEVEL_2)
        knd_log(".. %.*s: allocate gloss translation,  count: %zu",
                self->name_size, self->name, count);

    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return make_gsl_err_external(knd_NOMEM);

    memset(tr, 0, sizeof(struct kndTranslation));

    *item = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t append_gloss_item(void *accu,
                                   void *item)
{
    struct kndRelArg *self = accu;
    struct kndTranslation *tr = item;

    tr->next = self->tr;
    self->tr = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss_item(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndTranslation *tr = obj;
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = tr->curr_locale,
          .buf_size = &tr->curr_locale_size,
          .max_buf_size = sizeof tr->curr_locale
        },
        { .name = "t",
          .name_size = strlen("t"),
          .buf = tr->val,
          .buf_size = &tr->val_size,
          .max_buf_size = sizeof tr->val
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (tr->curr_locale_size == 0 || tr->val_size == 0)
        return make_gsl_err(gsl_FORMAT);  // error: both of them are required

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;

    if (DEBUG_RELARG_LEVEL_2)
        knd_log(".. read gloss translation: \"%.*s\",  text: \"%.*s\"",
                tr->locale_size, tr->locale, tr->val_size, tr->val);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss(void *obj,
                             const char *rec,
                             size_t *total_size)
{
    struct kndRelArg *self = obj;
    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .alloc = alloc_gloss_item,
        .append = append_gloss_item,
        .accu = self,
        .parse = parse_gloss_item
    };

    if (DEBUG_RELARG_LEVEL_2)
        knd_log(".. %.*s: reading gloss",
                self->name_size, self->name);

    return gsl_parse_array(&item_spec, rec, total_size);
}

static gsl_err_t parse_GSL(struct kndRelArg *self,
                           const char *rec,
                           size_t *total_size)
{
    if (DEBUG_RELARG_LEVEL_TMP)
        knd_log(".. Rel Arg parsing: \"%.*s\"..", 32, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = parse_gloss,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = parse_gloss,
          .obj = self
        },
        { .type = GSL_SET_STATE,
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

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
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

static gsl_err_t get_inst_classname(void *obj, const char *name, size_t name_size)
{
    struct kndRelArgInstance *self = obj;
    struct kndSet *class_idx = self->relarg->rel->class_idx;
    struct kndClassEntry *entry;
    void *result;
    int err;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    err = class_idx->get(class_idx, name, name_size, &result);
    if (err) {
        knd_log("-- no such class: %.*s :(",
                name_size, name);
        return make_gsl_err(gsl_FAIL);
    }
    entry = result;
    self->classname = entry->name;
    self->classname_size = entry->name_size;
    self->class_entry = entry;

    if (DEBUG_RELARG_LEVEL_2)
        knd_log("++ INST ARG CLASS NAME: \"%.*s\"",
                self->classname_size, self->classname);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t get_inst_obj(void *obj, const char *name, size_t name_size)
{
    struct kndRelArgInstance *self = obj;
    void *elem;
    struct kndClassEntry *entry = self->class_entry;
    int err;
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    err = entry->obj_idx->get(entry->obj_idx, name, name_size, &elem);
    if (err) {
        knd_log("-- no such obj: %.*s :(", name_size, name);
        return make_gsl_err(gsl_FAIL);
    }
    self->obj = elem;
    
    if (DEBUG_RELARG_LEVEL_2)
        knd_log("++ INST ARG OBJ NAME: \"%.*s\"",
                self->obj->name_size, self->obj->name);
    self->objname = self->obj->name;
    self->objname_size = self->obj->name_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_inst_val(void *obj, const char *name, size_t name_size)
{
    struct kndRelArgInstance *self = obj;
    self->val = name;
    self->val_size = name_size;
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

static gsl_err_t parse_class_inst(void *data,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndRelArgInstance *self = data;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_inst_classname,
          .obj = self
        },
        { .name = "inst",
          .name_size = strlen("inst"),
          .parse = parse_class_inst,
          .obj = self
        },
        { .name = "val",
          .name_size = strlen("val"),
          .run = set_inst_val,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_inst_class_id(void *data,
                                     const char *rec,
                                     size_t *total_size)
{
    struct kndRelArgInstance *self = data;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = get_inst_classname,
          .obj = self
        },
        { .name = "o",
          .name_size = strlen("o"),
          .run = get_inst_obj,
          .obj = self
        },
        { .name = "v",
          .name_size = strlen("v"),
          .run = set_inst_val,
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
    if (DEBUG_RELARG_LEVEL_TMP)
        knd_log(".. %.*s Rel Arg instance parsing: \"%.*s\"..",
                self->name_size, self->name, 32, rec);

    struct gslTaskSpec specs[] = {
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_inst,
          .obj = inst
        },
        { .name = "c",
          .name_size = strlen("c"),
          .parse = parse_inst_class_id,
          .obj = inst
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}

static int link_rel(struct kndRelArg *self,
                    struct kndRelArgInstance *arg_inst,
                    struct kndObjEntry *obj_entry)
{
    struct kndRel *rel = self->rel;
    struct kndRelRef *ref = NULL;
    struct kndRelInstance *inst = arg_inst->rel_inst;
    int err;

    if (DEBUG_RELARG_LEVEL_2)
        knd_log(".. %.*s OBJ to link rel %.*s.. rel inst:%.*s",
                obj_entry->name_size, obj_entry->name,
                rel->name_size, rel->name, inst->id_size, inst->id);

    for (ref = obj_entry->rels; ref; ref = ref->next) {
        if (ref->rel == rel) break;
    }

    if (!ref) {
        err = rel->mempool->new_rel_ref(rel->mempool, &ref);                      RET_ERR();

        err = rel->mempool->new_set(rel->mempool, &ref->idx);                     RET_ERR();
        ref->idx->type = KND_SET_REL_INST;

        ref->rel = rel;
        ref->next = obj_entry->rels;
        obj_entry->rels = ref;
    }

    err = ref->idx->add(ref->idx, inst->id, inst->id_size, (void*)inst);          RET_ERR();
    return knd_OK;
}


static int resolve(struct kndRelArg *self)
{
    struct kndClassEntry *entry;
    struct ooDict *name_idx = self->rel->entry->repo->root_class->class_name_idx;

    if (DEBUG_RELARG_LEVEL_TMP)
        knd_log(".. resolving Rel Arg %.*s..",
                self->classname_size, self->classname);

    entry = name_idx->get(name_idx,
                          self->classname, self->classname_size);
    if (!entry) {
        knd_log("-- Rel Arg resolve failed: no such class: %.*s :(",
                self->classname_size, self->classname);
        return knd_FAIL;
    }
    self->conc_entry = entry;

    return knd_OK;
}

static int resolve_inst(struct kndRelArg *self,
                        struct kndRelArgInstance *inst)
{
    struct kndClassEntry *entry;
    struct kndObjEntry *obj;
    struct ooDict *name_idx = self->rel->entry->repo->root_class->class_name_idx;
    int err;

    entry = name_idx->get(name_idx,
                          inst->classname, inst->classname_size);
    if (!entry) {
        knd_log("-- no such class: %.*s :(",
                inst->classname_size, inst->classname);
        return knd_FAIL;
    }

    /* TODO: check inheritance or role */

    inst->class_entry = entry;

    /* resolve obj ref */
    if (inst->objname_size) {
        if (DEBUG_RELARG_LEVEL_2)
            knd_log(".. resolving rel arg inst OBJ ref: \"%.*s\""
                    " CONC DIR: %.*s OBJ IDX:%p",
                    inst->objname_size, inst->objname,
                    entry->name_size, entry->name, entry->obj_name_idx);

        if (!entry->obj_name_idx) {
            knd_log("-- empty obj IDX in class \"%.*s\" :(",
                        entry->name_size, entry->name);
            return knd_FAIL;
        }

        obj = entry->obj_name_idx->get(entry->obj_name_idx,
                                     inst->objname, inst->objname_size);
        if (!obj) {
            knd_log("-- no such obj: %.*s :(",
                    inst->objname_size, inst->objname);
            return knd_FAIL;
        }

        err = link_rel(self, inst, obj);        RET_ERR();
        inst->obj = obj;

        if (DEBUG_RELARG_LEVEL_2) {
            knd_log("++ obj resolved: \"%.*s\"!",
                    inst->objname_size, inst->objname);
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
