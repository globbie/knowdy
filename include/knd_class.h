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

#include "knd_dict.h"
#include "knd_facet.h"
#include "knd_utils.h"
#include "knd_class_inst.h"
#include "knd_config.h"
#include "knd_state.h"

#include <gsl-parser/gsl_err.h>
#include <stdatomic.h>

struct kndAttr;
struct kndAttrVar;
struct kndProcCallArg;
struct kndClass;
struct kndTranslation;
struct kndClass;
struct kndRel;
struct kndTask;
struct kndSet;
struct kndUser;
struct kndClassUpdate;
struct kndClassUpdateRef;
struct kndObjEntry;
struct glbOutput;
struct kndClassInstEntry;
struct kndAttrRef;

struct kndClassUpdate
{
    struct kndUpdate     *update;
    struct kndClass      *class;
    struct kndClassEntry *entry;
    struct kndClassInst **insts;
    size_t                num_insts;
    struct kndClassUpdate *next;
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

struct kndClassRef
{
    struct kndClass      *class;
    struct kndClassEntry *entry;
    struct kndClassRef   *next;
};

struct kndClassVar
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    struct kndClassEntry *entry;

    struct kndAttrVar *attrs;
    struct kndAttrVar *tail;
    size_t num_attrs;

    struct kndState *states;
    size_t init_state;
    size_t num_states;

    //struct kndClass *root_class;
    struct kndClass *parent;

    struct kndClassVar *next;
};

struct kndClassEntry
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    const char *name;
    size_t name_size;

    struct kndClass *class;
    struct kndRepo *repo;
    struct kndClassEntry *orig;

    knd_state_phase phase;

    /* immediate children */
    struct kndClassRef *children;
    size_t num_children;
    size_t num_terminals;

    struct kndClassRef *ancestors;
    size_t num_ancestors;

    struct kndSet *descendants;

    struct kndSet   *inst_idx;
    struct kndDict  *inst_name_idx;
    atomic_size_t    num_insts;
    atomic_size_t    inst_id_count;

    struct kndSet *attr_idx;

    struct kndAttrHub *attr_hubs;
};

struct kndClass
{
    const char *name;
    size_t name_size;

    struct kndClassEntry *entry;

    struct kndState * _Atomic states;
    size_t init_state;
    size_t num_states;

    struct kndState *desc_states;
    size_t init_desc_state;
    size_t num_desc_states;

    struct kndState *inst_states;
    size_t init_inst_state;
    size_t num_inst_states;

    struct kndTranslation *tr;
    struct kndTranslation *summary;

    struct kndClassVar *baseclass_vars;
    size_t num_baseclass_vars;

    struct kndAttr *attrs;
    struct kndAttr *tail_attr;
    size_t num_attrs;

    struct kndSet *attr_idx;
    struct kndAttr *implied_attr;

    // detect vicious circles
    bool resolving_in_progress;
    bool is_resolved;
    bool indexing_in_progress;
    bool is_indexed;
    bool state_top;

    struct kndClass *next;

    /***********  public methods ***********/
    void (*str)(struct kndClass *self, size_t depth);
};

/* constructor */
extern void kndClass_init(struct kndClass *self);
int kndClass_new(struct kndClass **self,
                        struct kndMemPool *mempool);

int knd_class_coordinate(struct kndClass *self, struct kndTask *task);

int knd_get_class(struct kndRepo *self,
                         const char *name, size_t name_size,
                         struct kndClass **result,
                         struct kndTask *task);

int knd_get_class_by_id(struct kndRepo *self,
                               const char *id, size_t id_size,
                               struct kndClass **result,
                               struct kndTask *task);

extern gsl_err_t knd_import_class_var(struct kndClassVar *self,
                                      const char *rec,
                                      size_t *total_size,
                                      struct kndTask *task);

int knd_is_base(struct kndClass *self,
                       struct kndClass *base);

int knd_class_get_attr_var(struct kndClass *self,
                                  const char *name,
                                  size_t name_size,
                                  struct kndAttrVar **result);

int knd_class_get_attr(struct kndClass *self,
                              const char *name, size_t name_size,
                              struct kndAttrRef **result);

int knd_export_class_state_JSON(struct kndClass *self,
                                       struct kndTask *task);

int knd_empty_set_export_JSON(struct kndClass *self,
                                    struct kndTask *task);

int knd_class_set_export_JSON(struct kndSet *set,
                              struct kndTask *task);

int knd_class_facets_export_JSON(struct kndTask *task);

int knd_class_export_JSON(struct kndClass *self,
                                 struct kndTask *task);

int knd_class_export(struct kndClass *self,
                            knd_format format,
                            struct kndTask *task);

int knd_class_export_state(struct kndClass *self,
                                  knd_format format,
                                  struct kndTask *task);

extern gsl_err_t knd_parse_gloss_array(void *obj,
                                       const char *rec,
                                       size_t *total_size);
extern gsl_err_t knd_parse_summary_array(void *obj,
                                         const char *rec,
                                         size_t *total_size);

