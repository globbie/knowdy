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
struct kndRelInstance;

struct kndRelState
{
    knd_state_phase phase;
    char state[KND_STATE_SIZE];

    char val[KND_NAME_SIZE + 1];
    size_t val_size;

    struct kndRelState *next;
};

struct kndRelInstEntry
{
    struct kndRelInstance *inst;
};

struct kndRelInstance
{
    char name[KND_NAME_SIZE];
    size_t name_size;

    char subj_name[KND_SHORT_NAME_SIZE];
    size_t subj_name_size;

    char obj_name[KND_SHORT_NAME_SIZE];
    size_t obj_name_size;

    knd_state_phase phase;
    struct kndRel *rel;
    struct kndRelInstance *next;
};

struct kndRelDir
{
    char id[KND_ID_SIZE];
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

    char next_inst_id[KND_ID_SIZE];
    size_t next_inst_numid;

    /*struct kndRelInstDir **obj_dirs;
    size_t num_obj_dirs;
    struct kndRelInstanceEntry **objs;
    size_t num_objs;
    */
    struct ooDict *inst_idx;

    bool is_terminal;
    struct kndRelDir *next;
};

struct kndRel
{
    char name[KND_NAME_SIZE];
    size_t name_size;

    char id[KND_ID_SIZE];
    char next_id[KND_ID_SIZE];

    knd_state_phase phase;

    struct kndTranslation *tr;

    struct kndOutput *out;
    struct kndOutput *log;

    const char *dbpath;
    size_t dbpath_size;

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

    struct kndRelInstance *inst_inbox;
    size_t inst_inbox_size;

    struct kndRelDir *dir;
    struct ooDict *rel_idx;
    struct ooDict *class_idx;
    const char *frozen_output_file_name;
    size_t frozen_output_file_name_size;
    size_t frozen_size;
    
    bool is_resolved;
    size_t depth;

    struct kndRel *curr_rel;
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
    int (*select)(struct kndRel  *self,
                  const char *rec,
                  size_t *total_size);
    int (*coordinate)(struct kndRel *self);
    int (*resolve)(struct kndRel *self);
    int (*freeze)(struct kndRel *self,
                  size_t *total_frozen_size,
                  char *output,
                  size_t *total_size);
    int (*export)(struct kndRel *self);
    int (*export_reverse_rel)(struct kndRel *self);
};

extern void kndRel_init(struct kndRel *self);
extern void kndRelInstance_init(struct kndRelInstance *inst);
extern int kndRel_new(struct kndRel **self);
