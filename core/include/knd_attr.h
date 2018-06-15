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

typedef enum knd_attr_type {
    KND_ATTR_NONE,
    KND_ATTR_ATOM,
    KND_ATTR_STR,
    KND_ATTR_BIN,
    KND_ATTR_AGGR,
    KND_ATTR_TEXT, 
    KND_ATTR_CG,
    KND_ATTR_NUM,
    KND_ATTR_REF,
    KND_ATTR_CALC,
    KND_ATTR_FILE,
    KND_ATTR_PROC
} knd_attr_type;

static const char* const knd_attr_names[] = {
    "none",
    "atom",
    "str",
    "bin",
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

struct kndAttrItem
{
    knd_task_spec_type type;
    char name[KND_NAME_SIZE];
    size_t name_size;

    char id[KND_ID_SIZE];
    size_t id_size;

    char val[KND_VAL_SIZE];
    size_t val_size;

    struct kndAttr *attr;
    struct kndAttr *implied_attr;

    struct kndAttrItem *parent;

    struct kndAttrItem *children;
    struct kndAttrItem *tail;
    size_t num_children;

    bool is_list_item;
    size_t list_count;

    /* siblings */
    struct kndAttrItem *list;
    struct kndAttrItem *list_tail;
    size_t num_list_elems;

    struct kndClass *conc;
    struct kndClassEntry *conc_dir;
    struct kndProc *proc;
    //struct kndProcDir *proc_dir;

    struct kndMemPool *mempool;

    struct kndAttrItem *next;
};

struct kndAttrEntry
{
    char name[KND_NAME_SIZE];
    size_t name_size;

    struct kndAttr *attr;
    struct kndAttrItem *attr_item;

    //struct kndClassEntry *entry;

    /* TODO: facets */

    struct kndAttrEntry *next;
};

struct kndAttr 
{
    knd_attr_type type;
    knd_attr_access_type access_type;
    knd_attr_quant_type quant_type;

    char name[KND_NAME_SIZE];
    size_t name_size;

    //char classname[KND_NAME_SIZE];
    //size_t classname_size;
    struct kndClass *conc;

    char uniq_attr_name[KND_SHORT_NAME_SIZE];
    size_t uniq_attr_name_size;
    //struct kndAttr *uniq_attr;

    char validator_name[KND_SHORT_NAME_SIZE];
    size_t validator_name_size;

    bool is_a_set;
    //bool is_recursive;

    /* build reverse indices */
    bool is_indexed;

    /* attr name may not be specified */
    bool is_implied;

    struct kndClass *parent_conc;
    struct kndTask *task;

    const char *locale;
    size_t locale_size;
    knd_format format;

    /* if refclass is empty: assume self reference by default */
    char ref_classname[KND_NAME_SIZE];
    size_t ref_classname_size;

    char ref_procname[KND_NAME_SIZE];
    size_t ref_procname_size;
    struct kndProc *proc;

    /* concise representation */
    size_t concise_level;

    //int descr_level;
    //int browse_level;

    char calc_oper[KND_NAME_SIZE];
    size_t calc_oper_size;

    char calc_attr[KND_NAME_SIZE];
    //size_t calc_attr_size;

    char default_val[KND_NAME_SIZE];
    size_t default_val_size;

    char idx_name[KND_NAME_SIZE];
    size_t idx_name_size;

    struct kndRefSet *browser;
    struct glbOutput *out;
    
    struct kndTranslation *tr;
    size_t depth;
    struct kndAttr *next;
    
    /***********  public methods ***********/
    void (*str)(struct kndAttr *self);

    int (*parse)(struct kndAttr *self,
                 const char   *rec,
                 size_t *chunk_size);

    int (*validate)(struct kndAttr *self,
                    const char   *val,
                    size_t val_size);

    int (*export)(struct kndAttr   *self);
};


/* constructor */
extern void kndAttr_init(struct kndAttr *self);
extern int kndAttr_new(struct kndAttr **self);
