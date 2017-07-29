#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_repo.h"
#include "knd_user.h"
#include "knd_output.h"
#include "knd_concept.h"
#include "knd_object.h"
#include "knd_coderef.h"
#include "knd_sorttag.h"
#include "knd_parser.h"


#define DEBUG_OBJREF_LEVEL_0 0
#define DEBUG_OBJREF_LEVEL_1 0
#define DEBUG_OBJREF_LEVEL_2 0
#define DEBUG_OBJREF_LEVEL_3 0
#define DEBUG_OBJREF_LEVEL_TMP 1

static void
kndObjRef_del(struct kndObjRef *self)
{

    if (self->sorttag)
        self->sorttag->del(self->sorttag);


    if (self->elemrefs) {
        /* TODO */

    }

    free(self);
}

static int 
kndObjRef_str(struct kndObjRef *self, size_t depth)
{
    //char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size, curr_size;
    //char *c;
    size_t offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;
    //struct kndSortAttr *attr = NULL;
    char *offset;
    
    //struct kndOutput *out = self->out;
    struct kndElemRef *elemref;
    struct kndCodeRef *coderef;
    const char *is_delivered = "+";
    //int err;

    offset = malloc(offset_size + 1);
    if (!offset) return knd_NOMEM;

    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    if (self->type == KND_REF_TID) {
        if (!self->trn->proc_state)
            is_delivered = "-";

        knd_log("%s      * TRN REF UID:%s  %s  %s   %s\n",
                offset, self->trn->uid, self->trn->action, self->trn->query, is_delivered);

        if (self->sorttag) {
            self->sorttag->str(self->sorttag);
        }
        
        return knd_OK;
    }
    
    knd_log("%s      * %s \"%s\"\n",
            offset, self->obj_id, self->name);

    if (self->sorttag) {
        self->sorttag->str(self->sorttag);
    }
    
    if (self->type == KND_REF_CODE) {
        elemref = self->elemrefs;

        while (elemref) {
            knd_log("%s        \"%s\": ",
                    offset, elemref->name);

            coderef = elemref->coderefs;
            while (coderef) {
                knd_log("(C:%s)", coderef->name);
                coderef = coderef->next;
            }
            knd_log("\n");
            
            elemref = elemref->next;
        }


    }

    if (offset)
        free(offset);
    
    return knd_OK;
}

static int 
kndObjRef_export_JSON(struct kndObjRef *self,
                       size_t depth __attribute__((unused)))
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    struct kndOutput *out = self->out;
    struct kndElemRef *elemref;
    struct kndCodeRef *coderef;

    int err;

    if (self->type == KND_REF_TID) {
        err = out->write(out, 
                         "{\"uid\":\"", strlen("{\"uid\":\""));
        if (err) return err;
        err = out->write(out,
                         self->trn->uid, KND_UID_SIZE);
        if (err) return err;
        err = out->write(out, "\"", 1);
        if (err) return err;

        buf_size = strftime(buf,
                            KND_NAME_SIZE,
                            ",\"time\":\"%m:%d:%H:%M\"",
                            (const struct tm*)&self->trn->timeinfo);
        err = out->write(out,
                         buf, buf_size);
        if (err) return err;

        
        err = out->write(out, ",\"proc\":\"", strlen(",\"proc\":\""));
        if (err) return err;
        err = out->write(out,
                         self->trn->action, self->trn->action_size);
        if (err) return err;
        err = out->write(out, "\"", 1);
        if (err) return err;

        err = out->write(out, ",\"q\":\"", strlen(",\"q\":\""));
        if (err) return err;
        err = out->write(out,
                         self->trn->query, self->trn->query_size);
        if (err) return err;
        err = out->write(out, "\"", 1);
        if (err) return err;

        if (!self->trn->proc_state) {
            err = out->write(out, ",\"ret\":-1", strlen(",\"ret\":-1"));
            if (err) return err;
        }

        err = out->write(out, "}", 1);
        if (err) return err;

        return knd_OK;
    }

    if (self->name_size) {
        err = out->write(out, 
                         "{\"n\":\"", strlen("{\"n\":\""));
        if (err) return err;

        err = out->write(out,  
                         self->name, self->name_size);
        if (err) return err;
        
        err = out->write(out, "\"", 1);
    }
    else {
        buf_size = sprintf(buf,
                           "{\"id\":\"%s\"",
                           self->obj_id);
        err = out->write(out,  
                         buf, buf_size);
        if (err) return err;
    }

    /*if (self->obj) {
        knd_log("\n .. add OBJ summary!\n\n");

        err = out->write(out, 
                         ",\"sum\":{\"title\":\"", strlen(",\"sum\":{\"title\":\""));
        if (err) return err;

        err = out->write(out,  
                         self->obj->name, self->obj->name_size);
        if (err) return err;

        err = out->write(out, "\"}", 2);
        if (err) return err;
    }
    */
    
    if (self->hilite_pos) {
        buf_size = sprintf(buf,
                           ",\"off\":%lu",
                           (unsigned long)self->hilite_pos);
        err = out->write(out, 
                          buf, buf_size);
        if (err) return err;
    }
            
    if (self->type == KND_REF_CODE) {
        elemref = self->elemrefs;
        err = out->write(out, 
                          ",\"elems\":[", strlen(",\"elems\":["));
        if (err) return err;

        while (elemref) {
            err = out->write(out, 
                              "{\"path\":\"", strlen("{\"path\":\""));
            if (err) return err;

            err = out->write(out, 
                              elemref->name, elemref->name_size);
            if (err) return err;
            
            err = out->write(out,  
                              "\"", 1);
            if (err) return err;


            err = out->write(out, 
                              ",\"coderefs\":[", strlen(",\"coderefs\":["));
            if (err) return err;

            coderef = elemref->coderefs;
            err = coderef->export(coderef, 0, KND_FORMAT_JSON);
            if (err) return err;
            
            err = out->write(out,  "]", 1);
            if (err) return err;
            
            err = out->write(out,  "}", 1);
            if (err) return err;
            
            elemref = elemref->next;
        }

        err = out->write(out,  
                      "]", 1);
        if (err) return err;
    }

    err = out->write(out,  
                      "}", 1);
    if (err) return err;

    
    return err;
}



