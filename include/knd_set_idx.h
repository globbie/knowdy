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
 *   knd_set_idx.h
 *   Knowdy Set Index
 */

#pragma once

#include "knd_config.h"

#include <stdint.h>
#include <string.h>
#include <limits.h>

// Knowdy Set Key (string of 0-9A-Za-z)
_Static_assert(KND_RADIX_BASE <= CHAR_BIT * sizeof(uint64_t), "KND_RADIX_BASE is too big");  // requried for knd_set_idx_key_bit()

struct kndSetIdxFolder
{
    uint64_t elems_mask;
    uint64_t folders_mask;
    struct kndSetIdxFolder *folders[KND_RADIX_BASE];
    // void *elems[];  Can be optional
};

struct kndSetIdx
{
    struct kndSetIdxFolder root;
};

// public:
extern int knd_set_idx_add(struct kndSetIdx *self, const char *key, size_t key_size);
extern bool knd_set_idx_exist(struct kndSetIdx *self, const char *key, size_t key_size);

// knd_set_idx_new() -- Create a new empty set
//
// After that you can use it in knd_set_idx_add(), knd_set_idx_exist(), or as one of the operands
// of knd_set_idx_new_result_of_intersect().
extern int knd_set_idx_new(struct kndSetIdx **out);

// knd_set_idx_new() -- Create a new set, the result of intersection.
//
// After that you can use it in knd_set_idx_add(), knd_set_idx_exist(), or as one of the operands
// of knd_set_idx_new_result_of_intersect().
//
// WARNING: In case of errors, it can also return a valid set object.  Probably we can free |*out| object to
//          avoid memory leaks but for now you MUST check |*out| all the time!!
//
extern int knd_set_idx_new_result_of_intersect(struct kndSetIdx **out, struct kndSetIdx **idxs, size_t num_idxs);
