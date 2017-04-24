/**
 *   Copyright (c) 2011-2017 by Dmitri Dmitriev
 *   All rights reserved.
 *
 *   This file is part of the Knowdy Search Engine, 
 *   and as such it is subject to the license stated
 *   in the LICENSE file which you have received 
 *   as part of this distribution.
 *
 *   Project homepage:
 *   <http://www.globbie.net>
 *
 *   Initial author and maintainer:
 *         Dmitri Dmitriev aka M0nsteR <dmitri@globbie.net>
 *
 *   ----------
 *   knd_objref.h
 *   Knowdy Object Reference
 */

#ifndef KND_OBJREF_H
#define KND_OBJREF_H

#include "knd_config.h"
#include "knd_conc.h"

struct kndDataClass;
struct kndOutput;
struct kndRepoCache;
struct kndCodeRef;
struct kndTrans;

typedef enum knd_ref_type { KND_REF_OBJ,
                            KND_REF_OBJ_SUBORD,
                            KND_REF_ELEM,
                            KND_REF_CODE,
                            KND_REF_TID,
                            KND_REF_ATOM } knd_ref_type;

struct kndElemRef
{
    char name[KND_TEMP_BUF_SIZE];
    size_t name_size;

    size_t num_clauses;

    struct kndDataElem *elem;
    
    struct kndCodeRef *coderefs;
    size_t num_coderefs;
    
    struct kndElemRef *parent;

    struct kndElemRef *children;
    struct kndElemRef *next;
};



struct kndObjRef
{
    knd_ref_type type;
    
    char classname[KND_NAME_SIZE];
    size_t classname_size;

    char name[KND_NAME_SIZE];
    size_t name_size;

    char obj_id[KND_ID_SIZE + 1];
    size_t obj_id_size;

    struct kndDataClass *baseclass;
    struct kndObject *obj;
    struct kndTrans *trn;
    struct kndSortTag *sorttag;

    bool is_trivia;

    struct kndRepoCache *cache;
    
    /* for textual refs */
    struct kndElemRef *elemrefs;
 
    struct kndElem *title;
    struct kndElem *summary;

    /* visual emphasis of a chunk */
    size_t hilite_pos;
    size_t hilite_len;

    struct kndOutput *out;
    
    struct kndObjRef *next;
    
    /******** public methods ********/
    int (*init)(struct kndObjRef *self);
    void (*del)(struct kndObjRef *self);
    int (*str)(struct kndObjRef *self,
               size_t depth);


    int (*expand)(struct kndObjRef *self);

    int (*read_coderefs)(struct kndObjRef    *self,
                         struct kndElemRef *elemref,
                         const char *rec);

    int (*add_elemref)(struct kndObjRef    *self,
                       struct kndElemRef *elemref);

    int (*clone)(struct kndObjRef *self,
                 size_t attr_id,
                 const char *tail,
                 size_t tail_size,
                 struct kndObjRef **result);
    
    int (*reset)(struct kndObjRef *self);

    int (*sync)(struct kndObjRef *self);

    int (*import)(struct kndObjRef *self,
                  struct kndDataClass *baseclass,
                  char *rec);
    int (*export)(struct kndObjRef *self,
                   knd_format format);


};


/* constructor */
extern int kndObjRef_new(struct kndObjRef **self);

#endif /* KND_OBJREF_H */
