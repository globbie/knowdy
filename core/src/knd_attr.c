#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_attr.h"
#include "knd_dataclass.h"
#include "knd_output.h"

#include "knd_text.h"

#define DEBUG_ATTR_LEVEL_1 0
#define DEBUG_ATTR_LEVEL_2 0
#define DEBUG_ATTR_LEVEL_3 0
#define DEBUG_ATTR_LEVEL_4 0
#define DEBUG_ATTR_LEVEL_5 0
#define DEBUG_ATTR_LEVEL_TMP 1


/*  Attr Destructor */
static void
kndAttr_del(struct kndAttr *self)
{
    free(self);
}

static void
kndAttr_str(struct kndAttr *self, size_t depth)
{
    struct kndTranslation *tr;

    size_t offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;
    char *offset = malloc(offset_size + 1);

    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    if (self->fullname_size) 
        knd_log("\n%s{%s:%s %s", offset,  knd_elem_names[self->type], self->name, self->fullname);
    else
        knd_log("\n%s{%s:%s", offset, knd_elem_names[self->type], self->name);

    tr = self->tr;
    while (tr) {
        knd_log("%s   ~ %s:%s", offset, tr->lang_code, tr->seq);
        tr = tr->next;
    }

    if (self->classname_size) {
        knd_log("%s  class template: %s", offset, self->classname);
    }

    if (self->calc_oper_size) {
        knd_log("%s  oper: %s attr: %s", offset,
                self->calc_oper, self->calc_attr);
    }

    if (self->idx_name_size) {
        knd_log("%s  idx: %s", offset, self->idx_name);
    }

    if (self->default_val_size) {
        knd_log("%s  default VAL: %s", offset, self->default_val);
    }
    
    knd_log("%s}", offset);
}



static int
kndAttr_set_type(struct kndAttr *self,
                 const char *name,
                 size_t name_size)
{

    self->type = KND_ELEM_ATOM;

    switch (*name) {
    case 'a':
    case 'A':
        if (!strncmp("aggr", name, name_size))
            self->type = KND_ELEM_AGGR;
        break;
    case 'n':
    case 'N':
        if (!strncmp("num", name, name_size))
            self->type = KND_ELEM_NUM;
        break;
    case 'r':
    case 'R':
        if (!strncmp("ref", name, name_size))
            self->type = KND_ELEM_REF;
        break;
    case 't':
    case 'T':
        if (!strncmp("text", name, name_size))
            self->type = KND_ELEM_TEXT;
        break;
    default:
        break;
    }

    if (DEBUG_ATTR_LEVEL_2)
        knd_log("  .. set attr type: \"%s\" = %d", name, self->type);

    if (!self->type) return knd_FAIL;
    
    return knd_OK;
}




