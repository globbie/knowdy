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
static
int kndDataClass_del(struct kndDataClass *self __attribute__((unused)))
{
    return knd_OK;
}

static int
kndDataClass_elem_str(struct kndDataClass *self,
                      struct kndDataElem *elem,
                      size_t depth)
{
    struct kndDataElem *e;

    size_t offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;
    char *offset = malloc(offset_size + 1);

    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    if (elem->is_list) {
        knd_log("\n%s[%s:%s\n", offset, elem->name, elem->attr_name);

        if (elem->idx_name_size) {
            knd_log("%sIDX:%s\n", offset, elem->idx_name);
        }
    }
    else
        knd_log("\n%s{ELEM:%s\n", offset, elem->name);

    if (elem->is_recursive) {
        knd_log("\n%s->SELF}\n", offset, elem->name);
        return knd_OK;
    }

    
    if (elem->attr) {
        elem->attr->str(elem->attr, depth + 1);
        /*knd_log("%s  attr: %s [%d]\n", offset,
                elem->attr->name, elem->attr->type);

        dc = elem->attr->dataclass;
        if (!dc) {
            knd_log("     -- attr not linked to a DataClass: %s\n", elem->attr->name);
            return knd_FAIL;
        }
        knd_log("%sTEMPLATE: %s\n", offset, dc->name); */
    }

    if (elem->dc) {
        knd_log("%s  -> inline\n", offset);
        elem->dc->str(elem->dc, depth + 1);
    }
    
    e = elem->elems;
    while (e) {
        kndDataClass_elem_str(self, e, depth + 1);
        e = e->next;
    }
    
    if (elem->is_list)
        knd_log("\n%s]\n", offset);
    else
        knd_log("%s}\n", offset);

    return knd_OK;
}

static int
kndDataClass_str(struct kndDataClass *self, size_t depth)
{
    struct kndDataElem *elem;
    struct kndTranslation *tr;
    
    size_t offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;
    char *offset = malloc(offset_size + 1);

    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    knd_log("\n\n%s(CLASS:%s\n", offset, self->name);

    if (self->baseclass_name_size) {
        knd_log("\n%s_baseclass: \"%s\"\n", offset, self->baseclass_name);
        if (self->baseclass)
            self->baseclass->str(self->baseclass, depth + 1);
    }
    
    tr = self->tr;
    while (tr) {
        knd_log("   ~ %s:%s\n", tr->lang_code, tr->seq);
        tr = tr->next;
    }
    
    if (!self->elems) 
        knd_log("%s)", offset);

    knd_log("\n");
    
    elem = self->elems;
    while (elem) {
        kndDataClass_elem_str(self, elem, depth + 1);
        elem = elem->next;
    }


    if (self->elems) 
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
    
    self->curr_elem = self->elems;
    self->elems_left = self->num_elems;
}


static int
kndDataClass_next_elem(struct kndDataClass *self,
                       struct kndDataElem **result)
{
    struct kndDataElem *elem = NULL;

    /* nested classes first */
    if (self->baseclass) {
        self->baseclass->next_elem(self->baseclass, &elem);
        if (elem) {
            *result = elem;
            return knd_OK;
        }
    }
    
    if (!self->curr_elem) {
        *result = NULL;
        return knd_OK;
    }
    
    *result = self->curr_elem;
    self->curr_elem = self->curr_elem->next;
    self->elems_left--;
    
    return knd_OK;
}

/* fixme
static int
kndDataClass_read_GSL_params(struct kndDataClass *self,
                             struct kndDataElem *elem,
                             const char *rec,
                             size_t *chunk_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    const char *c;
    const char *b;
    
    size_t curr_size = 0;
    bool in_param = false;
    int err = knd_FAIL;

    c = rec;
    b = rec;
    
    while (*c) {
        switch (*c) {
        default:
            if (!in_param) {
                in_param = true;
            }
            break;
        case '>':
            *chunk_size = c - rec;
            err = knd_OK;
            goto final;
        }
        
        c++;
    }
    
 final:
    return err;
}
*/

