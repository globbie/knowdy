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
 *   knd_ref.h
 *   Knowdy Ref Element
 */

#pragma once

#include <glb-lib/output.h>

#include "knd_config.h"

struct kndElem;
struct kndElemRef;
struct glbOutput;

typedef enum knd_ref_t { knd_LOCAL, knd_FILESYSTEM, knd_URI } knd_ref_t;

struct kndRefState
{
    knd_state_phase phase;
    //char state[KND_STATE_SIZE];

    char val[KND_NAME_SIZE + 1];
    size_t val_size;

    struct kndClassInst *obj;

    struct kndRefState *next;
};

struct kndRef
{
    knd_ref_t type;
    struct kndElem *elem;

    char name[KND_NAME_SIZE];
    size_t name_size;
    char id[KND_ID_SIZE];

    struct glbOutput *out;
    struct glbOutput *log;

    knd_ref_t reftype;

    const char *locale;
    size_t locale_size;
    knd_format format;

    struct kndRefState *states;
    size_t num_states;

    size_t depth;
    struct kndRef *next;

    /******** public methods ********/
    void (*str)(struct kndRef *self);

    void (*del)(struct kndRef *self);
    
    gsl_err_t (*parse)(struct kndRef *self,
                 const char     *rec,
                 size_t          *total_size);

    int (*index)(struct kndRef *self);

    int (*resolve)(struct kndRef *self);
    
    int (*export)(struct kndRef *self);
    int (*export_reverse_rel)(struct kndRef *self);
};

/* constructors */
extern int kndRef_init(struct kndRef *self);
extern int kndRef_new(struct kndRef **self);
