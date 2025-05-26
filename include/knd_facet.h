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

struct kndSet;
struct kndMemPool;
struct kndTask;

typedef enum knd_facet_type {
    KND_FACET_SEQ_LENGTH,
    KND_FACET_ACCUM,
    KND_FACET_ADDRESS,
    KND_FACET_SUBCLASS
} knd_facet_type;

static const char* const knd_facet_type_names[] = {
    "Sequence Length",
    "Accumulation",
    "Address",
    "Subclass"
};

typedef int (*knd_facet_hash_fn)(void *curr_val, void *elem, void **val,
                                 size_t *numval, struct kndTask *task);
typedef void (*knd_facet_val_str_fn)(void *curr_val, size_t depth);

typedef int (*knd_facet_elem_key_fn)(void *elem, const char **key, size_t *key_size);
typedef void (*knd_facet_elem_str_fn)(void *elem, size_t depth);

typedef int (*knd_facet_map_fn)(void *elem, void *ctx);

struct kndFacetHashSpec {
    knd_facet_type type;
    const char *name;
    size_t name_size;

    knd_facet_hash_fn hash_fn;
    knd_facet_val_str_fn val_str_fn;

    struct kndFacetHashSpec *next;
};

struct kndFacetLinearBlock
{
    char id[KND_ID_SIZE];
    size_t id_size;

    //struct kndStorageLeaf *leaf;
    //struct kndSharedSetDirIdx *idx;

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
    void *val;

    /* providing hashing keys for subfacets */
    struct kndFacetHashSpec *hash_specs;
    size_t num_hash_specs;
    struct kndFacetHashSpec *curr_spec;

    void *cache[KND_FACET_MAX_ELEM_CACHE];
    size_t cache_size;

    size_t num_elems;
    /* providing keys (ids) for storing elems in kndSet
       if cache storage capacity is exceeded */
    knd_facet_elem_key_fn elem_key_fn;
    struct kndSet *idx;

    /* subfacets */
    struct kndFacet *children[KND_MAX_FACETS];
    size_t num_children;
};

struct kndFacetLeaf
{
    size_t numid;
    size_t min_leaf_size;
    size_t max_leaf_size;

    struct kndFacet *parent;
    //struct kndSharedSetDir *dir;

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

    struct kndFacetLeaf *next;
    struct kndFacetLeaf *tail;
    size_t num_leaves;
};

int knd_facet_new(struct kndFacet **result, void *val,
                  struct kndFacetHashSpec *hash_specs, size_t num_hash_specs,
                  knd_facet_elem_key_fn elem_key_fn, struct kndMemPool *mempool);

int knd_facet_hash_spec_new(struct kndFacetHashSpec **result, knd_facet_type facet_type,
                            knd_facet_hash_fn hash_fn, knd_facet_val_str_fn val_str_fn,
                            struct kndMemPool *mempool);

int knd_facet_add(struct kndFacet *facet, void *elem, struct kndTask *task);

int knd_facet_map(struct kndFacet *facet, void *val,
                  const char *range_from, size_t range_from_size,
                  const char *range_to, size_t range_to_size,
                  knd_facet_map_fn cb, void *ctx, struct kndTask *task);

void knd_facet_str(struct kndFacet *facet, knd_facet_elem_str_fn cb, size_t depth);
