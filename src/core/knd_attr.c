#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_attr.h"
#include "knd_dataclass.h"
#include "knd_output.h"
#include "../data_reader/knd_data_reader.h"
#include "../data_writer/knd_data_writer.h"
#include "knd_text.h"
#include "knd_utils.h"

#define DEBUG_ATTR_LEVEL_1 0
#define DEBUG_ATTR_LEVEL_2 0
#define DEBUG_ATTR_LEVEL_3 0
#define DEBUG_ATTR_LEVEL_4 0
#define DEBUG_ATTR_LEVEL_5 0
#define DEBUG_ATTR_LEVEL_TMP 1


/*  Attr Destructor */
static
int kndAttr_del(struct kndAttr *self __attribute__((unused)))
{
    return knd_OK;
}

static int
kndAttr_str(struct kndAttr *self, size_t depth)
{
    struct kndTranslation *tr;

    size_t offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;
    char *offset = malloc(offset_size + 1);

    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    knd_log("\n%s(ATTR:%s [type:%s]\n", offset, self->name, knd_elem_names[self->type]);

    tr = self->tr;
    while (tr) {
        knd_log("%s   ~ %s:%s\n", offset, tr->lang_code, tr->seq);
        tr = tr->next;
    }

    if (self->classname_size) {
        knd_log("%s  class template: %s\n", offset, self->classname);
    }

    if (self->calc_oper_size) {
        knd_log("%s  oper: %s attr: %s\n", offset,
                self->calc_oper, self->calc_attr);
    }

    if (self->idx_name_size) {
        knd_log("%s  idx: %s\n", offset, self->idx_name);
    }

    if (self->default_val_size) {
        knd_log("%s  default VAL: %s\n", offset, self->default_val);
    }
    
    knd_log("%s)\n", offset);
    

    return knd_OK;
}



static int
kndAttr_read_GSL_glosses(struct kndAttr *self __attribute__((unused)),
                         const char *rec,
                         size_t *chunk_size)
{
    //char buf[KND_NAME_SIZE];
    //size_t buf_size;
    const char *c;
    const char *b;
    
    //struct kndTranslation *tr = NULL;
    
    //size_t curr_size = 0;
    //bool in_key = false;
    //bool in_val = false;
    int err = knd_FAIL;

    c = rec;
    b = rec;
    
    while (*c) {
        switch (*c) {
        default:
            break;
            /*
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (in_key) {
                tr = malloc(sizeof(struct kndTranslation));
                if (!tr) return knd_NOMEM;
                memset(tr, 0, sizeof(struct kndTranslation));

                buf_size = c - b;
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
            break; */
        case ']': 
            *chunk_size = c - rec + 1;
            err = knd_OK;
            goto final;
        }
        
        c++;
    }

    
 final:
    return err;
}



static int
kndAttr_resolve(struct kndAttr *self)
{
    struct kndDataClass *dc;

    if (DEBUG_ATTR_LEVEL_3)
        knd_log("     .. resolving ATTR template: %s\n",
                self->name);

    if (self->type == KND_ELEM_ATOM) {

        if (DEBUG_ATTR_LEVEL_3)
            knd_log("  ++ ATOM class resolved: \"%s\"\n", self->name);
        return knd_OK;
    }

    if (self->type == KND_ELEM_FILE) {

        if (DEBUG_ATTR_LEVEL_3)
            knd_log("  ++ FILE class resolved: \"%s\"\n", self->name);
        return knd_OK;
    }

    if (self->type == KND_ELEM_TEXT) {
        if (DEBUG_ATTR_LEVEL_3)
            knd_log("  ++ TEXT class resolved: \"%s\"\n", self->name);
        return knd_OK;
    }


    if (self->type == KND_ELEM_CALC) {
        if (DEBUG_ATTR_LEVEL_3)
            knd_log("  ++ CALC class resolved: \"%s\"\n", self->name);

        return knd_OK;
    }

    if (self->type == KND_ELEM_REF) {
        if (DEBUG_ATTR_LEVEL_3)
            knd_log("  .. resolving REF class \"%s\" ..\n", self->name);
    }

    
    dc = (struct kndDataClass*)self->class_idx->get(self->class_idx,
                                                    (const char*)self->classname);
    if (!dc) {
        knd_log("   -- no such dataclass: \"%s\"\n", self->classname);
        return knd_FAIL;
    }

    self->dataclass = dc;
    
    if (DEBUG_ATTR_LEVEL_3)
        knd_log("   ++ ATTR template dataclass resolved: %s!\n", self->classname);


    return knd_OK;
}

