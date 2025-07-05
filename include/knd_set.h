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
#include "knd_task.h"

typedef enum knd_set_type { KND_SET_UNIQUE_VALUES,
			    KND_SET_MULTIPLE_VALUES } knd_set_type;

typedef enum knd_set_dir_type { KND_SET_DIR_FIXED,
                                KND_SET_DIR_VAR } knd_set_dir_type;

struct kndSet;

typedef int (*filter_cb_t)(void *elem, void *ctx);
typedef int (*map_cb_t)(void *elem, void *ctx);
typedef int (*reduce_cb_t)(void *elem, void *ctx);
typedef int (*compare_cb_t)(void *elem, void *ctx);

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
    size_t numval;
    struct kndSetElem *next;
};

struct kndSetRange
{
    const char *key_from;
    size_t key_from_size;
    const char *key_to;
    size_t key_to_size;
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
int knd_set_range_new(struct kndSetRange **result, struct kndMemPool *mempool);
int knd_set_elem_idx_new(struct kndSetElemIdx **result, struct kndMemPool *mempool);

int knd_set_add(struct kndSet *set, const char *key, size_t key_size, void *elem);
int knd_set_get(struct kndSet *set, const char *key, size_t key_size, void **elem);

int knd_set_filter(struct kndSet *set,
                   filter_cb_t filter_cb, void *filter_ctx,
                   struct kndSet **result);
int knd_set_map(struct kndSet *set, struct kndSetRange *range,
                filter_cb_t filter_cb, void *filter_ctx,
                map_cb_t map_cb, void *map_ctx);
int knd_set_reduce(struct kndSet *set, struct kndSetRange *range,
                   map_cb_t reduce_cb, void *reduce_ctx);

int knd_set_intersect(struct kndSet **sets, size_t num_sets,
                      struct kndSetRange *range, struct kndSet **result,
                      struct kndTask *task);

int knd_set_sort(struct kndSet *set, struct kndSetRange *range, compare_cb_t cb,
                 struct kndSet **result, struct kndTask *task);
