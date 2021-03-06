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
 *   knd_shared_set.h
 *   Knowdy Set
 */
#pragma once

#include "knd_config.h"

typedef int (*elem_marshall_cb)(void *obj, size_t *buf_size, struct kndTask *task);
typedef int (*elem_unmarshall_cb)(const char *elem_id, size_t elem_id_size, const char *val, size_t val_size,
                                  void **result, struct kndTask *task);
typedef int (*map_cb_func)(void *obj, const char *elem_id, size_t elem_id_size, size_t count, void *elem);

struct kndSharedSet;
struct kndSharedSetFooter;

struct kndSharedSetDir
{
    size_t elem_block_sizes[KND_RADIX_BASE];
    struct kndSharedSetDir * _Atomic subdirs[KND_RADIX_BASE];

    size_t num_elems;
    size_t payload_block_size;
    size_t payload_footer_size;

    size_t num_subdirs;
    size_t subdir_block_size;

    size_t cell_max_val;
    size_t total_elems;

    size_t global_offset;
    size_t total_size;

    /* options */
    bool elems_linear_scan;
    bool subdirs_expanded;
};

struct kndSharedSetElemIdx
{
    void * _Atomic elems[KND_RADIX_BASE];
    struct kndSharedSetElemIdx * _Atomic idxs[KND_RADIX_BASE];
};

struct kndSharedSet
{
    char path[KND_PATH_SIZE];
    size_t path_size;

    struct kndSharedSetElemIdx * _Atomic idx;
    atomic_size_t num_elems;
    atomic_size_t num_valid_elems;

    struct kndSharedSetDir *dir;

    struct kndMemPool *mempool;
    bool allow_overwrite;
};

int knd_shared_set_new(struct kndMemPool *mempool, struct kndSharedSet **result);
int knd_shared_set_elem_idx_new(struct kndMemPool *mempool, struct kndSharedSetElemIdx **result);
int knd_shared_set_dir_new(struct kndMemPool *mempool, struct kndSharedSetDir **result);

int knd_shared_set_get(struct kndSharedSet *self, const char *key, size_t key_size, void **elem);
int knd_shared_set_add(struct kndSharedSet *self, const char *key, size_t key_size, void *elem);
int knd_shared_set_map(struct kndSharedSet *self, map_cb_func cb, void *obj);
int knd_shared_set_intersect(struct kndSharedSet *self, struct kndSharedSet **sets, size_t num_sets);

int knd_shared_set_marshall(struct kndSharedSet *self, const char *filename, size_t filename_size,
                            elem_marshall_cb cb, size_t *total_size, struct kndTask *task);
int knd_shared_set_unmarshall_file(struct kndSharedSet *self, const char *filename, size_t filename_size,
                                   size_t filesize, elem_unmarshall_cb cb, struct kndTask *task);
int knd_shared_set_unmarshall_elem(struct kndSharedSet *self, const char *id, size_t id_size,
                                   elem_unmarshall_cb cb, void **elem, struct kndTask *task);
