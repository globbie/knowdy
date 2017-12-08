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
    if (DEBUG_RELARG_LEVEL_TMP)
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
        { .is_list = true,
          .name = "_g",
          .name_size = strlen("_g"),
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
        },
        { .name = "c",
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

static int set_inst_classname(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndRelArgInstance *self = obj;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val_ref;
            name_size = arg->val_size;
        }
    }
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE) return knd_LIMIT;

    self->classname = name;
    self->classname_size = name_size;

    if (DEBUG_RELARG_LEVEL_2)
        knd_log("++ INST ARG CLASS NAME: \"%.*s\"",
                self->classname_size, self->classname);

    return knd_OK;
}

static int set_inst_objname(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndRelArgInstance *self = obj;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val_ref;
            name_size = arg->val_size;
        }
    }
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE) return knd_LIMIT;

    self->objname = name;
    self->objname_size = name_size;

    if (DEBUG_RELARG_LEVEL_2)
        knd_log("++ INST ARG OBJ NAME: \"%.*s\"",
                self->objname_size, self->objname);

    return knd_OK;
}


static int parse_inst_obj(void *data,
                          const char *rec,
                          size_t *total_size)
{
    struct kndRelArgInstance *inst = data;
    int err;

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_inst_objname,
          .obj = inst
        }
    };
    err = knd_parse_task(rec, total_size, specs,
                         sizeof(specs) / sizeof(struct kndTaskSpec));             PARSE_ERR();
    return knd_OK;
}

static int parse_inst_class(void *data,
                            const char *rec,
                            size_t *total_size)
{
    struct kndRelArgInstance *self = data;
    int err;

    struct kndTaskSpec specs[] = {
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
    err = knd_parse_task(rec, total_size, specs,
                         sizeof(specs) / sizeof(struct kndTaskSpec));             PARSE_ERR();
    return knd_OK;
}

static int parse_inst_GSL(struct kndRelArg *self,
                          struct kndRelArgInstance *inst,
                          const char *rec,
                          size_t *total_size)
{
    if (DEBUG_RELARG_LEVEL_2)
        knd_log(".. %.*s Rel Arg instance parsing: \"%.*s\"..",
                self->name_size, self->name, 32, rec);

    struct kndTaskSpec specs[] = {
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_inst_class,
          .obj = inst
        }
    };
    int err;
    
    err = knd_parse_task(rec, total_size, specs,
                         sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}

static int
set_reverse_rel(struct kndRelArg *self,
		struct kndRelArgInstance *inst,
		struct kndObjEntry *obj_entry)
{
    struct kndRel *rel = self->rel;
    struct kndRelRef *ref = NULL;
    struct kndRelArgInstRef *rel_arg_inst_ref = NULL;
    int err;

    if (DEBUG_RELARG_LEVEL_TMP)
        knd_log(".. %.*s OBJ to set reverse rel %.*s..",
                obj_entry->name_size, obj_entry->name,
		rel->name_size, rel->name);

    for (ref = obj_entry->reverse_rels; ref; ref = ref->next) {
        if (ref->rel == rel) break;
    }

    if (!ref) {
	err = rel->mempool->new_rel_ref(rel->mempool, &ref);                         RET_ERR();
        ref->rel = rel;
        ref->next = obj_entry->reverse_rels;
        obj_entry->reverse_rels = ref;
    }

    err = rel->mempool->new_rel_arg_inst_ref(rel->mempool, &rel_arg_inst_ref);           RET_ERR();
    rel_arg_inst_ref->inst = inst;
    rel_arg_inst_ref->next = ref->insts;
    ref->insts = rel_arg_inst_ref;

    return knd_OK;
}


static int resolve(struct kndRelArg *self)
{
    struct kndConcept *c;

    c = self->rel->class_idx->get(self->rel->class_idx,
                                  self->classname, self->classname_size);
    if (!c) {
        knd_log("-- no such class: %.*s :(", self->classname_size, self->classname);
        return knd_FAIL;
    }

    self->conc = c;

    if (DEBUG_RELARG_LEVEL_2)
        knd_log("++ Rel Arg resolved: \"%.*s\"!",
                self->classname_size, self->classname);

    return knd_OK;
}

static int resolve_inst(struct kndRelArg *self,
			struct kndRelArgInstance *inst)
{
    struct kndConcDir *dir;
    struct kndObjEntry *obj;
    int err;

    dir = self->rel->class_idx->get(self->rel->class_idx,
				    inst->classname, inst->classname_size);
    if (!dir) {
        knd_log("-- no such class: %.*s :(", inst->classname_size, inst->classname);
        return knd_FAIL;
    }
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

	    if (DEBUG_RELARG_LEVEL_TMP)
		knd_log("++ obj resolved: \"%.*s\"!",  inst->objname_size, inst->objname);

	    /* set reverse rel */
	    err = set_reverse_rel(self, inst, obj);        RET_ERR();
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
    self->export = export;
    self->parse_inst = parse_inst_GSL;
    self->resolve = resolve;
    self->resolve_inst = resolve_inst;
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
