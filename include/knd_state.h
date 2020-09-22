/**
 *   Copyright (c) 2011-present by Dmitri Dmitriev
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

struct kndState;
struct kndClass;
struct kndStateRef;
struct kndMemPool;
struct kndOutput;

typedef enum knd_state_phase { KND_SELECTED,
                               KND_CREATED,
                               KND_UPDATED,
                               KND_REMOVED,
                               KND_FREED,
                               KND_FROZEN,
                               KND_RESTORED } knd_state_phase;

typedef enum knd_state_type { KND_STATE_CLASS,
                              KND_STATE_CLASS_VAR,
                              KND_STATE_ATTR,
                              KND_STATE_ATTR_VAR,
                              KND_STATE_CLASS_DESCENDANT,
                              KND_STATE_CLASS_INST,
                              KND_STATE_CLASS_INST_INNER,
                              KND_STATE_CLASS_INST_ELEM,
                              KND_STATE_PROC,
                              KND_STATE_PROC_INST
} knd_state_type;


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
    struct kndCommit *commit;

    void *data;
    /* typed val */
    struct kndStateVal *val;

    struct kndStateRef *children;
    size_t num_children;
    struct kndState *next;
};

struct kndStateRef
{
    knd_state_type type;
    void *obj;
    struct kndState *state;
    struct kndStateRef *next;
};

int knd_state_new(struct kndMemPool *mempool,
                  struct kndState **result);
int knd_state_mem(struct kndMemPool *mempool,
                  struct kndState **result);

int knd_state_ref_new(struct kndMemPool *mempool,
                      struct kndStateRef **result);
int knd_state_val_new(struct kndMemPool *mempool,
                      struct kndStateVal **result);
