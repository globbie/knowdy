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

    struct kndClassInst *parent;
    struct kndClassInst *root;

    struct kndClassInst *inner;
    struct kndClassInst *inner_tail;

    struct kndClassInst *ref_inst;

    bool is_list;

    const char *val;
    size_t val_size;

    struct kndClass *curr_class;

    struct glbOutput *out;
    struct glbOutput *log;

    struct kndText *text;
    struct kndNum *num;


    struct kndState *states;
    size_t init_state;
    size_t num_states;

    //knd_format format;
    //size_t depth;

    struct kndElem *next;

    /******** public methods ********/
    void (*str)(struct kndElem *self);
    //void (*del)(struct kndElem *self);
    int (*read)(struct kndElem *self);
    int (*resolve)(struct kndElem *self);
    int (*index)(struct kndElem *self);
    gsl_err_t (*parse)(struct kndElem *self,
                       const char *rec,
                       size_t *total_size);

};

/* constructors */
extern void kndElem_init(struct kndElem *self);
extern int kndElem_new(struct kndElem **self);

extern void knd_elem_str(struct kndElem *self, size_t depth);

extern gsl_err_t knd_elem_parse_select(struct kndElem *self,
                                       const char *rec,
                                       size_t *total_size);

extern int knd_elem_export(struct kndElem *self,
                           knd_format format,
                           struct glbOutput *out);

extern int knd_class_inst_elem_new(struct kndMemPool *mempool,
                                   struct kndElem **result);
