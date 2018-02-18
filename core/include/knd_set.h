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

#include "knd_config.h"

typedef enum knd_set_type { KND_SET_OBJ,
			    KND_SET_CLASS } knd_set_type;

struct kndSetElem
{
    char id[KND_ID_SIZE + 1];
    size_t id_size;

    struct kndConcDir *conc_dir;
    struct kndObjEntry *obj_entry;

    struct kndSetElem *next;
};

struct kndElemIdx
{
    struct kndElemIdx **idx;
    struct kndSetElem **elems;
    size_t num_elems;
};

struct kndSet
{
    knd_set_type type;

    char name[KND_NAME_SIZE];
    size_t name_size;
    struct kndConcept *base;
    bool is_terminal;

    struct kndElemIdx **idx;
    
    struct kndSetElem *inbox[KND_SET_INBOX_SIZE];
    size_t inbox_size;
    size_t max_inbox_size;
    size_t num_elems;

    struct kndFacet *parent_facet;
    struct kndFacet *facets[KND_MAX_ATTRS];
    size_t num_facets;
    
    struct kndSet *next;
    struct kndTask *task;
    struct kndOutput *out;
    knd_format format;
    
    /******** public methods ********/
    int (*init)(struct kndSet *self);
    void (*del)(struct kndSet *self);
    void (*str)(struct kndSet *self,
		size_t depth,
		size_t max_depth);

    int (*term_idx)(struct kndSet   *self,
                    struct kndSetElem *elem);

    int (*add_elem)(struct kndSet *self,
                   struct kndSetElem *elem);

    /*int (*lookup_name)(struct kndSet *self,
                       const char *name,
                       size_t name_size,
                       const char *remainder,
                       size_t remainder_size,
                       char *guid);
    */
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
                size_t         rec_size);

    int (*export)(struct kndSet *self,
		  knd_format format,
		  size_t depth);
};

extern int kndSet_init(struct kndSet *self);
extern int kndSet_new(struct kndSet **self);
