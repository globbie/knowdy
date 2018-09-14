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
 *   knd_state.h
 *   Knowdy State Manager
 */

#pragma once

#include <time.h>
#include "knd_config.h"

struct glbOutput;
struct kndState;
struct kndClass;
struct kndUpdate;

typedef enum knd_state_type { KND_INIT_STATE, 
                              KND_FAILED_STATE,
                              KND_CONFLICT_STATE,
                              KND_VALID_STATE
} knd_state_type;

struct kndRelUpdate
{
    struct kndRel *rel;
    struct kndUpdate *update;
    struct kndSet *idx;
    struct kndRelInstance **insts;
    size_t num_insts;
};

struct kndRelInstanceUpdate
{
    struct kndRelInstance *inst;
    struct kndRelInstanceUpdate *next;
};

struct kndProcUpdate
{
    struct kndProc *proc;
    struct kndUpdate *update;
    struct kndProcInstance **insts;
    size_t num_insts;
};

struct kndUpdate
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    size_t owner_id;
    knd_state_type phase;
    time_t timestamp;
    size_t orig_state;

    const char *spec;
    size_t spec_size;

    struct kndRepo *repo;

    struct kndClassUpdate *classes;
    size_t num_classes;
    size_t total_class_insts;

    struct kndRelUpdate **rels;
    size_t num_rels;

    struct kndProcUpdate **procs;
    size_t num_procs;

    struct kndUpdate *next;
};

struct kndState
{
    knd_state_phase phase;
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;
    struct kndUpdate *update;
    void *obj;
    void *val;
    size_t val_size;
    struct kndState *next;
};

struct kndStateControl
{
    struct kndUser *admin;
    struct kndTask *task;
    struct kndRepo *repo;

    /** transaction log:
     * index of confirmed updates */
    struct kndUpdate *updates;
    size_t max_updates;
    size_t num_updates;

    size_t total_objs;

    struct glbOutput *log;
    struct glbOutput *out;
    struct glbOutput *spec_out;
    struct glbOutput *update;

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

    //int (*select)(struct kndStateControl *self);
};

/* constructors */
extern int kndStateControl_new(struct kndStateControl **self);

extern int knd_update_new(struct kndMemPool *mempool,
                          struct kndUpdate **result);

extern int knd_state_new(struct kndMemPool *mempool,
                         struct kndState **result);