static int
kndDataClass_read_GSL_glosses(struct kndDataClass *self __attribute__((unused)),
                              struct kndDataElem *elem __attribute__((unused)),
                              const char *rec,
                              size_t *chunk_size)
{
    //char buf[KND_NAME_SIZE];
    //size_t buf_size = 0;
    const char *c;
    //const char *b;

    //struct kndTranslation *tr = NULL;
    
    //size_t curr_size = 0;
    //bool in_key = false;
    //bool in_val = false;
    int err = knd_FAIL;

    knd_log("  .. reading DATACLASS glosses..\n");
    
    c = rec;
    //b = rec;

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

            break; */
        case ']':

            *chunk_size = c - rec;
            return knd_OK;
        }

        c++;
    }

    return err;
}



static int
kndDataClass_read_GSL_list(struct kndDataClass *self,
                           struct kndDataElem *elem,
                           const char *rec,
                           size_t *chunk_size)
{
    size_t buf_size = 0;

    const char *c;
    const char *b;

    size_t curr_size = 0;
    bool in_init = false;
    bool got_attr_name = false;
    bool got_abbr = false;
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

                if (!elem) {
                    knd_log("  -- empty ELEM? :(\n");
                }
                
                memcpy(elem->attr_name, b, buf_size);
                elem->attr_name[buf_size] = '\0';
                elem->attr_name_size = buf_size;
                    
                if (!strcmp(elem->attr_name, "_gloss")) {
                    
                    err = kndDataClass_read_GSL_glosses(self, elem, c, &curr_size);
                    if (err) goto final;
                        
                    *chunk_size = (c + curr_size) - rec;

                    
                    knd_log("  .. GSL glosses size: %lu\n", (unsigned long)*chunk_size);
                    return knd_OK;
                }

                /*knd_log("LIST CLASS: \"%s\"\n", elem->attr_name);*/
                    
                got_attr_name = true;
                break;
            }

            if (got_attr_name) {
                if (!got_abbr) {
                    buf_size = c - b;
                    memcpy(elem->name, b, buf_size);
                    elem->name[buf_size] = '\0';
                    elem->name_size = buf_size;

                    /*knd_log("LIST CLASS abbr: \"%s\"\n", elem->name);*/
                    
                    got_abbr = true;
                }
            }

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
            if (in_idx) {
                buf_size = c - b;
                if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;

                memcpy(elem->idx_name, b, buf_size);
                elem->idx_name[buf_size] = '\0';
                elem->idx_name_size = buf_size;

                knd_log("  == IDX name: \"%s\"\n", elem->idx_name);

            }


            break;
        case '[':
            if (!in_init) {
                in_init = true;
                b = c + 1;
            }
            break;
        case ']':

            if (!got_abbr) {
                buf_size = c - b;
                memcpy(elem->name, b, buf_size);
                elem->name[buf_size] = '\0';
                elem->name_size = buf_size;

                /*knd_log("LIST CLASS abbr: \"%s\"\n", elem->name);*/
            }


            *chunk_size = c - rec;
            err = knd_OK;
            goto final;
        }

        c++;
    }
    
 final:
    return err;
}


