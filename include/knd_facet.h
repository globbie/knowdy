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
 *   knd_facet.h
 *   Knowdy Facet
 */
#pragma once

#include "knd_config.h"
#include "knd_set.h"
#include "knd_attr.h"
#include "knd_storage.h"

struct kndMemPool;
struct kndTask;
struct kndAttr;

typedef enum knd_facet_type {
    KND_FACET_LEN,
    KND_FACET_SUM,
    KND_FACET_LOC,
    KND_FACET_CLS
} knd_facet_type;

static const char* const knd_facet_type_names[] = {
    "len",
    "sum",
    "loc",
    "cls"
};

typedef int (*knd_facet_key_get_cb)(void *elem, void *ctx, void **result_key,
                                    struct kndTask *task);
typedef int (*knd_facet_key_encode_cb)(void *key, void *ctx, struct kndTask *task);

typedef void (*knd_facet_key_str_cb)(void *key, size_t depth);
typedef int (*knd_facet_hash_cb)(void *parent_val, void *curr_val, void *term_val,
                                 void *ctx, void **result, size_t *numval,
                                 struct kndTask *task);

typedef int (*knd_facet_elem_id_cb)(void *elem, const char **key, size_t *key_size);

struct kndFacetHashSpec {
    knd_facet_type type;
    const char *name;
    size_t name_size;

    knd_facet_key_get_cb key_get_cb;
    knd_facet_key_encode_cb key_encode_cb;
    knd_facet_key_str_cb key_str_cb;
    knd_facet_hash_cb hash_cb;

    struct kndFacetHashSpec *next;
};

struct kndFacetDir
{
    char id[KND_ID_SIZE];
    size_t id_size;

    struct kndFacetDirIdx *idx;

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

struct kndFacet
{
    size_t numid;
    void *key;

    /* providing hashing keys for subfacets */
    struct kndFacetHashSpec *hash_specs;
    size_t num_hash_specs;
    struct kndFacetHashSpec *curr_spec;

    void *cache[KND_FACET_MAX_ELEM_CACHE];
    size_t cache_size;
    size_t num_elems;

    /* providing ids for storing elems in kndSet
       if cache storage capacity is exceeded */
    knd_facet_elem_id_cb elem_id_cb;
    struct kndSet *idx;

    /* subfacets */
    struct kndFacet *children[KND_MAX_FACETS];
    size_t num_children;
};

int knd_facet_new(struct kndFacet **result, void *val,
                  struct kndFacetHashSpec *hash_specs, size_t num_hash_specs,
                  knd_facet_elem_id_cb elem_id_cb, struct kndMemPool *mempool);

int knd_facet_hash_spec_new(struct kndFacetHashSpec **result, knd_facet_type facet_type,
                            knd_facet_key_get_cb key_get_cb,
                            knd_facet_key_encode_cb key_encode_cb,
                            knd_facet_key_str_cb key_str_cb,
                            knd_facet_hash_cb hash_cb,
                            struct kndMemPool *mempool);

int knd_facet_add(struct kndFacet *facet, void *elem, struct kndRepo *repo, struct kndTask *task);
int knd_facet_get(struct kndFacet *facet, void *key,
                  struct kndFacet **result,
                  struct kndRepo *repo, struct kndTask *task);

int knd_facet_map(struct kndFacet *facet, void *key,
                  struct kndSetRange *range,
                  filter_cb_t filter_cb, void *filter_ctx,
                  map_cb_t map_cb, void *map_ctx,
                  struct kndRepo *repo, struct kndTask *task);

void knd_facet_str(struct kndFacet *facet, map_cb_t map_cb, struct kndTask *task, size_t depth);

int knd_facet_acquire(struct kndAttr *attr, struct kndFacet **result,
                      struct kndRepo *repo, struct kndTask *task);

int knd_facet_leaf_marshall(struct kndFacet *facet, knd_attr_type attr_type,
                            struct kndStorageLeaf *leaf, struct kndSetRange *range,
                            size_t *output_size, struct kndRepo *repo, struct kndTask *task);

int knd_facet_read(struct kndFacet *facet, knd_attr_type attr_type, struct kndTask *task);

int knd_facet_cls_key_get(void *elem, void *ctx, void **result, struct kndTask *task);
