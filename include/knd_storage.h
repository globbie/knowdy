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
 *   knd_storage.h
 *   Knowdy Storage
 */

#pragma once

#include "knd_config.h"

struct kndOutput;
struct kndTask;
struct kndSetRange;
struct kndStateRange;

typedef enum knd_storage_t {
    KND_STORAGE_DEFAULT,
    KND_STORAGE_LOCAL_FILESYS,
    KND_STORAGE_NFS,
    KND_STORAGE_S3
} knd_storage_t;

typedef enum knd_leaf_file_t {
    KND_LEAF_DEFAULT,
    KND_LEAF_GSP,
    KND_LEAF_WAL
} knd_leaf_file_t;

typedef enum knd_leaf_phase_t {
    KND_LEAF_PHASE_DEFAULT,
    KND_LEAF_PHASE_INIT,
    KND_LEAF_PHASE_ACTIVE,
    KND_LEAF_PHASE_FROZEN
} knd_leaf_phase_t;

static const char* const knd_storage_type_names[] = {
    [KND_STORAGE_DEFAULT] = "local-fs",
    [KND_STORAGE_LOCAL_FILESYS] = "local-fs",
    [KND_STORAGE_NFS] = "NFS",
    [KND_STORAGE_S3] = "S3"
};

typedef enum knd_storage_mode {
    KND_STORAGE_MODE_READ_ONLY,
    KND_STORAGE_MODE_READ_WRITE
} knd_storage_mode;

static const char* const knd_storage_mode_names[] = {
    [KND_STORAGE_MODE_READ_ONLY] = "read-only",
    [KND_STORAGE_MODE_READ_WRITE] = "read-write"
};

typedef enum knd_storage_unit_type {
    KND_STORAGE_UNIT_DEFAULT,
    KND_STORAGE_UNIT_KB,
    KND_STORAGE_UNIT_MB,
    KND_STORAGE_UNIT_GB,
    KND_STORAGE_UNIT_TB
} knd_storage_unit_type;

static const char* const knd_storage_unit_names[] = {
    [KND_STORAGE_UNIT_DEFAULT] = "default unit",
    [KND_STORAGE_UNIT_KB] = "K",
    [KND_STORAGE_UNIT_MB] = "M",
    [KND_STORAGE_UNIT_GB] = "G",
    [KND_STORAGE_UNIT_TB] = "T"
};

typedef int (*knd_leaf_rec_unmarshall_cb_t)(size_t rec_numid, const char *rec, size_t rec_size,
                                            void *ctx, size_t *result_size, void **result, struct kndTask *task);

struct kndStorage {
    knd_storage_t type;
    knd_storage_mode mode;

    char name[KND_SHORT_NAME_SIZE];
    size_t name_size;

    char path[KND_PATH_SIZE + 1];
    size_t path_size;

    knd_storage_unit_type quota_unit;
    size_t quota_total;

    // gt 0  lte 1
    float snapshot_threshold_ratio;
    size_t max_snapshots;

    knd_storage_unit_type leaf_storage_unit;
    size_t leaf_max_units_size;
    size_t leaf_max_size;

    size_t leaf_min_units_size;
    size_t leaf_min_size;

    struct kndStorage *next;
};

struct kndStorageLeaf
{
    knd_leaf_file_t file_type;
    knd_leaf_phase_t phase;
    knd_storage_mode mode;
    size_t numid;

    size_t min_size;
    size_t max_size;
    size_t curr_size;

    size_t num_elems;

    char range_from_addr[KND_PATH_SIZE + 1];
    size_t range_from_addr_size;
    size_t range_from;

    char range_to_addr[KND_PATH_SIZE + 1];
    size_t range_to_addr_size;
    size_t range_to;

    char name[KND_SHORT_NAME_SIZE + 1];
    size_t name_size;

    char filename[KND_PATH_SIZE + 1];
    size_t filename_size;

    char file_hash[KND_HASH_SIZE];
    size_t file_hash_size;

    struct kndStorageLeaf *next;
};

struct kndStorageWal
{
    knd_storage_mode mode;
    struct kndRepoSnapshot *snapshot;

    size_t agent_id;
    char name[KND_SHORT_NAME_SIZE + 1];
    size_t name_size;

    char path[KND_PATH_SIZE + 1];
    size_t path_size;

    size_t num_commits;

    struct kndStorageLeaf *leaves;
    struct kndStorageLeaf *leaf_tail;
    size_t num_leaves;

    struct kndStorageWal *next;
};

int knd_storage_new(struct kndStorage **result);

int knd_storage_wal_new(struct kndStorageWal **result,
                        size_t agent_id,
                        const char *name, size_t name_size,
                        const char *path, size_t path_size,
                        struct kndRepoSnapshot *snapshot,
                        knd_storage_mode mode);

int knd_storage_leaf_new(struct kndStorageLeaf **result, size_t numid,
                         const char *path, size_t path_size,
                         size_t min_size, size_t max_size,
                         knd_leaf_file_t file_type, knd_storage_mode mode);

void knd_storage_leaf_del(struct kndStorageLeaf *leaf);

int knd_storage_leaf_export_GSL(struct kndStorageLeaf *leaf, struct kndOutput *out,
                                size_t indent_size, size_t depth, struct kndTask *task);

gsl_err_t knd_storage_parse_conf(void *obj, const char *rec, size_t *total_size);

int knd_storage_build_filepath(const char *path, size_t path_size,
                               const char *filename, size_t filename_size,
                               const char *ext, size_t ext_size,
                               char *result, size_t *result_size,
                               struct kndTask *task);

int knd_wal_fetch(struct kndRepoSnapshot *snapshot, size_t worker_id,
                  knd_storage_mode mode, struct kndStorageWal **result, struct kndTask *task);

int knd_wal_state_read(struct kndStorageWal *wal,
                       const char *filename, size_t filename_size,
                       struct kndStateRange *range, struct kndTask *task);

int knd_wal_leaf_read(struct kndStorageLeaf *leaf,
                      size_t rec_id_size, size_t max_rec_size, struct kndSetRange *range,
                      knd_leaf_rec_unmarshall_cb_t cb, void *cb_ctx, struct kndTask *task);

static inline void knd_wal_append_leaf(struct kndStorageWal *wal, struct kndStorageLeaf *leaf)
{
    if (!wal->leaf_tail) {
        wal->leaf_tail = leaf;
        wal->leaves = leaf;
    }
    else {
        wal->leaf_tail->next = leaf;
        wal->leaf_tail->phase = KND_LEAF_PHASE_FROZEN;
        wal->leaf_tail = leaf;
    }
    wal->num_leaves++;
    leaf->phase = KND_LEAF_PHASE_ACTIVE;
}
