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

struct kndDataElem
{
    char name[KND_NAME_SIZE];
    size_t name_size;

    char attr_name[KND_NAME_SIZE];
    size_t attr_name_size;
    struct kndAttr *attr;

    char idx_name[KND_NAME_SIZE];
    size_t idx_name_size;

    struct kndDataClass *dc;

    bool is_list;
    bool is_recursive;
    bool for_summary;
    bool is_optional;
    
    struct kndDataElem *elems;
    struct kndDataElem *tail;
    size_t num_elems;

    
    struct kndDataElem *next;
};

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

    /* ontology file location */
    char *path;
    size_t path_size;

    char baseclass_name[KND_NAME_SIZE];
    size_t baseclass_name_size;
    struct kndDataClass *baseclass;
    
    struct kndTranslation *tr;
    
    struct kndDataIdx *indices;
    size_t num_indices;

    /*struct kndDataWriter *writer;
    struct kndDataReader *reader;
    */
    
    struct kndDataElem *elems;
    struct kndDataElem *tail;
    size_t num_elems;

    struct kndDataElem *curr_elem;
    size_t elems_left;

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
    int (*init)(struct kndDataClass  *self);
    int (*del)(struct kndDataClass   *self);
    int (*str)(struct kndDataClass *self,
               size_t depth);

    int (*read)(struct kndDataClass   *self,
                const char *rec,
                size_t *chunk_size);

    int (*read_onto)(struct kndDataClass   *self,
                     const char *filename);

    int (*coordinate)(struct kndDataClass   *self);
    int (*resolve)(struct kndDataClass   *self);

    int (*export)(struct kndDataClass   *self,
                  knd_format format);
    
    void (*rewind)(struct kndDataClass   *self);
    int (*next_elem)(struct kndDataClass   *self,
                     struct kndDataElem **result);
    
};

extern int kndDataClass_init(struct kndDataClass *self); 

/* constructor */
extern int kndDataClass_new(struct kndDataClass **self);

#endif
