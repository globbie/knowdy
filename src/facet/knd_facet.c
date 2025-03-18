#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_facet.h"
#include "knd_class.h"
#include "knd_task.h"
#include "knd_mempool.h"
#include "knd_output.h"
#include "knd_utils.h"
#include "knd_config.h"

#define DEBUG_FACET_LEVEL_1 0
#define DEBUG_FACET_LEVEL_2 0
#define DEBUG_FACET_LEVEL_3 0
#define DEBUG_FACET_LEVEL_4 0
#define DEBUG_FACET_LEVEL_5 0
#define DEBUG_FACET_LEVEL_TMP 1

int knd_facet_hash_spec_new(struct kndFacetHashSpec **result, knd_facet_type facet_type,
                            knd_facet_hash_fn hash_fn, struct kndMemPool *mempool)
{
    void *page;
    struct kndFacetHashSpec *spec;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndFacetHashSpec));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndFacetHashSpec));
    spec = page;

    spec->type = facet_type;
    spec->hash_fn = hash_fn;

    *result = spec;
    return knd_OK;
}

int knd_facet_new(struct kndFacet **result, void *val,
                  struct kndFacetHashSpec *hash_specs, size_t num_hash_specs,
                  knd_facet_elem_key_fn elem_key_fn, struct kndMemPool *mempool)
{
    struct kndFacet *f;
    void *page;
    int err;

    assert(mempool->base_page_size >= sizeof(struct kndFacet));

    err = knd_mempool_page(mempool, KND_MEMPAGE_BASE, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndFacet));
    f = page;
    f->val = val;
    f->hash_specs = hash_specs;
    f->num_hash_specs = num_hash_specs;

    f->elem_key_fn = elem_key_fn;
    f->curr_spec = hash_specs;

    *result = f;
    return knd_OK;
}

void knd_facet_str(struct kndFacet *parent, size_t depth)
{
    struct kndFacet *f;

    if (parent->num_children) {
        knd_log("%*s{facet {num-subfacets %zu} {num-elems %zu}}",
                depth * KND_OFFSET_SIZE, "", 
                parent->num_children, parent->num_elems);
    } else {
        knd_log("%*s{facet {num-elems %zu}}",
                depth * KND_OFFSET_SIZE, "", parent->num_elems);
    }

    for (size_t i = 0; i < KND_MAX_FACETS; i++) {
        f = parent->children[i];
        if (!f) continue;

        knd_facet_str(f, depth + 1);
    }
}

int knd_facet_leaf_new(struct kndFacetLeaf **result, size_t numid, struct kndFacet *f)
{
    struct kndFacetLeaf *leaf;
    leaf = calloc(1, sizeof(struct kndFacetLeaf));
    if (!leaf) return knd_NOMEM;

    leaf->numid = numid;
    leaf->parent = f;

    leaf->min_leaf_size = KND_SNAPSHOT_LEAF_MIN_THRESHOLD;
    leaf->max_leaf_size = KND_SNAPSHOT_LEAF_MAX_THRESHOLD;

    *result = leaf;
    return knd_OK;
}
