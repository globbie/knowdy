#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_concept.h"
#include "knd_output.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_text.h"
#include "knd_parser.h"

#define DEBUG_DC_LEVEL_1 0
#define DEBUG_DC_LEVEL_2 0
#define DEBUG_DC_LEVEL_3 0
#define DEBUG_DC_LEVEL_4 0
#define DEBUG_DC_LEVEL_5 0
#define DEBUG_DC_LEVEL_TMP 1

static int run_set_name(void *obj, struct kndTaskArg *args, size_t num_args);

static int read_GSL_file(struct kndConcept *self,
                         const char *filename,
                         size_t filename_size);

/*  Concept Destructor */
static void del(struct kndConcept *self)
{
    free(self);
}


static void str_attr_items(struct kndAttrItem *items, size_t depth)
{
    struct kndAttrItem *item;
    size_t offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;
    char *offset = malloc(offset_size + 1);

    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    for (item = items; item; item = item->next) {
        knd_log("%s%s_attr: \"%s\" => %s", offset, offset, item->name, item->val);
    }

}

static void str(struct kndConcept *self, size_t depth)
{
    struct kndAttr *attr;
    struct kndTranslation *tr;
    struct kndConcRef *ref;
    struct kndConcItem *item;

    size_t offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;
    char *offset = malloc(offset_size + 1);

    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    knd_log("\n\n%s{class %s%s", offset, self->namespace, self->name);

    for (tr = self->tr; tr; tr = tr->next) {
        knd_log("%s%s~ %s %s", offset, offset, tr->locale, tr->val);
    }

    if (self->summary)
        self->summary->str(self->summary, depth + 1);
    
    if (self->num_baseclass_items) {
        for (item = self->baseclass_items; item; item = item->next) {
            knd_log("%s%s_base \"%s\"", offset, offset, item->name);
            if (item->attrs) {
                str_attr_items(item->attrs, depth + 1);
            }
        }
    }

    attr = self->attrs;
    while (attr) {
        attr->str(attr, depth + 1);
        attr = attr->next;
    }

    for (size_t i = 0; i < self->num_children; i++) {
        ref = &self->children[i];
        knd_log("%s%sbase of --> %s", offset, offset, ref->conc->name);
    }

    knd_log("%s}", offset);
}

static void rewind_attrs(struct kndConcept *self)
{
    self->curr_attr = self->attrs;
    self->attrs_left = self->num_attrs;
}


static int next_attr(struct kndConcept *self,
                     struct kndAttr **result)
{
    struct kndAttr *attr = NULL;

    /* nested classes first */
    /*if (self->baseclass) {
        self->baseclass->next_attr(self->baseclass, &attr);
        if (attr) {
            *result = attr;
            return knd_OK;
        }
    }
    */

    if (!self->curr_attr) {
        *result = NULL;
        return knd_OK;
    }

    *result = self->curr_attr;
    self->curr_attr = self->curr_attr->next;
    self->attrs_left--;

    return knd_OK;
}

static int is_my_parent(struct kndConcept *self, struct kndConcept *parent)
{
    /*struct kndConcept *dc = self->baseclass;

    while (dc) {
        knd_log(".. parent: %s", dc->name);
        if (dc == parent) return knd_OK;

        dc = dc->baseclass;
        }*/

    return knd_NO_MATCH;
}

