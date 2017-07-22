#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_concept.h"
#include "knd_attr.h"
#include "knd_elem.h"
#include "knd_repo.h"
#include "knd_object.h"
#include "knd_text.h"
#include "knd_refset.h"
#include "knd_sorttag.h"
#include "knd_parser.h"

#include "knd_output.h"
#include "knd_user.h"

#define DEBUG_OBJ_LEVEL_1 0
#define DEBUG_OBJ_LEVEL_2 0
#define DEBUG_OBJ_LEVEL_3 0
#define DEBUG_OBJ_LEVEL_4 0
#define DEBUG_OBJ_LEVEL_TMP 1

static int
knd_compare_obj_by_match_descend(const void *a,
                                 const void *b)
{
    struct kndObject **obj1, **obj2;

    obj1 = (struct kndObject**)a;
    obj2 = (struct kndObject**)b;

    if ((*obj1)->average_score == (*obj2)->average_score) return 0;

    if ((*obj1)->average_score < (*obj2)->average_score) return 1;

    return -1;
}


static int 
del(struct kndObject *self)
{
    knd_log("  .. free obj: \"%s\".. \n", self->name);

    free(self);

    return knd_OK;
}


static void
str(struct kndObject *self,
    size_t depth)
{
    size_t offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;
    char *offset = malloc(offset_size + 1);
    struct kndElem *elem;
    
    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    if (!self->parent) {
        knd_log("\n%sOBJ %s \"%s\"\n",
                offset, self->name, self->id);
    }

    elem = self->elems;
    while (elem) {
        elem->str(elem, depth + 1);
        elem = elem->next;
    }
}


static int
kndObject_index_CG(struct kndObject *self)
{
    struct kndElem *b = NULL;
    struct kndElem *spec = NULL;
    struct kndElem *oper = NULL;
    struct kndElem *elem;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log("    .. indexing inline CG..\n");

    elem = self->elems;
    while (elem) {
        if (!strcmp(elem->name, "b")) {
            b = elem;
            goto next_elem;
        }

        if (!strcmp(elem->name, "spec")) {
            spec = elem;
            goto next_elem;
        }

        if (!strcmp(elem->name, "oper")) {
            oper = elem;
            goto next_elem;
        }

    next_elem:
        elem = elem->next;
    }
    

    if (!b || !spec || !oper) {
        if (DEBUG_OBJ_LEVEL_TMP)
            knd_log("\n   -- incomplete CG: base: %p spec: %p oper: %p\n",
                    b, spec, oper);
        return knd_FAIL;
    }
    
    return knd_OK;
}

static int
kndObject_index_inline(struct kndObject *self)
{
    struct kndElem *elem;
    int err;
    
    if (DEBUG_OBJ_LEVEL_TMP)
        knd_log(".. indexing inline OBJ of class %s  IDX: \"%s\"..",
                self->parent->attr->dc->name,
                self->parent->attr->dc->idx_name);

    if (!strcmp(self->parent->attr->dc->idx_name, "CG")) {
        return kndObject_index_CG(self);
    }

    elem = self->elems;
    while (elem) {
        elem->out = self->out;
        elem->tag = self->tag;

        err = elem->index(elem);
        if (err) return err;
        
        elem = elem->next;
    }
    
    return knd_OK;
}


static int
import_GSL(struct kndObject *self,
           const char *rec,
           size_t *total_size)
{
    char idbuf[KND_ID_SIZE];
    const char *c, *b, *e;
    struct kndElem *elem = NULL;
    size_t buf_size;
    
    size_t chunk_size = 0;
    
    bool in_name = false;
    bool in_elem = false;
    bool in_elem_name = false;
    bool in_elem_val = false;

    int err = knd_FAIL;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log(".. importing OBJ of class \"%s::%s\" [%s]\n\n%s",
                self->cache->baseclass->namespace,
                self->cache->baseclass->name, self->id, rec);
    
    /* parse and validate OBJ */
    c = rec;
    b = rec;
    e = rec;
    
    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:
            e = c + 1;
            if (!in_elem) break;

            if (!in_elem_name) break;

            if (!in_elem_val) {
                in_elem_val = true;
                b = c;
                break;
            }
            
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            break;
        case '{':
            if (!in_name) {
                in_name = true;
                b = c + 1;
                break;
            }

            if (!self->name_size) {
                buf_size = e - b;
                if (!buf_size) return knd_LIMIT;
                if (buf_size >= KND_NAME_SIZE)
                    return knd_LIMIT;

                memcpy(self->name, b, buf_size);
                self->name[buf_size] = '\0';
                
                if (DEBUG_OBJ_LEVEL_2)
                    knd_log("OBJ NAME: %s", self->name);

                self->name_size = buf_size;
                
                /* hypen sign indicates the absence of user-supplied
                 * name of an object */

                /* anonymous obj takes obj id as name */

                /* TODO:  add function to generate a unique and meaningful
                 * object name based on certain attr values */
                
                if (self->name[0] == '-') {
                    memcpy(self->name, self->id, KND_ID_SIZE);
                    self->name[KND_ID_SIZE] = '\0';
                    self->name_size = KND_ID_SIZE;
                }

                err = self->cache->name_idx->lookup_name(self->cache->name_idx,
                                                         self->name, self->name_size,
                                                         self->name, self->name_size, idbuf);
                if (!err) {
                    knd_log("-- OBJ name doublet: \"%s\"", self->name);
                    err = knd_FAIL;
                    goto final;
                }

            }
            
            err = kndElem_new(&elem);
            if (err) goto final;
            elem->obj = self;
            elem->root = self;
            elem->out = self->out;
            
            err = elem->parse(elem, c, &chunk_size);
            if (err) goto final;

            c += chunk_size;

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
            break;
        case '}':
            /* TODO: all elems set? */
            /* check completeness */
                
            /* check doublets? */

            *total_size = c - rec;
            
            return knd_OK;
        case '[':
            err = kndElem_new(&elem);
            if (err) goto final;

            elem->obj = self;
            elem->root = self;
            elem->is_list = true;
            elem->out = self->out;
            
            c++;
            err = elem->parse_list(elem, c, &chunk_size);
            if (err) goto final;

            if (!self->tail) {
                self->tail = elem;
                self->elems = elem;
            }
            else {
                self->tail->next = elem;
                self->tail = elem;
            }

            self->num_elems++;
            c += chunk_size;
            break;
        }

        c++;
    }
    
    err = knd_FAIL;

 final:
    
    return err;
}




