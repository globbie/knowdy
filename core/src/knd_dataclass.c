#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_dataclass.h"
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

static int read_GSL_file(struct kndDataClass *self,
                         const char *filename,
                         size_t filename_size);

/*  DataClass Destructor */
static void del(struct kndDataClass *self)
{
    free(self);
}

static int str(struct kndDataClass *self, size_t depth)
{
    struct kndAttr *attr;
    struct kndTranslation *tr;
    
    size_t offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;
    char *offset = malloc(offset_size + 1);

    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    knd_log("\n\n%s{class %s", offset, self->name);

    if (self->baseclass_name_size) {
        knd_log("\n%s_baseclass: \"%s\"", offset, self->baseclass_name);
        if (self->baseclass)
            self->baseclass->str(self->baseclass, depth + 1);
    }
    
    tr = self->tr;
    while (tr) {
        knd_log("   ~ %s:%s", tr->lang_code, tr->seq);
        tr = tr->next;
    }
    
    if (!self->attrs) 
        knd_log("%s)", offset);
    
    attr = self->attrs;
    while (attr) {
        attr->str(attr, depth + 1);
        attr = attr->next;
    }

    if (self->attrs) 
        knd_log("%s)", offset);
    
    if (self->idx_name_size) 
        knd_log("%s  == CLASS IDX: %s\n", offset, self->idx_name);

    return knd_OK;
}



static void
kndDataClass_rewind(struct kndDataClass *self)
{
    if (self->baseclass) {
        self->baseclass->rewind(self->baseclass);
    }
    
    self->curr_attr = self->attrs;
    self->attrs_left = self->num_attrs;
}


static int
kndDataClass_next_attr(struct kndDataClass *self,
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



static int
kndDataClass_set_name(struct kndDataClass *self,
                      char *rec)
{
    struct kndDataClass *dc;
    char *b, *c;
    size_t name_size = 0;
    
    c = rec;
    b = self->name;
    
    bool separ_present = false;
    bool in_init = true;
    
    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:
            in_init = false;
            
            if (separ_present) {
                *b = ' ';
                name_size++;
                b++;

                if (name_size >= KND_NAME_SIZE)
                return knd_LIMIT;

                separ_present = false;
            }

            *b = *c;
            name_size++;
            b++;
            
            if (name_size >= KND_NAME_SIZE)
                return knd_LIMIT;
            
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (in_init)
                break;
            
            if (!separ_present)
                separ_present = true;
            
            break;
        }
        c++;
    }

    
    if (!name_size) return knd_FAIL;

    self->name_size = name_size;

    /*knd_log("++ normalized class name: \"%s\"", self->name);*/
    
    dc = self->class_idx->get(self->class_idx,
                              (const char*)self->name);
    if (dc) {
        knd_log("  -- class name \"%s\" already exists :(", self->name);
        return knd_FAIL;
    }

    
    return knd_OK;
}


static int
kndDataClass_read_GSL_glosses(struct kndDataClass *self,
                              char *rec,
                              size_t *chunk_size)
{
    struct kndTranslation *tr = NULL;
    size_t buf_size = 0;
    char *c;
    char *b;

    bool in_key = false;
    bool in_val = false;
    int err = knd_FAIL;
    
    c = rec;
    b = rec;

    while (*c) {

        switch (*c) {
        default:
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (in_key) {
                tr = malloc(sizeof(struct kndTranslation));
                if (!tr) return knd_NOMEM;
                memset(tr, 0, sizeof(struct kndTranslation));

                buf_size = c - b;
                if (!buf_size) return knd_FAIL;
                                   
                tr->lang_code_size = buf_size;
                memcpy(tr->lang_code, b, buf_size);
                tr->lang_code[buf_size] = '\0';

                in_key = false;
                in_val = true;
                b = c + 1;
                break;
            }
            
            break;
        case '{':
            if (!in_key) {
                in_key = true;
            }
            b = c + 1;
            break;
        case '}':

            if (in_val) {
                buf_size = c - b;

                if (!buf_size) return knd_FAIL;

                if (buf_size > KND_LARGE_BUF_SIZE) return knd_LIMIT;

                tr->seq = malloc(buf_size);
                if (!tr->seq) return knd_NOMEM;

                memcpy(tr->seq, b, buf_size);
                tr->seq_size = buf_size;
                tr->seq[buf_size] = '\0';

                tr->next = self->tr;
                self->tr = tr;
                
                tr = NULL;
                in_val = false;
                b = c + 1;
                break;
            }


            self->tr = tr;
            
            break;
        case ']':
            *chunk_size = c - rec;
            return knd_OK;
        }

        c++;
    }

    return err;
}