static int resolve_names(struct kndConcept *self)
{
    struct kndConcept *c, *root;
    struct kndConcRef *ref;
    struct kndConcItem *item;
    struct kndAttr *attr;
    int err;

    if (DEBUG_DC_LEVEL_2)
        knd_log(".. resolving class \"%s\"", self->name);

    if (!self->baseclass_items) {
        root = self->root_class;

        /* a child of the root class
         * TODO: refset */
        ref = &root->children[root->num_children];
        ref->conc = self;
        root->num_children++;
    }


    /* check inheritance paths */
    for (item = self->baseclass_items; item; item = item->next) {
        if (!item->name_size) continue;

        if (DEBUG_DC_LEVEL_2)
            knd_log(".. \"%s\" class to check its base class: \"%s\"..",
                    self->name, item->name);
                
        c = (struct kndConcept*)self->class_idx->get(self->class_idx,
                                                      (const char*)item->name);
        if (!c) {
            knd_log("-- couldn't resolve the \"%s\" base class :(",
                    item->name);
            //continue;
            return knd_FAIL;
        }

        /* prevent circled relations */
        err = c->is_a(c, self);
        if (!err) {
            knd_log("-- circled relationship detected: \"%s\" can't be the parent of \"%s\" :(",
                    item->name, self->name);
            return knd_FAIL;
        }

        if (DEBUG_DC_LEVEL_2)
            knd_log("++ \"%s\" confirmed as a base class for \"%s\"!",
                    item->name, self->name);

        item->ref = c;

        /* should we keep track of our children? */
        if (c->ignore_children) continue;
        
        if (c->num_children >= KND_MAX_CONC_CHILDREN) {
            knd_log("-- %s as child to %s - max conc children exceeded :(",
                    self->name, item->name);
            return knd_FAIL;
        }

        ref = &c->children[c->num_children];
        ref->conc = self;
        c->num_children++;
    }

    attr = self->attrs;
    while (attr) {
        if (attr->type != KND_ELEM_AGGR) goto next_attr;

        if (!attr->ref_classname_size) {
            knd_log("-- no classname specified for attr \"%s\"",
                        attr->name);
            return knd_FAIL;
        }

        /* try to resolve as an inline class */
        c = (struct kndConcept*)self->class_idx->get(self->class_idx,
                                                        (const char*)attr->ref_classname);
        if (!c) {
            knd_log("-- couldn't resolve the \"%s\" aggr attr :(",
                    attr->name);
            return knd_FAIL;
        }
        attr->dc = c;

    next_attr:
        attr = attr->next;
    }

    return knd_OK;
}

static int get_attr(struct kndConcept *self,
                    const char *name, size_t name_size,
                    struct kndAttr **result)
{
    struct kndAttr *attr = NULL;
    int err;

    rewind_attrs(self);

    do {
        err = next_attr(self, &attr);
        if (!attr) break;

        knd_log("check ATTR: %s (%s)", attr->name, attr->parent_dc->name);

        if (!strncmp(attr->name, name, name_size)) {
            *result = attr;
            return knd_OK;
        }
    } while (attr);

    return knd_NO_MATCH;
}


static int parse_field(void *obj,
                       const char *name, size_t name_size,
                       const char *rec, size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndAttr *attr = NULL;
    int err;

    if (DEBUG_DC_LEVEL_TMP) {
        knd_log("\n.. validating attr: \"%s\"\n", self->curr_val);
    }

    err = get_attr(self, (const char*)self->curr_val, self->curr_val_size, &attr);
    if (err) {
        knd_log("-- no such attr: \"%s\" :(", self->curr_val);
        return err;
    }

    return knd_FAIL;
}