static int
kndObject_parse_GSC(struct kndObject *self,
                    const char *rec,
                    size_t rec_size)
{
    char recbuf[KND_TEMP_BUF_SIZE + 1];
    const char *b, *c, *e;
    struct kndElem *elem = NULL;

    size_t chunk_size = 0;
    
    bool in_body = false;
    bool in_name = false;

    bool in_elem = false;
    bool in_elem_name = false;
    bool in_elem_val = false;
    size_t curr_size = 0;
    
    int err = knd_FAIL;

    if (!rec_size)
        return knd_FAIL;
    
    if (DEBUG_OBJ_LEVEL_2) {
        if (rec_size < KND_TEMP_BUF_SIZE) {
            memcpy(recbuf, rec, rec_size);
            recbuf[rec_size] = '\0';
        }
        else {
            memcpy(recbuf, rec, KND_TEMP_BUF_SIZE);
            recbuf[KND_TEMP_BUF_SIZE] = '\0';
        }
        knd_log("\n  .. parse OBJ of class \"%s\"..  REC:\n %s\n REC SIZE: %lu\n\n\n",
                self->cache->baseclass->name, recbuf, (unsigned long)rec_size);
    }
    
    /* parse and validate OBJ */
    c = rec;
    b = rec;
    e = rec;
    
    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:
            e = c + 1;
            
            if (!in_elem) break;

            if (!in_elem_name) break;

            if (!in_elem_val) {
                in_elem_val = true;
                b = c;
                break;
            }
            
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            /* whitespace */
            if (!in_body)
                break;
            
            break;
        case '{':
            if (!in_body) {
                in_body = true;
                b = c + 1;
                break;
            }

            if (!in_name) {
                chunk_size = e - b;
                if (chunk_size >= KND_NAME_SIZE) {
                    err = knd_LIMIT;
                    return err;
                }
                memcpy(self->name, b, chunk_size);
                self->name[chunk_size] = '\0';
                self->name_size = chunk_size;
                
                in_name = true;
                b = c + 1;
            }

            
            /* special field? */
            /*if (curr_size < rec_size) {
                b = c + 1;
                if (*b == '_') {
                    err = kndObject_parse_special_GSC(self, c, &chunk_size);
                    if (err) return err;

                    c += chunk_size;
                    curr_size += chunk_size;
                    break;
                }
                }*/
      

            err = kndElem_new(&elem);
            if (err) return err;

            elem->obj = self;
            elem->root = self;
            elem->out = self->out;

            err = elem->parse(elem, c, &chunk_size);
            if (err) {
                if (DEBUG_OBJ_LEVEL_TMP)
                    knd_log("  -- failed to parse elem starting from \"%s\" :(\n\n", c);
                return err;
            }
            
            c += chunk_size;
            curr_size += chunk_size;
            
            if (!self->tail) {
                self->tail = elem;
                self->elems = elem;
            }
            else {
                self->tail->next = elem;
                self->tail = elem;
            }
            self->num_elems++;

            if (DEBUG_OBJ_LEVEL_2)
                knd_log("\n   ++ obj elem \"%s\" parsed OK, continue from: \"%s\"\n",
                        elem->name, c);
            
            elem = NULL;
            break;
        case '}':

            if (in_body) {
                in_body = false;

                /* all elems set? */
                
                /* check completeness */
                
                /* check doublets? */
                
                return knd_OK;
            }

            break;
        case '[':
            err = kndElem_new(&elem);
            if (err) return err;

            elem->obj = self;
            elem->root = self;
            elem->is_list = true;
            elem->out = self->out;
            
            c++;
            curr_size++;
            
            err = elem->parse_list(elem, c, &chunk_size);
            if (err) return err;

            if (!self->tail) {
                self->tail = elem;
                self->elems = elem;
            }
            else {
                self->tail->next = elem;
                self->tail = elem;
            }

            self->num_elems++;
            
            c += chunk_size;
            curr_size += chunk_size;
            break;
        }

        
        curr_size++;
        
        if (curr_size >= rec_size) break;
        c++;
    }
    
    return knd_FAIL;
}




