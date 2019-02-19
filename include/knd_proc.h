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
 *   knd_proc.h
 *   Knowdy Proc
 */

#pragma once

#include "knd_config.h"
#include "knd_state.h"
#include "knd_output.h"
#include "knd_proc_arg.h"

struct kndProcEstimate
{
    size_t cost;
    size_t aggr_cost;

    size_t time;
    size_t aggr_time;

    size_t num_agents;
    size_t aggr_num_agents;
};

// struct kndProcCall;
// struct kndProcCallArg;
struct kndUpdate;

struct kndProcInstEntry
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    const char *name;
    size_t name_size;

    //char *block;
    //size_t block_size;
    //size_t offset;
    knd_state_phase phase;
    struct kndProcInst *inst;
};

struct kndProcInst
{
    const char *name;
    size_t name_size;

    struct kndProcInstEntry *entry;
    struct kndProc *base;

    struct kndProcArgInst *arg_insts;
    size_t num_arg_insts;
    struct kndStateRef *arg_inst_state_refs;

    struct kndState * _Atomic states;
    size_t init_state;
    size_t num_states;
    
    struct kndProcArgInst *tail;
    struct kndProcInst *next;
};

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
    size_t block_size;

    struct kndDict *inst_idx;
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

    struct kndState * _Atomic states;
    size_t num_states;

    /* immediate args */
    struct kndProcArg *args;
    size_t num_args;

    struct kndSet *arg_idx;

    struct kndProcCall *proc_call;

    struct kndProcVar *bases;
    size_t num_bases;

    struct kndProcEntry *inherited;
    size_t num_inherited;

    struct kndProcEntry *children;
    size_t num_children;

    const char *result_classname;
    size_t result_classname_size;

    struct kndProcEstimate estimate;

    bool is_resolved;
    bool resolving_in_progress;

    struct kndProc *next;
};

int knd_proc_new(struct kndMemPool *mempool,
                 struct kndProc **result);

int knd_proc_entry_new(struct kndMemPool *mempool,
                       struct kndProcEntry **result);
int knd_proc_var_new(struct kndMemPool *mempool,
                     struct kndProcVar **result);
int knd_proc_arg_var_new(struct kndMemPool *mempool,
                         struct kndProcArgVar **result);

int knd_proc_inst_new(struct kndMemPool *mempool,
                      struct kndProcInst **result);
int knd_proc_inst_entry_new(struct kndMemPool *mempool,
                            struct kndProcInstEntry **result);

void knd_proc_str(struct kndProc *self, size_t depth);

gsl_err_t knd_proc_inst_import(struct kndProcInst *self,
                               struct kndRepo *repo,
                               const char *rec, size_t *total_size,
                               struct kndTask *task);
gsl_err_t knd_proc_inst_parse_import(struct kndProc *self,
                                     struct kndRepo *repo,
                                     const char *rec,
                                     size_t *total_size,
                                     struct kndTask *task);

int knd_inner_proc_import(struct kndProc *self,
                          const char *rec,
                          size_t *total_size,
                          struct kndRepo *repo,
                          struct kndTask *task);

int knd_get_proc(struct kndRepo *repo,
                 const char *name, size_t name_size,
                 struct kndProc **result,
                 struct kndTask *task);

int knd_proc_get_arg(struct kndProc *self,
                     const char *name, size_t name_size,
                     struct kndProcArgRef **result);

int knd_resolve_proc_ref(struct kndClass *self,
                         const char *name, size_t name_size,
                         struct kndProc *unused_var(base),
                         struct kndProc **result,
                         struct kndTask *unused_var(task));

int knd_proc_export(struct kndProc *self,
                    knd_format format,
                    struct kndTask *task,
                    struct kndOutput *out);

int knd_proc_coordinate(struct kndProc *self,
                        struct kndTask *task);

gsl_err_t knd_proc_select(struct kndRepo *repo,
                          const char *rec,
                          size_t *total_size,
                          struct kndTask *task);

int knd_proc_resolve(struct kndProc *self,
                     struct kndTask *task);

gsl_err_t knd_proc_import(struct kndRepo *repo,
                          const char *rec, size_t *total_size,
                          struct kndTask *task);

// knd_proc.gsl.c
int knd_proc_export_GSL(struct kndProc *self,
                        struct kndTask *task,
                        bool is_list_item,
                        size_t depth);
// knd_proc.json.c
int knd_proc_export_JSON(struct kndProc *self,
                         struct kndTask *task,
                         struct kndOutput  *out);
// knd_proc.svg.c
int knd_proc_export_SVG(struct kndProc *self,
                        struct kndTask *task,
                        struct kndOutput  *out);

//
// TODO(k15tfu): ?? Move to knd_proc_impl.h
//
#include <knd_proc_arg.h>
#include <knd_text.h>

static inline void knd_proc_var_declare_arg(struct kndProcVar *base, struct kndProcArgVar *base_arg)
{
    if (base->tail) {
        base->tail->next = base_arg;
        base->tail = base_arg;
    }
    else
        base->args = base->tail = base_arg;
    base->num_args++;
}

static inline void knd_proc_declare_arg(struct kndProc *self, struct kndProcArg *arg)
{
    arg->next = self->args;
    self->args = arg;
    self->num_args++;
}

int knd_proc_update_state(struct kndProc *self,
                          knd_state_phase phase,
                          struct kndTask *task);

static inline void kndProc_declare_tr(struct kndProc *self, struct kndTranslation *tr)
{
    tr->next = self->tr;
    self->tr = tr;
}

static inline void declare_base(struct kndProc *self, struct kndProcVar *base)
{
    base->next = self->bases;
    self->bases = base;
    self->num_bases++;
}
