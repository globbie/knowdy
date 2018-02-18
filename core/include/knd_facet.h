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
 *   knd_facet.h
 *   Knowdy Facet
 */

#pragma once

#include "knd_utils.h"

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

struct kndConcept;
struct kndSet;
struct kndQuery;
struct kndOutput;
struct kndSetElem;

struct kndFacet
{
    knd_facet_type type;

    struct kndAttr *attr;

    char name[KND_NAME_SIZE];
    size_t name_size;

    size_t numval;
    
    struct kndSetElem *inbox[KND_SET_INBOX_SIZE];
    size_t inbox_size;

    struct kndOutput *out;
    size_t rec_size;

    /*size_t num_items;*/
    struct kndSet *parent;

    struct kndSet **sets;
    size_t num_sets;

    size_t export_depth;
    size_t batch_size;
    
    /******** public methods ********/
    int  (*init)(struct kndFacet *self);
    void (*del)(struct kndFacet *self);
    void (*str)(struct kndFacet *self,
                size_t           depth,
                size_t           max_depth);

    int (*find)(struct kndFacet   *self,
                const char        *val,
                size_t val_size,
                struct kndSet **result);

    int (*add_elem)(struct kndFacet *self,
                   struct kndSetElem *elem,
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
                     struct kndSet *set);

    int (*export)(struct kndFacet *self,
                   knd_format format,
                   size_t depth);
};

extern void kndFacet_init(struct kndFacet *self);
extern int kndFacet_new(struct kndFacet **self);