static int 
kndObject_expand(struct kndObject *self,
                 size_t depth)
{
    struct kndElem *elem;
    struct kndConcept *dc;
    struct kndAttr *attr;
    struct kndElemState *elem_state;
    struct kndRepoCache *cache;

    struct kndRelClass *relc;
    struct kndRelType *reltype;
    struct kndObjRef *r;

    struct kndObject *obj;
    int err;

    
    if (DEBUG_OBJ_LEVEL_3)
        knd_log(" .. expanding OBJ %s/%s\n",
                self->cache->baseclass->name, self->id);
    
    /* any default values of elems? */
    dc = self->cache->baseclass;
    dc->rewind(dc);
    do {
        self->cache->baseclass->next_attr(self->cache->baseclass, &attr);
        if (!attr) break;
        if (!attr->default_val_size) continue;
        
        if (!strcmp(attr->default_val, "$NAME")) {

            err = kndElem_new(&elem);
            if (err) return err;
            elem->obj = self;
            elem->root = self;
            elem->out = self->out;
            elem->is_default = true;
            
            memcpy(elem->name, attr->name, attr->name_size);
            elem->name_size = attr->name_size;
            elem->name[elem->name_size] = '\0';

            elem->attr = attr;

            elem_state = malloc(sizeof(struct kndElemState));
            if (!elem_state)
                return knd_NOMEM;

            memset(elem_state, 0, sizeof(struct kndElemState));
            elem->states = elem_state;
            elem->num_states = 1;
            
            memcpy(elem_state->val, self->name, self->name_size);
            elem_state->val[self->name_size] = '\0';
            elem_state->val_size = self->name_size;

            /* get GUID */
            err = self->cache->repo->get_guid(self->cache->repo,
                                              attr->dc,
                                              self->name, self->name_size,
                                              elem_state->ref);
            if (err) return err;

            elem_state->ref_size = KND_ID_SIZE;


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
    } while (attr);


    /*  expand REFS */
    elem = self->elems;
    while (elem) {
        if (DEBUG_OBJ_LEVEL_2) {
            knd_log("    .. expanding elem \"%s\"?\n",
                    elem->name);
        }

        /* expand inner obj? */
        if (elem->inner) {
            
            if (DEBUG_OBJ_LEVEL_2) {
                knd_log("    .. expanding inner obj \"%s\"?\n",
                        elem->name);
            }

            obj = elem->inner;
            while (obj) {
                err = obj->expand(obj, depth);
                if (err) return err;
                obj = obj->next;
            }
            
            goto next_elem;
        }
                
        if (elem->attr->type != KND_ELEM_REF)
            goto next_elem;

        dc = elem->attr->dc;
        if (!dc) goto next_elem;


        /* TODO */
        
        /*if (elem->ref_class)
            dc = elem->ref_class;

        if (DEBUG_OBJ_LEVEL_2) {
            knd_log("    .. expanding elem REF to class \"%s\"\n",
                    dc->name);
        }

        err = self->cache->repo->get_cache(self->cache->repo, dc, &cache);
        if (err) return knd_FAIL;

        obj = elem->states->refobj;
        if (!obj) {
            cache->repo->out = self->out;
            
            err = cache->repo->read_obj(cache->repo,
                                      (const char*)elem->states->ref,
                                      cache,
                                      &obj);
            if (err) {
                if (DEBUG_OBJ_LEVEL_TMP)
                    knd_log("  -- failed to expand ELEM REF: %s:%s:%s :(\n",
                            dc->name, elem->states->ref,
                            elem->states->val);
                goto next_elem;
            }
            
            elem->states->refobj = obj;
        }

        if (DEBUG_OBJ_LEVEL_3)
            knd_log("  ?? further expansion needed? depth: %lu max: %lu \n",
                    (unsigned long)depth, (unsigned long)self->export_depth);

        if (depth < self->export_depth) {

            err = obj->expand(obj, depth + 1);
            if (err) return err;
        }
        */
        
        next_elem:
        elem = elem->next;
    }

    self->is_expanded = true;
    
    if (depth) return knd_OK;
    
    if (DEBUG_OBJ_LEVEL_3)
        knd_log(".. expand back rels..");
    
    /* expand back relations */
    relc = self->rel_classes;
    while (relc) {
        err = self->cache->repo->get_cache(self->cache->repo, relc->dc, &cache);
        if (err) return err;

        reltype = relc->rel_types;
        while (reltype) {
            r = reltype->refs;
            while (r) {
                r->cache = cache;

                err = r->expand(r);
                if (err) return err;

                r = r->next;
            }
            reltype = reltype->next;
        }
        relc = relc->next;
    }


    return knd_OK;
}



static int 
import(struct kndObject *self,
       const char *rec,
       size_t *total_size,
       knd_format format)
{
    int err;

    switch(format) {
    case KND_FORMAT_GSL:
        err = import_GSL(self, rec, total_size);
        if (err) {
            knd_log("-- GSL import of \"%s\" obj failed :(",
                    self->id);
            return err;
        }
        break;
    default:
        break;
    }
    
    if (DEBUG_OBJ_LEVEL_3) {
        knd_log("   ++ obj REC [%s] parsed and verified:\n %s\n\n",
                self->id, rec);
    }
    
    /* anything to save directly on the filesystem? */
    /*if (data->obj_size) {
        
        if (DEBUG_OBJ_LEVEL_2)
            knd_log("   ++ obj %s has attachment: %s [%s] size: %lu\n",
                    self->id,
                    data->filename,
                    data->mimetype,
                    (unsigned long)data->obj_size);

        sprintf(buf,
                "%s/%s",
                self->cache->repo->path, self->cache->baseclass->name);

        knd_make_id_path(dbpath,
                         buf,
                         self->id, NULL);

        if (DEBUG_OBJ_LEVEL_TMP)
            knd_log("  SAVE attachment file to: %s\n",
                    dbpath);
    */
    
        /* create path to object's folder */
        /*err = knd_mkpath(dbpath, 0755, false);
        if (err) {
            knd_log("  -- mkpath failed :(\n");
            goto final;
        }
        */
    
        /* write metadata */
        /*err = knd_write_file((const char*)dbpath,
                       data->filename, data->obj, data->obj_size);
        if (err) {
            knd_log("  -- write %s failed: %d :(\n", data->filename, err);
            goto final;
        }
        
        self->filesize = data->obj_size;
    }
        */

    if (DEBUG_OBJ_LEVEL_TMP)
        self->str(self, 1);

    return knd_OK;
}


static int
kndObject_update(struct kndObject *self,
                  const char *rec,
                  size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    const char *c;
    const char *b;

    struct kndElem *elem = NULL;

    size_t chunk_size = 0;
    
    bool in_oper = false;
    bool in_elem = false;
    bool in_list_elem = false;
    bool elem_found = false;
    bool have_oper_name = false;
    
    int err = knd_FAIL;
    
    if (DEBUG_OBJ_LEVEL_TMP)
        knd_log("  ...updating object \"%s\" (class: %s): %s\n\n",
                self->id, self->cache->baseclass->name, rec);

    /* parse and validate OBJ */
    c = rec;
    b = c;
    
    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            /* whitespace */
            break;
        case '[':
            if (!in_list_elem) 
                in_list_elem = true;
            
            break;
        case ']':
            if (in_list_elem)
                in_list_elem = false;
            break;
        case '{':
            if (!in_oper) {
                in_oper = true;
                b = c + 1;
                break;
            }
            
            if (in_list_elem) {
                buf_size = c - b;
                memcpy(buf, b, buf_size);
                buf[buf_size] = '\0';
                
                /*knd_log("\n  == got LIST ELEM name: \"%s\"\n", buf);*/

                elem = self->elems;
                while (elem) {
                    if (!strcmp(elem->name, buf)) {
                        err = elem->update(elem, c, &chunk_size);
                        if (err) goto final;
                        c += chunk_size;
                        break;
                    }
                    elem = elem->next;
                }


                break;
            }
            
            if (!in_elem) {

                if (!have_oper_name) {
                    buf_size = c - b;
                    memcpy(buf, b, buf_size);
                    buf[buf_size] = '\0';
                
                    knd_log("\n  == OPER: \"%s\"\n", buf);
                    
                    if (!strcmp(buf, "UPD")) {
                        knd_log("  .. updating elems of obj \"%s\"..\n", self->name);
                    }
                    have_oper_name = true;
                }
                
                in_elem = true;
                b = c + 1;
                break;
            }

            buf_size = c - b;
            memcpy(buf, b, buf_size);
            buf[buf_size] = '\0';
            
            knd_log("\n  == got ELEM name: \"%s\"\n", buf);
            in_elem = true;
            
            elem = self->elems;
            elem_found = false;
            while (elem) {
                if (!strcmp(elem->name, buf)) {
                    err = elem->update(elem, c, &chunk_size);
                    if (err) goto final;
                    
                    knd_log("  == elem chunk: %lu\n", (unsigned long)chunk_size);
                    
                    c += chunk_size;
                    elem_found = true;
                    break;
                    }
                elem = elem->next;
            }
            
            if (!elem_found) {
                knd_log("   -- elem %s not found :(\n", buf);
                return knd_FAIL;
            }
            
            knd_log("\n  == remainder after elem update: \"%s\"\n", c);
            b = c + 1;
            in_elem = false;
            
            break;
        case '}':
            if (in_oper) {
                buf_size = c - b;
                memcpy(buf, b, buf_size);
                buf[buf_size] = '\0';
                
                knd_log("\n  == OPER: \"%s\"\n", buf);

                if (!strcmp(buf, "DEL")) {
                    knd_log("\n  .. removing obj \"%s\"\n",
                            self->name);


                }
                
                in_oper = false;
                break;
            }
            
            
            *total_size = c - rec;
            return knd_OK;
        }

        c++;
    }
    
    err = knd_FAIL;

 final:
    
    return err;
}