static int 
kndDataClass_resolve(struct kndDataClass *self)
{
    struct kndDataClass *dc;
    struct kndAttr *attr;

    attr = self->attrs;
    while (attr) {
        if (attr->type != KND_ELEM_AGGR) goto next_attr;

        /* try to resolve as an inline class */
        dc = (struct kndDataClass*)self->class_idx->get(self->class_idx,
                                                        (const char*)attr->fullname);
        if (dc) {
            if (DEBUG_DC_LEVEL_2)
                knd_log("    ++ attr \"%s => %s\" is resolved as inline CLASS!",
                        attr->name, attr->fullname);
            attr->dc = dc;
            goto next_attr;
        }

        knd_log("-- attr \"%s => %s\" is not resolved? :(",
                attr->name, attr->fullname);

        return knd_FAIL;
        
    next_attr:
        attr = attr->next;
    }

    return knd_OK;
}





static int
parse_aggr(void *obj,
           const char *rec,
           size_t *total_size)
{
    struct kndDataClass *self = (struct kndDataClass*)obj;
    struct kndAttr *attr;
    int err;

    if (DEBUG_DC_LEVEL_1)
        knd_log(".. parsing the AGGR attr: \"%s\"", rec);

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
    
    return knd_OK;
}

static int
parse_str(void *obj,
          const char *rec,
          size_t *total_size)
{
    struct kndDataClass *self = (struct kndDataClass*)obj;
    struct kndAttr *attr;
    int err;

    if (DEBUG_DC_LEVEL_TMP)
        knd_log(".. parsing the STR attr: \"%s\"", rec);

    err = kndAttr_new(&attr);
    if (err) return err;
    attr->parent_dc = self;
    attr->type = KND_ELEM_STR;

    err = attr->parse(attr, rec, total_size);
    if (err) {
        if (DEBUG_DC_LEVEL_TMP)
            knd_log("-- failed to parse the STR attr: %d", err);
        return err;
    }
    
    return knd_OK;
}

static int
parse_ref(void *obj,
          const char *rec,
          size_t *total_size)
{
    struct kndDataClass *self = (struct kndDataClass*)obj;
    struct kndAttr *attr;
    int err;

    if (DEBUG_DC_LEVEL_TMP)
        knd_log(".. parsing the REF attr: \"%s\"", rec);

    err = kndAttr_new(&attr);
    if (err) return err;
    attr->parent_dc = self;
    attr->type = KND_ELEM_REF;

    
    err = attr->parse(attr, rec, total_size);
    if (err) {
        if (DEBUG_DC_LEVEL_TMP)
            knd_log("-- failed to parse the REF attr: %d", err);
        return err;
    }
    
    return knd_OK;
}

static int parse_class(void *obj,
                       const char *rec,
                       size_t *total_size)
{
    struct kndDataClass *self = (struct kndDataClass*)obj;
    size_t chunk_size;
    int err;
    
    if (DEBUG_DC_LEVEL_1)
        knd_log(".. parse CLASS fields: \"%s\"..", rec);

    struct kndTaskSpec specs[] = {
        { .name = "aggr",
          .name_size = strlen("aggr"),
          .parse = parse_aggr,
          .obj = self
        },
        { .name = "str",
          .name_size = strlen("str"),
          .parse = parse_str,
          .obj = self
        },
        { .name = "ref",
          .name_size = strlen("ref"),
          .parse = parse_ref,
          .obj = self
        }
    };
    
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}



static int run_set_namespace(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndDataClass *self = (struct kndDataClass*)obj;
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

static int run_read_include(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndDataClass *self = (struct kndDataClass*)obj;
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


static int parse_namespace(void *self,
                           const char *rec,
                           size_t *total_size)
{
    if (DEBUG_DC_LEVEL_1)
        knd_log(".. parse namespace REC: \"%s\"..", rec);

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_namespace,
          .obj = self
        },
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class,
          .obj = self
        }
    };
    int err;
    
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}

