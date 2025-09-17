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
#include "knd_memblock.h"
#include "knd_set.h"
#include "knd_storage.h"
#include "knd_mempool.h"

struct kndRepoSnapshot;
struct kndTask;
struct kndSharedSet;
struct kndSharedSetDir;
struct kndSharedSetFooter;

struct kndSharedSetDirIdx {
    size_t elem_block_sizes[KND_RADIX_BASE];
    struct kndSharedSetDir * _Atomic subdirs[KND_RADIX_BASE];
};

struct kndSharedSetDir
{
    char id[KND_ID_SIZE];
    size_t id_size;

    struct kndSharedSetDirIdx *idx;

    size_t num_term_elems;
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
    atomic_size_t num_term_elems;
    atomic_size_t total_elems;
};

struct kndSharedSet
{
    struct kndSharedSetElemIdx * _Atomic idx;
    atomic_size_t num_elems;
    atomic_size_t num_valid_elems;

    struct kndSharedSetDir *dir;

    struct kndMemPool *mempool;
    struct kndMemBlock *blocks;
    size_t num_blocks;
    size_t total_block_size;

    char path[KND_PATH_SIZE + 1];
    size_t path_size;

    struct kndStorageLeaf *leaves;
    struct kndStorageLeaf *leaf_tail;
    size_t num_leaves;

    bool allow_overwrite;
};

int knd_shared_set_new(struct kndSharedSet **result, struct kndMemPool *mempool);
int knd_shared_set_elem_idx_new(struct kndSharedSetElemIdx **result, struct kndMemPool *mempool);
int knd_shared_set_dir_new(struct kndSharedSetDir **result, struct kndMemPool *mempool);

int knd_shared_set_get(struct kndSharedSet *self, const char *key, size_t key_size, void **elem);
int knd_shared_set_add(struct kndSharedSet *self, const char *key, size_t key_size, void *elem);
int knd_shared_set_map(struct kndSharedSet *self, map_cb_t cb, void *obj);
int knd_shared_set_intersect(struct kndSharedSet *self, struct kndSharedSet **sets, size_t num_sets);

/* GSP marshalling */
int knd_shared_set_leaf_marshall(struct kndSharedSet *self, struct kndStorageLeaf *leaf,
                                 struct kndSetRange *range,
                                 knd_set_elem_marshall_cb_t cb, void *cb_ctx, struct kndTask *task);

int knd_shared_set_marshall(struct kndSharedSet *idx, struct kndSetRange *range,
                            const char *path, size_t path_size,
                            knd_set_elem_marshall_cb_t cb, void *cb_ctx,
                            struct kndStorageLeaf **result, size_t *total_leaves,
                            struct kndTask *task);

int knd_shared_set_build_path(struct kndSharedSet *idx, const char *path, size_t path_size,
                              const char *pref, size_t pref_size, struct kndTask *task);

/* GSP reading */
int knd_shared_set_find_leaf(struct kndSharedSet *idx, const char *id, size_t id_size,
                             struct kndStorageLeaf **result, struct kndTask *task);
int knd_shared_set_leaf_open(struct kndSharedSet *self, struct kndStorageLeaf *leaf,
                             knd_set_elem_unmarshall_cb_t cb, struct kndTask *task);

int knd_shared_set_leaf_read_elem(struct kndStorageLeaf *leaf, struct kndSharedSetDir *dir,
                                  const char *id, size_t id_size,
                                  knd_set_elem_unmarshall_cb_t cb, void *ctx, void **result,
                                  struct kndTask *task);