int knd_export_class_state_GSL(struct kndClass *self,
                                      struct kndTask *task);


int knd_class_export_GSL(struct kndClass *self,
                                struct kndTask *task,
                                bool is_list_item,
                                size_t depth);

int knd_empty_set_export_GSL(struct kndClass *self,
                                    struct kndTask *task);

int knd_export_gloss_GSL(struct kndTranslation *tr,
                         struct kndTask *task);

int knd_class_facets_export(struct kndTask *task);

int knd_class_set_export(struct kndSet *self,
                                knd_format format,
                                struct kndTask *task);
int knd_empty_set_export(struct kndClass *self,
                                knd_format format,
                                struct kndTask *task);
int knd_class_set_export_GSL(struct kndSet *set,
                                    struct kndTask *task);

int knd_class_export_GSP(struct kndClass *self,
                                struct kndTask *task);

int knd_class_export_updates_GSP(struct kndClass *self,
                                        struct kndClassUpdate *update,
                                        struct kndTask *task);

int knd_parse_import_class_inst(struct kndClass *self,
                                const char *rec,
                                size_t *total_size,
                                struct kndTask *task);

extern gsl_err_t knd_class_import(struct kndRepo *repo,
                                  const char *rec,
                                  size_t *total_size,
                                  struct kndTask *task);

int knd_inherit_attrs(struct kndClass *self,
                             struct kndClass *base,
                             struct kndTask *task);

int knd_compute_class_attr_num_value(struct kndClass *self,
                                            struct kndAttrVar *attr_var);

int knd_class_update_state(struct kndClass *self,
                           knd_state_phase phase,
                           struct kndTask *task);

extern gsl_err_t knd_read_class_inst_state(struct kndClass *self,
                                           struct kndClassUpdate *update,
                                           const char *rec,
                                           size_t *total_size);

extern gsl_err_t knd_read_class_state(struct kndClass *self,
                                      struct kndClassUpdate *update,
                                      const char *rec,
                                      size_t *total_size);

int knd_get_class_inst(struct kndClass *self,
                              const char *name, size_t name_size,
                              struct kndTask *task,
                              struct kndClassInst **result);

int knd_register_class_inst(struct kndClass *self,
                                   struct kndClassInstEntry *entry,
                                   struct kndMemPool *mempool);

int knd_unregister_class_inst(struct kndClass *self,
                                     struct kndClassInstEntry *entry,
                                     struct kndTask *task);

int knd_class_clone(struct kndClass *self,
                           struct kndRepo *target_repo,
                           struct kndClass **result,
                           struct kndMemPool *mempool);

int knd_class_copy(struct kndClass *self,
                          struct kndClass *target,
                          struct kndMemPool *mempool);

int knd_register_state(struct kndClass *self);
int knd_register_descendant_states(struct kndClass *self);
int knd_register_inst_states(struct kndClass *self);

int knd_export_class_inst_state_JSON(struct kndClass *self,
                                            struct kndTask *task);

int knd_get_class_attr_value(struct kndClass *src,
                                    struct kndAttrVar *query,
                                    struct kndProcCallArg *arg);

int knd_resolve_classes(struct kndClass *self, struct kndTask *task);
int knd_class_resolve(struct kndClass *self,
                             struct kndTask *task);
int knd_class_index(struct kndClass *self,
                    struct kndTask *task);

int knd_class_update_new(struct kndMemPool *mempool,
                                struct kndClassUpdate **result);

int knd_class_var_new(struct kndMemPool *mempool,
                             struct kndClassVar **result);

int knd_class_ref_new(struct kndMemPool *mempool,
                             struct kndClassRef **result);
int knd_class_facet_new(struct kndMemPool *mempool,
                               struct kndClassFacet **result);


int knd_class_entry_new(struct kndMemPool *mempool,
                               struct kndClassEntry **result);
int knd_inner_class_new(struct kndMemPool *mempool,
                               struct kndClass **self);
int knd_class_new(struct kndMemPool *mempool,
                         struct kndClass **result);

// knd_class.select.c
extern gsl_err_t knd_class_select(struct kndRepo *repo,
                                  const char *rec, size_t *total_size,
                                  struct kndTask *task);
int knd_class_match_query(struct kndClass *self,
                                 struct kndAttrVar *query);

// knd_class.states.c
int knd_retrieve_class_updates(struct kndStateRef *ref,
                                      struct kndSet *set);
int knd_class_get_updates(struct kndClass *self,
                                 size_t gt, size_t lt,
                                 size_t unused_var(eq),
                                 struct kndSet *set);
int knd_class_get_desc_updates(struct kndClass *self,
                                      size_t gt, size_t lt,
                                      size_t unused_var(eq),
                                      struct kndSet *set);
int knd_class_get_inst_updates(struct kndClass *self,
                                      size_t gt, size_t lt, size_t eq,
                                      struct kndSet *set);

// knd_class.resolve.c
int knd_resolve_class_ref(struct kndClass *self,
                                 const char *name, size_t name_size,
                                 struct kndClass *base,
                                 struct kndClass **result,
                                 struct kndTask *task);

