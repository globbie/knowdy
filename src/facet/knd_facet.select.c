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

static int apply_cb(struct kndFacet *facet, void *key,
                    struct kndSetRange *range,
                    filter_cb_t filter_cb, void *filter_ctx,
                    map_cb_t map_cb, void *map_ctx, struct kndTask *task)
{
    struct kndFacetHashSpec *spec = facet->hash_specs;
    void *elem;
    void *elem_key;
    int err;

    if (facet->cache_size) {
        for (size_t i = 0; i < facet->cache_size; i++) {
            elem = facet->cache[i];
            if (!elem) continue;

            // apply filtering
            /*err = spec->key_get_cb(elem, &elem_key, task);
            KND_TASK_ERR("failed to obtain a facet key from elem");
       
            if (elem_key != key) {
            }*/

            /* approved match */
            err = map_cb(elem, map_ctx);
            KND_TASK_ERR("failed to call a facet cb func to a cached elem");
        }
    }

    if (facet->idx) {
        err = knd_set_map(facet->idx, NULL,
                          filter_cb, filter_ctx, map_cb, map_ctx);
        KND_TASK_ERR("failed to apply a facet cb func to an idx");
    }

    for (size_t i = 0; i < KND_MAX_FACETS; i++) {
        if (!facet->children[i]) continue;

        err = apply_cb(facet->children[i], key, NULL,
                       filter_cb, filter_ctx,
                       map_cb, map_ctx, task);
        KND_TASK_ERR("failed to apply cb to a subfacet");
    }
    return knd_OK;
}

/**
 * find a matching facet and apply map_cb to all key matching elems
 * (apply elem filtering if required)
 */
int knd_facet_map(struct kndFacet *facet, void *key, struct kndSetRange *range,
                  filter_cb_t filter_cb, void *filter_ctx,
                  map_cb_t map_cb, void *map_ctx, struct kndTask *task)
{
    struct kndFacetHashSpec *spec = facet->hash_specs;
    struct kndFacet *f;
    void *next_key;
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
        err = apply_cb(facet, NULL, NULL, NULL, NULL, map_cb, map_ctx, task);
        KND_TASK_ERR("failed to apply a cb to a facet, given no query key");
        return knd_OK;
    }

    if (key == facet->key) {
        err = apply_cb(facet, NULL, NULL, NULL, NULL, map_cb, map_ctx, task);
        KND_TASK_ERR("failed to apply a cb to a facet, given no query key");
        return knd_OK;
    }

    if (!facet->num_children) {
        err = apply_cb(facet, key, NULL, NULL, NULL, map_cb, map_ctx, task);
        KND_TASK_ERR("failed to apply a cb to a terminal facet");
        return knd_OK;
    }

    err = spec->hash_cb(facet->key, NULL, key, &next_key, &numval, task);
    KND_TASK_ERR("failed to apply a facet hash func {err %d}", err);

    if (numval >= KND_MAX_FACETS) {
        return knd_LIMIT;
    }

    f = facet->children[numval];
    if (!f) {
        knd_log("no subfacets matching {key %zu}", numval);
        return knd_NO_MATCH;
    }

    err = knd_facet_map(f, next_key, NULL, NULL, NULL, map_cb, map_ctx, task);
    KND_TASK_ERR("failed to apply a cb to a subfacet");

    return knd_OK;
}
