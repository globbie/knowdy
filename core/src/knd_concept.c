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

static void str(struct kndConcept *self, size_t depth)
{
    struct kndAttr *attr;
    struct kndTranslation *tr;
    struct kndConcRef *ref;
    
    size_t offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;
    char *offset = malloc(offset_size + 1);

    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    knd_log("\n\n%s{class %s%s", offset, self->namespace, self->name);

    if (self->baseclass_name_size) {
        knd_log("\n%s%s_base: \"%s\"", offset, offset, self->baseclass_name);
    }
    
    tr = self->tr;
    while (tr) {
        knd_log("%s%s~ %s:%s", offset, offset, tr->lang_code, tr->val);
        tr = tr->next;
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
    if (self->baseclass) {
        self->baseclass->rewind(self->baseclass);
    }
    
    self->curr_attr = self->attrs;
    self->attrs_left = self->num_attrs;
}


static int next_attr(struct kndConcept *self,
                     struct kndAttr **result)
{
    struct kndAttr *attr = NULL;

    /* nested classes first */
    if (self->baseclass) {
        self->baseclass->next_attr(self->baseclass, &attr);
        if (attr) {
            *result = attr;
            return knd_OK;
        }
    }
    
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
    struct kndConcept *dc = self->baseclass;

    while (dc) {
        knd_log(".. parent: %s", dc->name);
        if (dc == parent) return knd_OK;
        
        dc = dc->baseclass;
    }
    return knd_NO_MATCH;
}

static int resolve_names(struct kndConcept *self)
{
    struct kndConcept *dc;
    struct kndConcRef *ref;
    struct kndAttr *attr;
    int err;
    
    /* check inheritance paths */
    if (self->baseclass_name_size) {
        knd_log(".. \"%s\" class to check its base class: \"%s\"..",
                self->name, self->baseclass_name);
        
        dc = (struct kndConcept*)self->class_idx->get(self->class_idx,
                                                        (const char*)self->baseclass_name);
        if (!dc) {
            knd_log("-- couldn't resolve the \"%s\" base class :(",
                    self->baseclass_name);
            return knd_FAIL;
        }

        /* prevent circled relations */
        err = dc->is_a(dc, self);
        if (!err) {
            knd_log("-- circled relationship detected: \"%s\" can't be the parent of \"%s\" :(",
                    self->baseclass_name, self->name);
            return knd_FAIL;
        }

        if (DEBUG_DC_LEVEL_TMP)
            knd_log("++ \"%s\" confirmed as a base class for \"%s\"!",
                    self->baseclass_name, self->name);

        self->baseclass = dc;
        ref = &dc->children[dc->num_children];
        ref->conc = self;
        dc->num_children++;
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
        dc = (struct kndConcept*)self->class_idx->get(self->class_idx,
                                                        (const char*)attr->ref_classname);
        if (!dc) {
            knd_log("-- couldn't resolve the \"%s\" aggr attr :(",
                    attr->name);
            return knd_FAIL;
        }
        attr->dc = dc;
        
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

    knd_log("== my baseclass: %p", self->baseclass);
    
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

    // TODO: remove to config values
    const char *locales[] = {"ru_RU", "en_EN"};
    size_t num_locales = sizeof(locales) / sizeof(const char*);
    int err;

    if (DEBUG_DC_LEVEL_1) {
        knd_log("\n.. validating gloss translation in \"%s\" REC: \"%s\"\n", name, rec);
    }

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_translation_text,
          .obj = tr
        }
    };

    for (size_t i = 0; i < num_locales; i++) {
        if (!strncmp(locales[i], name, name_size)) {
            memcpy(tr->lang_code, name, name_size);
            tr->lang_code[name_size] = '\0';
            tr->lang_code_size = name_size;

            err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
            if (err) return err;
 
            return knd_OK;
        }
    }

    knd_log("-- locale \"%s\" is not supported :(", name);
    
    return knd_FAIL;
}

static int parse_gloss_change(void *obj,
                              const char *rec,
                              size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndTranslation *tr;
    int err;

    if (DEBUG_DC_LEVEL_1)
        knd_log(".. parsing the gloss change: \"%s\"", rec);

    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return knd_NOMEM;
    memset(tr, 0, sizeof(struct kndTranslation));

    tr->lang_code_size = KND_LANG_CODE_SIZE;

    struct kndTaskSpec specs[] = {
        { .is_validator = true,
          .buf = tr->lang_code,
          .buf_size = &tr->lang_code_size,
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
    
    return knd_OK;
}


static int parse_class_change(void *obj,
                              const char *rec,
                              size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndConcept *dc;
    size_t chunk_size;
    int err;
    
    if (DEBUG_DC_LEVEL_2)
        knd_log(".. parse CLASS change: \"%s\"..", rec);

    err = kndConcept_new(&dc);
    if (err) return err;
    dc->out = self->out;
    dc->baseclass_name_size = KND_NAME_SIZE;
    dc->curr_val_size = KND_NAME_SIZE;
    dc->class_idx = self->class_idx;
    
    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = dc
        },
        { .type = KND_CHANGE_STATE,
          .name = "base",
          .name_size = strlen("base"),
          .buf = dc->baseclass_name,
          .buf_size = &dc->baseclass_name_size,
          .obj = dc
        },
        { .type = KND_CHANGE_STATE,
          .name = "gloss",
          .name_size = strlen("gloss"),
          .parse = parse_gloss_change,
          .obj = dc
        },
        { .type = KND_CHANGE_STATE,
          .name = "aggr",
          .name_size = strlen("aggr"),
          .parse = parse_aggr_change,
          .obj = dc
        },
        { .type = KND_CHANGE_STATE,
          .name = "str",
          .name_size = strlen("str"),
          .parse = parse_str_change,
          .obj = dc
        },
        {  .type = KND_CHANGE_STATE,
           .name = "ref",
          .name_size = strlen("ref"),
          .parse = parse_ref_change,
          .obj = dc
        },
        { .type = KND_CHANGE_STATE,
          .name = "text",
          .name_size = strlen("text"),
          .parse = parse_text_change,
          .obj = dc
        },
        { .is_validator = true,
          .buf = dc->curr_val,
          .buf_size = &dc->curr_val_size,
          .parse = parse_field,
          .obj = dc
        }
    };
    
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    if (!dc->name_size) {
        return knd_FAIL;
    }
    
    if (!(*dc->baseclass_name)) {
        dc->baseclass_name_size = 0;
        dc->baseclass = self;
    }
    
    //dc->str(dc, 1);

    err = self->class_idx->set(self->class_idx,
                               (const char*)dc->name, (void*)dc);
    if (err) return err;
    
    return knd_OK;
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
    const char *name = NULL;
    size_t name_size = 0;
    int err;
    
    if (DEBUG_DC_LEVEL_1)
        knd_log(".. running include file func..");

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

    err = read_GSL_file(self, name, name_size);
    if (err) return err;
    
    return knd_OK;
}


