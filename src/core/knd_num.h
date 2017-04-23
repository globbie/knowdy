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
 *   knd_num.h
 *   Knowdy Num Element
 */

#ifndef KND_NUM_H
#define KND_NUM_H

#include "../knd_config.h"

struct kndElem;
struct kndElemRef;
struct kndOutput;

typedef enum knd_num_base_type { KND_NUM_DIGIT,
                                 KND_NUM_TEN,
                                 KND_NUM_HUNDRED,
                                 KND_NUM_THOUSAND,
                                 KND_NUM_TEN_THOUSAND,
                                 KND_NUM_HUNDRED_THOUSAND,
                                 KND_NUM_MILLION,
                                 KND_NUM_BILLION,
                                 KND_NUM_TRILLION,
                                 KND_NUM_QUADRILLION } knd_num_base_type;

typedef enum knd_num_oper_type { KND_NUM_OPER_ADD,
                                 KND_NUM_OPER_SUBTR,
                                 KND_NUM_OPER_MULT,
                                 KND_NUM_OPER_DIV,
                                 KND_NUM_OPER_POW,
                                 KND_NUM_OPER_SQRT } knd_num_oper_type;

struct kndQuant
{
    knd_num_base_type base;
    int tot;
    
    knd_num_oper_type oper;
    struct kndQuant *arg;
};


struct kndNumState
{
    knd_update_status update_oper;
    size_t state;
    
    struct kndQuant *val;

    struct kndNumState *next;
};


struct kndNum
{
    struct kndElem *elem;
    struct kndOutput *out;

    struct kndNumState *states;
    size_t num_states;
    
    /******** public methods ********/
    int (*str)(struct kndNum *self,
               size_t depth);

    int (*del)(struct kndNum *self);
    
    int (*parse)(struct kndNum *self,
                 const char     *rec,
                 size_t          *total_size);

    int (*update)(struct kndNum *self,
                 const char     *rec,
                 size_t          *total_size);

    int (*index)(struct kndNum *self);
    
    int (*export)(struct kndNum *self,
                  knd_format format);
};

/* constructors */
extern int kndNum_init(struct kndNum *self);
extern int kndNum_new(struct kndNum **self);

#endif /* KND_NUM_H */