static int
kndObject_parse_inline(struct kndObject *self,
                       const char *rec,
                       size_t *total_size)
{
    struct kndElem *elem = NULL;
    const char *c;
    const char *b;

    size_t chunk_size;
    int err = knd_FAIL;

    c = rec;
    b = c;

    if (DEBUG_OBJ_LEVEL_2)
        knd_log("  .. parsing inline OBJ: \"%s\"\n", rec);
    
    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            /* whitespace */
            break;
        case '[':
            err = kndElem_new(&elem);
            if (err) goto final;

            elem->obj = self;
            elem->root = self->root;
            elem->is_list = true;
            elem->out = self->out;
            
            c++;
            err = elem->parse_list(elem, c, &chunk_size);
            if (err) goto final;

            if (!self->tail) {
                self->tail = elem;
                self->elems = elem;
            }
            else {
                self->tail->next = elem;
                self->tail = elem;
            }

            self->num_elems++;
            c += chunk_size;
            break;
        case '{':
            err = kndElem_new(&elem);
            if (err) goto final;

            elem->obj = self;
            elem->root = self->root;
            elem->out = self->out;
            
            err = elem->parse(elem, c, &chunk_size);
            if (err) goto final;

            c += chunk_size;

            if (!self->tail) {
                self->tail = elem;
                self->elems = elem;
            }
            else {
                self->tail->next = elem;
                self->tail = elem;
            }
            self->num_elems++;
            break;
        case '}':
            
            /*knd_log("  == the end of inner OBJ reached!  remainder: %s\n", c);*/

            *total_size = c - rec;

            return knd_OK;
        }
        
        c++;
    }

 final:
    return err;
}


static int 
kndObject_export_inline_JSON(struct kndObject *self)
{
    struct kndElem *elem;
    int err;

    /* anonymous obj */
    err = self->out->write(self->out, "{", 1);
    if (err) return err;

    elem = self->elems;
    while (elem) {

        elem->out = self->out;
        err = elem->export(elem, KND_FORMAT_JSON, 1);
        if (err) return err;
        
        if (elem->next) {
            err = self->out->write(self->out, ",", 1);
            if (err) return err;
        }

        elem = elem->next;
    }

    err = self->out->write(self->out, "}", 1);
    if (err) return err;

    return knd_OK;
}



static int 
kndObject_export_JSON(struct kndObject *self,
                      bool is_concise)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    //char pathbuf[KND_TEMP_BUF_SIZE];
    //size_t pathbuf_size;

    //struct ooDict *idx;
    //struct kndConcept *dc;
    struct kndElem *elem;
    //struct kndRefSet *refset;

    struct kndRelClass *relc;
    struct kndRepoCache *cache = NULL;
    struct kndRelType *reltype;
    struct kndObjRef *r;
    struct kndObject *obj;

    bool need_separ;
    int err;

    if (DEBUG_OBJ_LEVEL_3)
        knd_log("   .. export OBJ \"%s\"    is_concise: %d\n",
            self->name, is_concise);
    
    if (self->dc) {
        err = kndObject_export_inline_JSON(self);
        return err;
    }
    
    buf_size = sprintf(buf, "{\"n\":\"%s\"",
                       self->name);
    err = self->out->write(self->out, buf, buf_size);
    if (err) goto final;


    buf_size = sprintf(buf, ",\"guid\":\"%s\"",
                       self->id);
    err = self->out->write(self->out, buf, buf_size);
    if (err) goto final;
    

    /* ELEMS */
    if (self->elems) {
        err = self->out->write(self->out, ",\"elems\":{", strlen(",\"elems\":{"));
        if (err) goto final;
    }

    need_separ = false;

    elem = self->elems;
    while (elem) {

        /* filter out detailed presentation */
        if (is_concise) {
            /* inner obj? */
            if (elem->inner) {
                obj = elem->inner;
                obj->out = self->out;

                if (need_separ) {
                    err = self->out->write(self->out, ",", 1);
                    if (err) return err;
                }


                err = self->out->write(self->out, "\"", 1);
                if (err) return err;
                err = self->out->write(self->out, elem->name, elem->name_size);
                if (err) return err;
                err = self->out->write(self->out, "\":", 2);
                if (err) return err;
                
                err = obj->export(obj, KND_FORMAT_JSON, 1);
                if (err) return err;

                need_separ = true;
                goto next_elem;
            }
            
            if (elem->attr) 
                if (elem->attr->concise_level)
                    goto export_elem;

            if (DEBUG_OBJ_LEVEL_2)
                knd_log("  .. skip JSON elem: %s..\n", elem->name);

            goto next_elem;
        }

    export_elem:

        if (need_separ) {
            err = self->out->write(self->out, ",", 1);
            if (err) goto final;
        }

        /* default export */
        elem->out = self->out;
        err = elem->export(elem, KND_FORMAT_JSON, 0);
        if (err) {
            knd_log("-- elem not exported: %s", elem->name);
            goto final;
        }
        
        need_separ = true;
        
    next_elem:
        elem = elem->next;
    }

    if (self->elems) {
        err = self->out->write(self->out, "}", 1);
        if (err) goto final;
    }

    if (is_concise) goto closing;
    

    /* skip relations */
    if (self->export_depth)
        goto closing;
    
    /*  relations */
    if (self->rel_classes) {
        err = self->out->write(self->out, ",\"rels\":[", strlen(",\"rels\":["));
        if (err) return err;
    }

    relc = self->rel_classes;
    while (relc) {

        err = self->cache->repo->get_cache(self->cache->repo, relc->dc, &cache);
        if (err) return err;
        
        err = self->out->write(self->out, "{\"c\":\"",  strlen("{\"c\":\""));
        if (err) return err;

        err = self->out->write(self->out, relc->dc->name, relc->dc->name_size);
        if (err) return err;
        
        err = self->out->write(self->out, "\",\"attrs\":[", strlen("\",\"attrs\":["));
        if (err) return err;

        reltype = relc->rel_types;
        while (reltype) {
            err = self->out->write(self->out, "{\"attr\":\"", strlen("{\"attr\":\""));
            if (err) return err;

            err = self->out->write(self->out, reltype->attr_name, reltype->attr_name_size);
            if (err) return err;

            err = self->out->write(self->out, "\",\"refs\":[", strlen("\",\"refs\":["));
            if (err) return err;

            r = reltype->refs;
            while (r) {
                if (r->obj) {
                    err = self->out->write(self->out, "{\"obj\":", strlen("{\"obj\":"));
                    if (err) return err;

                    r->obj->out = self->out;
                    r->obj->export_depth = self->export_depth + 1;
                    err = r->obj->export(r->obj, KND_FORMAT_JSON, 0);
                    if (err) return err;

                    err = self->out->write(self->out, "}", 1);
                    if (err) return err;
                }
                else {
                    err = self->out->write(self->out, "{\"ref\":\"", strlen("{\"ref\":\""));
                    if (err) return err;

                    err = self->out->write(self->out, r->obj_id, KND_ID_SIZE);
                    if (err) return err;

                    err = self->out->write(self->out, "\"}", 2);
                    if (err) return err;
                }
                
                if (r->next) {
                    err = self->out->write(self->out, ",", 1);
                    if (err) return err;
                }
                
                r = r->next;
            }
            
            err = self->out->write(self->out, "]}", 2);
            if (err) return err;

            if (reltype->next) {
                err = self->out->write(self->out, ",", 1);
                if (err) return err;
            }

            reltype = reltype->next;
        }
        err = self->out->write(self->out, "]}", 2);
        if (err) return err;

        if (relc->next) {
            err = self->out->write(self->out, ",", 1);
            if (err) return err;
        }

        relc = relc->next;
    }

    if (self->rel_classes) {
        err = self->out->write(self->out, "]", 1);
        if (err) return err;
    }
    
 closing:

    err = self->out->write(self->out, "}", 1);
    if (err) goto final;

    
 final:
    return err;
}



