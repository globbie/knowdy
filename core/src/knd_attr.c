#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_attr.h"
#include "knd_proc.h"
#include "knd_task.h"
#include "knd_class.h"
#include "knd_text.h"
#include "knd_repo.h"
#include "knd_mempool.h"

#include <gsl-parser.h>
#include <glb-lib/output.h>

#define DEBUG_ATTR_LEVEL_1 0
#define DEBUG_ATTR_LEVEL_2 0
#define DEBUG_ATTR_LEVEL_3 0
#define DEBUG_ATTR_LEVEL_4 0
#define DEBUG_ATTR_LEVEL_5 0
#define DEBUG_ATTR_LEVEL_TMP 1

static int kndAttr_validate_email(struct kndAttr *self,
                                  const char *val,
                                  size_t val_size);

static struct kndAttrValidator knd_attr_validators[] = {
    { .name = "email_address",
      .name_size = strlen("email_address"),
      .proc = kndAttr_validate_email,
    }
};

static void str(struct kndAttr *self)
{
    struct kndTranslation *tr;
    struct kndProc *proc;
    const char *type_name = knd_attr_names[self->type];

    if (self->is_a_set)
        knd_log("\n%*s[%s", self->depth * KND_OFFSET_SIZE, "", self->name);
    else
        knd_log("\n%*s{%s %s", self->depth * KND_OFFSET_SIZE, "", type_name, self->name);

    if (self->access_type == KND_ATTR_ACCESS_RESTRICTED) {
        knd_log("%*s  ACL:restricted",
                self->depth * KND_OFFSET_SIZE, "");
    }

    if (self->quant_type == KND_ATTR_SET) {
        knd_log("%*s  QUANT:SET",
                self->depth * KND_OFFSET_SIZE, "");
    }

    if (self->concise_level) {
        knd_log("%*s  CONCISE:%zu",
                self->depth * KND_OFFSET_SIZE, "", self->concise_level);
    }
    if (self->is_implied) {
        knd_log("%*s  (implied)",
                self->depth * KND_OFFSET_SIZE, "");
    }
    
    tr = self->tr;
    while (tr) {
        knd_log("%*s   ~ %s %s",
                self->depth * KND_OFFSET_SIZE, "", tr->locale, tr->val);
        tr = tr->next;
    }

    if (self->ref_classname_size) {
        knd_log("%*s  REF class template: %.*s",
                self->depth * KND_OFFSET_SIZE, "",
                self->ref_classname_size, self->ref_classname);
    }
    if (self->uniq_attr_name_size) {
        knd_log("%*s  UNIQ attr: %.*s",
                self->depth * KND_OFFSET_SIZE, "",
                self->uniq_attr_name_size, self->uniq_attr_name);
    }

    if (self->ref_procname_size) {
        knd_log("%*s  PROC template: %s",
                self->depth * KND_OFFSET_SIZE, "", self->ref_procname);
    }

    if (self->proc) {
        proc = self->proc;
        knd_log("%*s  PROC: %.*s",
                self->depth * KND_OFFSET_SIZE, "", proc->name_size, proc->name);
        proc->depth = self->depth + 1;
        proc->str(proc);
    }
    
    if (self->calc_oper_size) {
        knd_log("%*s  oper: %s attr: %s",
                self->depth * KND_OFFSET_SIZE, "",
                self->calc_oper, self->calc_attr);
    }

    if (self->idx_name_size) {
        knd_log("%*s  idx: %s",
                self->depth * KND_OFFSET_SIZE, "", self->idx_name);
    }

    if (self->default_val_size) {
        knd_log("%*s  default VAL: %s",
                self->depth * KND_OFFSET_SIZE, "", self->default_val);
    }
    
    if (self->is_a_set)
        knd_log("%*s]", self->depth * KND_OFFSET_SIZE, "");
    else
        knd_log("%*s}",  self->depth * KND_OFFSET_SIZE, "");
}



/**
 *  VALIDATORS
 */
