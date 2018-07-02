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
 *   knd_proc.h
 *   Knowdy Proc
 */

#pragma once

#include "knd_config.h"

struct glbOutput;
struct kndProcCallArg;
struct kndUpdate;

typedef enum knd_proc_type {
    KND_PROC_USER,
    KND_PROC_SYSTEM,
    KND_PROC_ADD,
    KND_PROC_MULT,
    KND_PROC_MULT_PERCENT
} knd_proc_type;

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
    struct kndTranslation *tr;
    knd_proc_type type;

    struct kndProc *proc;
    struct kndProcCallArg *args;
    size_t num_args;
    struct kndMemPool *mempool;
};

struct kndProcEntry
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    char name[KND_NAME_SIZE];
    size_t name_size;
    struct kndProc *proc;
    struct kndRepo *repo;

    knd_state_phase phase;

    size_t global_offset;
    size_t curr_offset;
    size_t block_size;

    size_t body_size;
    size_t obj_block_size;
    size_t dir_size;

    struct kndProcEntry **children;
    size_t num_children;

    struct ooDict *inst_idx;
    struct kndMemPool *mempool;
    int fd;

    size_t num_procs;
    bool is_terminal;
    struct kndProcEntry *next;
};

struct kndProcArgVar
{
    char name[KND_NAME_SIZE];
    size_t name_size;

    struct kndProcArg *arg;

    char classname[KND_NAME_SIZE];
    size_t classname_size;

    struct kndProcArgVar *next;
};

struct kndProcArgEntry
{
    char name[KND_NAME_SIZE];
    size_t name_size;

    struct kndProcArg *arg;

    char classname[KND_NAME_SIZE];
    size_t classname_size;
    struct kndProcEntry *parent;

    struct kndProcArgVar *next;
};

struct kndProcVar
{
    char name[KND_NAME_SIZE];
    size_t name_size;

    //struct kndProcEntry *entry;
    struct kndProc *proc;

    struct kndProcArgVar *args;
    struct kndProcArgVar *tail;
    size_t num_args;

    struct kndProcVar *next;
};

struct kndProc
{
    char name[KND_NAME_SIZE];
    size_t name_size;

    size_t id;
    size_t next_id;

    struct kndProcEntry *entry;

    //struct kndRepo *repo;

    /*struct glbOutput *out;
    struct glbOutput *dir_out;
    struct glbOutput *log;
    */
    //knd_state_phase phase;

    struct kndTranslation *tr;
    struct kndTranslation *summary;

    const char *locale;
    size_t locale_size;
    knd_format format;
    
    struct kndProcState *states;
    size_t num_states;

    /* immediate args */
    struct kndProcArg *args;
    size_t num_args;
    /* all inherited args */
    struct ooDict *arg_idx;

    struct kndProcCall proc_call;
    size_t num_proc_calls;

    struct kndProcVar *bases;
    size_t num_bases;

    struct kndProcEntry *inherited[KND_MAX_INHERITED];
    size_t num_inherited;

    struct kndProcEntry *children[KND_MAX_PROC_CHILDREN];
    size_t num_children;

    char result_classname[KND_NAME_SIZE];
    size_t result_classname_size;
    struct kndClass *result;

    size_t estim_cost;
    size_t estim_cost_total;
    size_t estim_time;
    size_t estim_time_total;

    struct kndTask *task;
    struct kndVisualFormat *visual;

    /* allocator */
    //struct kndMemPool *mempool;
    //int fd;

    /* incoming */
    struct kndProc *inbox;
    size_t inbox_size;

    struct kndProcInstance *inst_inbox;
    size_t inst_inbox_size;

    size_t num_procs;

    struct ooDict *proc_idx;
    struct ooDict *rel_idx;

    struct kndSet *class_idx;
    struct ooDict *class_name_idx;
    struct ooDict *proc_name_idx;

    const char *frozen_output_file_name;
    size_t frozen_output_file_name_size;
    size_t frozen_size;

    bool batch_mode;
    bool is_resolved;

    size_t depth;
    size_t max_depth;

    struct kndProc *curr_proc;
    struct kndProc *next;

    /******** public methods ********/
    void (*str)(struct kndProc *self);

    void (*del)(struct kndProc *self);
    
    gsl_err_t (*read)(struct kndProc *self,
                const char    *rec,
                size_t        *total_size);
    gsl_err_t (*import)(struct kndProc *self,
                  const char    *rec,
                  size_t        *total_size);
    gsl_err_t (*select)(struct kndProc *self,
		  const char    *rec,
		  size_t        *total_size);
    int (*read_proc)(struct kndProc *self,
                     struct kndProcEntry *proc_entry,
                     int fd);
    int (*get_proc)(struct kndProc *self,
		    const char *name, size_t name_size,
		    struct kndProc **result);
    int (*coordinate)(struct kndProc *self);
    int (*resolve)(struct kndProc *self);
    int (*export)(struct kndProc *self);
    int (*freeze)(struct kndProc *self,
                  size_t *total_frozen_size,
                  char *output,
		  size_t *total_size);
    int (*freeze_procs)(struct kndProc *self,
                        size_t *total_frozen_size,
                        char *output,
                        size_t *total_size);
    int (*update)(struct kndProc *self, struct kndUpdate *update);
};

/* constructors */
extern void kndProc_init(struct kndProc *self);
extern int kndProc_new(struct kndProc **self, struct kndMemPool *mempool);