static int run_set_translation_text(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndTranslation *tr = (struct kndTranslation*)obj;
    struct kndTaskArg *arg;
    const char *val = NULL;
    size_t val_size = 0;
    int err;

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

    if (DEBUG_DC_LEVEL_2)
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

    if (DEBUG_DC_LEVEL_2) {
        knd_log("..  gloss translation in \"%s\" REC: \"%s\"\n",
                name, rec); }

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
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndTranslation *tr;
    int err;

    if (DEBUG_DC_LEVEL_2)
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


static int parse_summary_change(void *obj,
                                const char *rec,
                                size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndText *text;
    int err;

    if (DEBUG_DC_LEVEL_2)
        knd_log(".. parsing the summary change: \"%s\"", rec);

    text = self->summary;
    if (!text) {
        err = kndText_new(&text);
        if (err) return err;
        self->summary = text;
    }

    err = text->parse(text, rec, total_size);
    if (err) return err;

    return knd_OK;
}


static int parse_aggr_change(void *obj,
                             const char *rec,
                             size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndAttr *attr;
    int err;

    if (DEBUG_DC_LEVEL_2)
        knd_log(".. parsing the AGGR attr change: \"%s\"", rec);

    err = kndAttr_new(&attr);
    if (err) return err;
    attr->parent_dc = self;
    attr->type = KND_ELEM_AGGR;

    err = attr->parse(attr, rec, total_size);
    if (err) {
        if (DEBUG_DC_LEVEL_TMP)
            knd_log("-- failed to parse the AGGR attr: %d", err);
        return err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    return knd_OK;
}

static int parse_str_change(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndAttr *attr;
    int err;

    err = kndAttr_new(&attr);
    if (err) return err;
    attr->parent_dc = self;
    attr->type = KND_ELEM_STR;

    err = attr->parse(attr, rec, total_size);
    if (err) {
        knd_log("-- failed to parse the STR attr: %d", err);
        return err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    return knd_OK;
}

static int parse_ref_change(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndAttr *attr;
    int err;

    err = kndAttr_new(&attr);
    if (err) return err;
    attr->parent_dc = self;
    attr->type = KND_ELEM_REF;

    err = attr->parse(attr, rec, total_size);
    if (err) {
        knd_log("-- failed to parse the REF attr: %d", err);
        return err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    return knd_OK;
}

static int parse_text_change(void *obj,
                             const char *rec,
                             size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndAttr *attr;
    int err;

    err = kndAttr_new(&attr);
    if (err) return err;
    attr->parent_dc = self;
    attr->type = KND_ELEM_TEXT;

    err = attr->parse(attr, rec, total_size);
    if (err) {
        knd_log("-- failed to parse the TEXT attr: %d", err);
        return err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }
    return knd_OK;
}



static int run_set_baseclass_item(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;
    struct kndConcItem *item;
    int err;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE)
        return knd_LIMIT;

    item = malloc(sizeof(struct kndConcItem));
    memset(item, 0, sizeof(struct kndConcItem));

    memcpy(item->name, name, name_size);
    item->name_size = name_size;
    item->name[name_size] = '\0';

    if (DEBUG_DC_LEVEL_2)
        knd_log("== baseclass item name set: \"%s\"", item->name);

    item->next = self->baseclass_items;
    self->baseclass_items = item;
    self->num_baseclass_items++;

    return knd_OK;
}




static int set_attr_item_val(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndAttrItem *item = (struct kndAttrItem*)obj;
    struct kndTaskArg *arg;
    const char *val = NULL;
    size_t val_size = 0;
    int err;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            val = arg->val;
            val_size = arg->val_size;
        }
    }

    if (!val_size) return knd_FAIL;
    if (val_size >= KND_NAME_SIZE) return knd_LIMIT;

    if (DEBUG_DC_LEVEL_2)
        knd_log(".. run set attr item val: %s\n", val);

    memcpy(item->val, val, val_size);
    item->val[val_size] = '\0';
    item->val_size = val_size;

    return knd_OK;
}



static int parse_baseclass_item(void *obj,
                                const char *name, size_t name_size,
                                const char *rec, size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndConcItem *baseclass_item;
    struct kndAttrItem *item;
    int err;

    if (!self->baseclass_items) return knd_FAIL;

    baseclass_item = self->baseclass_items;

    item = malloc(sizeof(struct kndAttrItem));
    memset(item, 0, sizeof(struct kndAttrItem));
    memcpy(item->name, name, name_size);
    item->name_size = name_size;
    item->name[name_size] = '\0';

    if (DEBUG_DC_LEVEL_2)
        knd_log("== attr item name set: \"%s\" REC: %s", item->name, rec);

    item->val_size = KND_NAME_SIZE;

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_item_val,
          .obj = item
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    item->next = baseclass_item->attrs;
    baseclass_item->attrs = item;
    baseclass_item->num_attrs++;

    return knd_OK;
}

static int parse_baseclass(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    int err;

    if (DEBUG_DC_LEVEL_1)
        knd_log(".. parsing the base class: \"%s\"", rec);

    struct kndTaskSpec specs[] = {
        { .name = "baseclass item",
          .name_size = strlen("baseclass item"),
          .is_implied = true,
          .run = run_set_baseclass_item,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "baseclass_item",
          .name_size = strlen("baseclass_item"),
          .is_validator = true,
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_NAME_SIZE,
          .validate = parse_baseclass_item,
          .obj = self
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}


/* TODO: reconsider this */
static int run_set_children_setting(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndTaskArg *arg;
    const char *val = NULL;
    size_t val_size = 0;
    int err;

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

    if (DEBUG_DC_LEVEL_2)
        knd_log(".. keep track of children option: %s\n", val);

    if (!strncmp("false", val, val_size))
        self->ignore_children = true;
    
    return knd_OK;
}


static int parse_children_settings(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_children_setting,
          .obj = self
        }
    };
    int err;

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}

static int import_class(void *obj,
                        const char *rec,
                        size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndConcept *c, *prev;
    size_t chunk_size;
    int err;

    if (DEBUG_DC_LEVEL_2)
        knd_log(".. import \"%.*s\" class..", 16, rec);

    err = kndConcept_new(&c);
    if (err) return err;
    c->out = self->out;
    c->class_idx = self->class_idx;
    c->root_class = self;

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = c
        },
        { .type = KND_CHANGE_STATE,
          .name = "base",
          .name_size = strlen("base"),
          .parse = parse_baseclass,
          .obj = c
        },
        { .type = KND_CHANGE_STATE,
          .name = "children",
          .name_size = strlen("children"),
          .parse = parse_children_settings,
          .obj = c
        },
        { .type = KND_CHANGE_STATE,
          .name = "gloss",
          .name_size = strlen("gloss"),
          .parse = parse_gloss_change,
          .obj = c
        },
        { .type = KND_CHANGE_STATE,
          .name = "summary",
          .name_size = strlen("summary"),
          .parse = parse_summary_change,
          .obj = c
        },
        { .type = KND_CHANGE_STATE,
          .name = "aggr",
          .name_size = strlen("aggr"),
          .parse = parse_aggr_change,
          .obj = c
        },
        { .type = KND_CHANGE_STATE,
          .name = "str",
          .name_size = strlen("str"),
          .parse = parse_str_change,
          .obj = c
        },
        {  .type = KND_CHANGE_STATE,
           .name = "ref",
          .name_size = strlen("ref"),
          .parse = parse_ref_change,
          .obj = c
        },
        { .type = KND_CHANGE_STATE,
          .name = "text",
          .name_size = strlen("text"),
          .parse = parse_text_change,
          .obj = c
        },
        { .is_validator = true,
          .buf = c->curr_val,
          .buf_size = &c->curr_val_size,
          .max_buf_size = KND_NAME_SIZE,
          .validate = parse_field,
          .obj = c
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) goto final;

    if (!c->name_size) {
        err = knd_FAIL;
        goto final;
    }

    //c->str(c, 1);

    prev = (struct kndConcept*)self->class_idx->get(self->class_idx,
                                                    (const char*)c->name);
    if (prev) {
        knd_log("-- %s class name doublet found :(", c->name);
        err = knd_OK;
        //goto final;
    }
    
    err = self->class_idx->set(self->class_idx,
                               (const char*)c->name, (void*)c);
    if (err) goto final;



    
    return knd_OK;
 final:
    
    c->del(c);
    return err;
}


