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
 *   knd_set.h
 *   Knowdy Set
 */

#pragma once

#include "knd_config.h"

typedef enum knd_set_type { KND_SET_UNIQUE_VALUES,
			    KND_SET_MULTIPLE_VALUES } knd_set_type;

typedef enum knd_set_dir_type { KND_SET_DIR_FIXED,
                                KND_SET_DIR_VAR } knd_set_dir_type;

struct kndSet;

typedef int (*map_cb_func)(void *elem, void *ctx);

struct kndSetDirEntry
{
    knd_set_dir_type type;

    struct kndSetDir *subdir;
    size_t offset;
    void *payload;
    size_t payload_size;
    size_t payload_offset;
};

struct kndSetDir
{
    knd_set_dir_type type;
    size_t total_size;
    size_t total_elems;

    size_t dir_entry_offset_size;
    size_t num_elems;
    size_t num_subdirs;

    struct kndSetDirEntry entries[KND_RADIX_BASE];
};

struct kndSetElemIdx
{
    struct kndSetElemIdx *idxs[KND_RADIX_BASE];
    void *elems[KND_RADIX_BASE];
};

struct kndSetElem
{
    void *val;
    struct kndSetElem *next;
};

struct kndSet
{
    knd_set_type type;

    struct kndSetElemIdx *idx;
    size_t num_elems;
    
    struct kndMemPool *mempool;
};

int knd_set_new(struct kndSet **result, knd_set_type type, struct kndMemPool *mempool);
int knd_set_elem_new(struct kndSetElem **result, struct kndMemPool *mempool);
int knd_set_elem_idx_new(struct kndSetElemIdx **result, struct kndMemPool *mempool);

int knd_set_add(struct kndSet *self, const char *key, size_t key_size, void *elem);
int knd_set_map(struct kndSet *self, map_cb_func cb, void *ctx);
int knd_set_get(struct kndSet *self, const char *key, size_t key_size, void **elem);
int knd_set_intersect(struct kndSet *self, struct kndSet **sets, size_t num_sets);