static int
kndDataClass_read_GSL(struct kndDataClass *self,
                      const char *rec,
                      size_t *out_size)
{
    char keybuf[KND_NAME_SIZE];
    size_t keybuf_size = 0;

    size_t buf_size = 0;

    const char *c;
    const char *b;

    struct kndDataElem *elem = NULL;
    
    size_t curr_size = 0;
    size_t chunk_size = 0;
    
    bool in_body = false;
    bool in_elem = false;
    bool in_elem_attr = false;
    bool in_elem_name = false;

    bool in_spec = false;

    int err = knd_FAIL;

    c = rec;
    b = rec;
    
    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:
            if (!in_body) {
                in_body = true;
                in_elem = true;
                in_elem_attr = true;

                /*memcpy(buf, c, 8);
                buf[8] = '\0';

                knd_log("   == BODY begins: \"%s\"..\n\n", buf);
                */
                
                b = c;
                break;
            }

            if (!in_elem) {
                /*memcpy(buf, c, 8);
                buf[8] = '\0';

                knd_log("   .. elem AREA/CLASS begins: \"%s\"..\n\n", buf);
                */
                in_elem = true;
                in_elem_attr = true;
                b = c;
                break;
            }

            if (!in_elem_attr) {
                if (!in_elem_name) {
                    /*memcpy(buf, c, 8);
                    buf[8] = '\0';
                    knd_log("    .. elem NAME begins: \"%s\"..\n\n", buf);
                    */
                    in_elem_name = true;
                    b = c;
                    break;
                }
            }
            
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (!in_body) {
                b = c;
                break;
            }

            if (in_spec) {
                keybuf_size = c - b;
                if (keybuf_size < KND_NAME_SIZE) {
                    memcpy(keybuf, b, keybuf_size);
                    keybuf[keybuf_size] = '\0';
                }
                b = c + 1;
                break;
            }
            
            if (!in_elem) {
                b = c;
                break;
            }

            if (in_elem_attr) {
                
                elem = malloc(sizeof(struct kndDataElem));
                if (!elem) return knd_NOMEM;
                
                memset(elem, 0, sizeof(struct kndDataElem));
                
                elem->attr_name_size = c - b;
                memcpy(elem->attr_name, b, elem->attr_name_size);
                elem->attr_name[elem->attr_name_size] = '\0';

                if (!strcmp(elem->attr_name, self->name))
                    elem->is_recursive = true;
                
                in_elem_attr = false;
                b = c;
                break;
            }

            if (in_elem_name) {
                curr_size = c - b;
                if (curr_size) {
                    elem->name_size = curr_size;
                    memcpy(elem->name, b, curr_size);
                    elem->name[curr_size] = '\0';

                    /*knd_log("ELEM NAME BEFORE COMMA???: %s\n", elem->name);*/
                }
                in_elem_name = false;
            }
            
            break;
        case ',':
             if (in_elem_name) {

                 curr_size = c - b;
                if (curr_size) {
                    elem->name_size = curr_size;
                    memcpy(elem->name, b, curr_size);
                    elem->name[curr_size] = '\0';

                    /* special name */
                    if (elem->name[0] == '_') {
                        
                        /*knd_log("  == SPECIAL ATTR: %s\n", elem->name);*/

                        if (!strcmp(elem->name, "_base")) {
                            memcpy(self->baseclass_name, elem->attr_name, elem->attr_name_size);
                            self->baseclass_name_size = elem->attr_name_size;
                        }

                        free(elem);
                        
                        elem = NULL;

                        in_elem_name = false;
                        in_elem = false;

                        break;
                    }
                }

                
                if (!self->tail) {
                    self->tail = elem;
                    self->elems = elem;
                }
                else {
                    self->tail->next = elem;
                    self->tail = elem;
                }
                self->num_elems++;
                elem = NULL;

                in_elem_name = false;
                in_elem = false;
                break;
            }

            b = c;
            break;
        case '{':
            if (in_body) {
                in_spec = true;
                in_elem_attr = false;
                in_elem_name = false;
                in_elem = false;

                b = c + 1;
            }
            break;
        case '}':

            if (in_spec) {
                if (keybuf_size) {
                    if (!strcmp(keybuf, "idx")) {
                        buf_size = c - b;
                        if (buf_size < KND_NAME_SIZE) {
                            memcpy(self->idx_name, b, buf_size);
                            self->idx_name[buf_size] = '\0';
                            self->idx_name_size = buf_size;
                        }
                    }

                    if (!strcmp(keybuf, "style")) {
                        buf_size = c - b;
                        if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;
                        
                        memcpy(self->style_name, b, buf_size);
                        self->style_name[buf_size] = '\0';
                        self->style_name_size = buf_size;

                        knd_log("  == DATA CLASS style: %s\n", self->style_name);
                    }
                }



                in_spec = false;
                break;
            }
            
            /* last elem */
            if (elem) {
                if (elem->name_size) {
                    if (!self->tail) {
                        self->tail = elem;
                        self->elems = elem;
                    }
                    else {
                        self->tail->next = elem;
                        self->tail = elem;
                    }
                    
                
                    self->num_elems++;
                }
                elem = NULL;
            }
            
            *out_size = c - rec;
            
            return  knd_OK;
        case '<':

            /*err = kndDataClass_read_GSL_params(self, elem, c, &chunk_size);
            knd_log("  params result: %d total chars: %lu\n",
                    err, (unsigned long)chunk_size);
            if (err) goto final;
            
            c += chunk_size;*/
            
            break;
            
        case '[':
            
            elem = malloc(sizeof(struct kndDataElem));
            if (!elem) return knd_NOMEM;

            memset(elem, 0, sizeof(struct kndDataElem));
            elem->is_list = true;
            
            err = kndDataClass_read_GSL_list(self, elem, c, &chunk_size);

            knd_log("   list read result: %d   chunk_size: %lu\n",
                    err, (unsigned long)chunk_size);

            if (err) goto final;

            c += chunk_size;

            
            /* tech field, don't save as elem */
            /*if (!strcmp(elem->attr_name, "_gloss")) {
                free(elem);
                elem = NULL;
                break;
                }*/
            
            if (!self->tail) {
                self->tail = elem;
                self->elems = elem;
            }
            else {
                self->tail->next = elem;
                self->tail = elem;
            }


            /*memcpy(recbuf, c, 64);
            recbuf[64] = '\0';
            knd_log("  REMAINDER to parse: \"%s\"\n\n", recbuf);
            */
            
            elem = NULL;
            
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
kndDataClass_resolve(struct kndDataClass *self)
{

    struct kndDataClass *dc;
    struct kndDataElem *elem;
    struct kndAttr *attr;

    int err;

    elem = self->elems;
    while (elem) {
        attr = (struct kndAttr*)self->attr_idx->get(self->attr_idx,
                                                    (const char*)elem->attr_name);
        if (attr) {
            if (DEBUG_DATACLASS_LEVEL_3)
                knd_log("    ++ elem \"%s\" is resolved as ATTR!\n", elem->attr_name);

            elem->attr = attr;
            goto next_elem;
        }

        /* try to resolve as an inline class */
        dc = (struct kndDataClass*)self->class_idx->get(self->class_idx,
                                                    (const char*)elem->attr_name);
        if (dc) {
            if (DEBUG_DATACLASS_LEVEL_3)
                knd_log("    ++ elem \"%s\" is resolved as inline CLASS!\n", elem->attr_name);

            elem->dc = dc;
            goto next_elem;
        }
        else {
            if (DEBUG_DATACLASS_LEVEL_3)
                knd_log("\n    -- elem \"%s\" not found in class_idx :(\n", elem->attr_name);
        }

        if (DEBUG_DATACLASS_LEVEL_3)
            knd_log("\n   .. elem \"%s\" from class \"%s\" is not resolved?\n",
                    elem->attr_name, self->name);

    next_elem:
        elem = elem->next;
    }



    err = knd_OK;

    return err;
}


static int
kndDataClass_add_class(struct kndDataClass *self,
                       const char *rec, size_t rec_size,
                       size_t *rec_total)
{
    size_t buf_size = 0;
    struct kndDataClass *dc = NULL;
    struct kndDataClass *prev_dc = NULL;

    struct kndAttr *attr = NULL;
    struct kndAttr *prev_attr = NULL;

    size_t prefix_size;
    int err;

    if (rec[1] == ':') {
        err = kndAttr_new(&attr);
        if (err) goto final;

        /* data attr */
        switch (*rec) {
        case 'A':
            attr->type = KND_ELEM_ATOM;
            break;
        case 'C':
            attr->type = KND_ELEM_CONTAINER;
            break;
        case 'D':
            attr->type = KND_ELEM_CALC;
            break;
        case 'F':
            attr->type = KND_ELEM_FILE;
            break;
        case 'I':
            attr->type = KND_ELEM_INLINE;
            break;
        case 'N':
            attr->type = KND_ELEM_NUM;
            break;
        case 'P':
            attr->type = KND_ELEM_PROC;
            break;
        case 'R':
            attr->type = KND_ELEM_REF;
            break;
        case 'T':
            attr->type = KND_ELEM_TEXT;
            break;
        default:
            break;
        }

        prefix_size = 2; 
        rec += prefix_size;
        rec_size -= prefix_size;

        attr->name_size = rec_size;
        memcpy(attr->name, rec, rec_size);
        attr->name[rec_size] = '\0';
        rec += rec_size;

        prev_attr = self->attr_idx->get(self->attr_idx,
                                        (const char*)attr->name);
        if (prev_attr) {
            knd_log("  -- attr name \"%s\" already exists :(\n", attr->name);
            err = knd_FAIL;
            goto final;
        }

        err = attr->read(attr, rec, &buf_size);
        if (err) goto final;

        err = self->attr_idx->set(self->attr_idx,
                                  (const char*)attr->name, (void*)attr);
        if (err) return err;

        *rec_total = buf_size;

        return knd_OK;
    }

    err = kndDataClass_new(&dc);
    if (err) goto final;
    dc->out = self->out;

    dc->name_size = rec_size;
    memcpy(dc->name, rec, rec_size);
    dc->name[rec_size] = '\0';

    prev_dc = self->class_idx->get(self->class_idx,
                                    (const char*)dc->name);
    if (prev_dc) {
        knd_log("  -- class name \"%s\" already exists :(\n", dc->name);
        err = knd_FAIL;
        goto final;
    }

    dc->class_idx = self->class_idx;
    dc->attr_idx = self->attr_idx;

    rec += rec_size;
    buf_size = rec_size;

    /*knd_log(" .. reading CLASS rec \"%s\"   size: %lu\n\n", rec, (unsigned long)buf_size);
     */

    err = dc->read(dc, rec, &buf_size);
    if (err) goto final;

    err = self->class_idx->set(self->class_idx,
                               (const char*)dc->name, (void*)dc);
    if (err) return err;
    *rec_total = buf_size;

    if (DEBUG_DATACLASS_LEVEL_3) {
        knd_log("\n\n  ++ CLASS read:");
        dc->str(dc, 1);
    }


    return knd_OK;

final:
    if (dc)
        dc->del(dc);

    if (attr)
        attr->del(attr);
    
    return err;
}



static int
kndDataClass_read_onto_GSL(struct kndDataClass *self,
                           const char *filename)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    struct kndOutput *out = NULL;
    
    const char *c;
    const char *b;

    bool in_comment = false;
    const char *slash = NULL;
    const char *asterisk = NULL;

    bool in_init = true;
    bool in_import = true;
    bool in_class = false;
    int err;

    buf_size = sprintf(buf, "%s/%s", self->path, filename);

    err = kndOutput_new(&out, KND_IDX_BUF_SIZE);
    if (err) return err;

    err = out->read_file(out,
                         (const char*)buf, buf_size);
    if (err) goto final;

    c = out->file;
    b = c;
    
    while (*c) {
        switch (*c) {
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (in_comment) break;

            if (in_class) {
                buf_size = 0;

                err = kndDataClass_add_class(self, b, c - b, &buf_size);
                if (err) goto final;

                c += buf_size;
                b = c;
                in_class = false;
                break;
            }

            if (in_init) {
                buf_size = c - b;
                memcpy(buf, b, buf_size);
                buf[buf_size] = '\0';

                if (!strcmp(buf, "import")) {
                    in_import = true;
                }
                b = c + 1;
            }

            break;
        case '{':
            if (in_comment) break;

            if (!in_class) {
                in_class = true;
                in_init = false;
                b = c + 1;
                break;
            }
            break;
        case '}':
            if (in_comment) break;

            if (in_class) {
                buf_size = 0;

                err = kndDataClass_add_class(self, b, c - b, &buf_size);
                if (err) goto final;

                c += buf_size;
                b = c;
                in_class = false;
                in_init = true;
            }
            break;
        case ';':
            if (in_comment) break;

            if (in_import) {
                buf_size = c - b;
                memcpy(buf, b, buf_size);
                buf[buf_size] = '\0';

                /*knd_log("IMPORT MODULE: \"%s\"\n", buf);*/

                strncat(buf, ".gsl", 4);

                err = kndDataClass_read_onto_GSL(self,
                                                 (const char *)buf);
                if (err) goto final;

                in_import = false;
                b = c + 1;
            }
            in_init = true;
            break;
        case '/':
            slash = c + 1;
            if (in_comment && asterisk == c) {
                in_comment = false;
                asterisk = NULL;
                slash = NULL;
            }
            break;
        case '*':
            asterisk = c + 1;
            if (!in_comment && slash == c) {
                in_comment = true;
                asterisk = NULL;
                slash = NULL;
                /*knd_log("   == COMMENT: %s\n", c);*/
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
    struct kndAttr *attr;

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

        dc->attr_idx = self->attr_idx;
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

        dc->str(dc, 1);
        
    } while (key);

    key = NULL;
    self->attr_idx->rewind(self->attr_idx);
    do {
        self->attr_idx->next_item(self->attr_idx, &key, &val);
        if (!key) break;
        
        attr = (struct kndAttr*)val;
        
        attr->class_idx = self->class_idx;
        err = attr->resolve(attr);
        if (err) goto final;
        
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
kndDataClass_export_elem_JSON(struct kndDataClass *self, struct kndDataElem *elem)
{
    struct kndTranslation *tr;
    struct kndOutput *out;
    int err;

    
    if (DEBUG_DATACLASS_LEVEL_3)
        knd_log("JSON ELEM: %s\n",
                elem->name);

    out = self->out;

    err = out->write(out,
                     "{", 1);
    if (err) return err;

    /* attr class */
    err = out->write(out,
                     "\"c\":\"", strlen("\"c\":\""));
    if (err) return err;

    err = out->write(out,
                     elem->attr_name, elem->attr_name_size);
    if (err) return err;

    err = out->write(out,
                     "\"", 1);
    if (err) return err;


    if (elem->attr) {
        tr =  elem->attr->tr;
        while (tr) {

            /*knd_log("    == Dataclass LANG CODE: %s  curr: %s\n",
                    tr->lang_code, self->lang_code);
            */
            
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

        /* IDX */
        if (elem->attr->idx_name_size) {
            err = out->write(out,
                             ",\"idx\":\"", strlen(",\"idx\":\""));
            if (err) return err;

            err = out->write(out,
                             elem->attr->idx_name, elem->attr->idx_name_size);
            if (err) return err;

            err = out->write(out,
                             "\"", 1);
            if (err) return err;
        }


    }

    /* concise attr name */
    err = out->write(out,
                     ",\"n\":\"", strlen(",\"n\":\""));
    if (err) return err;

    err = out->write(out,
                     elem->name, elem->name_size);
    if (err) return err;

    err = out->write(out,
                     "\"", 1);
    if (err) return err;

    err = out->write(out, "}", 1);
    if (err) return err;

    return err;
}

static int
kndDataClass_export_JSON(struct kndDataClass *self)
{
    struct kndTranslation *tr;
    struct kndDataElem *elem;
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

    err = out->write(out,
                     ",\"elem_l\":[", strlen(",\"elem_l\":["));
    if (err) return err;

    i = 0;
    elem = self->elems;
    while (elem) {

        if (i) {
            err = out->write(out, ",", 1);
            if (err) return err;
        }

        err = kndDataClass_export_elem_JSON(self, elem);
        if (err) goto final;

        i++;

        elem = elem->next;
    }

    err = out->write(out, "]}", 2);
    if (err) return err;

final:
    return err;
}



static int
kndDataClass_export_HTML(struct kndDataClass *self)
{
    struct kndTranslation *tr;
    struct kndDataElem *elem;
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
                     ",\"elem_l\":[", strlen(",\"elem_l\":["));
    if (err) return err;

    i = 0;
    elem = self->elems;
    while (elem) {

        if (i) {
            err = out->write(out, ",", 1);
            if (err) return err;
        }

        /*err = kndDataClass_export_elem_JSON(self, elem);
        if (err) goto final;
        */
        
        i++;

        elem = elem->next;
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
int kndDataClass_init(struct kndDataClass *self)
{
    /* binding our methods */
    self->init = kndDataClass_init;
    self->del = kndDataClass_del;
    self->str = kndDataClass_str;
    self->read = kndDataClass_read_GSL;
    self->read_onto = kndDataClass_read_onto_GSL;
    self->coordinate = kndDataClass_coordinate;
    self->resolve = kndDataClass_resolve;
    self->export = kndDataClass_export;

    self->rewind = kndDataClass_rewind;
    self->next_elem = kndDataClass_next_elem;

    return knd_OK;
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
