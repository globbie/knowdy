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

struct kndOutput;
struct kndConcept;
struct kndConcDir;
struct kndObjEntry;
struct kndTask;
struct kndFacet;
struct ooDict;

#include "knd_config.h"

typedef enum knd_set_type { KND_SET_OBJ,
			    KND_SET_CLASS } knd_set_type;

struct kndSetElem
{
    struct kndConcDir  *conc_dir;
    struct kndObjEntry *obj_entry;
};

struct kndSetElemIdx
{
    struct kndSetElemIdx *idxs[KND_RADIX_BASE];
    struct kndSetElem *elems[KND_RADIX_BASE];
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
    
    struct kndSet *next;
    struct kndTask *task;
    struct kndOutput *out;
    struct kndMemPool *mempool;
    knd_format format;

    /******** public methods ********/
    void (*str)(struct kndSet *self,
		size_t depth);
    int (*add_conc)(struct kndSet *self,
		    struct kndConcept *conc);
    int (*add_ref)(struct kndSet *self,
		   struct kndAttr *attr,
		   struct kndConcept *topic,
		   struct kndConcept *spec);
    int (*lookup)(struct kndSet *self,
		  struct kndSetElem *elem);

    int (*intersect)(struct kndSet *self,
                     struct kndSet **sets,
                     size_t num_sets);

    int (*contribute)(struct kndSet *self,
		      size_t seqnum);

    int (*facetize)(struct kndSet *self);

    int (*find)(struct kndSet *self,
                const char *facet_name,
                const char *value,
                size_t val_size,
                struct kndSet **result);

    int (*sync)(struct kndSet *self);

    int (*read)(struct kndSet *self,
                const char    *rec,
                size_t         *rec_size);

    int (*export)(struct kndSet *self);
};

extern int kndSet_init(struct kndSet *self);
extern int kndSet_new(struct kndSet **self);
