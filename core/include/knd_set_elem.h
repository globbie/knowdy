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
 *   <http://www.knowdy.net>
 *
 *   Initial author and maintainer:
 *         Dmitri Dmitriev aka M0nsteR <dmitri@globbie.net>
 *
 *   ----------
 *   knd_set_elem.h
 *   Knowdy Set Reference
 */

#pragma once

#include "knd_config.h"

struct kndOutput;

struct kndSetElem
{    
    char id[KND_ID_SIZE + 1];
    size_t id_size;

    struct kndSortTag *sorttag;

    bool is_trivia;


    struct kndOutput *out;
    
    struct kndSetElem *next;
    
    /******** public methods ********/
    int (*init)(struct kndSetElem *self);
    void (*del)(struct kndSetElem *self);
    int (*str)(struct kndSetElem *self,
               size_t depth);


    int (*expand)(struct kndSetElem *self);

    int (*read_coderefs)(struct kndSetElem    *self,
                         struct kndElemRef *elemref,
                         const char *rec);

    int (*add_elemref)(struct kndSetElem    *self,
                       struct kndElemRef *elemref);

    int (*clone)(struct kndSetElem *self,
                 size_t attr_id,
                 const char *tail,
                 size_t tail_size,
                 struct kndSetElem **result);
    
    int (*reset)(struct kndSetElem *self);

    int (*sync)(struct kndSetElem *self);

    int (*import)(struct kndSetElem *self,
                  struct kndConcept *baseclass,
                  char *rec);
    int (*export)(struct kndSetElem *self,
                   knd_format format);


};

/* constructor */
extern int kndSetElem_new(struct kndSetElem **self);