static int 
kndObject_export_inline_HTML(struct kndObject *self)
{
    struct kndElem *elem;
    bool plain_mode = false;
    bool got_elem = false;
    int err;

    /* anonymous obj */
    if (!strcmp(self->dc->style_name, "PLAIN"))
        plain_mode = true;

    if (plain_mode) {
        err = self->out->write(self->out, "(", strlen("("));
        if (err) return err;
    }
    else {
        err = self->out->write(self->out, "<P class=\"oo-plain-txt\">", strlen("<P class=\"oo-plain-txt\">"));
        if (err) return err;
    }
    
    elem = self->elems;
    while (elem) {

        /* filter out irrelevant elems */
        if (elem->attr) 
            if (elem->attr->concise_level < 2) {
                /*knd_log("       -- filter out inner obj elem \"%s\" -- \n", elem->name);*/
                goto next_elem;
            }

        /* separator? */
        if (got_elem && plain_mode) {
            err = self->out->write(self->out, ", ", strlen(", "));
            if (err) return err;
        }
        
        elem->out = self->out;

        err = elem->export(elem, KND_FORMAT_HTML, 1);
        if (err) return err;

        got_elem = true;
        
    next_elem:
        elem = elem->next;
    }


    if (plain_mode) {
        err = self->out->write(self->out, ")", strlen(")"));
        if (err) return err;
    }
    else {
        err = self->out->write(self->out, "</P>", strlen("</P>"));
        if (err) return err;
    }

    return knd_OK;
}



static int 
kndObject_export_HTML(struct kndObject *self,
                      bool is_concise)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    //char pathbuf[KND_TEMP_BUF_SIZE];
    //size_t pathbuf_size;

    struct kndOutput *meta_out = NULL;
    //struct ooDict *idx;
    //struct kndConcept *dc;
    struct kndElem *elem;
    //struct kndRefSet *refset;
    
    struct kndRelClass *relc;
    struct kndRepoCache *cache = NULL;
    struct kndRelType *reltype;
    struct kndObjRef *r;
    int depth;
    bool need_separ;
    int err;


    /* TODO */
    meta_out = self->out; /*self->cache->repo->user->reader->obj_out;*/
        
    if (self->dc) {
        if (DEBUG_OBJ_LEVEL_3)
            knd_log("   .. export inline OBJ  style:\"%s\"\n",
                    self->dc->style_name);
        
        err = kndObject_export_inline_HTML(self);
        return err;
    }


    depth = self->export_depth + 1;

    if (self->export_depth < 1) {

        if (is_concise) {
            buf_size = sprintf(buf, "<A HREF=\"/%s\">%s</A>",
                               self->name, self->name);
        }
        else {
            buf_size = sprintf(buf, "<H%d class=\"obj\">%s</H%d>",
                               depth, self->name, depth);


            /* build meta tag TITLE */
            err = meta_out->write(meta_out,
                                  "<TITLE>", strlen( "<TITLE>"));
            if (err) return err;

            err = meta_out->write(meta_out,
                                  self->name, self->name_size);
            if (err) return err;

            
            /* generic site reference */
            /*if (self->cache->repo->user->reader->default_repo_title_size) {
                err = meta_out->write(meta_out,
                                      " | ", strlen(" | "));
                if (err) return err;

                err = meta_out->write(meta_out,
                                      self->cache->repo->user->reader->default_repo_title,
                                      self->cache->repo->user->reader->default_repo_title_size);
                if (err) return err;
                }*/
            

            err = meta_out->write(meta_out,
                                  "</TITLE>", strlen("</TITLE>"));

        }
        
        err = self->out->write(self->out, buf, buf_size);
        if (err) goto final;

    }

    
    /* ELEMS */
    /*if (self->elems) {
        err = self->out->write(self->out, ",\"elems\":{", strlen(",\"elems\":{"));
        if (err) goto final;
        }*/

    elem = self->elems;
    while (elem) {

        /*knd_log("   .. make HTML elem: %s\n",
                elem->name);
        */
        
        /* filter out irrelevant elems */
        if (elem->attr) {
            if (elem->attr->concise_level < 2) {
                
                knd_log("   -- filter out elem \"%s\" -- \n", elem->name);
                
                goto next_elem;
            }


            if (elem->attr->descr_level) {

                err = meta_out->write(meta_out,
                                      "<META name=\"description\" content=\"",
                                      strlen("<META name=\"description\" content=\""));
                if (err) return err;


                if (elem->attr->type == KND_ELEM_TEXT) {
                    elem->text->out = meta_out;

                    err = elem->text->export(elem->text, KND_FORMAT_HTML);
                    if (err) goto final;
                }

                /* TODO: special description content */
                
                err = meta_out->write(meta_out,
                                  "\">", strlen("\">"));
                if (err) return err;
             }

            
        }


        /* default export */
        elem->out = self->out;
        err = elem->export(elem, KND_FORMAT_HTML, 0);
        if (err) {
            knd_log("-- elem not exported: %s\n", elem->name);
            goto final;
        }
        
        need_separ = true;
        
    next_elem:
        elem = elem->next;
    }

    /*if (self->elems) {
        err = self->out->write(self->out, "}", 1);
        if (err) goto final;
        }*/

    /*dc = self->cache->baseclass;
    if (dc) {
        err = self->out->write(self->out, ",\"baseclass\":", strlen(",\"baseclass\":"));
        if (err) goto final;

        dc->out = self->out;
        memcpy(dc->lang_code, self->cache->repo->lang_code, self->cache->repo->lang_code_size);
        dc->lang_code_size = self->cache->repo->lang_code_size;
        
        err = dc->export(dc, KND_FORMAT_JSON);
        if (err) goto final;
        }*/
    

    /* skip relations */
    if (self->export_depth) {
        /*knd_log("   -- no export of RELS of %s\n", self->name);*/
        goto closing;
    }
    
    /*  relations */
    /*if (self->rel_classes) {
        err = self->out->write(self->out, "<DIV class=\"RELS\">", strlen("<DIV class=\"RELS\">"));
        if (err) return err;
        }*/

    relc = self->rel_classes;
    while (relc) {

        err = self->cache->repo->get_cache(self->cache->repo, relc->dc, &cache);
        if (err) return err;
        

        /*if (!strcmp(relc->dc->style_name, "PLAIN")) {
            err = self->out->write(self->out, "<BLOCKQUOTE>",  strlen("<BLOCKQUOTE>"));
              if (err) return err; 
        }
        else {
            err = self->out->write(self->out, "<H2>",  strlen("<H2>"));
            if (err) return err;

            err = self->out->write(self->out, relc->dc->name, relc->dc->name_size);
            if (err) return err;

            err = self->out->write(self->out, "</H2>",  strlen("</H2>"));
            if (err) return err;
            }*/
        
        
        /*err = self->out->write(self->out, "<DIV class=\"attrs\">", strlen("<DIV class=\"attrs\">"));
        if (err) return err;
        */
        
        reltype = relc->rel_types;
        while (reltype) {

            /*err = self->out->write(self->out, "{\"attr\":\"", strlen("{\"attr\":\""));
            if (err) return err;

            err = self->out->write(self->out, reltype->attr_name, reltype->attr_name_size);
            if (err) return err;

            err = self->out->write(self->out, "\",\"refs\":[", strlen("\",\"refs\":["));
            if (err) return err;
            */
            
            r = reltype->refs;
            while (r) {
                if (r->obj) {
                    /*err = self->out->write(self->out, "<div>", strlen("<div>"));
                    if (err) return err;
                    */
                    r->obj->out = self->out;
                    r->obj->export_depth = self->export_depth + 1;
                    err = r->obj->export(r->obj, KND_FORMAT_HTML, 0);
                    if (err) return err;

                    /*err = self->out->write(self->out, "</div>", strlen("</div>"));
                      if (err) return err; */
                    
                }
                else {
                    
                    /*err = self->out->write(self->out, "{\"ref\":\"", strlen("{\"ref\":\""));
                    if (err) return err;

                    err = self->out->write(self->out, r->obj_id, KND_ID_SIZE);
                    if (err) return err;

                    err = self->out->write(self->out, "\"}", 2);
                    if (err) return err; */

                }
                
                r = r->next;
            }
            
            /*err = self->out->write(self->out, "]}", 2);
            if (err) return err;

            if (reltype->next) {
                err = self->out->write(self->out, ",", 1);
                if (err) return err;
                } */

            reltype = reltype->next;
        }


        /*if (!strcmp(relc->dc->style_name, "PLAIN")) {
             err = self->out->write(self->out, "</BLOCKQUOTE>",  strlen("</BLOCKQUOTE>"));
               if (err) return err; 
        }
        else {
            err = self->out->write(self->out, "</DIV>", strlen("</DIV>"));
            if (err) return err;
            } */

        relc = relc->next;
    }

    /*if (self->rel_classes) {
        err = self->out->write(self->out, "</DIV>", strlen("</DIV>"));
        if (err) return err;
        }*/
    
 closing:
    
    /*err = self->out->write(self->out, "</blockquote></blockquote>", strlen("</blockquote></blockquote>"));
    if (err) goto final;
    */
    
 final:
    return err;
}


