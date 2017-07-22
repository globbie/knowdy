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
 *   knd_dataclass.h
 *   Knowdy Data Class
 */

#ifndef KND_DATACLASS_H
#define KND_DATACLASS_H

#include "knd_dict.h"
#include "knd_facet.h"
#include "knd_utils.h"
#include "knd_config.h"

struct kndAttr;
struct kndConcept;
struct kndOutput;
struct kndTranslation;

struct kndDataIdx
{
    knd_facet_type type;
    
    char name[KND_NAME_SIZE];
    size_t name_size;

    char abbr[KND_NAME_SIZE];
    size_t abbr_size;

    size_t numval;
    
    bool is_default;

    
    struct kndDataIdx *next;
};


struct kndConcRef
{
    size_t state;
    struct kndConcept *conc;
};

struct kndConcept 
{
    char name[KND_NAME_SIZE];
    size_t name_size;

    char namespace[KND_NAME_SIZE];
    size_t namespace_size;

    /* ontology file location */
    const char *dbpath;
    size_t dbpath_size;
    
    char *path;
    size_t path_size;

    char baseclass_name[KND_NAME_SIZE];
    size_t baseclass_name_size;
    struct kndConcept *baseclass;
    
    struct kndTranslation *tr;
    
    struct kndDataIdx *indices;
    size_t num_indices;

    struct kndAttr *attrs;
    struct kndAttr *tail_attr;
    size_t num_attrs;

    /* for traversal */
    struct kndAttr *curr_attr;
    size_t attrs_left;

    char curr_val[KND_NAME_SIZE];
    size_t curr_val_size;
    
    char lang_code[KND_NAME_SIZE];
    size_t lang_code_size;

    char idx_name[KND_NAME_SIZE];
    size_t idx_name_size;

    char style_name[KND_NAME_SIZE];
    size_t style_name_size;

    /* indices */
    struct ooDict *class_idx;
    struct ooDict *attr_idx;

    struct kndConcRef children[KND_MAX_CONC_CHILDREN];
    size_t num_children;
    
    struct kndRefSet *browser;

    struct kndOutput *out;
    struct kndOutput *log;
    
    /***********  public methods ***********/
    void (*init)(struct kndConcept  *self);
    void (*del)(struct kndConcept   *self);
    void (*str)(struct kndConcept *self,
                size_t depth);

    int (*read_file)(struct kndConcept   *self,
                     const char *filename,
                     size_t filename_size);

    int (*coordinate)(struct kndConcept   *self);
    int (*resolve)(struct kndConcept   *self);

    int (*export)(struct kndConcept   *self,
                  knd_format format);

    int (*is_a)(struct kndConcept   *self,
                struct kndConcept   *base);
    
    /* traversal */
    void (*rewind)(struct kndConcept   *self);
    int (*next_attr)(struct kndConcept   *self,
                     struct kndAttr **result);
    
};

/* constructor */
extern int kndConcept_new(struct kndConcept **self);

#endif
