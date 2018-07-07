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

struct kndClass;
struct kndSet;
struct kndQuery;
struct kndOutput;

struct kndFacet
{
    knd_facet_type type;
    struct kndAttr *attr;
    struct kndSet *parent;

    struct ooDict *set_name_idx;
    size_t export_depth;
    size_t batch_size;
};

extern void kndFacet_init(struct kndFacet *self);
extern int kndFacet_new(struct kndFacet **self);
