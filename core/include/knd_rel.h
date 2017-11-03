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
 *   knd_rel.h
 *   Knowdy Rel Element
 */

#pragma once

#include "knd_config.h"

struct kndOutput;
struct kndTask;
struct kndRelArg;

struct kndRelState
{
    knd_state_phase phase;
    char state[KND_STATE_SIZE];

    char val[KND_NAME_SIZE + 1];
    size_t val_size;

    struct kndRelState *next;
};

struct kndRelInstance
{
    struct kndRel *rel;
};


struct kndRelDir
{
    char name[KND_NAME_SIZE];
    size_t name_size;
    struct kndRel *rel;

    size_t global_offset;
    size_t curr_offset;
    size_t block_size;

    size_t body_size;
    size_t obj_block_size;
    size_t dir_size;

    struct kndRelDir **children;
    size_t num_children;

    char next_obj_id[KND_ID_SIZE];
    size_t next_obj_numid;

    /*struct kndRelInstDir **obj_dirs;
    size_t num_obj_dirs;
    struct kndRelInstanceEntry **objs;
    size_t num_objs;
    */
    //struct ooDict *rel_inst_idx;

    bool is_terminal;
    struct kndRelDir *next;
};

struct kndRel
{
    char name[KND_NAME_SIZE];
    size_t name_size;
    char id[KND_ID_SIZE];

    struct kndTranslation *tr;

    struct kndOutput *out;
    struct kndOutput *log;

    struct kndTask *task;
    const char *locale;
    size_t locale_size;
    knd_format format;

    struct kndRelState *states;
    size_t num_states;
    bool batch_mode;

    struct kndRelArg *args;
    struct kndRelArg *tail_arg;
    size_t num_atrs;

    /* allocator */
    struct kndMemPool *mempool;

    /* incoming */
    struct kndRel *inbox;
    size_t inbox_size;

    struct kndRelDir *dir;
    struct ooDict *rel_idx;

    size_t depth;
    struct kndRel *next;

    /******** public methods ********/
    void (*str)(struct kndRel *self);

    void (*del)(struct kndRel *self);

    int (*parse)(struct kndRel *self,
                 const char    *rec,
                 size_t        *total_size);
    int (*import)(struct kndRel *self,
                  const char    *rec,
                  size_t        *total_size);

    int (*index)(struct kndRel *self);

    int (*resolve)(struct kndRel *self);
    
    int (*export)(struct kndRel *self);
    int (*export_reverse_rel)(struct kndRel *self);
};

extern void kndRel_init(struct kndRel *self);
extern int kndRel_new(struct kndRel **self);
