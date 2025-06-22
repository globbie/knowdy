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

int knd_facet_map(struct kndFacet *facet, void *query_val,
                  knd_facet_map_fn cb, void *ctx, struct kndTask *task)
{
    struct kndFacetHashSpec *spec = facet->hash_specs;
    void *hashval;
    size_t numval;
    int err;

    if (!facet->num_hash_specs) {
        err = knd_LIMIT;
        KND_TASK_ERR("no facet hash specs available");
    }

    if (DEBUG_FACET_SELECT_LEVEL_TMP) {
        spec->val_str_fn(facet->val, 1);

        if (query_val) {
            spec->val_str_fn(query_val, 1);
        }
    }

    /* if (query_val) {
        err = spec->hash_fn(facet->val, elem, &hashval, &numval, task);
    if (err) {
        switch (err) {
        case knd_NO_MATCH:
            err = update_index(facet, elem, task);
            KND_TASK_ERR("failed to update a facet index");
            return knd_OK;
        default:
            KND_TASK_ERR("failed to apply a facet hash func {err %d}", err);
        }
    }

    if (numval >= KND_MAX_FACETS) {
        return knd_LIMIT;
    }

    f = facet->children[numval];
    int err;
    */

    if (facet->cache_size) {
        for (size_t i = 0; i < facet->cache_size; i++) {
            err = cb(facet->cache[i], ctx);
            KND_TASK_ERR("failed to call a facet cb func to a cached elem");
        }
    }

    if (facet->idx) {
        err = knd_set_map(facet->idx, cb, ctx);
        KND_TASK_ERR("failed to apply a facet cb func to an idx");
    }

    for (size_t i = 0; i < facet->num_children; i++) {
        err = knd_facet_map(facet->children[i], query_val, cb, ctx, task);
        KND_TASK_ERR("failed to iterate a subfacet");
    }

    return knd_OK;
}
