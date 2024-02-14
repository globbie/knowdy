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
#include "knd_shared_dict.h"
#include "knd_output.h"
#include "knd_text.h"
#include "knd_proc_call.h"
#include "knd_proc_arg.h"

struct kndProcDuration
{
    size_t begin;
    size_t end;
    size_t total;
};

struct kndProcEstimate
{
    size_t cost;
    size_t aggr_cost;

    size_t time;
    size_t aggr_time;

    //size_t num_agents;
    //size_t aggr_num_agents;

    struct kndProcDuration duration;
};

struct kndProcIdx
{
    struct kndProcEntry *entry;

    struct kndSharedIdx * _Atomic idx;
    struct kndTextLoc * _Atomic locs;
    atomic_size_t num_locs;

    struct kndProcArgRef *arg_roles;

    struct kndProcIdx *children;
    struct kndProcIdx *next;
};

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
    struct kndProcEntry *is_a;
    struct kndProcInst *inst;

    struct kndRepo *repo;

    struct kndProcInstEntry *next;
};

struct kndProcInst
{
    const char *name;
    size_t name_size;

    const char *alias;
    size_t alias_size;

    struct kndProcInstEntry *entry;
    struct kndProc *is_a;

    struct kndProcVar *procvar;

    // proc_phase_t phase;
    //proc_timeline_t timeline;

    /* natural language expression if available */
    struct kndTextRepr *repr;

    struct kndState * _Atomic states;
    size_t init_state;
    size_t num_states;

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

    struct kndProcEntry *orig;

    knd_state_phase phase;

    size_t global_offset;
    size_t block_size;

    /* immediate children */
    struct kndProcRef *children;
    size_t num_children;
    size_t num_terminals;

    struct kndProcRef *ancestors;
    size_t num_ancestors;
    struct kndSet *descendants;

    struct kndSharedDict *inst_name_idx;
    atomic_size_t    num_insts;
    atomic_size_t    inst_id_count;

    struct kndClassRef *text_idxs;

    struct kndSharedDictItem *dict_item;
};

struct kndProcRef
{
    struct kndProc      *proc;
    struct kndProcEntry *entry;
    struct kndProcRef   *next;
};

struct kndProcVar
{
    const char *name;
    size_t name_size;

    struct kndProcEntry *parent;
    struct kndProc *proc;

    struct kndProcArgVar *args;
    struct kndProcArgVar *tail;
    size_t num_args;

    struct kndProcArgVar *effects;
    struct kndProcArgVar *effect_tail;
    size_t num_effects;

    struct kndProcVar *next;
};

struct kndProc
{
    const char *name;
    size_t name_size;

    struct kndProcEntry *entry;
    struct kndText *tr;

    struct kndClass *agent;

    /* immediate args */
    struct kndProcArg *args;
    size_t num_args;

    struct kndProcArg *effects;
    size_t num_effects;

    struct kndSet *arg_idx;

    struct kndProcCall *calls;
    size_t num_calls;
    
    struct kndProcVar *bases;
    size_t num_bases;

    struct kndProcEntry *inherited;
    size_t num_inherited;

    struct kndProcEntry *children;
    size_t num_children;

    const char *result_classname;
    size_t result_classname_size;
    struct kndClassEntry *result;

    struct kndProcEstimate estimate;

    struct kndState * _Atomic states;
    size_t num_states;

    bool is_resolved;
    bool is_computed;
    bool resolving_in_progress;
    bool base_resolving_in_progress;
    bool base_is_resolved;

    struct kndProc *next;
};

int knd_proc_new(struct kndMemPool *mempool, struct kndProc **result);
int knd_proc_entry_new(struct kndMemPool *mempool, struct kndProcEntry **result);
int knd_proc_ref_new(struct kndMemPool *mempool, struct kndProcRef **result);
int knd_proc_idx_new(struct kndMemPool *mempool, struct kndProcIdx **result);
int knd_proc_var_new(struct kndMemPool *mempool, struct kndProcVar **result);
int knd_proc_inst_new(struct kndMemPool *mempool, struct kndProcInst **result);
int knd_proc_inst_entry_new(struct kndMemPool *mempool, struct kndProcInstEntry **result);