static int 
kndObjRef_export_GSC(struct kndObjRef *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndOutput *out = self->out;
    struct kndSortAttr *attr = NULL;
    struct kndSortTag *tag = NULL;
    int err;

    /* REF only */
    if (!self->obj) {
        if (!self->sorttag) return knd_OK;
        
        err = out->write(out, "{", 1);
        if (err) return err;

        for (size_t i = 0; i < self->sorttag->num_attrs; i++) {
            attr =  self->sorttag->attrs[i];
            if (!attr) continue;
        
            err = out->write(out, "{", 1);
            if (err) return err;

            err = out->write(out, attr->name, attr->name_size);
            if (err) return err;

            buf_size = sprintf(buf, ":%lu", (unsigned long)attr->type);
            err = out->write(out, buf, buf_size);
            if (err) return err;

            err = out->write(out, " ", 1);
            if (err) return err;
        
            err = out->write(out, attr->val, attr->val_size);
            if (err) return err;

            err = out->write(out, "}", 1);
            if (err) return err;
        }


        err = out->write(out, "}", 1);
        if (err) return err;

        return knd_OK;
    }

    tag = self->sorttag;
    if (self->obj->tag)
        tag = self->obj->tag;

    if (!tag) {
        knd_log("  -- no OBJ sorttag in \"%s\"\n", self->obj_id);
        return knd_FAIL;
    }
    
    /*self->obj->tag->str(self->obj->tag); */

    err = out->write(out, "{", 1);
    if (err) return err;

    for (size_t i = 0; i < tag->num_attrs; i++) {
        attr =  tag->attrs[i];
        if (!attr) continue;
        
        err = out->write(out, "{", 1);
        if (err) return err;

        err = out->write(out, attr->name, attr->name_size);
        if (err) return err;

        buf_size = sprintf(buf, ":%lu", (unsigned long)attr->type);
        err = out->write(out, buf, buf_size);
        if (err) return err;

        err = out->write(out, " ", 1);
        if (err) return err;
        
        /*knd_log("     GSC attr val: \"%s\"\n", attr->val);*/
        
        err = out->write(out, attr->val, attr->val_size);
        if (err) return err;

        err = out->write(out, "}", 1);
        if (err) return err;
    }

    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}