static int 
kndObject_export_GSC(struct kndObject *self,
                     bool is_concise)
{
    bool got_elem = false;
    struct kndElem *elem;

    //struct kndRelClass *relc;
    //struct kndRelType *reltype;
    //struct kndTermIdx *idx, *term_idx;

    //size_t i, j, ri;
    int err;
    
    if (DEBUG_OBJ_LEVEL_2)
        knd_log("  .. export GSC obj \"%s\" [id: %s]..\n",
                self->name, self->id);

    err = self->out->write(self->out, "{", 1);
    if (err) return err;

    if (!self->parent) {
        err = self->out->write(self->out, self->name, self->name_size);
        if (err) return err;

        err = self->out->write(self->out, " ", 1);
        if (err) return err;
    }
    else {
        if (!self->parent->is_list) {
            err = self->out->write(self->out, self->parent->name, self->parent->name_size);
            if (err) return err;
        }

    }
    
    /*if (self->filename_size) {
        err = knd_make_id_path(buf, "repos",
                               self->cache->repo->id,
                               self->cache->baseclass->name);
        if (err) return err;

        if (DEBUG_OBJ_LEVEL_3)
            knd_log("  == relative Repo path: \"%s\"\n",
                    buf);

        err = knd_make_id_path(pathbuf, buf, self->id, self->filename);
        if (err) return err;

        if (DEBUG_OBJ_LEVEL_3)
            knd_log("   == relative filename: %s\n",
                    pathbuf);
        
        buf_size = sprintf(buf, "{_fn %s}",
                           pathbuf);
        err = self->out->write(self->out, buf, buf_size);
        if (err) return err;
        
        self->filepath = strdup(pathbuf);
        if (!self->filepath) {
            err = knd_NOMEM;
            return err;
        }
        
    }

    if (self->filesize) {
        buf_size = sprintf(buf, "{_fsize %lu}",
                           (unsigned long)self->filesize);
        err = self->out->write(self->out, buf, buf_size);
        if (err) return err;
        } */
    

    /* ELEMS */
    got_elem = false;
    elem = self->elems;
    while (elem) {
        /* filter out detailed presentation */
        /*if (is_concise) {
            if (elem->attr->concise_level)
                goto export_elem;
            goto next_elem;
            }*/

        //export_elem:

        /* default export */
        elem->out = self->out;
        err = elem->export(elem, KND_FORMAT_GSC, is_concise);
        if (err) {
            knd_log("-- export of \"%s\" elem failed: %d :(", elem->name, err);
            return err;
        }
        
        got_elem = true;
        
        //next_elem:
        elem = elem->next;
    }

    /*relc = self->cache->rel_classes;
    while (relc) {
        reltype = relc->rel_types;
        while (reltype) {
            refset = reltype->idx->get(reltype->idx, (const char*)self->name);
            if (!refset) goto next_reltype;

            err = self->out->write(self->out, "{_cls{", strlen("{_cls{"));
            if (err) return err;

            err = self->out->write(self->out, relc->dc->name, relc->dc->name_size);
            if (err) return err;

            err = self->out->write(self->out, "{_rel{", strlen("{_rel{"));
            if (err) return err;

            err = self->out->write(self->out,
                                   reltype->attr->name,
                                   reltype->attr->name_size);
            

            err = self->out->write(self->out, "[", 1);
            if (err) return err;

            for (i = 0; i < KND_ID_BASE; i++) {
                idx = refset->idx[i];
                if (!idx) continue;

                for (j = 0; j < KND_ID_BASE; j++) {
                    term_idx = idx->idx[j];
                    if (!term_idx) continue;

                    for (ri = 0; ri < KND_ID_BASE; ri++) {
                        ref = term_idx->refs[ri];
                        if (!ref) continue;

                        err = self->out->write(self->out, "{", 1);
                        if (err) return err;

                        err = self->out->write(self->out, ref->obj_id, KND_ID_SIZE);
                        if (err) return err;
                        
                        err = self->out->write(self->out, "}", 1);
                        if (err) return err;
                    }
                    
                }
            }

            
            err = self->out->write(self->out, "]", 1);
            if (err) return err;
            

            err = self->out->write(self->out, "}}}}", 4);
            if (err) return err;
            
        next_reltype:
            reltype = reltype->next;
        }

        
        relc = relc->next;
    }
    */
    
    err = self->out->write(self->out, "}", 1);
    if (err) return err;
    
    return knd_OK;
}


