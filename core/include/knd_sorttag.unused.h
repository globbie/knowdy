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
 *   knd_sorttag.h
 *   Knowdy Sorting Tag
 */
#ifndef KND_SORTTAG_H
#define KND_SORTTAG_H

#include "knd_config.h"
#include "knd_facet.h"

struct kndConcept;
struct kndOutput;

struct kndSortAttr
{
    knd_facet_type type;
    bool is_default;
    bool skip;
    
    struct kndDataIdx *idx;
    
    char name[KND_NAME_SIZE + 1];
    size_t name_size;

    char val[KND_NAME_SIZE + 1];
    size_t val_size;

    unsigned int numval;

    bool is_trivia;
    
    /* linear hilite */
    size_t start_pos;
    size_t len;
    
    /* nested attr */
    struct kndSortAttr *children;
    size_t num_children;
    
    struct kndSortAttr *next;
};

struct kndSortTag
{
    char name[KND_NAME_SIZE + 1];
    size_t name_size;
    
    struct kndSortAttr *default_attr;
    
    struct kndSortAttr *attrs[KND_MAX_ATTRS + 1];
    size_t num_attrs;

    struct kndOutput *out;
    
    /******** public methods ********/
    int (*init)(struct kndSortTag *self);
    void (*del)(struct kndSortTag *self);
    int (*str)(struct kndSortTag *self);

    int (*export)(struct kndSortTag *self,
                  knd_format format);

    int (*reset)(struct kndSortTag *self);

    int (*import)(struct kndSortTag *self,
                  struct kndConcept *dc,
                  char *rec);
};


extern int 
knd_compare_attr_ascend(const void *a,
                        const void *b);

extern int
knd_compare_attr_descend(const void *a,
                         const void *b);

/* constructor */
extern int kndSortTag_new(struct kndSortTag **self);

#endif /* KND_SORTTAG_H */
