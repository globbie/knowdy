/**
 *   Copyright (c) 2011-2018 by Dmitri Dmitriev
 *   All rights reserved.
 *
 *   This file is part of the Knowdy Graph DB, 
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
 *   Knowdy Object Element (Attr Instance)
 */

#pragma once

#include "knd_config.h"
#include "knd_ref.h"
#include "knd_text.h"

struct kndClassInst;
struct glbOutput;
struct kndObjRef;
struct kndRelType;

struct kndUser;
struct kndRepo;
struct kndClass;

struct kndElem
{
    struct kndAttr *attr;

    struct kndClassInst *obj;
    struct kndClassInst *root;
    struct kndSortTag *tag;

    struct kndClassInst *aggr;
    struct kndClassInst *aggr_tail;

    bool is_list;

    const char *val;
    size_t val_size;
    
    struct glbOutput *out;
    struct glbOutput *log;

    struct kndText *text;
    struct kndNum *num;
    struct kndRef *ref;

    struct kndElem *next;

    struct kndState *states;
    size_t init_state;
    size_t num_states;

    knd_format format;
    size_t depth;
    /******** public methods ********/
    void (*str)(struct kndElem *self);
    void (*del)(struct kndElem *self);
    int (*read)(struct kndElem *self);
    int (*resolve)(struct kndElem *self);
    int (*index)(struct kndElem *self);
    gsl_err_t (*parse)(struct kndElem *self,
                 const char *rec,
                 size_t *total_size);
//    int (*match)(struct kndElem *self,
//                 const char *rec,
//                 size_t rec_size);
    int (*export)(struct kndElem *self);
};

/* constructors */
extern void kndElem_init(struct kndElem *self);
extern int kndElem_new(struct kndElem **self);
