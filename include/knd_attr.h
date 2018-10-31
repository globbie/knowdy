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

#include <gsl-parser/gsl_err.h>

#include <stddef.h>

struct kndClass;
struct kndClassEntry;
struct glbOutput;
struct kndTranslation;
struct kndAttr;
struct kndProc;
struct kndClassUpdate;
struct kndProcCallArg;

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
    KND_ATTR_FILE,
    KND_ATTR_PROC
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
    "file",
    "proc"
};

typedef enum knd_attr_access_type {
    KND_ATTR_ACCESS_USER,
    KND_ATTR_ACCESS_RESTRICTED,
    KND_ATTR_ACCESS_PUB
} knd_attr_access_type;

typedef enum knd_attr_quant_type {
    KND_ATTR_SINGLE,
    KND_ATTR_SET,
    KND_ATTR_LIST
} knd_attr_quant_type;

//struct kndAttrValidator
//{
//    char name[KND_SHORT_NAME_SIZE];
//    size_t name_size;
//    int (*proc)(struct kndAttr *self,
//                const char   *val,
//                size_t val_size);
//};

//struct kndAttrUpdate
//{
//    struct kndAttr *attr;
//    struct kndAttrVarRef *attr_var;
//};

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

    struct kndTask *task;
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
    knd_attr_type type;

    knd_attr_access_type access_type;
    knd_attr_quant_type quant_type;

    const char *name;
    size_t name_size;
    size_t numid;

    struct kndClass *parent_class;

    bool is_a_set;
    //bool is_recursive;

    /* build reverse indices */
    bool is_indexed;

    /* attr name may not be specified */
    bool is_implied;

    struct kndTask *task;

    /* if refclass is empty: assume self reference by default */
    const char *ref_classname;
    size_t ref_classname_size;
    struct kndClass *ref_class;

    const char *ref_procname;
    size_t ref_procname_size;
    struct kndProc *proc;

    /* concise representation */
    size_t concise_level;

    const char *calc_attr;//[KND_NAME_SIZE];

    const char *idx_name; //[KND_SHORT_NAME_SIZE];
    size_t idx_name_size;

    struct kndState *states;
    size_t init_state;
    size_t num_states;

    struct kndTranslation *tr;
    size_t depth;

    struct kndAttr *next;

    /***********  public methods ***********/
    void (*str)(struct kndAttr *self);

    /*int (*export)(struct kndAttr *self,
                  knd_format format,
                  struct glbOutput *out);*/
};

/* constructor */
extern void kndAttr_init(struct kndAttr *self);
extern int kndAttr_new(struct kndAttr **self);

extern int knd_attr_var_export_GSL(struct kndAttrVar *self,
                                   struct kndTask *task,
                                   size_t depth);
extern int knd_attr_vars_export_GSL(struct kndAttrVar *items,
                                    struct kndTask *task,
                                    bool is_concise,
                                    size_t depth);

extern int knd_attr_var_export_JSON(struct kndAttrVar *self,
                                    struct kndTask *task);

extern int knd_export_inherited_attr(void *obj,
                                     const char *elem_id,
                                     size_t elem_id_size,
                                     size_t count,
                                     void *elem);

extern int knd_attr_vars_export_JSON(struct kndAttrVar *items,
                                     struct kndTask *task,
                                     bool is_concise);

extern int knd_attr_var_export_GSP(struct kndAttrVar *self,
                                    struct glbOutput *out);

extern int knd_attr_vars_export_GSP(struct kndAttrVar *items,
                                     struct glbOutput *out,
                                     size_t depth,
                                     bool is_concise);

extern int knd_present_computed_inner_attrs(struct kndAttrVar *attr_var,
                                           struct glbOutput *out);

extern int knd_compute_num_value(struct kndAttr *attr,
                                 struct kndAttrVar *attr_var,
                                 long *result);

extern int knd_apply_attr_var_updates(struct kndClass *self,
                                      struct kndClassUpdate *update);

extern int knd_copy_attr_ref(void *obj,
                             const char *elem_id,
                             size_t elem_id_size,
                             size_t count,
                             void *elem);
extern int knd_register_attr_ref(void *obj,
                                 const char *elem_id,
                                 size_t elem_id_size,
                                 size_t count,
                                 void *elem);

extern int knd_get_arg_value(struct kndAttrVar *src,
                             struct kndAttrVar *query,
                             struct kndProcCallArg *arg);

extern int knd_attr_export(struct kndAttr *self,
                           knd_format format, struct kndTask *task);
extern void str_attr_vars(struct kndAttrVar *item, size_t depth);

extern int knd_attr_var_new(struct kndMemPool *mempool,
                            struct kndAttrVar **result);
extern int knd_attr_ref_new(struct kndMemPool *mempool,
                            struct kndAttrRef **result);
extern int knd_attr_new(struct kndMemPool *mempool,
                        struct kndAttr **result);

// knd_attr.import.c
extern gsl_err_t knd_import_attr(struct kndTask *task, const char *rec, size_t *total_size);
