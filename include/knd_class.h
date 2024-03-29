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
 *   <http://www.knowdy.net>
 *
 *   Initial author and maintainer:
 *         Dmitri Dmitriev aka M0nsteR <dmitri@globbie.net>
 *
 *   ----------
 *   knd_class.h
 *   Knowdy Concept Class
 */
#pragma once

#include "knd_utils.h"
#include "knd_class_inst.h"
#include "knd_text.h"
#include "knd_config.h"
#include "knd_state.h"

#include <gsl-parser/gsl_err.h>
#include <stdatomic.h>

struct kndAttr;
struct kndAttrVar;
struct kndProcCallArg;
struct kndClass;
struct kndTask;
struct kndSet;
struct kndUser;
struct kndClassCommit;
struct kndClassCommitRef;
struct glbOutput;
struct kndClassInstEntry;
struct kndAttrRef;

typedef enum knd_classvar_t {
    KND_BASE_CLASS,
    KND_INSTANCE_BLUEPRINT
} knd_classvar_t;

struct kndClassCommit
{
    struct kndCommit     *commit;
    struct kndClass      *class;
    struct kndClassEntry *entry;
    struct kndClassInst **insts;
    size_t                num_insts;
    struct kndClassCommit *next;
};

struct kndClassFacet
{
    struct kndClassEntry *base;
    struct kndSet *set;
    struct kndClassRef *elems;
    size_t num_elems;
    struct kndClassFacet *children;
    struct kndClassFacet *next;
};

struct kndClassIdx
{
    struct kndClassEntry *entry;

    struct kndSharedIdx * _Atomic idx;
    struct kndTextLoc * _Atomic locs;
    atomic_size_t num_locs;
    atomic_size_t total_locs;

    struct kndProcArgRef *arg_roles;

    struct kndClassRef * _Atomic children;
    atomic_size_t num_children;
};

struct kndClassRef
{
    struct kndClassEntry *entry;
    struct kndClass      *class;
    struct kndAttr       *attr;
    struct kndClassInstRef *insts;
    struct kndSet        *inst_idx;

    struct kndClassIdx   *idx;
    struct kndProcIdx    *proc_idx;
    struct kndClassRef   *next;
};

struct kndClassVar
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    knd_classvar_t type;
    struct kndClassEntry *entry;

    struct kndAttrVar *attrs;
    struct kndAttrVar *tail;
    size_t num_attrs;

    struct kndState *states;
    size_t init_state;
    size_t num_states;

    struct kndClass *parent;
    struct kndClassInst *inst;

    struct kndClassVar *next;
};

struct kndClassEntry
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    const char *name;
    size_t name_size;
    struct kndCharSeq *seq;

    struct kndRepo *repo;
    struct kndClassEntry *base;
    struct kndClass * _Atomic class;
    atomic_int num_readers;

    struct kndSharedDictItem *dict_item;

    knd_state_phase phase;
};

struct kndClass
{
    const char *name;
    size_t name_size;

    char abbr[KND_ID_SIZE];
    size_t abbr_size;

    struct kndClassEntry *entry;
    struct kndMemBlock *memblock;

    struct kndText *tr;
    struct kndText *summary;

    struct kndClassVar *baseclass_vars;
    struct kndClassVar *baseclass_tail;
    size_t num_baseclass_vars;

    struct kndAttr *attrs;
    struct kndAttr *attr_tail;
    size_t num_attrs;

    struct kndSet *attr_idx;
    struct kndAttr *implied_attr;
    struct kndAttrRef *uniq;

    struct kndState * _Atomic states;
    size_t init_state;
    size_t num_states;

    /* immediate children */
    struct kndClassRef *children;
    size_t num_children;
    size_t num_terminals;

    struct kndClassRef *ancestors;
    size_t num_ancestors;
    struct kndSet *descendants;

    struct kndState * _Atomic desc_states;
    size_t init_desc_state;
    size_t num_desc_states;

    struct kndAttrHub *attr_hubs;