static int kndAttr_validate_email(struct kndAttr *self,
                                  const char   *val,
                                  size_t val_size)
{
    
    if (DEBUG_ATTR_LEVEL_TMP)
        knd_log(".. %s attr validating email: \"%.*s\"", self->name, val_size, val);

    return knd_OK;
}


/**
 *  EXPORT
 */
static int export_JSON(struct kndAttr *self)
{
    struct glbOutput *out;
    struct kndTranslation *tr;
    struct kndProc *p;
    const char *type_name = knd_attr_names[self->type];
    size_t type_name_size = strlen(knd_attr_names[self->type]);
    int err;

    knd_log(".. JSON export attr: \"%.*s\"..", self->name_size, self->name);

    out = self->parent_class->entry->repo->out;

    err = out->writec(out, '"');
    if (err) return err;
    err = out->write(out, self->name, self->name_size);
    if (err) return err;
    err = out->write(out, "\":{", strlen("\":{"));
    if (err) return err;
    
    err = out->write(out, "\"type\":\"", strlen("\"type\":\""));
    if (err) return err;
    err = out->write(out, type_name, type_name_size);
    if (err) return err;
    err = out->writec(out, '"');
    if (err) return err;

    if (self->is_a_set) {
        err = out->write(out, ",\"is_a_set\":true", strlen(",\"is_a_set\":true"));
        if (err) return err;
    }
    
    if (self->ref_classname_size) {
        err = out->write(out, ",\"refclass\":\"", strlen(",\"refclass\":\""));
        if (err) return err;
        err = out->write(out, self->ref_classname, self->ref_classname_size);
        if (err) return err;
        err = out->write(out, "\"", 1);
        if (err) return err;
   }
    
    /* choose gloss */
    tr = self->tr;
    while (tr) {
        if (DEBUG_ATTR_LEVEL_2)
            knd_log("LANG: %s == CURR LOCALE: %s [%lu] => %s",
                    tr->locale, self->task->locale, (unsigned long)self->locale_size, tr->val);

        if (strncmp(self->task->locale, tr->locale, tr->locale_size)) {
            goto next_tr;
        }
        
        err = out->write(out,
                         ",\"_gloss\":\"", strlen(",\"_gloss\":\""));
        if (err) return err;

        err = out->write(out, tr->val,  tr->val_size);
        if (err) return err;

        err = out->write(out, "\"", 1);
        if (err) return err;
        break;

    next_tr:
        tr = tr->next;
    }

    if (self->proc) {
        knd_log(".. attr:%p proc: %p  entry:%p", self, self->proc, self->proc->entry);

        err = out->write(out, ",\"proc\":", strlen(",\"proc\":"));
        if (err) return err;
        p = self->proc;
        p->depth = 0;
        p->max_depth = KND_MAX_DEPTH;
        err = p->export(p);
        if (err) return err;
    }
    
    err = out->writec(out, '}');
    if (err) return err;

    return knd_OK;
}

