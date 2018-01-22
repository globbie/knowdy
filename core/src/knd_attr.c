#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_attr.h"
#include "knd_task.h"
#include "knd_concept.h"
#include "knd_output.h"

#include "knd_text.h"
#include "knd_parser.h"

#include <gsl-parser.h>

#define DEBUG_ATTR_LEVEL_1 0
#define DEBUG_ATTR_LEVEL_2 0
#define DEBUG_ATTR_LEVEL_3 0
#define DEBUG_ATTR_LEVEL_4 0
#define DEBUG_ATTR_LEVEL_5 0
#define DEBUG_ATTR_LEVEL_TMP 1

static int kndAttr_validate_email(struct kndAttr *self,
                                  const char   *val,
                                  size_t val_size);

static struct kndAttrValidator knd_attr_validators[] = {
    { .name = "email_address",
      .name_size = strlen("email_address"),
      .proc = kndAttr_validate_email,
    }
};


/*  Attr Destructor */
static void del(struct kndAttr *self)
{
    free(self);
}

static void str(struct kndAttr *self)
{
    struct kndTranslation *tr;
    const char *type_name = knd_attr_names[self->type];

    if (self->is_list)
        knd_log("\n%*s[%s", self->depth * KND_OFFSET_SIZE, "", self->name);
    else
        knd_log("\n%*s{%s %s", self->depth * KND_OFFSET_SIZE, "", type_name, self->name);

    if (self->access_type == KND_ATTR_ACCESS_RESTRICTED) {
        knd_log("%*s  ACL:restricted", self->depth * KND_OFFSET_SIZE, "");
    }

    if (self->quant_type == KND_ATTR_SET) {
        knd_log("%*s  QUANT:SET", self->depth * KND_OFFSET_SIZE, "");
    }
    
    tr = self->tr;
    while (tr) {
        knd_log("%*s   ~ %s %s", self->depth * KND_OFFSET_SIZE, "", tr->locale, tr->val);
        tr = tr->next;
    }

    if (self->ref_classname_size) {
        knd_log("%*s  REF class template: %.*s", self->depth * KND_OFFSET_SIZE, "",
		self->ref_classname_size, self->ref_classname);
    }
    if (self->uniq_attr_name_size) {
        knd_log("%*s  UNIQ attr: %.*s", self->depth * KND_OFFSET_SIZE, "",
		self->uniq_attr_name_size, self->uniq_attr_name);
    }

    if (self->ref_procname_size) {
        knd_log("%*s  REF proc template: %s", self->depth * KND_OFFSET_SIZE, "", self->ref_procname);
    }

    if (self->calc_oper_size) {
        knd_log("%*s  oper: %s attr: %s", self->depth * KND_OFFSET_SIZE, "",
                self->calc_oper, self->calc_attr);
    }

    if (self->idx_name_size) {
        knd_log("%*s  idx: %s", self->depth * KND_OFFSET_SIZE, "", self->idx_name);
    }

    if (self->default_val_size) {
        knd_log("%*s  default VAL: %s", self->depth * KND_OFFSET_SIZE, "", self->default_val);
    }
    
    if (self->is_list)
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
    struct kndOutput *out;
    struct kndTranslation *tr;
    
    const char *type_name = knd_attr_names[self->type];
    size_t type_name_size = strlen(knd_attr_names[self->type]);
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
    

    if (self->is_list) {
        err = out->write(out, ",\"is_list\":true", strlen(",\"is_list\":true"));
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

static int export_GSP(struct kndAttr *self)
{
    struct kndOutput *out;
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
    
    if (self->is_list) {
        err = out->write(out, "{list 1}", strlen("{list 1}"));
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
	self->is_list = true;
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_set_translation_text(void *obj, const char *val, size_t val_size)
{
    struct kndTranslation *tr = obj;

    if (!val_size) return make_gsl_err(gsl_FAIL);
    if (val_size >= sizeof tr->val)
        return make_gsl_err(gsl_LIMIT);

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. run set translation text: %s\n", val);

    memcpy(tr->val, val, val_size);
    tr->val[val_size] = '\0';
    tr->val_size = val_size;

    return make_gsl_err(gsl_OK);
}

//static int parse_gloss_translation(void *obj,
//                                   const char *name, size_t name_size,
//                                   const char *rec, size_t *total_size)
//{
//    struct kndTranslation *tr = obj;
//    int err;
//
//    if (DEBUG_ATTR_LEVEL_2) {
//        knd_log("..  gloss translation in \"%.*s\" REC: \"%s\"\n",
//                name_size, name, rec); }
//
//    struct gslTaskSpec specs[] = {
//        { .is_implied = true,
//          .run = run_set_translation_text,
//          .obj = tr
//        }
//    };
//
//    memcpy(tr->curr_locale, name, name_size);
//    tr->curr_locale[name_size] = '\0';
//    tr->curr_locale_size = name_size;
//
//    tr->locale = tr->curr_locale;
//    tr->locale_size = tr->curr_locale_size;
//
//    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
//    if (err) return err;
//
//    return gsl_OK;
//}


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
    struct kndAttr *self = accu;
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
    struct kndAttr *self = obj;
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

static gsl_err_t parse_quant(void *obj,
                             const char *rec,
                             size_t *total_size)
{
    struct kndAttr *self = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_quant,
          .obj = self
        },
        { .name = "uniq",
          .name_size = strlen("uniq"),
          .buf = self->uniq_attr_name,
          .buf_size = &self->uniq_attr_name_size,
          .max_buf_size = sizeof self->uniq_attr_name
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
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
    struct kndAttr *self = (struct kndAttr*)obj;
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_validator,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int parse_GSL(struct kndAttr *self,
                     const char *rec,
                     size_t *total_size)
{
    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. attr parsing: \"%.*s\"..", 32, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = self->name,
          .buf_size = &self->name_size,
          .max_buf_size = sizeof self->name,
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
          .buf = self->ref_classname,
          .buf_size = &self->ref_classname_size,
          .max_buf_size = sizeof self->ref_classname,
        },
        { .name = "c",
          .name_size = strlen("c"),
          .buf = self->ref_classname,
          .buf_size = &self->ref_classname_size,
          .max_buf_size = sizeof self->ref_classname,
        },
        { .name = "proc",
          .name_size = strlen("proc"),
          .buf = self->ref_procname,
          .buf_size = &self->ref_procname_size,
          .max_buf_size = sizeof self->ref_procname,
        },
        { .name = "quant",
          .name_size = strlen("quant"),
          .parse = parse_quant,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "acl",
          .name_size = strlen("acl"),
          .parse = parse_access_control,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "validate",
          .name_size = strlen("validate"),
          .parse = parse_validator,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "val",
          .name_size = strlen("val"),
          .buf = self->default_val,
          .buf_size = &self->default_val_size,
          .max_buf_size = sizeof self->default_val,
          .obj = self
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return knd_FAIL;  // FIXME(ki.stfu): convert gsl_err_t to knd_err_codes

    if (self->type == KND_ATTR_AGGR) {
        if (!self->ref_classname_size) {
            knd_log("-- ref class not specified in %.*s",
                    self->name_size, self->name);
            return knd_FAIL;
        }
    }

    return knd_OK;
}


/*  Attr Initializer */
static void init(struct kndAttr *self)
{
    /* binding our methods */
    self->init = init;
    self->del = del;
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

    init(self);

    *c = self;

    return knd_OK;
}