static int parse_schema(void *self,
                        const char *rec,
                        size_t *total_size)
{
    if (DEBUG_DC_LEVEL_1)
        knd_log(".. parse schema REC: \"%s\"..", rec);

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_namespace,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_change,
          .obj = self
        }
    };
    int err;
    
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    if (DEBUG_DC_LEVEL_1)
        knd_log("++ schema parse OK!");

    return knd_OK;
}

static int parse_include(void *self,
                         const char *rec,
                         size_t *total_size)
{
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
    if (DEBUG_DC_LEVEL_1)
        knd_log(".. parse GSL REC: \"%s\"..", rec);

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
    size_t chunk_size = 0;
    int err;

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

    return knd_OK;
}


static int resolve_class_refs(struct kndConcept *self)
{
    struct kndConcept *dc, *bc;
    const char *key;
    void *val;
    int err = knd_FAIL;

    if (DEBUG_DC_LEVEL_TMP)
        knd_log("   .. resolving class refs..");
    
    key = NULL;
    self->class_idx->rewind(self->class_idx);
    do {
        self->class_idx->next_item(self->class_idx, &key, &val);
        if (!key) break;

        dc = (struct kndConcept*)val;

        err = dc->resolve(dc);
        if (err) goto final;

        if (dc->baseclass_name_size) {
            if (DEBUG_DC_LEVEL_3)
                knd_log("   .. looking up baseclass \"%s\" for \"%s\"..\n",
                        dc->baseclass_name, dc->name);

            bc = (struct kndConcept*)self->class_idx->get(self->class_idx,
                                                            (const char*)dc->baseclass_name);
            if (!bc) {
                knd_log("  -- baseclass \"%s\" for \"%s\" not found :(\n",
                        dc->baseclass_name, dc->name);
                err = knd_FAIL;
                goto final;
            }
            dc->baseclass = bc;
        }
        
    } while (key);


    /* display all classes */
    if (DEBUG_DC_LEVEL_TMP) {
        key = NULL;
        self->class_idx->rewind(self->class_idx);
        do {
            self->class_idx->next_item(self->class_idx, &key, &val);
            if (!key) break;

            dc = (struct kndConcept*)val;

            dc->str(dc, 1);
            
        } while (key);
    }

    
    err = knd_OK;
    
final:
    return err;
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



static int
kndConcept_export_JSON(struct kndConcept *self)
{
    struct kndTranslation *tr;
    struct kndAttr *attr;
    struct kndOutput *out;
    int i, err;
    
    if (DEBUG_DC_LEVEL_3)
        knd_log("   .. export JSON DATACLASS: %s\n",
                self->name);

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
            knd_log("LANG: %s\n", self->lang_code);
        
        if (strcmp(tr->lang_code, self->lang_code)) goto next_tr;
        
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
        err = attr->export(attr, KND_FORMAT_JSON);
        if (err) {
            if (DEBUG_DC_LEVEL_TMP)
                knd_log("-- failed to export %s attr to JSON: %s\n", attr->name);
            return err;
        }
        
        i++;

        attr = attr->next;
    }

    err = out->write(out, "}}", 2);
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
            knd_log("LANG: %s\n", self->lang_code);
        
        if (strcmp(tr->lang_code, self->lang_code)) goto next_tr;
        
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

    err = out->write(out,
                     ",\"attr_l\":[", strlen(",\"attr_l\":["));
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


static int 
kndConcept_export(struct kndConcept *self, knd_format format)
{
    int err = knd_FAIL;
    
    switch(format) {
        case KND_FORMAT_JSON:
        err = kndConcept_export_JSON(self);
        if (err) goto final;
        break;
    case KND_FORMAT_HTML:
        err = kndConcept_export_HTML(self);
        if (err) goto final;
        break;
    case KND_FORMAT_GSL:
        err = kndConcept_export_GSL(self);
        if (err) goto final;
        break;
    default:
        break;
    }

 final:
    return err;
}

/*  Concept Initializer */
static void init(struct kndConcept *self)
{
    self->del = del;
    self->str = str;
    self->read_file = read_GSL_file;

    self->coordinate = resolve_class_refs;
    self->resolve = resolve_names;
    self->export = kndConcept_export;

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
