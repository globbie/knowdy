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

struct LocalContext {
    struct kndTask *task;
    struct kndFacetHashSpec *spec;
    void *key;
    filter_cb_t filter_cb;
    void *filter_ctx;
};

static int match_elem(void *elem, void *ctx_obj)
{
    struct LocalContext *ctx = ctx_obj;
    struct kndFacetHashSpec *spec = ctx->spec;
    void *key = ctx->key;
    struct kndTask *task = ctx->task;
    filter_cb_t filter_cb = ctx->filter_cb;
    void *filter_ctx = ctx->filter_ctx;
    void *elem_key;
    void *next_key;
    size_t numval;
    int err;

    if (key) {
        err = spec->key_get_cb(elem, &elem_key, task);
        KND_TASK_ERR("failed to obtain a facet key from elem");

        if (elem_key != key) {
            err = spec->hash_cb(key, NULL, elem_key, &next_key, &numval, task);
            switch (err) {
            case knd_OK:
                break;
            case knd_NO_MATCH:
                return err;
            default:
                KND_TASK_ERR("failed to run a hash func on a set elem");
                break;
            }
        }
    }

    if (filter_cb) {
        return filter_cb(elem, filter_ctx);
    }
    return knd_OK;
}
 
static int apply_cb(struct kndFacet *facet, void *key,
                    struct kndSetRange *unused_var(range),
                    filter_cb_t filter_cb, void *filter_ctx,
                    map_cb_t map_cb, void *map_ctx, struct kndTask *task)
{
    struct kndFacetHashSpec *spec = facet->hash_specs;
    void *elem;
    int err;

    struct LocalContext ctx = {
        .task = task,
        .spec = spec,
        .key = key,
        .filter_cb = filter_cb,
        .filter_ctx = filter_ctx
    };

    if (facet->cache_size) {
        for (size_t i = 0; i < facet->cache_size; i++) {
            elem = facet->cache[i];
            if (!elem) continue;

            if (key) {
                err = match_elem(elem, &ctx);
                switch (err) {
                case knd_OK:
                    break;
                case knd_NO_MATCH:
                    continue;
                default:
                    KND_TASK_ERR("failed to apply a facet hash func {err %d}", err);
                }
            }
            if (filter_cb) {
                err = filter_cb(elem, filter_ctx);
                switch (err) {
                case knd_OK:
                    break;
                case knd_NO_MATCH:
                    continue;
                default:
                    KND_TASK_ERR("failed to apply a filter func {err %d}", err);
                }
            }
            err = map_cb(elem, map_ctx);
            KND_TASK_ERR("failed to call a facet cb func to a cached elem");
        }
    }

    if (facet->idx) {
        err = knd_set_map(facet->idx, NULL, match_elem, &ctx, map_cb, map_ctx);
        KND_TASK_ERR("failed to apply a facet cb func to an idx");
    }

    for (size_t i = 0; i < KND_MAX_FACETS; i++) {
        if (!facet->children[i]) continue;

        err = apply_cb(facet->children[i], key, NULL,
                       filter_cb, filter_ctx, map_cb, map_ctx, task);
        KND_TASK_ERR("failed to apply cb to a subfacet");
    }
    return knd_OK;
}

/**
 *   find a matching facet and apply map_cb to its elems
 *   (apply extra filtering if required)
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

    if (DEBUG_FACET_SELECT_LEVEL_2) {
        knd_log("{facet-key");
        spec->key_str_cb(facet->key, 1);
        if (key) {
            knd_log("  {query-key");
            spec->key_str_cb(key, 2);
            knd_log("  }");
        }
        knd_log("}");
    }

    if (!key) {
        err = apply_cb(facet, NULL, range, filter_cb, filter_ctx, map_cb, map_ctx, task);
        KND_TASK_ERR("failed to apply a cb to a facet, given no query key");
        return knd_OK;
    }

    if (key == facet->key) {
        err = apply_cb(facet, NULL, range, filter_cb, filter_ctx, map_cb, map_ctx, task);
        KND_TASK_ERR("failed to apply a cb to a facet, given no query key");
        return knd_OK;
    }

    if (!facet->num_children) {
        err = apply_cb(facet, key, range, filter_cb, filter_ctx, map_cb, map_ctx, task);
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

    err = knd_facet_map(f, key, range, filter_cb, filter_ctx, map_cb, map_ctx, task);
    KND_TASK_ERR("failed to apply a cb to a subfacet");

    return knd_OK;
}
