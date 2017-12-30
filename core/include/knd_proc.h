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
 *   Knowdy Proc
 */

#pragma once

#include "knd_config.h"

struct kndOutput;
struct kndProcCallArg;
struct kndUpdate;

struct kndProcState
{
    knd_state_phase phase;
    char state[KND_STATE_SIZE];

    char val[KND_NAME_SIZE + 1];
    size_t val_size;

    struct kndObject *obj;
    struct kndProcState *next;
};

struct kndProcUpdateRef
{
    knd_state_phase phase;
    struct kndUpdate *update;
    struct kndProcUpdateRef *next;
};

struct kndProcInstance
{
    struct kndProc *proc;
};

struct kndProcCall
{
    char name[KND_NAME_SIZE];
    size_t name_size;

    struct kndProcCallArg *args;
    size_t num_args;

    struct kndProc *proc;
};

struct kndProcDir
{
    char name[KND_NAME_SIZE];
    size_t name_size;
    struct kndProc *proc;

    size_t global_offset;
    size_t curr_offset;
    size_t block_size;

    size_t body_size;
    size_t obj_block_size;
    size_t dir_size;

    struct kndProcDir **children;
    size_t num_children;

    /*struct kndProcInstDir **inst_dirs;
    size_t num_inst_dirs;
    struct kndProcInstanceEntry **insts;
    size_t num_insts;
    */
    struct ooDict *inst_idx;

    bool is_terminal;
    struct kndProcDir *next;
};

struct kndProc
{
    char name[KND_NAME_SIZE];
    size_t name_size;
    char id[KND_ID_SIZE];

    struct kndOutput *out;
    struct kndOutput *log;

    knd_state_phase phase;

    struct kndTranslation *tr;

    const char *locale;
    size_t locale_size;
    knd_format format;
    
    struct kndProcState *states;
    size_t num_states;

    struct kndProcArg *args;
    size_t num_args;

    struct kndProcCall *proc_call;
    size_t num_proc_calls;

    struct kndTask *task;

    /* allocator */
    struct kndMemPool *mempool;

    /* incoming */
    struct kndProc *inbox;
    size_t inbox_size;

    struct kndProcDir *dir;
    struct ooDict *proc_idx;
    struct ooDict *class_idx;

    bool batch_mode;
    bool is_resolved;
    size_t depth;
    struct kndProc *next;

    /******** public methods ********/
    void (*str)(struct kndProc *self);

    void (*del)(struct kndProc *self);
    
    int (*parse)(struct kndProc *self,
                 const char     *rec,
                 size_t          *total_size);
    int (*import)(struct kndProc *self,
                  const char    *rec,
                  size_t        *total_size);
    int (*coordinate)(struct kndProc *self);
    int (*resolve)(struct kndProc *self);
    int (*export)(struct kndProc *self);
    int (*update)(struct kndProc *self, struct kndUpdate *update);
};

/* constructors */
extern void kndProc_init(struct kndProc *self);
extern int kndProc_new(struct kndProc **self);
