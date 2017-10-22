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
 *   knd_proc.h
 *   Knowdy Proc Element
 */

#pragma once

#include "knd_config.h"

struct kndOutput;

struct kndProcState
{
    knd_state_phase phase;
    char state[KND_STATE_SIZE];

    char val[KND_NAME_SIZE + 1];
    size_t val_size;

    struct kndObject *obj;
    struct kndProcState *next;
};


struct kndProcInstance
{
    struct kndProc *proc;
};

struct kndProc
{
    char name[KND_NAME_SIZE];
    size_t name_size;
    char id[KND_ID_SIZE];

    struct kndOutput *out;
    struct kndOutput *log;

    const char *locale;
    size_t locale_size;
    knd_format format;
    
    struct kndProcState *states;
    size_t num_states;

    struct ooDict *proc_idx;

    size_t depth;
    struct kndProc *next;

    /******** public methods ********/
    void (*str)(struct kndProc *self);

    void (*del)(struct kndProc *self);
    
    int (*parse)(struct kndProc *self,
                 const char     *rec,
                 size_t          *total_size);

    int (*index)(struct kndProc *self);

    int (*resolve)(struct kndProc *self);
    
    int (*export)(struct kndProc *self);
    int (*export_reverse_proc)(struct kndProc *self);
};

/* constructors */
extern int kndProc_init(struct kndProc *self);
extern int kndProc_new(struct kndProc **self);
