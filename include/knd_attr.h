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
 *   knd_attr.h
 *   Knowdy Concept Attr
 */

#pragma once

#include "knd_dict.h"
#include "knd_utils.h"
#include "knd_task.h"
#include "knd_config.h"
#include "knd_output.h"

#include <gsl-parser/gsl_err.h>

#include <stddef.h>

struct kndClass;
struct kndClassEntry;
struct kndText;
struct kndAttr;
struct kndProc;
struct kndClassUpdate;
struct kndProcCallArg;
struct kndTask;

typedef enum knd_attr_type {
    KND_ATTR_NONE,
    KND_ATTR_ATOM,
    KND_ATTR_STR,
    KND_ATTR_BIN,
    KND_ATTR_CDATA,
    KND_ATTR_INNER,
    KND_ATTR_TEXT,
    KND_ATTR_CG,
    KND_ATTR_NUM,
    KND_ATTR_FLOAT,
    KND_ATTR_TIME,
    KND_ATTR_DATE,
    KND_ATTR_BOOL,
    KND_ATTR_PROB,
    KND_ATTR_REF,
    KND_ATTR_ATTR_REF,
    KND_ATTR_PROC_REF,
    KND_ATTR_PROC_ARG_REF,
    KND_ATTR_FILE
} knd_attr_type;

static const char* const knd_attr_names[] = {
    "none",
    "atom",
    "str",
    "bin",
    "cdata",
    "inner",
    "text",
    "CG",
    "num",
    "float",
    "time",
    "date",
    "bool",
    "prob",
    "ref",
    "attr-ref",
    "proc-ref",
    "proc-arg-ref",
    "file"
};

typedef enum knd_attr_quant_type {
    KND_ATTR_SINGLE,
    KND_ATTR_SET,
    KND_ATTR_LIST
} knd_attr_quant_type;


/* index of direct attr values */
struct kndAttrFacet
{
    struct kndSet *topics;
};

/* index of reverse attr paths */
struct kndAttrHub
{
    struct kndAttr     *attr;
    struct kndSet      *topics;
    struct kndSet      *specs;

    struct kndAttrHub  *parent;
    struct kndAttrHub  *next;
};

struct kndAttrVarCtx
{
    struct kndAttrVar *parent_var;
    struct kndAttrVar *attr_var;
    struct kndTask *task;
};

struct kndAttrVarRef
{
    struct kndAttr *attr;
    struct kndState *states;
    struct kndAttrVarRef *children;
    struct kndAttrVarRef *next;
};

struct kndAttrVar
{
    struct kndAttr *attr;

    char id[KND_ID_SIZE];
    size_t id_size;

    const char *name;
    size_t name_size;

    const char *val;
    size_t val_size;

    long numval;
    bool is_cached; // for computed fields
    knd_logic logic;

    struct kndAttr *implied_attr;

    struct kndClassVar *class_var;

    struct kndText *text;

    struct kndAttrVar *parent;
    struct kndAttrVar *children;
    struct kndAttrVar *tail;
    size_t num_children;

    bool is_list_item;
    size_t list_count;

    size_t depth;
    size_t max_depth;

    struct kndState *states;
    size_t init_state;
    size_t num_states;

    /* siblings */
    struct kndAttrVar *list;
    struct kndAttrVar *list_tail;
    size_t num_list_elems;

    struct kndClass *class;
    struct kndClassEntry *class_entry;
    struct kndProc *proc;

    struct kndAttr *ref_attr;

    
    struct kndAttrVar *next;
};

struct kndAttrRef
{
    struct kndAttr *attr;
    struct kndAttrVar *attr_var;
    struct kndClassEntry *class_entry;
    struct kndAttrRef *next;
};

struct kndAttr
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;
    knd_attr_type type;

    knd_attr_quant_type quant_type;

    const char *name;
    size_t name_size;

    struct kndClass *parent_class;

    bool is_a_set;
    bool set_is_unique;
    bool set_is_atomic;

    /* build reverse indices */
    bool is_indexed;
    /* attr name may not be specified */
    bool is_implied;

    /* if refclass is empty: assume self reference by default */
    const char *ref_classname;
    size_t ref_classname_size;
    struct kndClass *ref_class;

    const char *ref_procname;
    size_t ref_procname_size;
    struct kndProc *proc;

    /* concise representation */
    size_t concise_level;

    const char *calc_attr;

    /* facet values indexing */
    struct kndSet *facet_idx;

    struct kndState *states;
    size_t init_state;
    size_t num_states;

    struct kndText *tr;

    struct kndAttr *next;

    /***********  public methods ***********/
    void (*str)(struct kndAttr *self,
                size_t depth);
};

