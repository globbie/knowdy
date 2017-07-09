#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_dataclass.h"
#include "knd_output.h"
#include "knd_attr.h"
#include "knd_text.h"

#define DEBUG_DATACLASS_LEVEL_1 0
#define DEBUG_DATACLASS_LEVEL_2 0
#define DEBUG_DATACLASS_LEVEL_3 0
#define DEBUG_DATACLASS_LEVEL_4 0
#define DEBUG_DATACLASS_LEVEL_5 0
#define DEBUG_DATACLASS_LEVEL_TMP 1


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


static int read_GSL(struct kndDataClass *self,
                    char *rec,
                    size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    size_t chunk_size = 0;
    
    char *c;
    char *b;

    struct kndDataClass *dc;
    struct kndAttr *attr;
    
    bool in_body = false;
    bool in_classname = false;

    int err = knd_FAIL;

    c = rec;
    b = rec;

    err = kndDataClass_new(&dc);
    if (err) return err;

    dc->dbpath = self->dbpath;
    dc->dbpath_size = self->dbpath_size;
    
    dc->out = self->out;
    dc->class_idx = self->class_idx;

    if (self->namespace_size) {
        memcpy(dc->namespace, self->namespace, self->namespace_size);
        dc->namespace_size = self->namespace_size;
    }

    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (!in_body)
                break;

            if (in_classname)
                break;
            
            buf_size = c - b;
            if (!buf_size)
                return knd_FAIL;
            if (buf_size >= KND_NAME_SIZE)
                return knd_LIMIT;
            
            memcpy(buf, b, buf_size);
            buf[buf_size] = '\0';
            
            if (!strcmp(buf, "class")) {
                in_classname = true;
                b = c + 1;
                break;
            }
            
            break;
        case '{':
            if (!in_body) {
                in_body = true;
                b = c + 1;
                break;
            }
            
            if (in_classname) {
                *c = '\0';
                err = dc->set_name(dc, b);
                if (err) goto final;
                in_classname = false;
            }

            err = kndAttr_new(&attr);
            if (err) return err;
            attr->parent_dc = self;

            chunk_size = 0;
            err = attr->read(attr, c, &chunk_size);
            if (err) goto final;

            c += chunk_size;
            if (!dc->tail_attr) {
                dc->tail_attr = attr;
                dc->attrs = attr;
            }
            else {
                dc->tail_attr->next = attr;
                dc->tail_attr = attr;
            }
            
            attr = NULL;
            break;
        case '}':

            memcpy(buf, self->namespace, self->namespace_size);
            buf_size = self->namespace_size;

            memcpy(buf + buf_size, "::", strlen("::"));
            buf_size += strlen("::");
            
            memcpy(buf + buf_size, dc->name, dc->name_size);
            buf_size += dc->name_size;
            buf[buf_size] = '\0';
            
            /* save class */
            err = self->class_idx->set(self->class_idx,
                                       (const char*)buf, (void*)dc);
            if (err) return err;
            
            if (DEBUG_DATACLASS_LEVEL_TMP)
                knd_log("++ register class: \"%s\"", buf);

            *total_size = c - rec;
            return  knd_OK;
            
        case '[':

            if (in_classname) {
                *c = '\0';
                err = dc->set_name(dc, b);
                if (err) goto final;
                in_classname = false;
                *c = '[';
            }


            err = kndAttr_new(&attr);
            if (err) return err;
            attr->is_list = true;
            attr->parent_dc = self;

            chunk_size = 0;
            err = attr->read(attr, c, &chunk_size);
            if (err) goto final;

            c += chunk_size;
            
            if (!dc->tail_attr) {
                dc->tail_attr = attr;
                dc->attrs = attr;
            }
            else {
                dc->tail_attr->next = attr;
                dc->tail_attr = attr;
            }
            
            attr = NULL;
            break;
        }
        
        c++;
    }

    
 final:

    dc->del(dc);
    
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
            if (DEBUG_DATACLASS_LEVEL_2)
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
kndDataClass_read_onto_GSL(struct kndDataClass *self,
                           const char *filename)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    struct kndOutput *out = NULL;
    
    char *c;
    char *b;

    const char *GSL_file_suffix = ".gsl";
    size_t GSL_file_suffix_size = strlen(GSL_file_suffix);
    
    bool in_comment = false;
    const char *slash = NULL;
    const char *asterisk = NULL;

    bool in_body = false;
    bool in_include = false;
    bool in_namespace = false;
    bool in_classes = false;
    int err;

    buf_size = sprintf(buf, "%s/%s", self->dbpath, filename);

    err = kndOutput_new(&out, KND_IDX_BUF_SIZE);
    if (err) return err;

    err = out->read_file(out,
                         (const char*)buf, buf_size);
    if (err) {
        knd_log("-- couldn't read GSL class file \"%s\" :(", buf);
        goto final;
    }

    c = out->file;
    b = c;
    
    while (*c) {
        switch (*c) {
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (!in_body) break;
            if (in_comment) break;

            if (in_include) {
                b = c + 1;
                break;
            }

            if (in_namespace) {
                self->namespace_size = c - b;
                if (!self->namespace_size) {
                    return knd_FAIL;
                }
                if (self->namespace_size >= KND_NAME_SIZE)
                    return knd_LIMIT;
            
                memcpy(self->namespace, b, self->namespace_size);
                self->namespace[self->namespace_size] = '\0';

                /*knd_log("== namespace: %s [%lu]",
                        self->namespace, (unsigned long)self->namespace_size);
                */
                
                in_namespace = false;
                in_classes = true;
                break;
            }

            if (in_classes) {
                b = c + 1;
                break;
            }
            
            buf_size = c - b;
            if (!buf_size) {
                return knd_FAIL;
            }
            if (buf_size >= (KND_TEMP_BUF_SIZE + GSL_file_suffix_size)) {
                return knd_LIMIT;
            }
                
            memcpy(buf, b, buf_size);
            buf[buf_size] = '\0';

            if (strcmp(buf, "include")) {
                return knd_FAIL;
            }
            
            in_include = true;
            b = c + 1;

            break;
        case '{':
            if (!in_body) {
                in_body = true;
                b = c + 1;
                break;
            }

            if (in_comment) break;

            if (in_classes) {
                size_t chunk_size = 0;

                err = read_GSL(self, c, &chunk_size);
                if (err) goto final;

                c += chunk_size;
                b = c;
                break;
            }

            b = c + 1;
            
            break;
        case '}':
            if (in_comment) break;

            if (in_include) {
                buf_size = c - b;
                if (!buf_size) {
                    return knd_FAIL;
                }
                if (buf_size >= (KND_TEMP_BUF_SIZE + GSL_file_suffix_size)) {
                    return knd_LIMIT;
                }
                *c = '\0';
                buf_size = sprintf(buf, "%s%s", b, GSL_file_suffix);

                if (DEBUG_DATACLASS_LEVEL_TMP)
                    knd_log("INCLUDE MODULE: \"%s\"", buf);

                err = kndDataClass_read_onto_GSL(self,
                                                 (const char *)buf);
                if (err) goto final;

                in_include = false;
                in_body = false;
                b = c + 1;
            }
            break;
        case '/':
            slash = c + 1;
            if (in_comment && asterisk == c) {
                in_comment = false;
                asterisk = NULL;
                slash = NULL;
            }
            break;
        case ':':
            buf_size = c - b;
            if (!buf_size) {
                return knd_FAIL;
            }
            if (buf_size >= (KND_NAME_SIZE)) {
                return knd_LIMIT;
            }
                
            memcpy(buf, b, buf_size);
            buf[buf_size] = '\0';

            if (DEBUG_DATACLASS_LEVEL_2)
                knd_log("KEYWORD: \"%s\"", buf);

            if (in_body) {
                if (!strcmp(buf, "ns")) {
                    in_namespace = true;
                    b = c + 1;
                    break;
                }
            }
            
            break;
        case '*':
            asterisk = c + 1;
            if (!in_comment && slash == c) {
                in_comment = true;
                asterisk = NULL;
                slash = NULL;
            }
            break;
        default:
            break;
        }

        
        c++;
    }

    err = knd_OK;

 final:

    out->del(out);

    return err;
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
            if (DEBUG_DATACLASS_LEVEL_3)
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
    
    if (DEBUG_DATACLASS_LEVEL_3)
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
        if (DEBUG_DATACLASS_LEVEL_3)
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

    err = out->write(out, "\"attrs\": {",
                     strlen("\"attrs\": {"));
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
            if (DEBUG_DATACLASS_LEVEL_TMP)
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

    if (DEBUG_DATACLASS_LEVEL_3)
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
        if (DEBUG_DATACLASS_LEVEL_3)
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
    self->read = read_GSL;
    self->read_onto = kndDataClass_read_onto_GSL;
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
