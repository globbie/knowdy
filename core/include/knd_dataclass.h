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
struct kndDataWriter;
struct kndDataReader;
struct kndDataClass;
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

struct kndDataClass 
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
    struct kndDataClass *baseclass;
    
    struct kndTranslation *tr;
    
    struct kndDataIdx *indices;
    size_t num_indices;

    struct kndAttr *attrs;
    struct kndAttr *tail_attr;
    size_t num_attrs;

    /* for traversal */
    struct kndAttr *curr_attr;
    size_t attrs_left;

    
    char lang_code[KND_NAME_SIZE];
    size_t lang_code_size;

    char idx_name[KND_NAME_SIZE];
    size_t idx_name_size;

    char style_name[KND_NAME_SIZE];
    size_t style_name_size;

    /* children */
    struct ooDict *class_idx;
    struct ooDict *attr_idx;
    
    struct kndOutput *out;
    
    /***********  public methods ***********/
    void (*init)(struct kndDataClass  *self);
    void (*del)(struct kndDataClass   *self);
    int (*str)(struct kndDataClass *self,
               size_t depth);
 
    int (*set_name)(struct kndDataClass   *self,
                    char *rec);

    int (*read_file)(struct kndDataClass   *self,
                     const char *filename,
                     size_t filename_size);

    int (*coordinate)(struct kndDataClass   *self);
    int (*resolve)(struct kndDataClass   *self);

    int (*export)(struct kndDataClass   *self,
                  knd_format format);

    /* traversal */
    void (*rewind)(struct kndDataClass   *self);
    int (*next_attr)(struct kndDataClass   *self,
                     struct kndAttr **result);
    
};

/* constructor */
extern int kndDataClass_new(struct kndDataClass **self);

#endif