void knd_proc_str(struct kndProc *self, size_t depth);
void knd_proc_inst_str(struct kndProcInst *self, size_t depth);
int knd_proc_inst_export_GSL(struct kndProcInst *self, bool is_list_item, knd_state_phase phase,
                             struct kndTask *task, size_t depth);
int knd_proc_inst_export_JSON(struct kndProcInst *self, bool is_list_item, knd_state_phase phase,
                              struct kndTask *task, size_t depth);

int knd_import_proc_inst(struct kndProcEntry *self, const char *rec, size_t *total_size, struct kndTask *task);
int knd_inner_proc_import(struct kndProc *self, const char *rec, size_t *total_size, struct kndRepo *repo,
                          struct kndTask *task);

int knd_proc_is_base(struct kndProc *self, struct kndProc *child);
int knd_get_proc(struct kndRepo *repo, const char *name, size_t name_size, struct kndProc **result, struct kndTask *task);
int knd_get_proc_entry(struct kndRepo *repo, const char *name, size_t name_size, struct kndProcEntry **result, struct kndTask *task);
int knd_proc_get_arg(struct kndProc *self, const char *name, size_t name_size, struct kndProcArgRef **result);
int knd_resolve_proc_ref(struct kndClass *self, const char *name, size_t name_size,
                         struct kndProc *unused_var(base), struct kndProcEntry **result,
                         struct kndTask *unused_var(task));

int knd_proc_export(struct kndProc *self, knd_format format, struct kndTask *task, struct kndOutput *out);

gsl_err_t knd_proc_select(struct kndRepo *repo, const char *rec, size_t *total_size, struct kndTask *task);

int knd_proc_entry_clone(struct kndProcEntry *self, struct kndRepo *target_repo, struct kndProcEntry **result, struct kndTask *task);

int knd_proc_resolve(struct kndProc *self, struct kndTask *task);
//int knd_proc_compute(struct kndProc *self, struct kndTask *task);

// knd_proc.import.c
gsl_err_t knd_proc_import(struct kndRepo *repo, const char *rec, size_t *total_size, struct kndTask *task);

// knd_proc.gsl.c
int knd_proc_export_GSL(struct kndProc *self,
                        struct kndTask *task,
                        bool is_list_item,
                        size_t depth);
// knd_proc.json.c
int knd_proc_export_JSON(struct kndProc *self,
                         struct kndTask *task,
                         bool is_list_item,
                         size_t depth);

// knd_proc.svg.c
int knd_proc_export_SVG(struct kndProc *self,
                        struct kndTask *task,
                        struct kndOutput  *out);

//
// TODO(k15tfu): ?? Move to knd_proc_impl.h
//
#include <knd_proc_arg.h>

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

static inline void knd_proc_var_declare_effect(struct kndProcVar *base, struct kndProcArgVar *base_arg)
{
    if (base->effect_tail) {
        base->effect_tail->next = base_arg;
        base->effect_tail = base_arg;
    }
    else
        base->effects = base->effect_tail = base_arg;
    base->num_effects++;
}


static inline void knd_proc_declare_arg(struct kndProc *self, struct kndProcArg *arg)
{
    arg->parent = self;
    arg->next = self->args;
    self->args = arg;
    self->num_args++;
}

static inline void knd_proc_declare_effect(struct kndProc *self, struct kndProcArg *arg)
{
    arg->parent = self;
    arg->next = self->effects;
    self->effects = arg;
    self->num_effects++;
}

static inline void knd_proc_declare_call(struct kndProc *self, struct kndProcCall *call)
{
    call->next = self->calls;
    self->calls = call;
    self->num_calls++;
}

int knd_proc_commit_state(struct kndProc *self, knd_state_phase phase, struct kndTask *task);

static inline void kndProc_declare_tr(struct kndProc *self, struct kndText *tr)
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
