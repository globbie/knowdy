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
 *   knd_facet.h
 *   Knowdy Facet
 */

#ifndef KND_FACET_H
#define KND_FACET_H

#include "knd_utils.h"
#include "knd_objref.h"

typedef enum knd_facet_type { KND_FACET_UNREC,
                              KND_FACET_ATOMIC,
                              KND_FACET_CONC_BASE,
                              KND_FACET_CONC_SPEC,
                              KND_FACET_POSITIONAL,
                              KND_FACET_ACCUMULATED, 
                              KND_FACET_CATEGORICAL,
                              KND_FACET_TOPICAL } knd_facet_type;


static const char* const knd_facet_names[] = {
    "UNREC",
    "ATOMIC",
    "CONC_BASE", 
    "CONC_SPEC",
    "POS", 
    "ACC",
    "CAT",
    "TOPIC" };

struct kndObjRef;
struct kndConcept;
struct kndRefSet;
struct kndRepoCache;
struct kndQuery;
struct kndOutput;

struct kndFacet
{
    knd_facet_type type;

    char name[KND_NAME_SIZE];
    size_t name_size;

    char unit_name[KND_NAME_SIZE];
    size_t unit_name_size;

    size_t numval;
    
    struct kndConcept *baseclass;
    struct kndRepoCache *cache;
    
    struct kndObjRef *inbox[KND_MAX_INBOX_SIZE + 1];
    size_t inbox_size;

    struct kndOutput *out;
    
    size_t rec_size;

    /*size_t num_items;*/
    struct kndRefSet *parent;
    
    struct kndRefSet *refsets[KND_MAX_ATTRS];
    size_t num_refsets;

    size_t export_depth;
    size_t batch_size;

    const char *query;
    size_t query_size;
    
    /******** public methods ********/
    int (*init)(struct kndFacet *self);
    void (*del)(struct kndFacet *self);
    int (*str)(struct kndFacet *self,
               size_t           depth,
               size_t           max_depth);

    int (*find)(struct kndFacet   *self,
                const char        *val,
                size_t val_size,
                struct kndRefSet **result);

    int (*add_ref)(struct kndFacet *self,
                   struct kndObjRef *ref,
                   size_t attr_id,
                   knd_facet_type attr_type);
    
    int (*sync)(struct kndFacet *self);

    int (*extract_objs)(struct kndFacet *self);

    int (*read)(struct kndFacet   *self,
                const char        *rec,
                size_t            rec_size);
    
    int (*read_tags)(struct kndFacet  *self,
                     const char       *rec,
                     size_t           rec_size,
                     struct kndRefSet *refset);

    int (*export)(struct kndFacet *self,
                   knd_format format,
                   size_t depth);

};


/* constructor */
extern int kndFacet_new(struct kndFacet **self);

#endif /* KND_FACET_H */
