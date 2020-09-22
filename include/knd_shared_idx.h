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
 *   knd_shared_idx.h
 *   Knowdy Shared Index
 */

#pragma once

#include "knd_config.h"

#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <stdatomic.h>

// Knowdy Set Key (string of 0-9A-Za-z)
_Static_assert(KND_RADIX_BASE <= CHAR_BIT * sizeof(atomic_uint_least64_t), "KND_RADIX_BASE is too big");  // required for knd_shared_idx_key_bit()

struct kndSharedIdxFolder
{
    atomic_uint_least64_t elems_mask;
    atomic_uint_least64_t folders_mask;
    struct kndSharedIdxFolder * _Atomic folders[KND_RADIX_BASE];
};

struct kndSharedIdx
{
    struct kndSharedIdxFolder root;
    atomic_size_t num_elems;
};

int  knd_shared_idx_new(struct kndSharedIdx **self);
int  knd_shared_idx_add(struct kndSharedIdx *self, const char *key, size_t key_size);
bool knd_shared_idx_exists(struct kndSharedIdx *self, const char *key, size_t key_size);
void knd_shared_idx_del(struct kndSharedIdx *self);

// WARNING: In case of errors, it can also return a valid set object.  Probably we can free |*out| object to
//          avoid memory leaks but for now you MUST check |*out| all the time!!
//
int knd_shared_idx_intersect(struct kndSharedIdx **result, struct kndSharedIdx **idxs, size_t num_idxs);