    /* text indices grouped by source type (messages, posts, articles, books etc.) */
    struct kndClassRef * _Atomic text_idxs;
    
    struct kndState * _Atomic inst_states;
    size_t init_inst_state;
    size_t num_inst_states;

    struct kndSharedSet  * _Atomic inst_idx;
    struct kndSharedDict * _Atomic inst_name_idx;
    size_t           num_snapshot_insts;
    atomic_size_t    num_insts;
    atomic_size_t    inst_id_count;
    
    /* detect vicious circles */
    bool resolving_in_progress;
    bool is_resolved;
    bool base_resolving_in_progress;
    bool base_is_resolved;

    bool indexing_in_progress;
    bool is_indexed;
    bool state_top;

    /***********  public methods ***********/
    void (*str)(struct kndClass *self, size_t depth);
};

void knd_class_entry_str(struct kndClassEntry *self, size_t depth);
int knd_get_class_entry(struct kndRepo *self, const char *name, size_t name_size, bool check_ancestors,
                        struct kndClassEntry **result, struct kndTask *task);
int knd_get_class(struct kndRepo *self, const char *name, size_t name_size, struct kndClass **result, struct kndTask *task);
int knd_get_class_by_id(struct kndRepo *self, const char *id, size_t id_size, struct kndClass **result, struct kndTask *task);
int knd_get_class_entry_by_id(struct kndRepo *repo, const char *id, size_t id_size,
                              struct kndClassEntry **result, struct kndTask *task);

int knd_is_base(struct kndClass *self, struct kndClass *child);

// int knd_class_get_attr(struct kndClass *self, const char *name, size_t name_size, struct kndAttrRef **result);
int knd_class_get_attr(struct kndClass *self, const char *name, size_t name_size, struct kndAttrRef **result);
int knd_class_get_attr_var(struct kndClass *self, const char *name, size_t name_size, struct kndAttrVar **result);

int knd_export_class_state_JSON(struct kndClassEntry *self, struct kndTask *task);
int knd_empty_set_export_JSON(struct kndClass *self, struct kndTask *task);

int knd_class_set_export_JSON(struct kndSet *set, struct kndTask *task);
int knd_class_facets_export_JSON(struct kndTask *task);
int knd_class_export_JSON(struct kndClass *self, struct kndTask *task, bool is_list_item, size_t depth);

int knd_class_export(struct kndClass *self, knd_format format, struct kndTask *task);

int knd_class_export_state(struct kndClassEntry *self, knd_format format, struct kndTask *task);


// knd_class.gsl.c
int knd_export_class_state_GSL(struct kndClassEntry *self, struct kndTask *task);
int knd_class_export_GSL(struct kndClassEntry *self, struct kndTask *task, bool is_list_item, size_t depth);

int knd_class_read_GSL(const char *rec, size_t *total_size, struct kndClassEntry **self, struct kndTask *task);
gsl_err_t knd_read_class_var(struct kndClassVar *self, const char *rec, size_t *total_size, struct kndTask *task);

int knd_empty_set_export_GSL(struct kndClass *self, struct kndTask *task);
int knd_export_gloss_GSL(struct kndText *tr, struct kndTask *task);

int knd_class_facets_export(struct kndTask *task);

int knd_class_set_export(struct kndSet *self, knd_format format, struct kndTask *task);
int knd_empty_set_export(struct kndClass *self, knd_format format, struct kndTask *task);
int knd_class_set_export_GSL(struct kndSet *set, struct kndTask *task);

// knd_class.gsp.c
int knd_class_acquire(struct kndClassEntry *self, struct kndClass **result, struct kndTask *task);
int knd_class_release(struct kndClassEntry *self, struct kndTask *task);
int knd_class_marshall(void *elem, size_t *output_size, struct kndTask *task);
int knd_class_entry_unmarshall(const char *elem_id, size_t elem_id_size,
                               const char *val, size_t val_size, void **result, struct kndTask *task);
int knd_class_unmarshall(const char *elem_id, size_t elem_id_size,
                         const char *val, size_t val_size, void **result, struct kndTask *task);