static int
kndObjRef_export_HTML(struct kndObjRef *self)
{
    //char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size;

    struct kndOutput *out = self->out;
    //struct kndElemRef *elemref;
    //struct kndCodeRef *coderef;

    int err;

    if (self->type == KND_REF_TID) {
        err = out->write(out, 
                         "{\"n\":\"", strlen("{\"n\":\""));
        if (err) return err;

        err = out->write(out,
                         self->trn->tid, KND_TID_SIZE);
        if (err) return err;
        
        err = out->write(out, "\"", 1);
        if (err) return err;

        err = out->write(out, "}", 1);
        if (err) return err;
        
        return knd_OK;
    }

    if (self->name_size) {
        err = out->write(out, 
                         "<A HREF=\"/", strlen("<A HREF=\"/"));
        if (err) return err;

        err = out->write(out,  
                         self->name, self->name_size);
        if (err) return err;

        err = out->write(out, "\">", strlen("\">"));

        err = out->write(out,  
                         self->name, self->name_size);
        if (err) return err;
        

        err = out->write(out, "</A>", strlen("</A>"));
        return knd_OK;
    }
    
    /*if (self->obj) {
        knd_log("\n .. add OBJ summary!\n\n");

        err = out->write(out, 
                         ",\"sum\":{\"title\":\"", strlen(",\"sum\":{\"title\":\""));
        if (err) return err;

        err = out->write(out,  
                         self->obj->name, self->obj->name_size);
        if (err) return err;

        err = out->write(out, "\"}", 2);
        if (err) return err;
    }
    */

    
    return knd_OK;
}




static int 
kndObjRef_export(struct kndObjRef *self,
                 knd_format format)
{
    int err = knd_FAIL;
    size_t depth = 0;
    
    switch(format) {
    case KND_FORMAT_JSON:
        err = kndObjRef_export_JSON(self, depth);
        if (err) return err;
        break;
    case KND_FORMAT_GSC:
        err = kndObjRef_export_GSC(self);
        if (err) return err;
        break;
    case KND_FORMAT_HTML:
        err = kndObjRef_export_HTML(self);
        if (err) return err;
        break;
    default:
        break;
    }

    return err;
}



static int 
kndObjRef_expand(struct kndObjRef *self)
{
    struct kndObject *obj = NULL;
    int err;
   
    if (DEBUG_OBJREF_LEVEL_TMP)
        knd_log("  .. expanding objref \"%s::%s\" of class \"%s\"..\n",
                self->obj_id, self->name, self->cache->baseclass->name);

    if (self->obj)
        return knd_OK;

    obj = self->cache->db->get(self->cache->db, self->obj_id);
    if (obj) {
        self->obj = obj;
        return knd_OK;
    }

    if (DEBUG_OBJREF_LEVEL_TMP)
        knd_log(".. reading obj \"%s\".. repo: %p",
                self->obj_id, self->cache->repo);

    err = self->cache->repo->read_obj(self->cache->repo,
                                      self->obj_id,
                                      self->cache,
                                      &obj);
    if (err) return err;

    if (DEBUG_OBJREF_LEVEL_TMP)
        knd_log(".. expanding obj \"%s\"..",
                self->obj_id);

    err = obj->expand(obj, 1);
    if (err) return err;

    
    self->obj = obj;

    return knd_OK;
}



