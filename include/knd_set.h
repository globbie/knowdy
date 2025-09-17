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

typedef enum knd_set_type { KND_SET_UNIQUE_VALUES,
			    KND_SET_MULTIPLE_VALUES } knd_set_type;

typedef enum knd_set_dir_type { KND_SET_DIR_FIXED,
                                KND_SET_DIR_VAR } knd_set_dir_type;


struct kndSetDirEntry
{
    knd_set_dir_type type;

    struct kndSetDir *subdir;
    size_t offset;
    void *payload;
    size_t payload_size;
    size_t payload_offset;
};

struct kndSetDirBlock
{
    char id[KND_ID_SIZE];
    size_t id_size;

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

struct kndSetDir
{
    struct kndSetDir *subdirs[KND_RADIX_BASE];
    void *elems[KND_RADIX_BASE];
    struct kndSetBlock *block;
};

struct kndSetElem
{
    void *val;
    size_t numval;
    struct kndSetElem *next;
};

struct kndSetRange
{
    size_t num_elems;

    char from_addr[KND_PATH_SIZE + 1];
    const char *curr_from_addr;
    size_t from_addr_size;

    char to_addr[KND_PATH_SIZE + 1];
    const char *curr_to_addr;
    size_t to_addr_size;
};

struct kndSet
{
    knd_set_type type;

    struct kndSetDir *dir;
    size_t num_elems;

    struct kndStorageLeaf *leaves;
    struct kndStorageLeaf *leaf_tail;
    size_t num_leaves;
    
    struct kndMemPool *mempool;
};

typedef int (*filter_cb_t)(void *elem, void *ctx);
typedef int (*map_cb_t)(void *elem, void *ctx);
typedef int (*reduce_cb_t)(void *elem, void *ctx);
typedef int (*compare_cb_t)(void *elem, void *ctx);

typedef int (*knd_set_elem_marshall_cb_t)(void *elem, void *ctx,
                                          struct kndStorageLeaf *leaf,
                                          size_t *output_size, struct kndTask *task);
typedef int (*knd_set_elem_unmarshall_cb_t)(const char *elem_id, size_t elem_id_size,
                                            const char *rec, size_t rec_size,
                                            void *ctx, void **result, struct kndTask *task);

int knd_set_new(struct kndSet **result, knd_set_type type, struct kndMemPool *mempool);
int knd_set_elem_new(struct kndSetElem **result, struct kndMemPool *mempool);
int knd_set_range_new(struct kndSetRange **result, struct kndMemPool *mempool);
int knd_set_dir_new(struct kndSetDir **result, struct kndMemPool *mempool);
int knd_set_dir_block_new(struct kndSetDirBlock **result, struct kndMemPool *mempool);

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

/* GSP marshalling */
int knd_set_marshall(struct kndSet *s, struct kndSetRange *range,
                     knd_set_elem_marshall_cb_t cb, void *cb_ctx,
                     const char *path, size_t path_size,
                     size_t *total_size, struct kndTask *task);
int knd_set_leaf_marshall(struct kndSet *s, struct kndStorageLeaf *leaf,
                          struct kndSetRange *range,
                          knd_set_elem_marshall_cb_t cb, void *cb_ctx,
                          size_t *total_size, struct kndTask *task);

int knd_set_leaf_open(struct kndSet *s, struct kndStorageLeaf *leaf,
                      knd_set_elem_unmarshall_cb_t cb, struct kndTask *task);
int knd_set_build_path(struct kndSet *idx, const char *path, size_t path_size,
                       const char *pref, size_t pref_size, struct kndTask *task);