int knd_class_read(struct kndClass *self, const char *rec, size_t *total_size, struct kndTask *task);
int knd_class_inst_idx_fetch(struct kndClass *self, struct kndSharedDict **result, struct kndTask *task);

int knd_class_export_GSP(struct kndClass *self, struct kndTask *task);
int knd_class_export_commits_GSP(struct kndClass *self, struct kndClassCommit *commit, struct kndTask *task);

// knd_class.import.c
gsl_err_t knd_class_import(struct kndRepo *repo, const char *rec, size_t *total_size, struct kndTask *task);

int knd_inherit_attrs(struct kndClass *self, struct kndClass *base, struct kndTask *task);

int knd_compute_class_attr_num_value(struct kndClass *self, struct kndAttrVar *attr_var);

int knd_class_commit_state(struct kndClassEntry *self, knd_state_phase phase, struct kndTask *task);

gsl_err_t knd_read_class_inst_state(struct kndClass *self, struct kndClassCommit *commit,
                                    const char *rec, size_t *total_size);


int knd_get_class_inst(struct kndClass *self, const char *name, size_t name_size, struct kndTask *task, struct kndClassInst **result);

int knd_register_class_inst(struct kndClass *self, struct kndClassInstEntry *entry, struct kndTask *task);

int knd_unregister_class_inst(struct kndClass *self, struct kndClassInstEntry *entry, struct kndTask *task);

int knd_class_entry_clone(struct kndClassEntry *self, struct kndRepo *target_repo, struct kndClassEntry **result, struct kndTask *task);
int knd_class_copy(struct kndClass *self, struct kndClass *target, struct kndTask *task);

int knd_register_state(struct kndClass *self);
int knd_register_descendant_states(struct kndClass *self);
int knd_register_inst_states(struct kndClass *self);

int knd_export_class_inst_state_JSON(struct kndClass *self, struct kndTask *task);

int knd_get_class_attr_value(struct kndClass *src, struct kndAttrVar *query, struct kndProcCallArg *arg);

int knd_class_commit_new(struct kndMemPool *mempool, struct kndClassCommit **result);
int knd_class_var_new(struct kndMemPool *mempool, struct kndClassVar **result);
int knd_class_ref_new(struct kndMemPool *mempool, struct kndClassRef **result);
int knd_class_idx_new(struct kndMemPool *mempool, struct kndClassIdx **result);
int knd_class_facet_new(struct kndMemPool *mempool, struct kndClassFacet **result);
int knd_class_entry_new(struct kndMemPool *mempool, struct kndClassEntry **result);
int knd_inner_class_new(struct kndMemPool *mempool, struct kndClass **self);
int knd_class_new(struct kndMemPool *mempool, struct kndClass **result);

// knd_class.select.c
extern gsl_err_t knd_class_select(struct kndRepo *repo,
                                  const char *rec, size_t *total_size, struct kndTask *task);
int knd_class_match_query(struct kndClass *self, struct kndAttrVar *query);

// knd_class.states.c
int knd_retrieve_class_updates(struct kndStateRef *ref, struct kndSet *set);
int knd_class_get_updates(struct kndClass *self, size_t gt, size_t lt,
                          size_t unused_var(eq), struct kndSet *set);
int knd_class_get_desc_updates(struct kndClass *self,
                               size_t gt, size_t lt,
                               size_t unused_var(eq),
                               struct kndSet *set);

int knd_class_get_inst_updates(struct kndClass *self, size_t gt, size_t lt, size_t eq, struct kndSet *set);

// knd_class.resolve.c
int knd_class_resolve(struct kndClass *self, struct kndTask *task);
int knd_resolve_class_ref(struct kndClass *self, const char *name, size_t name_size,
                          struct kndClass *base, struct kndClass **result, struct kndTask *task);

// knd_class.index.c
int knd_class_update_indices(struct kndRepo *repo, struct kndClassEntry *self, struct kndState *state, struct kndTask *task);
int knd_class_index(struct kndClass *self, struct kndTask *task);

