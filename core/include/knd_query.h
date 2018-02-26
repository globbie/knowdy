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
 *   knd_query.h
 *   Knowdy Query
 */

#pragma once

#include "knd_utils.h"
#include "knd_config.h"
#include "knd_coderef.h"

struct kndSet;
struct kndRepoCache;
struct kndOutput;

typedef enum knd_query_type { KND_QUERY_OBJ,
                              KND_QUERY_ELEM,
                              KND_QUERY_CONC } knd_query_type;
/**
 *  Query:
 *  a nested set of clauses
 */

struct kndQuery 
{
    knd_query_type type;

    struct kndSet *set;
    char facet_name[KND_NAME_SIZE];
    size_t facet_name_size;
    
    char val[KND_TEMP_BUF_SIZE];
    size_t val_size;

    bool is_negated;
    knd_logic logic;

    struct kndQuery *children[KND_MAX_ATTRS];
    size_t num_children;
    
    //struct kndCodeRef *coderefs;
    struct kndSet *result_set;
    struct kndOutput *out;
    struct kndMemPool *mempool;

    struct kndQuery *next;
    
    /***********  public methods ***********/
    int (*init)(struct kndQuery  *self);
    void (*del)(struct kndQuery   *self);
    void (*reset)(struct kndQuery   *self);
    char* (*str)(struct kndQuery *self);
    int (*parse)(struct kndQuery   *self,
                 const char        *rec,
                 size_t             rec_size);

    int (*exec)(struct kndQuery   *self);

};

extern int kndQuery_init(struct kndQuery *self); 

/* constructor */
extern int kndQuery_new(struct kndQuery **self);