static int parse_include(void *self,
                         const char *rec,
                         size_t *total_size)
{
    if (DEBUG_DC_LEVEL_TMP)
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


static int parse_GSL(struct kndDataClass *self,
                     const char *rec,
                     size_t *total_size)
{
    if (DEBUG_DC_LEVEL_1)
        knd_log(".. parse GSL REC: \"%s\"..", rec);

    struct kndTaskSpec specs[] = {
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class,
          .obj = self
        },
        { .name = "ns",
          .name_size = strlen("ns"),
          .parse = parse_namespace,
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

static int read_GSL_file(struct kndDataClass *self,
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
    
    err = out->read_file(out,
                         (const char*)out->buf, out->buf_size);
    if (err) {
        knd_log("-- couldn't read GSL class file \"%s\" :(", out->buf);
        return err;
    }

    out->file[out->file_size] = '\0';
    
    err = parse_GSL(self, (const char*)out->file, &chunk_size);
    if (err) return err;

    return knd_OK;
}


static int 
kndDataClass_coordinate(struct kndDataClass *self)
{
    struct kndDataClass *dc, *bc;
    const char *key;
    void *val;
    int err = knd_FAIL;

    /* TODO: coordinate classes, resolve all refs */
    key = NULL;
    self->class_idx->rewind(self->class_idx);
    do {
        self->class_idx->next_item(self->class_idx, &key, &val);
        if (!key) break;

        dc = (struct kndDataClass*)val;

        err = dc->resolve(dc);
        if (err) goto final;

        if (dc->baseclass_name_size) {
            if (DEBUG_DC_LEVEL_3)
                knd_log("   .. looking up baseclass \"%s\" for \"%s\"..\n",
                        dc->baseclass_name, dc->name);

            bc = (struct kndDataClass*)self->class_idx->get(self->class_idx,
                                                            (const char*)dc->baseclass_name);
            if (!bc) {
                knd_log("  -- baseclass \"%s\" for \"%s\" not found :(\n",
                        dc->baseclass_name, dc->name);
                err = knd_FAIL;
                goto final;
            }
            dc->baseclass = bc;
        }

        //dc->str(dc, 1);
        
    } while (key);
    
    err = knd_OK;
    
final:
    return err;
}



static int 
kndDataClass_export_GSL(struct kndDataClass *self)
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
kndDataClass_export_JSON(struct kndDataClass *self)
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
kndDataClass_export_HTML(struct kndDataClass *self)
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

        /*err = kndDataClass_export_attr_JSON(self, attr);
        if (err) goto final;
        */
        
        i++;

        attr = attr->next;
    }

    err = out->write(out, "]}", 2);

    return err;
}


static int 
kndDataClass_export(struct kndDataClass *self, knd_format format)
{
    int err = knd_FAIL;
    
    switch(format) {
        case KND_FORMAT_JSON:
        err = kndDataClass_export_JSON(self);
        if (err) goto final;
        break;
    case KND_FORMAT_HTML:
        err = kndDataClass_export_HTML(self);
        if (err) goto final;
        break;
    case KND_FORMAT_GSL:
        err = kndDataClass_export_GSL(self);
        if (err) goto final;
        break;
    default:
        break;
    }

 final:
    return err;
}



/*  DataClass Initializer */
static void
kndDataClass_init(struct kndDataClass *self)
{
    self->init = kndDataClass_init;
    self->del = del;
    self->str = str;
    self->read_file = read_GSL_file;
    self->set_name = kndDataClass_set_name;

    self->coordinate = kndDataClass_coordinate;
    self->resolve = kndDataClass_resolve;
    self->export = kndDataClass_export;

    self->rewind = kndDataClass_rewind;
    self->next_attr = kndDataClass_next_attr;
}


extern int 
kndDataClass_new(struct kndDataClass **c)
{
    struct kndDataClass *self;

    self = malloc(sizeof(struct kndDataClass));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndDataClass));

    kndDataClass_init(self);

    *c = self;

    return knd_OK;
}