static int
kndObjRef_read_coderefs(struct kndObjRef    *self,
                        struct kndElemRef *elemref,
                        const char *rec)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;

    struct kndCodeRef *coderef = NULL;
    struct kndCodeRef *spec = NULL;
    const char *c, *b;

    bool in_base = true;
    bool in_spec = false;
    long numval;
    int err;
    
    b = rec;
    c = rec;

    if (DEBUG_OBJREF_LEVEL_3)
        knd_log("  .. ObjRef parsing coderefs: \"%s\"..\n", rec);
    
    while (*c) {
        switch (*c) {
        case '+':
            buf_size = c - b;
            memcpy(buf, b, buf_size);
            buf[buf_size] = '\0';
            
            /* get the numeric offset */
            err = knd_parse_num((const char*)buf, &numval);
            if (err) return err;

            if (in_base) {
                err = kndCodeRef_new(&coderef);
                if (err) goto final;
                coderef->out = self->out;
                coderef->linear_pos = (size_t)numval;
            }
            
            if (in_spec) {
                spec->linear_pos = (size_t)numval;
            }
            
            b = c + 1;
            break;

        case ':':
            buf_size = c - b;
            memcpy(buf, b, buf_size);
            buf[buf_size] = '\0';
            
            /* get the numeric offset */
            err = knd_parse_num((const char*)buf, &numval);
            if (err) return err;

            if (in_base) {
                coderef->linear_len = (size_t)numval;
                in_base = false;
            }

            in_spec = true;
            err = kndCodeRef_new(&spec);
            if (err) goto final;
            spec->out = self->out;
            
            b = c + 1;
            break;
        case ';':
            in_base = true;
            in_spec = false;
            b = c + 1;
            break;
        default:
            break;
        }
        c++;
    }

    buf_size = c - b;

    if (buf_size) {
        memcpy(buf, b, buf_size);
        buf[buf_size] = '\0';

        err = knd_parse_num((const char*)buf, &numval);
        if (err) return err;


        if (in_spec) {
            spec->linear_len = (size_t)numval;
            coderef->spec = spec;
        }
    }
    
    if (!coderef) return knd_FAIL;

    
    coderef->next = elemref->coderefs;
    elemref->coderefs = coderef;

    /*coderef->str(coderef, 1);*/
    
    return knd_OK;

 final:

    if (coderef)
        coderef->del(coderef);
    
    return err;
}



static int
kndObjRef_add_elemref(struct kndObjRef    *self __attribute__((unused)),
                      struct kndElemRef *elemref)
{
    knd_log("  .. add elemref \"%s\"\n\n", elemref->name);

    return knd_OK;
}


static int 
kndObjRef_sync_elemref(struct kndObjRef *self,
                       struct kndElemRef *elemref)
{
    //char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size;
    struct kndCodeRef *coderef;
    //char *c;
    int err;

    if (elemref->elem) {
        /*err = self->out->write(self->out, 
                                "~", 1);
        if (err) goto final;

        err = self->out->write(self->out, 
                                elemref->elem->abbr, elemref->elem->abbr_size);
        if (err) goto final;


        if (elemref->name_size > elemref->elem->path_size) {
            c = elemref->name + elemref->elem->path_size;

            err = self->out->write(self->out, 
                                    c, elemref->name_size - elemref->elem->path_size);
            if (err) goto final;
            }*/

    }
    else {
        err = self->out->write(self->out, 
                                elemref->name, elemref->name_size);
        if (err) goto final;
    }

    coderef = elemref->coderefs;
    while (coderef) {

        coderef->out = self->out;
        err = coderef->sync(coderef);
        if (err) goto final;

        coderef = coderef->next;
    }


final:

    return err;
}
static int 
kndObjRef_sync(struct kndObjRef *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    struct kndElemRef *elemref;
    struct kndSortAttr *attr;
    int err;

    /*err = maze->write(maze,  GSL_OPEN_DELIM, GSL_OPEN_DELIM_SIZE);
    if (err) goto final;
    */
    
    err = self->out->write(self->out, 
                            self->obj_id, KND_ID_SIZE);
    if (err) goto final;
    
    /*for (i = 0; i < self->sorttag->num_attrs; i++) {
        attr =  self->sorttag->attrs[i];
        curr_size = sprintf(buf, "(%s:%s)",
                            attr->name, attr->val);
        self->out->write(self->out,  buf, curr_size);
        }*/

    if (self->sorttag && self->sorttag->num_attrs) {

        /* get default facet, the last one */
        attr =  self->sorttag->attrs[self->sorttag->num_attrs - 1];
        if (attr->start_pos) {
            buf_size = sprintf(buf, "@%lu", (unsigned long)attr->start_pos);
            err = self->out->write(self->out,  buf, buf_size);
            if (err) goto final;
        }

        if (attr->val_size) {
            err = self->out->write(self->out, "~", 1);
            if (err) goto final;
                
            err = self->out->write(self->out,  attr->val, attr->val_size);
            if (err) goto final;
        }
    }

    if (self->type == KND_REF_CODE) {
        elemref = self->elemrefs;
        
        while (elemref) {
            err = kndObjRef_sync_elemref(self, elemref);
            if (err) goto final;
            
            elemref = elemref->next;
        }
        
    }
    

    
 final:
    
    return err;
}

