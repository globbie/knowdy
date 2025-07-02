#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_facet.h"
#include "knd_class.h"
#include "knd_task.h"
#include "knd_set.h"
#include "knd_mempool.h"
#include "knd_output.h"
#include "knd_utils.h"
#include "knd_config.h"

#define DEBUG_FACET_SELECT_LEVEL_1 0
#define DEBUG_FACET_SELECT_LEVEL_2 0
#define DEBUG_FACET_SELECT_LEVEL_3 0
#define DEBUG_FACET_SELECT_LEVEL_4 0
#define DEBUG_FACET_SELECT_LEVEL_5 0
#define DEBUG_FACET_SELECT_LEVEL_TMP 1

static int apply_map_cb(struct kndFacet *facet, void *key,
                        knd_facet_map_cb cb, void *ctx, struct kndTask *task)
{
    int err;

    if (facet->cache_size) {
        for (size_t i = 0; i < facet->cache_size; i++) {
            if (!facet->cache[i]) continue;

            err = cb(facet->cache[i], ctx);
            KND_TASK_ERR("failed to call a facet cb func to a cached elem");
        }
    }

    if (facet->idx) {
        err = knd_set_map(facet->idx, cb, ctx);
        KND_TASK_ERR("failed to apply a facet cb func to an idx");
    }

    for (size_t i = 0; i < KND_MAX_FACETS; i++) {
        if (!facet->children[i]) continue;

        err = apply_map_cb(facet->children[i], key, cb, ctx, task);
        KND_TASK_ERR("failed to apply cb to a subfacet");
    }
    return knd_OK;
}

/**
 * find a matching facet and apply cb to all key matching elems
 */
int knd_facet_map(struct kndFacet *facet, void *key,
                  knd_facet_map_cb cb, void *ctx, struct kndTask *task)
{
    struct kndFacetHashSpec *spec = facet->hash_specs;
    struct kndFacet *f;
    void *hashval;
    size_t numval;
    int err;

    if (!facet->num_hash_specs) {
        err = knd_LIMIT;
        KND_TASK_ERR("no facet hash specs available");
    }

    if (DEBUG_FACET_SELECT_LEVEL_TMP) {
        spec->key_str_cb(facet->key, 1);
        if (key) {
            spec->key_str_cb(key, 1);
        }
    }

    if (!key) {
        err = apply_map_cb(facet, NULL, cb, ctx, task);
        KND_TASK_ERR("failed to apply a cb to a facet, given no query key");
        return knd_OK;
    }

    if (key == facet->key) {
        err = apply_map_cb(facet, key, cb, ctx, task);
        KND_TASK_ERR("failed to apply a cb to a facet, given no query key");
        return knd_OK;
    }

    if (!facet->num_children) {
        err = apply_map_cb(facet, key, cb, ctx, task);
        KND_TASK_ERR("failed to apply a cb to a terminal facet");
        return knd_OK;
    }

    err = spec->hash_cb(facet->key, key, &hashval, &numval, task);
    KND_TASK_ERR("failed to apply a facet hash func {err %d}", err);

    if (numval >= KND_MAX_FACETS) {
        return knd_LIMIT;
    }
   
    f = facet->children[numval];
    if (!f) {
        knd_log("no subfacets matching {key %zu}", numval);
        return knd_NO_MATCH;
    }

    err = knd_facet_map(f, hashval, cb, ctx, task);
    KND_TASK_ERR("failed to apply a cb to a subfacet");

    return knd_OK;
}
