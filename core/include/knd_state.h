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
struct kndStateRef;
struct kndMemPool;

typedef enum knd_state_phase { KND_SELECTED,
                               KND_SUBMITTED,
                               KND_CREATED,
                               KND_UPDATED,
                               KND_REMOVED,
                               KND_FREED,
                               KND_FROZEN,
                               KND_RESTORED } knd_state_phase;

typedef enum knd_update_confirm { KND_INIT_STATE, 
                                 KND_FAILED_STATE,
                                 KND_CONFLICT_STATE,
                                 KND_VALID_STATE
} knd_update_confirm;

typedef enum knd_state_type { KND_STATE_CLASS,
                               KND_STATE_CLASS_VAR,
                               KND_STATE_ATTR,
                               KND_STATE_ATTR_VAR,
                               KND_STATE_CLASS_DESCENDANT,
                               KND_STATE_CLASS_INST,
                               KND_STATE_CLASS_INST_INNER,
                               KND_STATE_CLASS_INST_ELEM
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
    size_t numid;

    size_t owner_id;
    knd_update_confirm confirm;
    time_t timestamp;
    size_t orig_state;

    struct kndRepo *repo;
    struct kndStateRef *states;

    struct kndUpdate *next;
};

struct kndStateVal
{
    void *obj;
    const char *val;
    size_t val_size;
    long numid;
    void *ref;
};

struct kndState
{
    size_t numid;
    knd_state_phase phase;
    struct kndUpdate *update;
    struct kndStateVal *val;
    struct kndStateRef *children;
    struct kndState *next;
};

struct kndStateRef
{
    knd_state_type type;
    void *obj;
    struct kndState *state;
    struct kndStateRef *next;
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
};

/* constructors */
extern int kndStateControl_new(struct kndStateControl **self);

extern int knd_update_new(struct kndMemPool *mempool,
                          struct kndUpdate **result);

extern int knd_state_new(struct kndMemPool *mempool,
                         struct kndState **result);
extern int knd_state_ref_new(struct kndMemPool *mempool,
                             struct kndStateRef **result);
extern int knd_state_val_new(struct kndMemPool *mempool,
                             struct kndStateVal **result);