static int 
kndObjRef_clone(struct kndObjRef *self,
                size_t attr_id,
                const char *tail,
                size_t tail_size,
                struct kndObjRef **result)
{
    struct kndSortAttr *attr, *sub_attr, *orig_attr;
    struct kndObjRef *ref = NULL;
    struct kndSortTag *tag;
   
    int err;

    /* make a ref copy */
    err = kndObjRef_new(&ref);
    if (err) return knd_NOMEM;

    if (self->obj_id_size) {
        memcpy(ref->obj_id, self->obj_id, KND_ID_SIZE);
        ref->obj_id[KND_ID_SIZE] = '\0';
        ref->obj_id_size = KND_ID_SIZE;
    }
    
    ref->obj = self->obj;
    ref->cache = self->cache;
    ref->trn = self->trn;

    if (self->name_size) {
        memcpy(ref->name, self->name, self->name_size);
        ref->name_size = self->name_size;
    }
    
    err = kndSortTag_new(&tag);
    if (err) goto cleanup;

    ref->baseclass = self->baseclass;
   
    attr = self->sorttag->attrs[attr_id];

    if (attr->is_trivia)
        ref->is_trivia = true;
    
    /* remainder if any */
    /*if (attr->type == KND_FACET_POSITIONAL) {
        if (!tail_size) {
            tail = "_";
            tail_size = 1;
        }
        }*/

    if (tail_size) {
        sub_attr = malloc(sizeof(struct kndSortAttr));
        if (!sub_attr) {
            err = knd_NOMEM;
            goto cleanup;
        }
        memset(sub_attr, 0, sizeof(struct kndSortAttr));

        sub_attr->type = attr->type;
        sub_attr->numval = attr->numval + 1;
        sub_attr->start_pos = attr->start_pos;
        sub_attr->len = attr->len;

        memcpy(sub_attr->name, attr->name, attr->name_size);
        sub_attr->name_size = attr->name_size;
        sub_attr->name[sub_attr->name_size] = '\0';

        memcpy(sub_attr->val, tail, tail_size);
        sub_attr->val_size = tail_size;
        sub_attr->val[tail_size] = '\0';
        
        tag->attrs[tag->num_attrs] = sub_attr;
        tag->num_attrs++;
    }
    
    for (size_t i = attr_id + 1; i < self->sorttag->num_attrs; i++) {
        orig_attr = self->sorttag->attrs[i];

        if (orig_attr->skip) continue;

        sub_attr = malloc(sizeof(struct kndSortAttr));
        if (!sub_attr) {
            err = knd_NOMEM;
            goto cleanup;
        }
        
        memset(sub_attr, 0, sizeof(struct kndSortAttr));

        sub_attr->type = orig_attr->type;

        memcpy(sub_attr->name, orig_attr->name, orig_attr->name_size);
        sub_attr->name_size = orig_attr->name_size;
        sub_attr->name[sub_attr->name_size] = '\0';
        
        memcpy(sub_attr->val, orig_attr->val, orig_attr->val_size);
        sub_attr->val_size = orig_attr->val_size;
        sub_attr->val[sub_attr->val_size] = '\0';
        
        tag->attrs[tag->num_attrs] = sub_attr;
        tag->num_attrs++;
    }

    ref->type = self->type;
    ref->sorttag = tag;
    ref->elemrefs = self->elemrefs;

    *result = ref;
    
    return knd_OK;
    
 cleanup:
    
    if (ref)
        ref->del(ref);

    return err;
}


static int 
kndObjRef_import(struct kndObjRef     *self __attribute__((unused)),
                  struct kndConcept *baseclass __attribute__((unused)),
                  char                *rec)
{
    char *b;
    char *c;
    //size_t label_size;
    int err;

    /* parse DB rec */
    b = rec;
    c = rec;

    err = knd_OK;

    return err;
}

extern int 
kndObjRef_new(struct kndObjRef **objref)
{
    struct kndObjRef *self;
    
    self = malloc(sizeof(struct kndObjRef));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndObjRef));

    self->del = kndObjRef_del;
    self->str = kndObjRef_str;
    self->export = kndObjRef_export;

    self->read_coderefs = kndObjRef_read_coderefs;
    self->add_elemref = kndObjRef_add_elemref;

    self->expand = kndObjRef_expand;
    
    self->clone = kndObjRef_clone;
    self->sync = kndObjRef_sync;
    self->import = kndObjRef_import;

    *objref = self;

    return knd_OK;
}
