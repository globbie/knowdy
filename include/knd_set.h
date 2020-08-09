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

struct kndClass;
struct kndClassEntry;
struct kndObjEntry;
struct kndTask;
struct kndAttr;
struct kndFacet;
struct ooDict;
struct kndTask;

#include "knd_config.h"

typedef enum knd_set_type { KND_SET_CLASS,
			    KND_SET_CLASS_INST,
			    KND_SET_STATE_UPDATE,
			    KND_SET_REL,
			    KND_SET_REL_INST } knd_set_type;

typedef enum knd_set_dir_type { KND_SET_DIR_FIXED,
                                KND_SET_DIR_VAR } knd_set_dir_type;

struct kndSet;
struct kndSetFooter;

typedef int (*map_cb_func)(void *obj, const char *elem_id, size_t elem_id_size,
                           size_t count, void *elem);

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

struct kndSet
{
    struct kndClassEntry *base;

    struct kndSetElemIdx *idx;
    size_t num_elems;
    size_t num_valid_elems;
    
    struct kndMemPool *mempool;

    struct kndSet *next;

    knd_set_type type;
    bool allow_overwrite;

    /******** public methods ********/
    int (*add)(struct kndSet *self,
               const char *key,
               size_t key_size,
               void *elem);
    int (*get)(struct kndSet *self,
               const char *key,
               size_t key_size,
               void **elem);
    int (*map)(struct kndSet *self,
               map_cb_func cb,
               void *obj);
};

int knd_set_new(struct kndMemPool *mempool,
                struct kndSet **result);
int knd_set_dir_new(struct kndMemPool *mempool,
                    struct kndSetDir **result);
int knd_set_dir_entry_new(struct kndMemPool *mempool,
                          struct kndSetDirEntry **result);

int knd_set_mem(struct kndMemPool *mempool,
                struct kndSet **result);
int knd_set_init(struct kndSet *self);

int knd_set_elem_idx_new(struct kndMemPool *mempool,
                         struct kndSetElemIdx **result);
int knd_set_elem_idx_mem(struct kndMemPool *mempool,
                         struct kndSetElemIdx **result);

int knd_set_intersect(struct kndSet *self,
                      struct kndSet **sets,
                      size_t num_sets);

int knd_set_add(struct kndSet *self,
                const char *key,
                size_t key_size,
                void *elem);

int knd_set_sync(struct kndSet *self,
                 map_cb_func cb,
                 size_t *total_size,
                 struct kndTask *task);

int knd_set_add_ref(struct kndSet *self,
                    struct kndAttr *attr,
                    struct kndClassEntry *topic,
                    struct kndClassEntry *spec);
int knd_set_map(struct kndSet *self,
                map_cb_func cb,
                void *obj);
int knd_set_get(struct kndSet *self,
                const char *key,
                size_t key_size,
                void **elem);