/* export object */
static int 
kndObject_export(struct kndObject *self,
                 knd_format format,
                 bool is_concise)
{
    int err;
    
    switch(format) {
    case KND_FORMAT_JSON:
        err = kndObject_export_JSON(self, is_concise);
        if (err) return err;
        break;
    case KND_FORMAT_HTML:
        err = kndObject_export_HTML(self, is_concise);
        if (err) return err;
        break;
    case KND_FORMAT_GSC:
        err = kndObject_export_GSC(self, is_concise);
        if (err) return err;
        break;
    default:
        break;
    }
    
    return knd_OK;
}

/* parse object */
static int 
kndObject_parse(struct kndObject *self,
                const char *rec,
                size_t rec_size,
                knd_format format)
{
    int err;
    
    switch(format) {
    case KND_FORMAT_GSC:
        err = kndObject_parse_GSC(self, rec, rec_size);
        if (err) return err;
        break;
    default:
        break;
    }
    
    return knd_OK;
}


static int 
kndObject_contribute(struct kndObject *self,
                     size_t  matchpoint_num,
                     size_t orig_pos)
{
    struct kndMatchPoint *mp;
    //struct kndMatchResult *res;
    //float score;
    int idx_pos, err;

    if (!self->num_matchpoints) return knd_OK;

    if ((matchpoint_num) > self->num_matchpoints) return knd_OK;
    
    if (self->cache->repo->match_state > self->match_state) {
        self->match_state = self->cache->repo->match_state;
        memset(self->matchpoints, 0, sizeof(struct kndMatchPoint) * self->num_matchpoints);
        self->match_score = 0;
        self->match_idx_pos = -1;
    }
        
    mp = &self->matchpoints[matchpoint_num];

    if (mp->orig_pos) {
        knd_log("  .. this matchpoint was already covered by another unit?\n");
        err = knd_FAIL;
        goto final;
    }
    
    mp->score = KND_MATCH_MAX_SCORE;
    self->match_score += mp->score;
    mp->orig_pos = orig_pos;
    
    self->average_score = (float)self->match_score / (float)self->max_score;

    /*knd_log("   == \"%s\": matched in %lu!    SCORE: %.2f [%lu:%lu]\n",
            self->name,
            (unsigned long)matchpoint_num,
            self->average_score,
            (unsigned long)self->match_score,
            (unsigned long)self->max_score); */

    if (self->average_score >= KND_MATCH_SCORE_THRESHOLD) {

        knd_log("   ++ \"%s\": matching threshold reached: %.2f!\n",
                self->name, self->average_score);

        if (self->match_idx_pos >= 0)
            idx_pos = self->match_idx_pos;
        else {

            if (self->cache->num_matches > KND_MAX_MATCHES) {
                knd_log("  -- results buffer limit reached :(\n");
                return knd_FAIL;
            }
                
            idx_pos = self->cache->num_matches;
            self->match_idx_pos = idx_pos;
            self->cache->matches[idx_pos] = self;
            self->cache->num_matches++;
        }

    }
    
    err = knd_OK;
 final:
    
    return err;
}

static int 
kndObject_match(struct kndObject *self,
                const char *rec,
                size_t     rec_size)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    
    struct kndObject *obj;
    struct kndElem *elem;
    struct kndOutput *out;
    int err;

    self->cache->num_matches = 0;
    
    /* elem with linear seq */
    err = kndElem_new(&elem);
    if (err) goto final;

    elem->obj = self;
    
    err = elem->match(elem, rec, rec_size);
    if (err) goto final;

    qsort(self->cache->matches,
          self->cache->num_matches,
          sizeof(struct kndObject*),
          knd_compare_obj_by_match_descend);

    knd_log("== MATCH RESULTS:");

    out = self->out;

    err = out->write(out, "[", 1);
    if (err) goto final;

    for (size_t i = 0; i < self->cache->num_matches; i++) {
        obj = self->cache->matches[i];

        buf_size = sprintf(buf, "{%s {score %.2f}}",
                           obj->name, obj->average_score);

        knd_log("%s\n", buf);

        err = out->write(out, buf, buf_size);
        if (err) goto final;

        if (i >= KND_MATCH_MAX_RESULTS) break;
    }

    err = out->write(out, "]", 1);
    if (err) goto final;
    
    err = knd_OK;

 final:
    return err;
}


static int 
kndObject_flatten(struct kndObject *self,
                  struct kndFlatTable *table,
                  unsigned long *span)
{
    char idbuf[KND_ID_SIZE];
    struct kndRefSet *refset;
    struct kndObjRef *ref;
    struct kndFlatCell *cell;
    struct kndFlatRow *row;
    struct ooDict *idx = NULL;
    struct kndElem *elem, *e;
    //unsigned long currspan;
    unsigned long timespan = 0;
    unsigned long maxspan = 0;
    long numval = 0;
    long estim = 0;
    int err;

    /*idx = self->cache->contain_idx;*/

    /* TODO */
    // fixme: idx is NULL
    refset = (struct kndRefSet*)idx->get(idx, (const char*)self->name);
    if (refset) {

        knd_log("    ++ DIV with children: %s\n", self->name);

        for (size_t i = 0; i < refset->num_refs; i++) {
            ref = refset->inbox[i];

            err = self->cache->name_idx->lookup_name(self->cache->name_idx,
                                                     self->name, self->name_size,
                                                     self->name, self->name_size, idbuf);
            if (err) {
                knd_log("  -- obj %s not found :(\n", self->name);
                continue;
            }
            
            //err = obj->flatten(obj, table, &currspan);
            //if (err) goto final;

            //if (currspan > maxspan)
            //    maxspan = currspan;
        }


        row = &table->rows[refset->num_refs - 1];
        cell = &row->cols[row->num_cols];
        row->num_cols++;

        /* write terminal cell values */
        cell->span = row->num_cols;
        cell->obj = self;
        cell->estim = estim;
        
    } else {

        knd_log("  == terminal DIV: %s\n", self->name);
        timespan = 0;
        estim = 0;
        elem = self->elems;
        while (elem) {
            if (!strcmp(elem->name, "time")) {
                if (elem->inner) {
                    e = elem->inner->elems;
                    while (e) {
                        if (!strcmp(e->name, "plan")) {
                            err = knd_parse_num(e->states->val, &numval);
                            if (err) return err;
                            if (numval >= 0) timespan = (unsigned long)numval;
                            break;
                        }
                        e = e->next;
                    }
                }
            }
            
            if (!strcmp(elem->name, "estim")) {
                err = knd_parse_num(elem->states->val, &estim);
                if (err) break;
            }
            
            elem = elem->next;
        }

        
        row = &table->rows[table->num_rows];
        cell = &row->cols[row->num_cols];

        // fixme: timespan < 0?
        for (size_t i = 0; i < (size_t)timespan; i++) {
            table->totals[i] += estim;
        }
        
        row->num_cols++;

        /* write terminal cell values */
        cell->span = timespan;
        cell->estim = estim;
        
        maxspan = timespan;
        
        cell->obj = self;

        table->num_rows++;
        
    }
    
    *span = (unsigned long)maxspan;
    return knd_OK;
}


