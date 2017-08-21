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
 *   knd_refset.h
 *   Knowdy RefSet
 */

#pragma once

struct kndObject;
struct kndObjRef;
struct kndElem;
struct kndElemRef;
struct kndDataClass;
struct kndQuery;
struct kndRepoCache;
struct kndOutput;

#include "knd_config.h"
#include "knd_facet.h"

struct kndTermIdx
{
    struct kndTermIdx **idx;
    struct kndObjRef **refs;
    size_t num_refs;
};


struct kndRefSet
{
    char name[KND_NAME_SIZE];
    size_t name_size;

    knd_facet_type type;

    size_t numval;
    bool is_terminal;

    struct kndTermIdx **idx;

    struct kndTrans **trns;
    size_t num_trns;
    size_t max_trns;
    
    size_t rec_size;
    
    struct kndRepoCache *cache;
    struct kndDataClass *baseclass;

    bool is_updated;
    bool tags_needed;
    
    struct kndObjRef *inbox[KND_MAX_INBOX_SIZE + 1];
    size_t inbox_size;
    size_t max_inbox_size;
    
    size_t num_refs;
    size_t num_trivia;
    
    const char *summaries;
    size_t summaries_size;
    
    struct kndOutput *out;
    
    struct kndObjRef *matches[KND_MAX_INBOX_SIZE + 1];
    size_t num_matches;
    
    struct kndFacet *parent;
    
    struct kndFacet *facets[KND_MAX_ATTRS];
    size_t num_facets;

    struct kndFacet *default_facet;

    size_t batch_size;
    size_t export_depth;

    const char *query;
    size_t query_size;
    
    struct kndRefSet *next;
    knd_format format;
    
    /******** public methods ********/
    int (*init)(struct kndRefSet *self);

    void (*del)(struct kndRefSet *self);
    int (*str)(struct kndRefSet *self,
               size_t depth,
               size_t max_depth);

    int (*term_idx)(struct kndRefSet   *self,
                    struct kndObjRef *ref);

    int (*add_ref)(struct kndRefSet *self,
                   struct kndObjRef *ref);

    int (*lookup_name)(struct kndRefSet *self,
                       const char *name,
                       size_t name_size,
                       const char *remainder,
                       size_t remainder_size,
                       char *guid);

    int (*lookup_ref)(struct kndRefSet *self,
                      struct kndObjRef *ref);

    int (*extract_objs)(struct kndRefSet *self);

    int (*export_summaries)(struct kndRefSet *self,
                            knd_format format,
                            size_t depth,
                            size_t num_items);

    int (*intersect)(struct kndRefSet *self,
                     struct kndRefSet **refsets,
                     size_t num_refsets);

    int (*contribute)(struct kndRefSet *self,
                      size_t seqnum);

    int (*facetize)(struct kndRefSet *self);

    int (*find)(struct kndRefSet *self,
                const char *facet_name,
                const char *value,
                size_t val_size,
                struct kndRefSet **result);
    
    int (*sync)(struct kndRefSet *self);
    int (*sync_objs)(struct kndRefSet *self,
                     const char *path);

    int (*read)(struct kndRefSet *self,
                const char       *rec,
                size_t           rec_size);

    int (*read_tags)(struct kndRefSet *self,
                     const char       *rec,
                     size_t           rec_size);

    int (*export)(struct kndRefSet *self,
                   knd_format format,
                   size_t depth);

};


/* constructor */
extern int kndRefSet_new(struct kndRefSet **self);