void kndAttr_init(struct kndAttr *self);
int kndAttr_new(struct kndAttr **self);

int knd_attr_var_export_GSL(struct kndAttrVar *self,
                            struct kndTask *task,
                            size_t depth);
int knd_attr_vars_export_GSL(struct kndAttrVar *items,
                             struct kndTask *task,
                             bool is_concise,
                             size_t depth);

int knd_attr_var_export_JSON(struct kndAttrVar *self,
                             struct kndTask *task);

int knd_export_inherited_attr(void *obj,
                              const char *elem_id,
                              size_t elem_id_size,
                              size_t count,
                              void *elem);

int knd_attr_vars_export_JSON(struct kndAttrVar *items,
                                     struct kndTask *task,
                                     bool is_concise);

int knd_attr_var_export_GSP(struct kndAttrVar *self,
                                   struct kndTask *task,
                                   struct kndOutput *out);

int knd_attr_vars_export_GSP(struct kndAttrVar *items,
                                    struct kndOutput *out,
                                    struct kndTask *task,
                                    size_t depth,
                                    bool is_concise);

int knd_present_computed_inner_attrs(struct kndAttrVar *attr_var,
                                           struct kndOutput *out);

int knd_compute_num_value(struct kndAttr *attr,
                                 struct kndAttrVar *attr_var,
                                 long *result);

int knd_apply_attr_var_updates(struct kndClass *self,
                                      struct kndClassUpdate *update,
                                      struct kndTask *task);

int knd_register_attr_ref(void *obj,
                                 const char *elem_id,
                                 size_t elem_id_size,
                                 size_t count,
                                 void *elem);

int knd_get_arg_value(struct kndAttrVar *src,
                             struct kndAttrVar *query,
                             struct kndProcCallArg *arg);

int knd_attr_export_GSL(struct kndAttr *self, struct kndTask *task, size_t depth);

int knd_attr_export(struct kndAttr *self,
                           knd_format format, struct kndTask *task);
extern void str_attr_vars(struct kndAttrVar *item, size_t depth);

int knd_attr_var_new(struct kndMemPool *mempool,
                            struct kndAttrVar **result);
int knd_attr_ref_new(struct kndMemPool *mempool,
                            struct kndAttrRef **result);
int knd_attr_facet_new(struct kndMemPool *mempool,
                       struct kndAttrFacet **result);
int knd_attr_hub_new(struct kndMemPool *mempool,
                     struct kndAttrHub **result);

int knd_attr_new(struct kndMemPool *mempool,
                        struct kndAttr **result);

// knd_attr.import.c
int knd_import_attr_var(struct kndClassVar *self,
                        const char *name, size_t name_size,
                        const char *rec, size_t *total_size,
                        struct kndTask *task);
int knd_import_attr_var_list(struct kndClassVar *self,
                             const char *name, size_t name_size,
                             const char *rec, size_t *total_size,
                             struct kndTask *task);

extern gsl_err_t knd_import_attr(struct kndAttr *attr,
                                 struct kndTask *task,
                                 const char *rec, size_t *total_size);

// knd_attr.select.c
int knd_attr_var_match(struct kndAttrVar *self,
                              struct kndAttrVar *template);

int knd_attr_select_clause(struct kndAttr *attr,
                           struct kndClass *c,
                           struct kndRepo *repo,
                           struct kndTask *task,
                           const char *rec, size_t *total_size);

extern gsl_err_t knd_select_attr_var(struct kndClass *class,
                                     const char *name, size_t name_size,
                                     const char *rec, size_t *total_size,
                                     struct kndTask *task);

// knd_attr.resolve.c
int knd_resolve_attr_vars(struct kndClass *self,
                                 struct kndClassVar *parent_item,
                                 struct kndTask *task);
int knd_resolve_primary_attrs(struct kndClass *self,
                                     struct kndTask *task);

// knd_attr.index.c
int knd_index_attr(struct kndClass *self,
                   struct kndAttr *attr,
                   struct kndAttrVar *item,
                   struct kndTask *task);
int knd_index_attr_var_list(struct kndClass *self,
                            struct kndAttr *attr,
                            struct kndAttrVar *item,
                            struct kndTask *task);
int knd_index_inner_attr_var(struct kndClass *self,
                             struct kndAttrVar *item,
                             struct kndTask *task);