static int
kndAttr_read_GSL_glosses(struct kndAttr *self,
                         char *rec,
                         size_t *chunk_size)
{
    struct kndTranslation *tr = NULL;
    //size_t buf_size = 0;
    char *c;
    char *b;
    
    //size_t curr_size = 0;

    bool in_key = false;
    //bool in_val = false;
    int err = knd_FAIL;

    
    c = rec;
    //b = rec;

    while (*c) {

        switch (*c) {
        default:
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            /*if (in_key) {
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
            */
            
            break;
        case '{':
            if (!in_key) {
                in_key = true;
            }
            b = c + 1;
            break;
        case '}':

            /*if (in_val) {
                buf_size = c - b;

                if (!buf_size) return knd_FAIL;

                if (buf_size > KND_LARGE_BUF_SIZE) return knd_LIMIT;

                tr->seq = malloc(buf_size);
                if (!tr->seq) return knd_NOMEM;

                memcpy(tr->seq, b, buf_size);
                tr->seq_size = buf_size;
                tr->seq[buf_size] = '\0';

                tr->next = self->dc->tr;
                self->dc->tr = tr;

                
                tr = NULL;
                in_val = false;
                b = c + 1;
                break;
            }
            */


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
kndAttr_read_list(struct kndAttr *self,
                  char *rec,
                  size_t *total_size)
{
    size_t buf_size = 0;

    char *c;
    char *b;

    size_t curr_size = 0;
    bool in_init = false;
    bool got_attr_name = false;

    bool in_abbr = false;
    bool in_idx = false;

    int err = knd_FAIL;

    c = rec;
    b = rec;

    while (*c) {
        switch (*c) {
        default:
            if (got_attr_name) {
                if (!in_abbr) {
                    in_abbr = true;
                    b = c;
                }
            }
            break;
            /* whitespace char */
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (!got_attr_name) {
                buf_size = c - b;

                if (!buf_size) return knd_FAIL;
                if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;

                memcpy(self->name, b, buf_size);
                self->name[buf_size] = '\0';
                self->name_size = buf_size;
                
                if (!strcmp(self->name, "_gloss")) {
                    err = kndAttr_read_GSL_glosses(self, c, &curr_size);
                    if (err) goto final;

                    *total_size = (c + curr_size) - rec;
                    return knd_OK;
                }

                got_attr_name = true;
                break;
            }

            /*if (got_attr_name) {
                if (!got_abbr) {
                    buf_size = c - b;
                    memcpy(elem->name, b, buf_size);
                    elem->name[buf_size] = '\0';
                    elem->name_size = buf_size;

                    
                    got_abbr = true;
                }
                }*/
            

            if (in_idx) {
                b = c + 1;
            }
            
            break;
        case '{':
            if (!in_idx) {
                in_idx = true;
                b = c + 1;
            }
            break;
        case '}':
            /*if (in_idx) {
                buf_size = c - b;
                if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;

                memcpy(elem->idx_name, b, buf_size);
                elem->idx_name[buf_size] = '\0';
                elem->idx_name_size = buf_size;

                knd_log("  == IDX name: \"%s\"\n", elem->idx_name);

            }
            */
            

            break;
        case '[':
            if (!in_init) {
                in_init = true;
                b = c + 1;
            }
            break;
        case ']':

            /*if (!got_abbr) {
                buf_size = c - b;
                memcpy(elem->name, b, buf_size);
                elem->name[buf_size] = '\0';
                elem->name_size = buf_size;

                } */

            *total_size = c - rec;
            return knd_OK;
        }

        c++;
    }
    
 final:
    return err;
}


static int
kndAttr_read_GSL(struct kndAttr *self,
                 char *rec,
                 size_t *total_size)
{
    /*char buf[KND_NAME_SIZE];*/
    size_t buf_size = 0;
    char *c;
    char *b;

    struct kndAttr *attr = NULL;

    bool in_body = false;
    bool in_abbr = false;
    bool in_fullname = false;

    c = rec;
    b = rec;
    size_t chunk_size;
    int err = knd_FAIL;
    
    /*knd_log(".. reading attr from \"%s\"..", rec);*/
    
    while (*c) {
        switch (*c) {
            /* whitespace */
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (in_abbr) {
                buf_size = c - b;
                if (!buf_size)
                    return knd_FAIL;
                if (buf_size >= KND_NAME_SIZE)
                    return knd_LIMIT;

                memcpy(self->name, b, buf_size);
                self->name_size = buf_size;
                self->name[buf_size] = '\0';
                
                in_abbr = false;
                in_fullname = true;
                b = c + 1;
            }

            if (in_fullname) break;
            
            b = c + 1;
            break;
        case '[':
            if (!in_body) {
                chunk_size = 0;
                err = kndAttr_read_list(self, c, &chunk_size);
                if (err) return err;
                
                *total_size = chunk_size;
                return knd_OK;
            }
            break;
        case '{':
            if (!in_body) {
                in_body = true;
                b = c + 1;
            }
            
            break;
        case '}':
            if (!in_body)
                return knd_FAIL;

            /* no fullname found? */
            if (in_abbr) {
                buf_size = c - b;
                if (!buf_size)
                    return knd_FAIL;
                if (buf_size >= KND_NAME_SIZE)
                    return knd_LIMIT;
                memcpy(self->name, b, buf_size);
                self->name_size = buf_size;
                self->name[buf_size] = '\0';
            }

            /* TODO: trim occasional whitespace */
            if (in_fullname) {
                buf_size = c - b;
                if (!buf_size)
                    return knd_FAIL;
                if (buf_size >= KND_NAME_SIZE)
                    return knd_LIMIT;

                self->fullname_size = self->parent_dc->namespace_size;
                memcpy(self->fullname,
                       self->parent_dc->namespace, self->fullname_size);
                
                memcpy(self->fullname + self->fullname_size, b, buf_size);
                self->fullname_size += buf_size;
                self->fullname[self->fullname_size] = '\0';
            }
            
            *total_size = c - rec;
            return knd_OK;
        case ':':
            if (!in_abbr) {
                buf_size = c - b;
                if (!buf_size)
                    return knd_FAIL;
                if (buf_size >= KND_NAME_SIZE)
                    return knd_LIMIT;

                *c = '\0';
                
                err = kndAttr_set_type(self, b, buf_size);
                if (err) goto final;
                
                b = c + 1;
                in_abbr = true;
            }
            
            break;
        default:
            break;
        }

        c++;
    }

 final:

    if (attr)
        attr->del(attr);
    
    return err;
}



static int 
kndAttr_present_GSL(struct kndAttr *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    struct kndOutput *out;
    int err;

    out = self->out;
    
    buf_size = sprintf(buf, "(N^%s)",
                       self->name);
    out->reset(out);

    err = out->write(out, buf, buf_size);
    if (err) return err;
    
    return knd_OK;
}



static int
kndAttr_present_JSON(struct kndAttr *self)
{
    //char buf[KND_MED_BUF_SIZE];
    //size_t buf_size;

    struct kndOutput *out;
    //const char *key = NULL;
    //void *val = NULL;
    int err;
    
    if (DEBUG_ATTR_LEVEL_TMP)
        knd_log("   .. present JSON ATTR: %s\n",
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
    /*tr = self->tr;
    while (tr) {
        if (strcmp(tr->lang_code, self->reader->lang_code)) goto next_tr;
        
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
    */

    return err;
}

static int 
kndAttr_present(struct kndAttr *self, knd_format format)
{
    int err = knd_FAIL;

    switch(format) {
        case KND_FORMAT_JSON:
        err = kndAttr_present_JSON(self);
        if (err) goto final;
        break;
    case KND_FORMAT_GSL:
        err = kndAttr_present_GSL(self);
        if (err) goto final;
        break;
    default:
        break;
    }

 final:
    return err;
}

/*  Attr Initializer */
static void
kndAttr_init(struct kndAttr *self)
{
    /* binding our methods */
    self->init = kndAttr_init;
    self->del = kndAttr_del;
    self->str = kndAttr_str;
    self->read = kndAttr_read_GSL;
    self->present = kndAttr_present;
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
