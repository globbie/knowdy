/**
 *   Copyright (c) 2011-present by Dmitri Dmitriev
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

#include "knd_config.h"

struct kndSet;
struct kndMemPool;

static const char* const knd_facet_types[] = {
    "Subclass",
    "Sequence Size",
    "Accumulation"
};

typedef enum knd_attr_facet_type {
    KND_ATTR_FACET_SUBCLASS,
    KND_ATTR_FACET_SEQ_SIZE,
    KND_ATTR_FACET_ACCUM
} knd_attr_facet_type;

struct kndAttrFacetElem
{
    char id[KND_ID_SIZE];
    struct kndClassEntry *entry;
    struct kndAttrStm *stm;
};

struct kndAttrFacetElemIdx
{
    void *cache[KND_FACET_MAX_ELEM_CACHE];
    struct kndSet *idx;
};

struct kndAttrFacet
{
    knd_attr_facet_type type;
    void *val;

    size_t depth;

    struct kndAttrFacetElemIdx *elems;
    size_t num_elems;

    struct kndAttrFacet *children[KND_MAX_FACETS];
    size_t num_children;

    struct kndAttrFacet *next;
};

int knd_attr_facet_new(struct kndAttrFacet **result, knd_attr_facet_type type, struct kndMemPool *mempool);
int knd_attr_facet_elem_new(struct kndAttrFacetElem **result, struct kndMemPool *mempool);