static int export_GSP(struct kndAttr *self)
{
    char buf[KND_NAME_SIZE] = {0};
    size_t buf_size = 0;
    struct glbOutput *out;
    struct kndTranslation *tr;

    const char *type_name = knd_attr_names[self->type];
    size_t type_name_size = strlen(knd_attr_names[self->type]);
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
    
    if (self->is_a_set) {
        err = out->write(out, "{t set}", strlen("{t set}"));
        if (err) return err;
    }

    if (self->is_implied) {
        err = out->write(out, "{impl}", strlen("{impl}"));
        if (err) return err;
    }

    if (self->is_indexed) {
        err = out->write(out, "{idx}", strlen("{idx}"));
        if (err) return err;
    }

    if (self->concise_level) {
        buf_size = sprintf(buf, "%zu", self->concise_level);
        err = out->write(out, "{concise ", strlen("{concise "));
        if (err) return err;
        err = out->write(out, buf, buf_size);
        if (err) return err;
        err = out->writec(out, '}');
        if (err) return err;
    }
    
    if (self->ref_classname_size) {
        err = out->write(out, "{c ", strlen("{c "));
        if (err) return err;
        err = out->write(out, self->ref_classname, self->ref_classname_size);
        if (err) return err;
        err = out->write(out, "}", 1);
        if (err) return err;
    }

    if (self->ref_procname_size) {
        err = out->write(out, "{p ", strlen("{p "));
        if (err) return err;
        err = out->write(out, self->ref_procname, self->ref_procname_size);
        if (err) return err;
        err = out->write(out, "}", 1);
        if (err) return err;
    }
    
    /* choose gloss */
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

static int export(struct kndAttr *self)
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


/**
 * PARSER
 */

static gsl_err_t run_set_quant(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = (struct kndAttr*)obj;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. run set quant!\n");
    
    if (!name_size) return make_gsl_err(gsl_FAIL);
    if (name_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    if (!memcmp("set", name, name_size)) {
        self->quant_type = KND_ATTR_SET;
        self->is_a_set = true;
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_idx(void *obj,
                             const char *name __attribute__((unused)),
                             size_t name_size __attribute__((unused)))
{
    struct kndAttr *self = obj;

    if (DEBUG_ATTR_LEVEL_1)
        knd_log(".. confirm IDX");
    self->is_indexed = true;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_implied(void *obj,
                                 const char *name __attribute__((unused)),
                                 size_t name_size __attribute__((unused)))
{
    struct kndAttr *self = obj;
    self->is_implied = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t alloc_gloss_item(void *obj,
                                  const char *name,
                                  size_t name_size,
                                  size_t count,
                                  void **item)
{
    struct kndAttr *self = obj;
    struct kndTranslation *tr;

    assert(name == NULL && name_size == 0);

    if (DEBUG_ATTR_LEVEL_2)
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
    struct kndAttr *self = accu;
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

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. read gloss translation: \"%.*s\",  text: \"%.*s\"",
                tr->locale_size, tr->locale, tr->val_size, tr->val);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss(void *obj,
                             const char *rec,
                             size_t *total_size)
{
    struct kndAttr *self = obj;
    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .alloc =        alloc_gloss_item,
        .append =       append_gloss_item,
        .accu =         self,
        .parse =        parse_gloss_item
    };

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. %.*s: reading gloss",
                self->name_size, self->name);

    return gsl_parse_array(&item_spec, rec, total_size);
}

static gsl_err_t parse_quant_type(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndAttr *self = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_quant,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "uniq",
          .name_size = strlen("uniq"),
          .buf = self->uniq_attr_name,
          .buf_size = &self->uniq_attr_name_size,
          .max_buf_size = sizeof(self->uniq_attr_name)
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_proc(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndAttr *self = obj;
    struct kndProc *proc;
    struct kndProcEntry *entry;
    struct kndMemPool *mempool;
    int err;

    mempool = self->parent_class->entry->repo->mempool;
    err = mempool->new_proc(mempool, &proc);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    err = mempool->new_proc_entry(mempool, &entry); 
    if (err) return *total_size = 0, make_gsl_err_external(err);
    entry->repo = self->parent_class->entry->repo;
    entry->proc = proc;
    proc->entry = entry;

    err = proc->read(proc, rec, total_size);
    if (err) return make_gsl_err_external(err);
    self->proc = proc;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_set_access_control(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = (struct kndAttr*)obj;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. run set ACL..");

    if (!name_size) return make_gsl_err(gsl_FAIL);
    if (name_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    if (!strncmp(name, "restrict", strlen("restrict"))) {
        self->access_type = KND_ATTR_ACCESS_RESTRICTED;
        if (DEBUG_ATTR_LEVEL_2)
            knd_log("** NB: restricted attr: %.*s!",
                    self->name_size, self->name);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_access_control(void *obj,
                                      const char *rec,
                                      size_t *total_size)
{
    struct kndAttr *self = (struct kndAttr*)obj;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. parsing ACL: \"%s\"", rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_access_control,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t run_set_validator(void *obj, const char *name, size_t name_size)
{
    struct kndAttr *self = (struct kndAttr*)obj;
    struct kndAttrValidator *validator;
    
    if (!name_size) return make_gsl_err(gsl_FAIL);
    if (name_size >= sizeof self->validator_name) return make_gsl_err(gsl_LIMIT);
            
    memcpy(self->validator_name, name, name_size);
    self->validator_name_size = name_size;
    self->validator_name[name_size] = '\0';

    if (DEBUG_ATTR_LEVEL_2)
        knd_log("== validator name set: %.*s", name_size, name);

    size_t knd_num_attr_validators = sizeof(knd_attr_validators) / sizeof(struct kndAttrValidator);

    for (size_t i = 0; i < knd_num_attr_validators; i++) {
        validator = &knd_attr_validators[i];
        /* TODO: assign validator
           knd_log("existing validator: \"%s\"", validator->name);
        */
    }
    
    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_validator(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct kndAttr *self = obj;
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_validator,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_GSL(struct kndAttr *self,
                     const char *rec,
                     size_t *total_size)
{
    if (DEBUG_ATTR_LEVEL_1)
        knd_log(".. attr parsing: \"%.*s\"..", 32, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = self->name,
          .buf_size = &self->name_size,
          .max_buf_size = sizeof(self->name)
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
          .buf = self->ref_classname,
          .buf_size = &self->ref_classname_size,
          .max_buf_size = sizeof(self->ref_classname)
        },
        { .name = "c",
          .name_size = strlen("c"),
          .buf = self->ref_classname,
          .buf_size = &self->ref_classname_size,
          .max_buf_size = sizeof(self->ref_classname)
        },
        { .type = GSL_SET_STATE,
          .name = "p",
          .name_size = strlen("p"),
          .buf = self->ref_procname,
          .buf_size = &self->ref_procname_size,
          .max_buf_size = sizeof self->ref_procname,
        },
        { .name = "p",
          .name_size = strlen("p"),
          .buf = self->ref_procname,
          .buf_size = &self->ref_procname_size,
          .max_buf_size = sizeof self->ref_procname,
        },
        { .type = GSL_SET_STATE,
          .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "t",
          .name_size = strlen("t"),
          .parse = parse_quant_type,
          .obj = self
        },
        { .name = "t",
          .name_size = strlen("t"),
          .parse = parse_quant_type,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "acl",
          .name_size = strlen("acl"),
          .parse = parse_access_control,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "idx",
          .name_size = strlen("idx"),
          .run = confirm_idx,
          .obj = self
        },
        { .name = "idx",
          .name_size = strlen("idx"),
          .run = confirm_idx,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "impl",
          .name_size = strlen("impl"),
          .run = confirm_implied,
          .obj = self
        },
        { .name = "impl",
          .name_size = strlen("impl"),
          .run = confirm_implied,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "concise",
          .name_size = strlen("concise"),
          .parse = gsl_parse_size_t,
          .obj = &self->concise_level
        },
        { .name = "concise",
          .name_size = strlen("concise"),
          .parse = gsl_parse_size_t,
          .obj = &self->concise_level
        },
        { .type = GSL_SET_STATE,
          .name = "validate",
          .name_size = strlen("validate"),
          .parse = parse_validator,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "val",
          .name_size = strlen("val"),
          .buf = self->default_val,
          .buf_size = &self->default_val_size,
          .max_buf_size = sizeof(self->default_val)
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (self->type == KND_ATTR_AGGR) {
        if (!self->ref_classname_size) {
            knd_log("-- ref class not specified in %.*s",
                    self->name_size, self->name);
            return make_gsl_err_external(knd_FAIL);
        }
    }

    return make_gsl_err(gsl_OK);
}


extern void kndAttr_init(struct kndAttr *self)
{
    /* binding our methods */
    self->str = str;
    self->parse = parse_GSL;
    self->export = export;
}


extern int
kndAttr_new(struct kndAttr **c)
{
    struct kndAttr *self;

    self = malloc(sizeof(struct kndAttr));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndAttr));

    kndAttr_init(self);

    *c = self;

    return knd_OK;
}
