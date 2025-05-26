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
#include "knd_mempool.h"

struct kndRepoSnapshot;
struct kndTask;
struct kndSharedSet;
struct kndSharedSetDir;
struct kndSharedSetFooter;

typedef int (*elem_marshall_cb)(void *obj, size_t *buf_size, struct kndTask *task);
typedef int (*elem_unmarshall_cb)(const char *elem_id, size_t elem_id_size,
                                  const char *rec, size_t rec_size,
                                  void *ctx, void **result, struct kndTask *task);
typedef int (*leaf_unmarshall_cb)(const char *elem_id, size_t elem_id_size,
                                  const char *rec, size_t rec_size, struct kndTask *task);

struct kndSharedSetDirIdx {
    size_t elem_block_sizes[KND_RADIX_BASE];
    struct kndSharedSetDir * _Atomic subdirs[KND_RADIX_BASE];
};

struct kndStorageLeaf
{
    size_t numid;
    size_t min_leaf_size;
    size_t max_leaf_size;

    struct kndSharedSet *parent;
    struct kndSharedSetDir *dir;

    size_t num_elems;

    char range_from_id[KND_ID_SIZE];
    size_t range_from_id_size;
    size_t range_from;

    char range_to_id[KND_ID_SIZE];
    size_t range_to_id_size;
    size_t range_to;

    char name[KND_SHORT_NAME_SIZE + 1];
    size_t name_size;

    char filepath[KND_PATH_SIZE + 1];
    size_t filepath_size;
    size_t file_size;

    char file_hash[KND_HASH_SIZE];
    size_t file_hash_size;

    struct kndStorageLeaf *next;
    struct kndStorageLeaf *tail;
    size_t num_leaves;
};

struct kndSharedSetDir
{
    char id[KND_ID_SIZE];
    size_t id_size;

    struct kndStorageLeaf *leaf;
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
    struct kndStorageLeaf *tail;
    size_t num_leaves;

    bool allow_overwrite;
};

int knd_shared_set_new(struct kndSharedSet **result, struct kndMemPool *mempool);
int knd_shared_set_elem_idx_new(struct kndSharedSetElemIdx **result, struct kndMemPool *mempool);
int knd_shared_set_dir_new(struct kndSharedSetDir **result, struct kndMemPool *mempool);

int knd_shared_set_get(struct kndSharedSet *self, const char *key, size_t key_size, void **elem);
int knd_shared_set_add(struct kndSharedSet *self, const char *key, size_t key_size, void *elem);
int knd_shared_set_map(struct kndSharedSet *self, map_cb_func cb, void *obj);
int knd_shared_set_intersect(struct kndSharedSet *self, struct kndSharedSet **sets, size_t num_sets);

int knd_shared_set_marshall(struct kndSharedSet *idx, const char *path, size_t path_size,
                            const char *pref, size_t pref_size,
                            elem_marshall_cb cb, struct kndSharedSet *result_idx,
                            struct kndTask *task);

int knd_idx_build_path(struct kndSharedSet *idx,
                       const char *snapshot_path, size_t snapshot_path_size,
                       const char *pref, size_t pref_size, struct kndTask *task);

int knd_shared_set_find_leaf(struct kndSharedSet *class_idx, const char *id, size_t id_size,
                             struct kndStorageLeaf **result, struct kndTask *task);
int knd_storage_leaf_open(struct kndSharedSet *self, struct kndStorageLeaf *leaf,
                          leaf_unmarshall_cb cb, struct kndTask *task);

int knd_storage_leaf_read_elem(struct kndStorageLeaf *leaf, const char *id, size_t id_size,
                               elem_unmarshall_cb cb, void *ctx, void **result, struct kndTask *task);

int knd_storage_leaf_new(struct kndStorageLeaf **result, size_t numid, struct kndSharedSet *idx);
