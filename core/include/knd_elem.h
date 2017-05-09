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
 *   knd_elem.h
 *   Knowdy Object Element
 */

#ifndef KND_ELEM_H
#define KND_ELEM_H

#include "knd_config.h"

struct kndObject;
struct kndOutput;
struct kndObjRef;
struct kndText;
struct kndRelType;
struct kndSortAttr;

struct kndDataWriter;
struct kndDataReader;



struct kndElemState
{
    knd_update_status update_oper;
    size_t state;

    char ref[KND_ID_SIZE + 1];
    size_t ref_size;

    char val[KND_NAME_SIZE + 1];
    size_t val_size;

    struct kndObject *refobj;

    char mimetype[KND_NAME_SIZE + 1];
    size_t filesize;
    
    struct kndElemState *next;
};


struct kndElem
{
    char name[KND_NAME_SIZE + 1];
    size_t name_size;

    struct kndAttr *attr;

    struct kndObject *obj;
    struct kndObject *root;
    struct kndSortTag *tag;
     
    struct kndObject *inner;
    struct kndObject *inner_tail;

    char ref_classname[KND_NAME_SIZE];
    size_t ref_classname_size;
    struct kndDataClass *ref_class;
    
    bool is_list;
    bool is_list_item;
    bool is_default;
    
    struct kndOutput *out;

    struct kndText *text;
    struct kndNum *num;

    struct kndElem *elems;
    struct kndElem *tail;
    size_t num_elems;

    
    struct kndElem *next;

    struct kndElemState *states;
    size_t num_states;

    /******** public methods ********/
    int (*str)(struct kndElem *self,
               size_t depth);

    void (*del)(struct kndElem *self);

    int (*read)(struct kndElem *self);

    int (*index)(struct kndElem *self);

    int (*parse)(struct kndElem *self,
                 const char *rec,
                 size_t *total_size);

    int (*update)(struct kndElem *self,
                 const char *rec,
                 size_t *total_size);

    int (*parse_list)(struct kndElem *self,
                      const char *rec,
                      size_t *total_size);

    int (*match)(struct kndElem *self,
                 const char *rec,
                 size_t rec_size);

    int (*export)(struct kndElem *self, knd_format format, bool is_concise);
};

/* constructors */
extern void kndElem_init(struct kndElem *self);
extern int kndElem_new(struct kndElem **self);

#endif /* KND_ELEM_H */
