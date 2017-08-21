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
 *   knd_ref.h
 *   Knowdy Ref Element
 */

#ifndef KND_REF_H
#define KND_REF_H

#include "knd_config.h"

struct kndElem;
struct kndElemRef;
struct kndOutput;

typedef enum knd_ref_t { knd_LOCAL, knd_FILESYSTEM, knd_URI } knd_ref_t;

struct kndRefState
{
    knd_state_phase phase;
    char state[KND_STATE_SIZE];

    /*struct kndConcRef *cg;*/

    struct kndRefState *next;
};

struct kndRef
{
    knd_ref_t type;
    struct kndElem *elem;
    
    struct kndOutput *out;
    struct kndOutput *log;

    knd_ref_t reftype;
    char user_name[KND_NAME_SIZE];
    size_t user_name_size;
    struct kndUser *user;

    char repo_name[KND_NAME_SIZE];
    size_t repo_name_size;
    struct kndRepo *repo;

    char classname[KND_NAME_SIZE];
    size_t classname_size;
    struct kndDataClass *dc;

    char val[KND_NAME_SIZE + 1];
    size_t val_size;


    struct kndRefState *states;
    size_t num_states;
    
    /******** public methods ********/
    void (*str)(struct kndRef *self,
               size_t depth);

    void (*del)(struct kndRef *self);
    
    int (*parse)(struct kndRef *self,
                 const char     *rec,
                 size_t          *total_size);

    int (*index)(struct kndRef *self);
    
    int (*export)(struct kndRef *self,
                  knd_format format);
};

/* constructors */
extern int kndRef_init(struct kndRef *self);
extern int kndRef_new(struct kndRef **self);

#endif /* KND_REF_H */