static int
kndAttr_read_GSL(struct kndAttr *self,
                 const char *rec,
                 size_t *out_size)
{
    char keybuf[KND_NAME_SIZE];
    size_t keybuf_size;

    char buf[KND_NAME_SIZE];
    size_t buf_size;
    const char *c;
    const char *b;

    size_t chunk_size = 0;
    
    //bool in_body = true;
    bool in_gloss = false;

    bool in_elem = false;
    bool in_elem_name = false;
    bool in_elem_val = false;
    long numval = 0;
    
    int err = knd_FAIL;

    c = rec;
    b = rec;

    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:
            if (in_elem_name) {
                if (!in_elem_val) {
                    b = c;
                    in_elem_val = true;
                }
                break;
            }
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (in_gloss) {
                buf_size = c - b;
                memcpy(buf, b, buf_size);
                buf[buf_size] = '\0';

                if (!strcmp(buf, "_gloss")) {
                    err = kndAttr_read_GSL_glosses(self,
                                                   c,
                                                   &chunk_size);
                    if (err) goto final;

                    /*knd_log("  == glosses read OK: %lu\n",
                            (unsigned long)chunk_size);
                    */
                    c += chunk_size;
                    
                    in_gloss = false;
                    break;
                }
                break;
            }

            if (in_elem) {
                if (!in_elem_name) {
                    keybuf_size = c - b;
                    memcpy(keybuf, b, keybuf_size);
                    keybuf[keybuf_size] = '\0';
                        
                    /*knd_log("ATTR FIELD NAME: \"%s\"\n", keybuf);*/

                    in_elem_name = true;
                }
            }
            
            break;
        case '{':
            if (!in_elem) {
                in_elem = true;
                b = c + 1;
                break;
            }
            
            break;
        case '}':
            if (!in_elem) {
                *out_size = c - rec;
                err = knd_OK;
                goto final;
            }

            /* in elem */
            if (in_elem_val) {
                buf_size = c - b;

                if (!buf_size) return knd_FAIL;
                if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;
                
                if (!strcmp(keybuf, "type")) {
                    self->classname_size = buf_size;
                    memcpy(self->classname, b, buf_size);
                    self->classname[buf_size] = '\0';
                }

                /* dynamic calc */
                if (!strcmp(keybuf, "oper")) {
                    self->calc_oper_size = buf_size;
                    memcpy(self->calc_oper, b, buf_size);
                    self->calc_oper[buf_size] = '\0';
                }
                
                if (!strcmp(keybuf, "attr")) {
                    self->calc_attr_size = buf_size;
                    memcpy(self->calc_attr, b, buf_size);
                    self->calc_attr[buf_size] = '\0';
                }

                if (!strcmp(keybuf, "idx")) {
                    self->idx_name_size = buf_size;
                    memcpy(self->idx_name, b, buf_size);
                    self->idx_name[buf_size] = '\0';
                }

                if (!strcmp(keybuf, "default")) {
                    self->default_val_size = buf_size;
                    memcpy(self->default_val, b, buf_size);
                    self->default_val[buf_size] = '\0';
                }

                if (!strcmp(keybuf, "concise")) {
                    self->concise_level = 1;
                    memcpy(buf, b, buf_size);
                    buf[buf_size] = '\0';
                    
                    err = knd_parse_num(buf, &numval);
                    if (err) return err;
                    
                    self->concise_level = numval;
                }
                
                if (!strcmp(keybuf, "descr")) {
                    self->descr_level = 1;
                    memcpy(buf, b, buf_size);
                    buf[buf_size] = '\0';
                    
                    err = knd_parse_num(buf, &numval);
                    if (err) return err;
                    
                    self->descr_level = numval;
                }

                /* browsing policy */
                if (!strcmp(keybuf, "browse")) {
                    self->browse_level = 1;
                    memcpy(buf, b, buf_size);
                    buf[buf_size] = '\0';
                    err = knd_parse_num(buf, &numval);
                    if (err) return err;
                    self->browse_level = numval;
                }
                                
                in_elem = false;
                in_elem_name = false;
                in_elem_val = false;
            }
            break;
        case '[':
            if (!in_gloss) {
                in_gloss = true;
                b = c + 1;
            }
            break;
        case '<':

            /*knd_log("  .. checking params..\n");
            err = kndAttr_read_GSL_params(self, elem, c, &chunk_size);
            knd_log("  params result: %d total chars: %lu\n",
                    err, (unsigned long)chunk_size);
            if (err) goto final;
            
            c += chunk_size;*/
            
            break;
        case ':':
            break;
        }

        c++;
    }

    
 final:
    return err;
}



static int 
kndAttr_present_GSL(struct kndAttr *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    struct kndOutput *out;
    int err;

    if (self->writer) out = self->writer->out;
    if (self->reader) out = self->reader->out;

    if (!out) return knd_FAIL;
    
    buf_size = sprintf(buf, "(N^%s)",
                       self->name);
    out->reset(out);

    err = out->write(out, buf, buf_size);

    return err;
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

    out = self->reader->out;

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
int kndAttr_init(struct kndAttr *self)
{
    /* binding our methods */
    self->init = kndAttr_init;
    self->del = kndAttr_del;
    self->str = kndAttr_str;
    self->read = kndAttr_read_GSL;
    self->resolve = kndAttr_resolve;
    self->present = kndAttr_present;

    return knd_OK;
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
