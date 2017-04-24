/**
 *   Copyright (c) 2011-2015 by Dmitri Dmitriev
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
 *   knd_coderef.h
 *   Knowdy Code Reference
 */

#ifndef KND_CODEREF_H
#define KND_CODEREF_H

#include "knd_config.h"

struct kndOutput;

struct kndCodeRef
{
    knd_elem_type type;
    
    char name[KND_NAME_SIZE];
    size_t name_size;

    char classname[KND_NAME_SIZE];
    size_t classname_size;

    char baseclass[KND_NAME_SIZE];
    size_t baseclass_size;

    char concpath[KND_NAME_SIZE];
    size_t concpath_size;
    
    char spec_role_name[KND_NAME_SIZE];
    size_t spec_role_name_size;
    struct kndCodeRef *spec;

    struct kndConc *conc;
    struct kndOutput *out;
    
    /* instance id */
    size_t linear_pos;
    size_t linear_len;

    struct kndCodeRef *next;

    /******** public methods ********/

    int (*init)(struct kndCodeRef *self);
    int (*del)(struct kndCodeRef *self);
    int (*reset)(struct kndCodeRef *self);
    int (*str)(struct kndCodeRef *self, size_t depth);

    int (*export)(struct kndCodeRef *self,
                   size_t depth,
                   knd_format format);

    int (*parse)(struct kndCodeRef *self,
                      const char *rec,
                      size_t rec_size);
    
    /*int (*parse_ter)(struct kndCodeRef *self,
                 char *rec,
                 size_t rec_size);
    */
    
    int (*sync)(struct kndCodeRef *self);

    int (*intersect)(struct kndCodeRef *self);
};

/* interfaces */
extern int kndCodeRef_init(struct kndCodeRef *self);
extern int kndCodeRef_new(struct kndCodeRef **self);

#endif /* KND_CODEREF_H */
