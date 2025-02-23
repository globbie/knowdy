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

static const char* const knd_facet_types[] = {
    "Sequence Length",
    "Accumulation",
    "Address",
    "Subclass",
};

typedef int (*knd_facet_hash_fn)(void *obj, size_t *numval, struct kndTask *task);

typedef enum knd_facet_type {
    KND_FACET_SEQ_LENGTH,
    KND_FACET_ACCUM,
    KND_FACET_ADDRESS,
    KND_FACET_SUBCLASS
} knd_facet_type;

struct kndFacetHashSpec {
    knd_facet_type type;
    const char *name;
    size_t name_size;
    knd_facet_hash_fn hash_fn;

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
    struct kndAttr *attr;

    struct kndFacetHashSpec *hash_specs;
    size_t num_hash_specs;
    size_t curr_hash_spec;

    void *cache[KND_FACET_MAX_ELEM_CACHE];
    struct kndSet *idx;
    size_t num_elems;

    struct kndFacet *children[KND_MAX_FACETS];
    size_t num_children;

    //struct kndFacet *spec;
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

int knd_facet_new(struct kndFacet **result,
                  struct kndFacetHashSpec *hash_specs, size_t num_hash_specs,
                  struct kndMemPool *mempool);
int knd_facet_hash_spec_new(struct kndFacetHashSpec **result,
                            const char *name, size_t name_size, knd_facet_hash_fn hash_fn,
                            struct kndMemPool *mempool);

void knd_facet_str(struct kndFacet *parent, size_t depth);
int knd_facet_add(struct kndFacet *facet, void *elem, struct kndTask *task);
