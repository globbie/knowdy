/**
 *   Copyright (c) 2011-2017 by Dmitri Dmitriev
 *   All rights reserved.
 *
 *   This file is part of the Knowdy Search Engine, 
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
 *   knd_attr.h
 *   Knowdy Concept Attr
 */

#ifndef KND_ATTR_H
#define KND_ATTR_H

#include "knd_dict.h"
#include "knd_utils.h"
#include "knd_task.h"
#include "knd_config.h"

struct kndConcept;
struct kndOutput;
struct kndTranslation;
struct kndAttr;

struct kndAttrItem
{
    knd_task_spec_type type;
    char name[KND_NAME_SIZE];
    size_t name_size;

    char val[KND_NAME_SIZE];
    size_t val_size;

    struct kndAttr *ref;

    struct kndAttrItem *children;
    size_t num_children;
    
    struct kndAttrItem *next;
};

struct kndAttr 
{
    knd_elem_type type;

    char name[KND_NAME_SIZE];
    size_t name_size;

    char classname[KND_NAME_SIZE];
    size_t classname_size;

    struct kndConcept *parent_dc;
    struct kndConcept *dc;

    const char *locale;
    size_t locale_size;
    knd_format format;

    /* refclass not set: self reference by default */
    char ref_classname[KND_NAME_SIZE];
    size_t ref_classname_size;
    struct kndConcept *ref_class;

    int concise_level;
    int descr_level;
    int browse_level;

    bool is_list;
    bool is_recursive;
    
    char calc_oper[KND_NAME_SIZE];
    size_t calc_oper_size;

    char calc_attr[KND_NAME_SIZE];
    size_t calc_attr_size;

    char default_val[KND_NAME_SIZE];
    size_t default_val_size;

    char idx_name[KND_NAME_SIZE];
    size_t idx_name_size;

    struct kndRefSet *browser;

    struct kndOutput *out;
    
    struct kndTranslation *tr;

    struct kndAttr *next;
    
    /***********  public methods ***********/
    void (*init)(struct kndAttr  *self);
    void (*del)(struct kndAttr   *self);
    void (*str)(struct kndAttr *self, size_t depth);

    int (*parse)(struct kndAttr *self,
                 const char   *rec,
                 size_t *chunk_size);

    int (*export)(struct kndAttr   *self);
};


/* constructor */
extern int kndAttr_new(struct kndAttr **self);
#endif
