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
 *   knd_state.h
 *   Knowdy State Manager
 */

#pragma once

#include <time.h>
#include "knd_config.h"

struct kndOutput;
struct kndState;
struct kndConcept;

struct kndClassUpdate
{
    struct kndConcept *conc;
    struct kndObject **objs;
    size_t num_objs;
};

struct kndUpdate
{
    size_t id;
    time_t timestamp;
    size_t orig_state;
    size_t userid;

    const char *spec;
    size_t spec_size;

    struct kndClassUpdate **classes;
    size_t num_classes;

    struct kndRelUpdate **rels;
    size_t num_rels;
};

struct kndStateControl
{
    struct kndUser *admin;

    char state[KND_STATE_SIZE];
    char next_state[KND_STATE_SIZE];
    size_t global_state_count;

    struct kndTask *task;

    /** transaction ledger:
     * index of confirmed updates */
    struct kndUpdate **updates;
    size_t max_updates;
    size_t num_updates;
    struct kndUpdate **selected;
    size_t num_selected;

    struct kndOutput *log;
    struct kndOutput *out;
    struct kndOutput *spec_out;
    struct kndOutput *update;

    /******** public methods ********/
    void (*str)(struct kndStateControl *self);
    void (*del)(struct kndStateControl *self);
    void (*reset)(struct kndStateControl *self);
    
    int (*get)(struct kndStateControl *self,
               const char     **state_id,
               size_t   *state_id_size,
               struct tm *date);

    int (*confirm)(struct kndStateControl *self,
                   struct kndUpdate *update);

    int (*select)(struct kndStateControl *self);
};

/* constructors */
extern void kndUpdate_init(struct kndUpdate *self);
extern void kndClassUpdate_init(struct kndClassUpdate *self);
extern int kndStateControl_new(struct kndStateControl **self);

