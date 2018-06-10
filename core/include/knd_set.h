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
 *   knd_set.h
 *   Knowdy Set
 */

#pragma once

struct glbOutput;
struct kndClass;
struct kndConcDir;
struct kndObjEntry;
struct kndTask;
struct kndAttr;
struct kndFacet;
struct ooDict;

#include "knd_config.h"

typedef enum knd_set_type { KND_SET_CLASS,
			    KND_SET_OBJ,
			    KND_SET_REL,
			    KND_SET_REL_INST } knd_set_type;

struct kndSet;

typedef int (*map_cb_func)(void *obj, const char *elem_id, size_t elem_id_size,
                           size_t count, void *elem);

struct kndSetElemIdx
{
    struct kndSetElemIdx *idxs[KND_RADIX_BASE];
    void *elems[KND_RADIX_BASE];
    size_t num_elems;
};

struct kndSet
{
    knd_set_type type;
    struct kndConcDir *base;
    bool is_terminal;

    struct kndSetElemIdx idx;
    size_t num_elems;

    struct kndFacet *parent_facet;
    struct kndFacet *facets[KND_MAX_ATTRS];
    size_t num_facets;
    
    struct kndMemPool *mempool;
    struct kndSet *next;

    /******** public methods ********/
    void (*str)(struct kndSet *self,
		size_t depth);
    int (*add)(struct kndSet *self,
               const char *key,
               size_t key_size,
               void *elem);
    int (*get)(struct kndSet *self,
               const char *key,
               size_t key_size,
               void **elem);
    int (*map)(struct kndSet *self,
               map_cb_func cb,
               void *obj);
    int (*add_ref)(struct kndSet *self,
		   struct kndAttr *attr,
		   struct kndConcDir *topic,
		   struct kndConcDir *spec);
    int (*get_facet)(struct kndSet *self,
		     struct kndAttr *attr,
		     struct kndFacet **facet);

    int (*intersect)(struct kndSet *self,
                     struct kndSet **sets,
                     size_t num_sets);

    int (*facetize)(struct kndSet *self);

    int (*export)(struct kndSet *self);
};

extern int kndSet_init(struct kndSet *self);
extern int kndSet_new(struct kndSet **self);
