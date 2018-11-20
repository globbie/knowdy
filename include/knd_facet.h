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

#include "knd_utils.h"
struct kndAttr;
struct kndMemPool;

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

struct kndSet;

struct kndFacet
{
    knd_facet_type type;
    struct kndAttr *attr;
    struct kndSet *parent;

    struct kndSet *set_idx;
    struct kndFacet *children;
    struct kndFacet *next;
};

extern void kndFacet_init(struct kndFacet *self);
extern int kndFacet_new(struct kndFacet **self);
extern int knd_facet_new(struct kndMemPool *mempool,
                         struct kndFacet **result);
