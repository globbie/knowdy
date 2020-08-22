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

struct kndClass;
struct kndClassEntry;
struct kndObjEntry;
struct kndTask;
struct kndAttr;
struct kndFacet;
struct ooDict;
struct kndTask;

#include "knd_config.h"

typedef enum knd_shared_set_type { KND_SHARED_SET_CLASS,
                                   KND_SHARED_SET_CLASS_INST,
                                   KND_SHARED_SET_STATE_UPDATE } knd_shared_set_type;

typedef enum knd_shared_set_dir_type { KND_SHARED_SET_DIR_FIXED,
                                       KND_SHARED_SET_DIR_VAR } knd_shared_set_dir_type;

struct kndSharedSet;
struct kndSharedSetFooter;

typedef int (*map_cb_func)(void *obj, const char *elem_id, size_t elem_id_size,
                           size_t count, void *elem);

struct kndSharedSetDirEntry
{
    knd_shared_set_dir_type type;

    struct kndSharedSetDir *subdir;
    size_t offset;
    void *payload;
    size_t payload_size;
    size_t payload_offset;
};

struct kndSharedSetDir
{
    knd_shared_set_dir_type type;
    size_t total_size;
    size_t total_elems;

    size_t dir_entry_offset_size;
    size_t num_elems;
    size_t num_subdirs;

    struct kndSharedSetDirEntry entries[KND_RADIX_BASE];
};

struct kndSharedSetElemIdx
{
    struct kndSharedSetElemIdx * _Atomic idxs[KND_RADIX_BASE];
    void * _Atomic elems[KND_RADIX_BASE];
};

struct kndSharedSet
{
    struct kndSharedSetElemIdx * _Atomic idx;
    atomic_size_t num_elems;
    atomic_size_t num_valid_elems;
    
    struct kndMemPool *mempool;

    knd_shared_set_type type;
    bool allow_overwrite;
};

int knd_shared_set_new(struct kndMemPool *mempool, struct kndSharedSet **result);
int knd_shared_set_dir_new(struct kndMemPool *mempool, struct kndSharedSetDir **result);
int knd_shared_set_dir_entry_new(struct kndMemPool *mempool, struct kndSharedSetDirEntry **result);

int knd_shared_set_init(struct kndSharedSet *self);

int knd_shared_set_elem_idx_new(struct kndMemPool *mempool, struct kndSharedSetElemIdx **result);

int knd_shared_set_intersect(struct kndSharedSet *self, struct kndSharedSet **sets, size_t num_sets);

int knd_shared_set_add(struct kndSharedSet *self, const char *key, size_t key_size, void *elem);

int knd_shared_set_sync(struct kndSharedSet *self, map_cb_func cb, size_t *total_size, struct kndTask *task);

int knd_shared_set_map(struct kndSharedSet *self, map_cb_func cb, void *obj);
int knd_shared_set_get(struct kndSharedSet *self, const char *key, size_t key_size, void **elem);
