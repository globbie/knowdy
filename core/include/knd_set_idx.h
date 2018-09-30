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

_Static_assert(KND_RADIX_BASE == 62);

struct kndSetIdxFolder
{
    uint64_t elems_idx;
    uint64_t folders_idx;
    // void *(*elems)[KND_RADIX_BASE];  Can be optional
    struct kndSetIdxFolder *folders[KND_RADIX_BASE];
};

extern inline void knd_set_idx_folder_init(struct kndSetIdxFolder *self);
extern inline void knd_set_idx_folder_mark_elem(struct kndSetIdxFolder *self, uint8_t elem_bit);
extern inline bool knd_set_idx_folder_test_elem(struct kndSetIdxFolder *self, uint8_t elem_bit);
extern inline void knd_set_idx_folder_mark_folder(struct kndSetIdxFolder *self, uint8_t folder_bit, struct kndSetIdxFolder *folder);

struct kndSetIdx
{
    struct kndSetIdxFolder root;
};

extern inline void knd_set_idx_init(struct kndSetIdx *self);

extern int knd_set_idx_add(struct kndSetIdx *self, const char *key, size_t key_size);
extern bool knd_set_idx_exist(struct kndSetIdx *self, const char *key, size_t key_size);

extern int knd_set_idx_intersect(struct kndSetIdx *self, struct kndSetIdx *other, struct kndSetIdx *out_result);