static int run_set_namespace(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;
    int err;

    if (DEBUG_DC_LEVEL_1)
        knd_log(".. run set namespace..");

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE)
        return knd_LIMIT;

    memcpy(self->namespace, name, name_size);
    self->namespace_size = name_size;
    self->namespace[name_size] = '\0';

    return knd_OK;
}

static int run_set_name(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;
    int err;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE)
        return knd_LIMIT;

    memcpy(self->name, name, name_size);
    self->name_size = name_size;
    self->name[name_size] = '\0';


    return knd_OK;
}

static int run_read_include(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndTaskArg *arg;
    struct kndConcFolder *folder;
    const char *name = NULL;
    size_t name_size = 0;
    int err;

    if (DEBUG_DC_LEVEL_2)
        knd_log(".. running include file func.. num args: %lu", (unsigned long)num_args);

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }

    if (!name_size) return knd_FAIL;

    if (DEBUG_DC_LEVEL_2)
        knd_log("== got include file name: \"%s\"..", name);

    folder = malloc(sizeof(struct kndConcFolder));
    if (!folder) return knd_NOMEM;
    memset(folder, 0, sizeof(struct kndConcFolder));

    memcpy(folder->name, name, name_size);
    folder->name_size = name_size;
    folder->name[name_size] = '\0';

    folder->next = self->folders;
    self->folders = folder;
    self->num_folders++;

    return knd_OK;
}


