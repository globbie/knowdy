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
#include "knd_storage.h"
#include "knd_task.h"

typedef enum knd_set_store_t { KND_SET_STORE_DEFAULT,
                               KND_SET_STORE_MEMONLY,
                               KND_SET_STORE_PERSIST } knd_set_store_t;

typedef enum knd_set_cardinal_t { KND_SET_UNIQUE_VALUES,
                                  KND_SET_MULTIPLE_VALUES } knd_set_cardinal_t;

typedef enum knd_set_elem_format_t { KND_SET_ELEM_STR,
                                     KND_SET_ELEM_BIN } knd_set_elem_format_t;

typedef enum knd_set_dir_t { KND_SET_DIR_FIXED,
                             KND_SET_DIR_VAR } knd_set_dir_t;

typedef int (*filter_cb_t)(void *elem, void *ctx);
typedef int (*map_cb_t)(void *elem, void *ctx, struct kndTask *task);
typedef int (*reduce_cb_t)(void *elem, void *ctx);
typedef int (*compare_cb_t)(void *elem, void *ctx);

typedef int (*knd_set_elem_marshall_cb_t)(void *elem, void *ctx, struct kndStorageLeaf *leaf,
                                          size_t *output_size, struct kndTask *task);
typedef int (*knd_set_elem_unmarshall_cb_t)(const char *elem_id, size_t elem_id_size,
                                            const char *rec, size_t rec_size,
                                            void *ctx, size_t *result_size,
                                            void **result, struct kndTask *task);

struct kndSetDirEntry
{
    knd_set_dir_t type;

    struct kndSetDir *subdir;
    size_t offset;
    void *payload;
    size_t payload_size;
    size_t payload_offset;
};

struct kndSetDirBlock
{
    size_t offset;
    size_t size;

    /* effective range */
    unsigned int from_elem;
    unsigned int to_elem;
    unsigned int from_dir;
    unsigned int to_dir;

    size_t num_elems;
    size_t total_elems;

    size_t max_elem_size;
    size_t elems_rec_size;
    size_t elems_footer_size;

    size_t num_subdirs;
    size_t max_subdir_size;
    size_t subdirs_rec_size;
    size_t subdirs_footer_size;

    bool use_elem_keys;
    bool use_dir_keys;
    struct kndSetDirBlock *next;
};

struct kndSetDir
{
    char id[KND_ID_SIZE];
    size_t id_size;

    struct kndSetDir *subdirs[KND_RADIX_BASE];
    struct kndSetElem *elems[KND_RADIX_BASE];

    size_t total_elems;

    struct kndSetDirBlock *blocks;
    //size_t num_blocks;
};

struct kndSetElem
{
    char id[KND_ID_SIZE];
    size_t id_size;

    void *val;
    size_t numval;

    size_t size;

    struct kndSetElem *next;
};

struct kndSetRange
{
    size_t num_elems;

    char from_id[KND_ID_SIZE];
    size_t from_id_size;

    char to_id[KND_ID_SIZE];
    size_t to_id_size;
};

struct kndSetStore
{
    struct kndStorageLeaf *leaves[KND_MAX_STORAGE_LEAVES];
    size_t num_leaves;
};

struct kndSet
{
    knd_set_store_t store_t;
    knd_set_cardinal_t cardinal_t;
    knd_set_elem_format_t format_t;

    knd_set_elem_unmarshall_cb_t elem_unmarshall_cb;
    void *elem_unmarshall_ctx;

    struct kndSetDir *dir;
    size_t num_elems;

    const char *path;
    size_t path_size;

    struct kndSetStore *store;
    struct kndMemPool *mempool;
};

int knd_set_new(struct kndSet **result, knd_set_store_t store_t, struct kndMemPool *mempool);

int knd_set_store_new(struct kndSetStore **result, struct kndMemPool *mempool);
int knd_set_elem_new(struct kndSetElem **result, struct kndMemPool *mempool);
int knd_set_range_new(struct kndSetRange **result, struct kndMemPool *mempool);
int knd_set_dir_new(struct kndSetDir **result, const char *parent_id, size_t parent_id_size,
                    const char *curr_id, struct kndMemPool *mempool);
int knd_set_dir_block_new(struct kndSetDirBlock **result, struct kndMemPool *mempool);

int knd_set_add(struct kndSet *self, const char *key, size_t key_size, void *elem, struct kndTask *task);
int knd_set_get(struct kndSet *set, const char *key, size_t key_size, void **elem, struct kndTask *task);

int knd_set_filter(struct kndSet *set, filter_cb_t filter_cb, void *filter_ctx, struct kndSet **result);
int knd_set_map(struct kndSet *set, struct kndSetRange *range,
                filter_cb_t filter_cb, void *filter_ctx,
                map_cb_t map_cb, void *map_ctx, struct kndTask *task);
int knd_set_reduce(struct kndSet *set, struct kndSetRange *range,
                   map_cb_t reduce_cb, void *reduce_ctx);

int knd_set_intersect(struct kndSet **sets, size_t num_sets,
                      struct kndSetRange *range, struct kndSet **result,
                      struct kndTask *task);

int knd_set_sort(struct kndSet *set, struct kndSetRange *range, compare_cb_t cb,
                 struct kndSet **result, struct kndTask *task);

/* GSP marshalling */
int knd_set_marshall(struct kndSet *s, struct kndSetRange *range, const char *path, size_t path_size,
                     knd_set_elem_marshall_cb_t cb, void *cb_ctx, struct kndStorage *store,
                     struct kndStorageLeaf **leaves, size_t *num_leaves, struct kndTask *task);

int knd_set_leaf_marshall(struct kndSet *s, struct kndSetRange *range, struct kndStorageLeaf *leaf,
                          knd_set_elem_marshall_cb_t cb, void *cb_ctx, struct kndTask *task);

int knd_set_build_path(struct kndSet *idx, const char *path, size_t path_size,
                       const char *pref, size_t pref_size, struct kndTask *task);
int knd_set_read_leaf(struct kndSet *idx, struct kndStorageLeaf *leaf, struct kndSetRange *range,
                      knd_set_elem_unmarshall_cb_t cb, void *cb_ctx, struct kndTask *task);

int knd_set_fetch_elem(struct kndSet *s, const char *key, size_t key_size,
                       knd_set_elem_unmarshall_cb_t cb, void *cb_ctx, void **result, struct kndTask *task);