static int 
kndObject_get_idx(struct kndObject *self,
                  const char *idx_name,
                  size_t idx_name_size,
                  struct kndRefSet **result)
{
    struct kndRefSet *refset;
    int err;
    
    refset = self->cache->idxs;
    while (refset) {
        if (!strcmp(refset->name, idx_name)) break;

        refset = refset->next;
    }

    if (!refset) {
        err = kndRefSet_new(&refset);
        if (err) return err;
            
        memcpy(refset->name,
               idx_name,
               idx_name_size);
        refset->name_size = idx_name_size;

        refset->cache = self->cache;
        
        refset->next = self->cache->idxs;
        self->cache->idxs = refset;
    }

    
    *result = refset;
    
    return knd_OK;
}


static int 
kndObject_sync(struct kndObject *self)
{
    char idbuf[KND_ID_SIZE];
    struct kndElem *elem;
    struct kndConcept *dc;
    struct kndRepoCache *cache;
    struct kndObject *obj;
    struct kndRefSet *refset;
    struct kndSortTag *tag;
    struct kndSortAttr *attr;
    struct kndSortAttr *a;
    struct kndObjRef *ref;
    int err;

    if (DEBUG_OBJ_LEVEL_TMP) {
        if (!self->root) {
            knd_log("\n    !! syncing primary OBJ %s::%s\n",
                    self->id, self->name);
        }
        else {
            knd_log("    .. syncing inner obj %s..\n",
                    self->parent->name);
        }
    }
    
    elem = self->elems;
    while (elem) {
        /* resolve refs of inner obj */
        if (elem->inner) {
            obj = elem->inner;
            
            knd_log("    .. syncing inner obj in %s..\n",
                    elem->name);

            while (obj) {
                err = obj->sync(obj);
                if (err) return err;
                obj = obj->next;
            }
            
            goto next_elem;
        }
        
        if (!elem->attr) goto next_elem;

        if (elem->attr->type != KND_ELEM_REF)
            goto next_elem;
        
        dc = elem->attr->dc;
        if (!dc) goto next_elem;

        /*
        if (elem->ref_class)
            dc = elem->ref_class;
        
        if (DEBUG_OBJ_LEVEL_TMP)
            knd_log("\n    .. sync expanding ELEM REF: %s::%s..\n",
                    dc->name,
                    elem->states->val);

        obj = elem->states->refobj;
        if (!obj) {
            err = self->cache->repo->get_cache(self->cache->repo, dc, &cache);
            if (err) return knd_FAIL;

            err = self->cache->name_idx->lookup_name(self->cache->name_idx,
                                                     self->name, self->name_size,
                                                     self->name, self->name_size, idbuf);
            if (err) {
                if (DEBUG_OBJ_LEVEL_TMP)
                    knd_log("  -- failed to sync expand ELEM REF: %s::%s :(\n",
                        dc->name,
                        elem->states->val);
                goto next_elem;
            }
            elem->states->refobj = obj;
        }
        */
        
        next_elem:
        elem = elem->next;
    }

    knd_log("  .. to refset..");

    if (!self->tag) {
        if (DEBUG_OBJ_LEVEL_3)
            knd_log("    -- obj %s:%s is not meant for browsing\n",
                    self->id, self->name);
        return knd_OK;
    }

  
    for (size_t i = 0; i < self->tag->num_attrs; i++) {
        attr = self->tag->attrs[i];
        
        err = kndObject_get_idx(self,
                                (const char*)attr->name,
                                attr->name_size, &refset);
        if (err) {
            knd_log("  -- no refset %s :(\n", attr->name);
            return err;
        }
        
        err = kndObjRef_new(&ref);
        if (err) return err;
        
        ref->obj = self;
        if (self->root)
            ref->obj = self->root;
        
        memcpy(ref->obj_id, ref->obj->id, KND_ID_SIZE);
        ref->obj_id_size = KND_ID_SIZE;
    
        memcpy(ref->name, ref->obj->name, ref->obj->name_size);
        ref->name_size = ref->obj->name_size;

        err = kndSortTag_new(&tag);
        if (err) return err;

        ref->sorttag = tag;

        a = malloc(sizeof(struct kndSortAttr));
        if (!a) return knd_NOMEM;
        memset(a, 0, sizeof(struct kndSortAttr));
        a->type = attr->type;
    
        memcpy(a->name, attr->name, attr->name_size);
        a->name_size = attr->name_size;
            
        memcpy(a->val, attr->val, attr->val_size);
        a->val_size = attr->val_size;

        tag->attrs[tag->num_attrs] = a;
        tag->num_attrs++;

        /* subordinate objs not included to AZ */
        if (self->is_subord) {
            if (!strcmp(attr->name, "AZ")) {
                if (DEBUG_OBJ_LEVEL_3)
                    knd_log("  -- %s:%s excluded from AZ\n", self->id, self->name);
                continue;
            }
        }
        
        if (DEBUG_OBJ_LEVEL_TMP)
            knd_log("  .. add ref: %s %p", ref->obj_id, refset);

        err = refset->add_ref(refset, ref);
        if (DEBUG_OBJ_LEVEL_TMP)
            knd_log("  .. result: %d", err);
        if (err) {
            if (DEBUG_OBJ_LEVEL_TMP) {
                ref->str(ref, 1);
                knd_log("  -- ref \"%s\" not added to refset :(\n", ref->obj_id);
            }
            return err;
        }
        
    }
    
    
    return knd_OK;
}


extern void
kndObject_init(struct kndObject *self)
{
    self->del = del;
    self->str = str;
    self->import = import;
    self->sync = kndObject_sync;
    self->update = kndObject_update;
    self->expand = kndObject_expand;
    self->export = kndObject_export;
    self->flatten = kndObject_flatten;
    self->match = kndObject_match;
    self->contribute = kndObject_contribute;
    self->parse = kndObject_parse;
    self->parse_inline = kndObject_parse_inline;
    self->index_inline = kndObject_index_inline;
}

extern int
kndObject_new(struct kndObject **obj)
{
    struct kndObject *self;

    self = malloc(sizeof(struct kndObject));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndObject));
    
    kndObject_init(self);
    *obj = self;

    return knd_OK;
}
