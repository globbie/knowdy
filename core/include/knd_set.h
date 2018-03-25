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
struct kndConcept;
struct kndConcDir;
struct kndObjEntry;
struct kndTask;
struct kndFacet;
struct ooDict;

#include "knd_config.h"

typedef enum knd_set_type { KND_SET_CLASS,
			    KND_SET_OBJ,
			    KND_SET_REL,
			    KND_SET_REL_INST } knd_set_type;

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

    struct ooDict *name_idx;
    struct kndSetElemIdx *idx;
    size_t num_elems;

    struct kndFacet *parent_facet;
    struct kndFacet *facets[KND_MAX_ATTRS];
    size_t num_facets;
    
    knd_format format;
    struct kndTask *task;
    struct glbOutput *out;
    struct kndMemPool *mempool;

    struct kndSet *next;

    /******** public methods ********/
    void (*str)(struct kndSet *self,
		size_t depth);
    int (*add_conc)(struct kndSet *self,
		    struct kndConcept *conc);
    int (*add_ref)(struct kndSet *self,
		   struct kndAttr *attr,
		   struct kndConcept *topic,
		   struct kndConcept *spec);
    int (*get_facet)(struct kndSet *self,
		     struct kndAttr *attr,
		     struct kndFacet **facet);

    int (*intersect)(struct kndSet *self,
                     struct kndSet **sets,
                     size_t num_sets);

    int (*facetize)(struct kndSet *self);

    int (*read)(struct kndSet *self,
                const char    *rec,
                size_t         *rec_size);

    int (*export)(struct kndSet *self);
};

extern int kndSet_init(struct kndSet *self);
extern int kndSet_new(struct kndSet **self);