static int parse_schema(void *self,
                        const char *rec,
                        size_t *total_size)
{
    if (DEBUG_DC_LEVEL_2)
        knd_log(".. parse schema REC: \"%s\"..", rec);

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_namespace,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = import_class,
          .obj = self
        }
    };
    int err;

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    if (DEBUG_DC_LEVEL_2)
        knd_log("++ schema parse OK!");

    return knd_OK;
}

static int parse_include(void *self,
                         const char *rec,
                         size_t *total_size)
{
    const char *c;
    
    if (DEBUG_DC_LEVEL_2)
        knd_log(".. parse include REC: \"%s\"..", rec);

    struct kndTaskSpec specs[] = {
        { .name = "default",
          .name_size = strlen("default"),
          .is_default = true,
          .run = run_read_include,
          .obj = self
        },
        { .name = "lazy",
          .name_size = strlen("lazy"),
          .obj = self
        }
    };
    int err;

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}


static int parse_GSL(struct kndConcept *self,
                     const char *rec,
                     size_t *total_size)
{
    struct kndTaskSpec specs[] = {
        { .name = "schema",
          .name_size = strlen("schema"),
          .parse = parse_schema,
          .obj = self
        },
        { .name = "include",
          .name_size = strlen("include"),
          .parse = parse_include,
          .obj = self
        }
    };
    int err;

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}

static int read_GSL_file(struct kndConcept *self,
                         const char *filename,
                         size_t filename_size)
{
    struct kndOutput *out = self->out;
    struct kndConcFolder *folder, **folders;
    size_t num_folders;
    size_t chunk_size = 0;
    int err;

    if (DEBUG_DC_LEVEL_TMP)
        knd_log("..read \"%s\"..", filename);

    out->reset(out);

    err = out->write(out, self->dbpath, self->dbpath_size);
    if (err) return err;
    err = out->write(out, "/", 1);
    if (err) return err;
    err = out->write(out, filename, filename_size);
    if (err) return err;
    err = out->write(out, ".gsl", strlen(".gsl"));
    if (err) return err;

    err = out->read_file(out, (const char*)out->buf, out->buf_size);
    if (err) {
        knd_log("-- couldn't read GSL class file \"%s\" :(", out->buf);
        return err;
    }

    out->file[out->file_size] = '\0';
    
    err = parse_GSL(self, (const char*)out->file, &chunk_size);
    if (err) return err;

    /* high time to read our folders */
    folders = self->folders;
    self->folders = NULL;
    self->num_folders = 0;
    
    for (folder = folders; folder; folder = folder->next) {
        /*knd_log(".. should now read the \"%s\" folder..",
                folder->name);
        */
        
        err = read_GSL_file(self, folder->name, folder->name_size);
        if (err) return err;
    }
    return knd_OK;
}

static int resolve_class_refs(struct kndConcept *self)
{
    struct kndConcept *dc, *bc;
    const char *key;
    void *val;
    int err;

    if (DEBUG_DC_LEVEL_2)
        knd_log(".. resolving class refs by \"%s\"", self->name);

    key = NULL;
    self->class_idx->rewind(self->class_idx);
    do {
        self->class_idx->next_item(self->class_idx, &key, &val);
        if (!key) break;

        dc = (struct kndConcept*)val;

        err = dc->resolve(dc);
        if (err) {
            knd_log("-- couldn't resolve the \"%s\" class :(", dc->name);
            return err;
        }
    } while (key);

    /* display all classes */
    if (DEBUG_DC_LEVEL_2) {
        key = NULL;
        self->class_idx->rewind(self->class_idx);
        do {
            self->class_idx->next_item(self->class_idx, &key, &val);
            if (!key) break;

            dc = (struct kndConcept*)val;

            dc->str(dc, 1);

        } while (key);
    }

    return knd_OK;
}



