#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_attr.h"
#include "knd_task.h"
#include "knd_concept.h"
#include "knd_output.h"

#include "knd_text.h"
#include "knd_parser.h"

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

    if (self->cardinality_size) {
        knd_log("\n%*s    [%s]", self->depth * KND_OFFSET_SIZE, "", self->cardinality);
    }
    
    tr = self->tr;
    while (tr) {
        knd_log("%*s   ~ %s %s", self->depth * KND_OFFSET_SIZE, "", tr->locale, tr->val);
        tr = tr->next;
    }

    if (self->classname_size) {
        knd_log("%*s  class template: %s", self->depth * KND_OFFSET_SIZE, "", self->classname);
    }

    if (self->ref_classname_size) {
        knd_log("%*s  REF class template: %s", self->depth * KND_OFFSET_SIZE, "", self->ref_classname);
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
        err = out->write(out, "{refc ", strlen("{refc "));
        if (err) return err;
        err = out->write(out, self->ref_classname, self->ref_classname_size);
        if (err) return err;
        err = out->write(out, "}", 1);
        if (err) return err;
    }
    
    /* choose gloss */
    tr = self->tr;
    if (self->tr) {
        err = out->write(out,
                         "[gloss", strlen("[gloss"));
        if (err) return err;
    }
    
    while (tr) {
        err = out->write(out,
                         "{", 1);
        if (err) return err;
        err = out->write(out, tr->locale,  tr->locale_size);
        if (err) return err;
        err = out->write(out, " ", 1);
        if (err) return err;
        err = out->write(out, tr->val,  tr->val_size);
        if (err) return err;
        err = out->write(out, "}", 1);
        if (err) return err;
        break;

    next_tr:
        tr = tr->next;
    }
    if (self->tr) {
        err = out->write(out,
                         "]", 1);
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

static int run_set_name(void *obj,
                        struct kndTaskArg *args, size_t num_args)
{
    struct kndAttr *self = (struct kndAttr*)obj;
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
    self->name[name_size] = '\0';

    return knd_OK;
}


static int run_set_cardinality(void *obj,
                               struct kndTaskArg *args, size_t num_args)
{
    struct kndAttr *self = (struct kndAttr*)obj;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. run set cardinality!\n");
    
    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }

    if (!name_size) return knd_FAIL;
    if (name_size >= KND_SHORT_NAME_SIZE) return knd_LIMIT;

    memcpy(self->cardinality, name, name_size);
    self->cardinality_size = name_size;
    self->cardinality[name_size] = '\0';

    return knd_OK;
}

static int run_set_translation_text(void *obj,
                                    struct kndTaskArg *args, size_t num_args)
{
    struct kndTranslation *tr = (struct kndTranslation*)obj;
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
    if (val_size >= KND_NAME_SIZE)
        return knd_LIMIT;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. run set translation text: %s\n", val);

    memcpy(tr->val, val, val_size);
    tr->val[val_size] = '\0';
    tr->val_size = val_size;

    return knd_OK;
}


static int parse_gloss_translation(void *obj,
                                   const char *name, size_t name_size,
                                   const char *rec, size_t *total_size)
{
    struct kndTranslation *tr = (struct kndTranslation*)obj;
    int err;

    if (DEBUG_ATTR_LEVEL_2) {
        knd_log("..  gloss translation in \"%.*s\" REC: \"%s\"\n",
                name_size, name, rec); }

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_translation_text,
          .obj = tr
        }
    };

    memcpy(tr->curr_locale, name, name_size);
    tr->curr_locale[name_size] = '\0';
    tr->curr_locale_size = name_size;

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}

static int parse_gloss_change(void *obj,
                              const char *rec,
                              size_t *total_size)
{
    struct kndAttr *self = (struct kndAttr*)obj;
    struct kndTranslation *tr;
    int err;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. parsing the gloss change: \"%s\"", rec);

    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return knd_NOMEM;
    memset(tr, 0, sizeof(struct kndTranslation));

    struct kndTaskSpec specs[] = {
        { .is_validator = true,
          .buf = tr->curr_locale,
          .buf_size = &tr->curr_locale_size,
          .max_buf_size = KND_LOCALE_SIZE,
          .validate = parse_gloss_translation,
          .obj = tr
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    /* assign translation */
    tr->next = self->tr;
    self->tr = tr;

    return knd_OK;
}


static int parse_cardinality(void *obj,
                             const char *rec,
                             size_t *total_size)
{
    struct kndAttr *self = (struct kndAttr*)obj;
    int err;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. parsing the cardinality: \"%s\"", rec);

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_cardinality,
          .obj = self
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}

static int run_set_access_control(void *obj,
                                  struct kndTaskArg *args, size_t num_args)
{
    struct kndAttr *self = (struct kndAttr*)obj;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. run set ACL..");
    
    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }

    if (!name_size) return knd_FAIL;
    if (name_size >= KND_SHORT_NAME_SIZE) return knd_LIMIT;

    if (!strncmp(name, "restrict", strlen("restrict"))) {
        self->access_type = KND_ATTR_ACCESS_RESTRICTED;
        knd_log("** NB: restricted attr: %.*s!", self->name_size, self->name);
    }
    
    return knd_OK;
}

static int parse_access_control(void *obj,
                             const char *rec,
                             size_t *total_size)
{
    struct kndAttr *self = (struct kndAttr*)obj;
    int err;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. parsing ACL: \"%s\"", rec);

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_access_control,
          .obj = self
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}

static int run_set_validator(void *obj,
                             struct kndTaskArg *args, size_t num_args)
{
    struct kndAttr *self = (struct kndAttr*)obj;
    struct kndTaskArg *arg;
    struct kndAttrValidator *validator;
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
    if (name_size >= KND_SHORT_NAME_SIZE) return knd_LIMIT;
            
    memcpy(self->validator_name, name, name_size);
    self->validator_name_size = name_size;
    self->validator_name[name_size] = '\0';

    if (DEBUG_ATTR_LEVEL_TMP)
        knd_log("== validator name set: %.*s", name_size, name);

    size_t knd_num_attr_validators = sizeof(knd_attr_validators) / sizeof(struct kndAttrValidator);

    for (size_t i = 0; i < knd_num_attr_validators; i++) {
        validator = &knd_attr_validators[i];
        knd_log("existing validator: \"%s\"", validator->name);

    }
    
    return knd_OK;
}


static int parse_validator(void *obj,
                             const char *rec,
                             size_t *total_size)
{
    struct kndAttr *self = (struct kndAttr*)obj;
    int err;
    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_validator,
          .obj = self
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}

static int parse_GSL(struct kndAttr *self,
                     const char *rec,
                     size_t *total_size)
{
    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. attr parsing: \"%s\"..", rec);

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "gloss",
          .name_size = strlen("gloss"),
          .parse = parse_gloss_change,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "c",
          .name_size = strlen("c"),
          .is_terminal = true,
          .buf = self->ref_classname,
          .buf_size = &self->ref_classname_size,
          .max_buf_size = KND_NAME_SIZE,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "card",
          .name_size = strlen("card"),
          .parse = parse_cardinality,
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
          .max_buf_size = KND_NAME_SIZE,
          .obj = self
        }
    };
    int err;
    
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    if (!*self->ref_classname)
        self->ref_classname_size = 0;
    if (!*self->default_val)
        self->default_val_size = 0;
    
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
