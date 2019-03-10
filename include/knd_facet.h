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
struct kndSet;

struct kndFacet
{
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