static int select_classes(struct kndConcept *self, struct kndTaskArg *args, size_t num_args)
{
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;
    int err;

    if (DEBUG_DC_LEVEL_TMP)
        knd_log(".. selecting classes in \"%s\".. [locale: %s] format: %d",
                self->name, self->locale, self->format);

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }

    err = self->export(self);
    if (err) return err;

    return knd_OK;
}




static int get_class(struct kndConcept *self,
                     const char *name, size_t name_size)
{
    struct kndConcept *c;
    int err;

    if (DEBUG_DC_LEVEL_TMP)
        knd_log(".. get class: \"%s\".. [locale: %s %lu] format: %d",
                name, self->locale, (unsigned long)self->locale_size, self->format);
   
    c = (struct kndConcept*)self->class_idx->get(self->class_idx, name);
    if (!c) {
        knd_log("-- no such class: \"%s\" :(", name);
        return knd_NO_MATCH;
    }

    c->out = self->out;
    c->locale = self->locale;
    c->locale_size = self->locale_size;
    c->format = self->format;

    err = c->export(c);
    if (err) return err;

    return knd_OK;
}


static int
kndConcept_export_GSL(struct kndConcept *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    int err;

    buf_size = sprintf(buf, "(N^%s)",
                       self->name);

    err = self->out->write(self->out, buf, buf_size);

    return err;
}



static int attr_items_JSON(struct kndConcept *self,
                           struct kndAttrItem *items, size_t depth)
{
    struct kndAttrItem *item;
    struct kndOutput *out;
    size_t attr_count = 0;
    int err;

    out = self->out;

    for (item = items; item; item = item->next) {
        err = out->write(out, ",", 1);
        if (err) return err;
        err = out->write(out, "\"", 1);
        if (err) return err;
        err = out->write(out, item->name, item->name_size);
        if (err) return err;
        err = out->write(out, "\":\"", strlen("\":\""));
        if (err) return err;
        err = out->write(out, item->val, item->val_size);
        if (err) return err;
        err = out->write(out, "\"", 1);
        if (err) return err;
    }
    
    return knd_OK;
}


