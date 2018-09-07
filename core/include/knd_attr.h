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

struct kndClass;
struct kndClassEntry;
struct glbOutput;
struct kndTranslation;
struct kndAttr;
struct kndProc;
struct kndClassUpdate;

typedef enum knd_attr_type {
    KND_ATTR_NONE,
    KND_ATTR_ATOM,
    KND_ATTR_STR,
    KND_ATTR_BIN,
    KND_ATTR_CDATA,
    KND_ATTR_AGGR,
    KND_ATTR_TEXT,
    KND_ATTR_CG,
    KND_ATTR_NUM,
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
    "aggr",
    "text",
    "CG",
    "num",
    "ref",
    "calc",
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

struct kndAttrValidator
{
    char name[KND_SHORT_NAME_SIZE];
    size_t name_size;
    int (*proc)(struct kndAttr *self,
                const char   *val,
                size_t val_size);
};

struct kndAttrUpdate
{
    struct kndAttr *attr;
    struct kndAttrVar *attr_var;
};

struct kndAttrVar
{
    struct kndAttr *attr;

    char name[KND_SHORT_NAME_SIZE];
    size_t name_size;

    char id[KND_ID_SIZE];
    size_t id_size;

    char *valbuf;
    char *val;
    size_t val_size;

    long numval;
    bool is_cached; // for computed fields

    struct kndAttr *implied_attr;

    struct kndClassVar *class_var;

    struct kndAttrVar *parent;
    struct kndAttrVar *children;
    struct kndAttrVar *tail;
    size_t num_children;

    bool is_list_item;
    size_t list_count;

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

struct kndAttrEntry
{
    char name[KND_NAME_SIZE];
    size_t name_size;
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    struct kndAttr *attr;
    struct kndAttrVar *attr_var;

    struct kndAttrEntry *parent;

    struct kndAttrEntry *next;
};

struct kndAttr
{
    knd_attr_type type;
    knd_attr_access_type access_type;
    knd_attr_quant_type quant_type;

    char name[KND_NAME_SIZE];
    size_t name_size;
    size_t numid;

    struct kndAttrEntry *entry;
    struct kndClass *parent_class;
    struct kndClass *conc;

    char uniq_attr_name[KND_SHORT_NAME_SIZE];
    size_t uniq_attr_name_size;

    bool is_a_set;
    //bool is_recursive;

    /* build reverse indices */
    bool is_indexed;

    /* attr name may not be specified */
    bool is_implied;

    struct kndTask *task;

    /* if refclass is empty: assume self reference by default */
    char ref_classname[KND_SHORT_NAME_SIZE];
    size_t ref_classname_size;

    char ref_procname[KND_SHORT_NAME_SIZE];
    size_t ref_procname_size;
    struct kndProc *proc;

    /* concise representation */
    size_t concise_level;

    char calc_attr[KND_NAME_SIZE];
    //size_t calc_attr_size;

    char default_val[KND_SHORT_NAME_SIZE];
    size_t default_val_size;

    char idx_name[KND_SHORT_NAME_SIZE];
    size_t idx_name_size;

    struct kndState *states;
    size_t init_state;
    size_t num_states;

    struct kndTranslation *tr;
    size_t depth;

    struct kndAttr *next;
    struct kndAttr *mem_next;
    struct kndAttr *mem_prev;

    /***********  public methods ***********/
    void (*str)(struct kndAttr *self);

    gsl_err_t (*parse)(struct kndAttr *self,
                       const char *rec,
                       size_t *chunk_size);

    int (*validate)(struct kndAttr *self,
                    const char   *val,
                    size_t val_size);

    int (*export)(struct kndAttr *self,
                  knd_format format,
                  struct glbOutput *out);
};

/* constructor */
extern void kndAttr_init(struct kndAttr *self);
extern int kndAttr_new(struct kndAttr **self);

extern int knd_attr_var_export_JSON(struct kndAttrVar *self,
                                    struct glbOutput *out);
extern int knd_attr_vars_export_JSON(struct kndAttrVar *items,
                                     struct glbOutput *out,
                                     size_t depth __attribute__((unused)),
                                     bool is_concise);

extern int knd_attr_var_export_GSP(struct kndAttrVar *self,
                                    struct glbOutput *out);
extern int knd_attr_vars_export_GSP(struct kndAttrVar *items,
                                     struct glbOutput *out,
                                     size_t depth __attribute__((unused)),
                                     bool is_concise);

extern int knd_present_computed_aggr_attrs(struct kndAttrVar *attr_var,
                                           struct glbOutput *out);
extern int knd_compute_num_value(struct kndAttr *attr,
                                 struct kndAttrVar *attr_var,
                                 long *result);

extern int knd_apply_attr_var_updates(struct kndClass *self,
                                      struct kndClassUpdate *update);

extern int knd_attr_var_new(struct kndMemPool *mempool,
                            struct kndAttrVar **result);
extern int knd_attr_new(struct kndMemPool *mempool,
                        struct kndAttr **result);
