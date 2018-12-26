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
#include "knd_state.h"

#include "knd_proc_arg.h"
#include "knd_proc_call.h"

struct glbOutput;
struct kndProcCallArg;
struct kndUpdate;

struct kndProcEntry
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    const char *name;
    size_t name_size;
    struct kndProc *proc;
    struct kndRepo *repo;

    knd_state_phase phase;

    size_t global_offset;
//    size_t curr_offset;
    size_t block_size;

//    size_t body_size;
//    size_t obj_block_size;
//    size_t dir_size;

//    struct kndProcEntry **children;
//    size_t num_children;

    struct ooDict *inst_idx;
    //struct kndMemPool *mempool;
    //int fd;

//    size_t num_procs;
//    bool is_terminal;
    //struct kndProcEntry *next;
};

struct kndProcArgVar
{
    const char *name;
    size_t name_size;

    struct kndProcArg *arg;

    const char *classname;
    size_t classname_size;

    struct kndProcArgVar *next;
};

struct kndProcArgEntry
{
    const char *name;
    size_t name_size;

    struct kndProcArg *arg;

    struct kndProcArgVar *next;
};

struct kndProcVar
{
    const char *name;
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
    const char *name;
    size_t name_size;

    size_t id;
    size_t next_id;

    struct kndProcEntry *entry;

    struct kndTranslation *tr;

    struct kndState *states;
    size_t num_states;

    /* immediate args */
    struct kndProcArg *args;
    size_t num_args;

    /* all inherited args */
    struct ooDict *arg_idx;

    struct kndProcCall *proc_call;

    struct kndProcVar *bases;
    size_t num_bases;

    struct kndProcEntry *inherited;
    size_t num_inherited;

    struct kndProcEntry *children;
    size_t num_children;

    const char *result_classname;
    size_t result_classname_size;
//    struct kndClass *result;

//    size_t estim_cost;
    size_t estim_cost_total;
//    size_t estim_time;
//    size_t estim_time_total;

    //struct kndTask *task;
    struct kndVisualFormat *visual;

    //bool batch_mode;
    bool is_resolved;

    struct kndProc *next;

    /******** public methods ********/
    void (*str)(struct kndProc *self);
};

/* constructors */
int knd_proc_new(struct kndMemPool *mempool,
                 struct kndProc **result);

int knd_proc_entry_new(struct kndMemPool *mempool,
                       struct kndProcEntry **result);
int knd_proc_var_new(struct kndMemPool *mempool,
                     struct kndProcVar **result);
int knd_proc_arg_var_new(struct kndMemPool *mempool,
                         struct kndProcArgVar **result);

void knd_proc_init(struct kndProc *self);

gsl_err_t knd_proc_read(struct kndProc *self,
                        const char *rec,
                        size_t *total_size);

int knd_proc_resolve(struct kndProc *self);

int knd_get_proc(struct kndRepo *repo,
                 const char *name, size_t name_size,
                 struct kndProc **result);

int knd_resolve_proc_ref(struct kndClass *self,
                         const char *name, size_t name_size,
                         struct kndProc *unused_var(base),
                         struct kndProc **result,
                         struct kndTask *unused_var(task));

int knd_proc_export(struct kndProc *self,
                    knd_format format,
                    struct kndTask *task,
                    struct glbOutput *out);

int knd_proc_coordinate(struct kndProc *self,
                        struct kndTask *task);

gsl_err_t knd_proc_select(struct kndRepo *repo,
                          const char *rec,
                          size_t *total_size,
                          struct kndTask *task);

gsl_err_t knd_proc_import(struct kndRepo *repo,
                          const char *rec, size_t *total_size,
                          struct kndTask *task);

//
// TODO(k15tfu): ?? Move to knd_proc_impl.h
//
#include <knd_proc_arg.h>
#include <knd_text.h>

static inline void kndProcVar_declare_arg(struct kndProcVar *base, struct kndProcArgVar *base_arg)
{
    if (base->tail) {
        base->tail->next = base_arg;
        base->tail = base_arg;
    }
    else
        base->args = base->tail = base_arg;
    base->num_args++;
}

static inline void kndProc_declare_arg(struct kndProc *self, struct kndProcArg *arg)
{
    arg->next = self->args;
    self->args = arg;
    self->num_args++;
}

static inline void kndProc_declare_tr(struct kndProc *self, struct kndTranslation *tr)
{
    tr->next = self->tr;
    self->tr = tr;
}

static inline void kndProc_declare_base(struct kndProc *self, struct kndProcVar *base)
{
    base->next = self->bases;
    self->bases = base;
    self->num_bases++;
}