static int
kndConcept_export_JSON(struct kndConcept *self)
{
    struct kndTranslation *tr;
    struct kndAttr *attr;

    struct kndConcept *c;
    struct kndConcItem *item;
    struct kndConcRef *ref;

    struct kndOutput *out;
    size_t item_count;
    int i, err;

    if (DEBUG_DC_LEVEL_2)
        knd_log(".. JSON export concept: \"%s\"   locale: %s\n", self->name, self->locale);

    out = self->out;

    err = out->write(out, "{", 1);
    if (err) return err;

    err = out->write(out, "\"n\":\"", strlen("\"n\":\""));
    if (err) return err;

    err = out->write(out, self->name, self->name_size);
    if (err) return err;

    err = out->write(out, "\"", 1);
    if (err) return err;

    /* choose gloss */
    tr = self->tr;
    while (tr) {
        if (DEBUG_DC_LEVEL_2)
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

    /* display base classes only once */
    if (self->num_baseclass_items) {
        err = out->write(out, ",\"base\":[", strlen(",\"base\":["));
        if (err) return err;

        item_count = 0;
        for (item = self->baseclass_items; item; item = item->next) {
            if (item->ref && item->ref->ignore_children) continue;

            if (item_count) {
                err = out->write(out, ",", 1);
                if (err) return err;
            }

            err = out->write(out, "{\"n\":\"", strlen("{\"n\":\""));
            if (err) return err;
            
            err = out->write(out, item->name, item->name_size);
            if (err) return err;
            err = out->write(out, "\"", 1);
            if (err) return err;

            if (item->attrs) {
                err = attr_items_JSON(self, item->attrs, 0);
                if (err) return err;
            }
            
            err = out->write(out, "}", 1);
            if (err) return err;

            item_count++;
        }
        err = out->write(out, "]", 1);
        if (err) return err;
    }


    

    
    if (self->attrs) {
        err = out->write(out, ",\"attrs\": {",
                         strlen(",\"attrs\": {"));
        if (err) return err;

        i = 0;
        attr = self->attrs;
        while (attr) {
            if (i) {
                err = out->write(out, ",", 1);
                if (err) return err;
            }

            attr->out = out;
            attr->format = KND_FORMAT_JSON;
            err = attr->export(attr);
            if (err) {
                if (DEBUG_DC_LEVEL_TMP)
                    knd_log("-- failed to export %s attr to JSON: %s\n", attr->name);
                return err;
            }

            i++;
            attr = attr->next;
        }
        err = out->write(out, "}", 1);
        if (err) return err;

    }

    if (self->num_children) {
        err = out->write(out, ",\"children\":[", strlen(",\"children\":["));
        if (err) return err;
        for (size_t i = 0; i < self->num_children; i++) {
            ref = &self->children[i];

            if (DEBUG_DC_LEVEL_2)
                knd_log("base of --> %s", ref->conc->name);

            if (i) {
                err = out->write(out, ",", 1);
                if (err) return err;
            }

            c = ref->conc;
            c->out = self->out;
            c->locale = self->locale;
            c->locale_size = self->locale_size;
            c->format =  KND_FORMAT_JSON;
            c->depth = self->depth + 1;
            
            err = c->export(c);
            if (err) return err;
        }
        err = out->write(out, "]", 1);
        if (err) return err;
    }

    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}



static int
kndConcept_export_HTML(struct kndConcept *self)
{
    struct kndTranslation *tr;
    struct kndAttr *attr;
    struct kndOutput *out;
    int i;
    int err;

    if (DEBUG_DC_LEVEL_3)
        knd_log("   .. export HTML: %s\n",
                self->name);

    return knd_OK;

    out = self->out;

    err = out->write(out,
                     "{", 1);
    if (err) return err;

    err = out->write(out,
                     "\"n\":\"", strlen("\"n\":\""));
    if (err) return err;

    err = out->write(out,
                     self->name, self->name_size);
    if (err) return err;

    err = out->write(out,
                     "\"", 1);
    if (err) return err;

    /* choose gloss */
    tr = self->tr;
    while (tr) {
        if (DEBUG_DC_LEVEL_3)
            knd_log("LANG: %s\n", self->locale);

        if (strcmp(tr->locale, self->locale)) goto next_tr;

        err = out->write(out,
                         ",\"gloss\":\"", strlen(",\"gloss\":\""));
        if (err) return err;

        err = out->write(out, tr->seq,  tr->seq_size);
        if (err) return err;

        err = out->write(out, "\"", 1);
        if (err) return err;

        break;

    next_tr:
        tr = tr->next;
    }

    err = out->write(out, ",\"attr_l\":[", strlen(",\"attr_l\":["));
    if (err) return err;

    i = 0;
    attr = self->attrs;
    while (attr) {
        if (i) {
            err = out->write(out, ",", 1);
            if (err) return err;
        }

        /*err = kndConcept_export_attr_JSON(self, attr);
        if (err) goto final;
        */

        i++;
        attr = attr->next;
    }

    err = out->write(out, "]}", 2);
    return err;
}


static int export(struct kndConcept *self)
{
    switch(self->format) {
        case KND_FORMAT_JSON:
        return kndConcept_export_JSON(self);
    case KND_FORMAT_HTML:
        return kndConcept_export_HTML(self);
    case KND_FORMAT_GSL:
        return kndConcept_export_GSL(self);
    default:
        break;
    }

    knd_log("-- format %d not supported :(", self->format);
    return knd_FAIL;
}

/*  Concept initializer */
static void init(struct kndConcept *self)
{
    self->del = del;
    self->str = str;
    self->open = read_GSL_file;

    self->coordinate = resolve_class_refs;
    self->resolve = resolve_names;

    self->import = import_class;

    self->export = export;
    self->select = select_classes;
    self->get = get_class;

    self->is_a = is_my_parent;

    self->rewind = rewind_attrs;
    self->next_attr = next_attr;
}

extern int
kndConcept_new(struct kndConcept **c)
{
    struct kndConcept *self;

    self = malloc(sizeof(struct kndConcept));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndConcept));

    init(self);

    *c = self;

    return knd_OK;
}
