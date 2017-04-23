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
 *   Knowdy Attr
 */

#ifndef KND_ATTR_H
#define KND_ATTR_H

#include "oodict.h"
#include "knd_utils.h"
#include "../knd_config.h"

struct kndDataWriter;
struct kndDataReader;
struct kndDataClass;
struct kndOutput;
struct kndTranslation;

struct kndAttr 
{
    knd_elem_type type;

    char name[KND_NAME_SIZE];
    size_t name_size;

    char classname[KND_NAME_SIZE];
    size_t classname_size;
    struct kndDataClass *dataclass;

    int concise_level;
    int descr_level;
    int browse_level;
    
    char calc_oper[KND_NAME_SIZE];
    size_t calc_oper_size;

    char calc_attr[KND_NAME_SIZE];
    size_t calc_attr_size;

    char default_val[KND_NAME_SIZE];
    size_t default_val_size;

    char idx_name[KND_NAME_SIZE];
    size_t idx_name_size;

    struct ooDict *class_idx;
    
    struct kndDataWriter *writer;
    struct kndDataReader *reader;

    struct kndTranslation *tr;
    
    /***********  public methods ***********/
    int (*init)(struct kndAttr  *self);
    int (*del)(struct kndAttr   *self);
    int (*str)(struct kndAttr *self,
               size_t depth);

    int (*read)(struct kndAttr *self,
                const char     *rec,
                size_t *chunk_size);

    int (*resolve)(struct kndAttr *self);

    int (*present)(struct kndAttr   *self,
                   knd_format format);
};

/* constructor */
extern int kndAttr_new(struct kndAttr **self);
#endif
