#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_facet.h"
#include "knd_task.h"
#include "knd_class.h"
#include "knd_mempool.h"
#include "knd_set.h"
#include "knd_output.h"
#include "knd_utils.h"

#define DEBUG_FACET_IDX_LEVEL_1 0
#define DEBUG_FACET_IDX_LEVEL_2 0
#define DEBUG_FACET_IDX_LEVEL_3 0
#define DEBUG_FACET_IDX_LEVEL_4 0
#define DEBUG_FACET_IDX_LEVEL_5 0
#define DEBUG_FACET_IDX_LEVEL_TMP 1

static int update_index(struct kndFacet *facet, void *elem, struct kndTask *task)
{
    const char *key;
    size_t key_size;
    void *result;
    int err;

    assert (facet->elem_id_cb != NULL);
    assert (elem != NULL);

    err = facet->elem_id_cb(elem, &key, &key_size);
    KND_TASK_ERR("failed to get facet elem key");

    if (!facet->idx) {
        err = knd_set_new(&facet->idx, KND_SET_MULTIPLE_VALUES, task->mempool);
        KND_TASK_ERR("failed to alloc a facet idx");
    }

    err = knd_set_add(facet->idx, key, key_size, elem, task);
    KND_TASK_ERR("failed to add an elem to facet idx {err %d}", err);

    err = knd_set_get(facet->idx, key, key_size, &result, task);
    KND_TASK_ERR("failed to get an elem from facet idx {err %d}", err);

    return knd_OK;
}

static int facetize_elem(struct kndFacet *facet, void *elem,
                         struct kndRepo *repo, struct kndTask *task)
{
    struct kndFacetHashSpec *spec = facet->hash_specs;
    struct kndFacet *f;
    void *term_key;
    void *result_key;
    void *curr_key = NULL;
    size_t numval = 0;
    bool is_idx_updated = false;
    size_t count = 0;
    int err;

    if (!facet->num_hash_specs) {
        err = knd_LIMIT;
        KND_TASK_ERR("no facet hash specs available");
    }
    assert (spec->key_get_cb != NULL);
    assert (spec->hash_cb != NULL);

    err = spec->key_get_cb(elem, (void*)repo, &term_key, task);
    KND_TASK_ERR("failed to obtain a facet key from elem");

    do {
        err = spec->hash_cb(facet->key, curr_key, term_key, (void*)repo, &result_key, &numval, task);
        if (err) {
            if (err == knd_NO_MATCH) break;
            KND_TASK_ERR("failed to apply a facet hash func {err %d}", err);
        }

        if (numval >= KND_MAX_FACETS) {
            knd_log("{numval %zu} exceeds max facets limit?", numval);
            break;
        }

        f = facet->children[numval];
        if (!f) {
            err = knd_facet_new(&f, result_key, facet->hash_specs, facet->num_hash_specs,
                                facet->elem_id_cb, task->mempool);
            KND_TASK_ERR("failed to alloc a subfacet");
            facet->children[numval] = f;
            facet->num_children++;
        }
        err = knd_facet_add(f, elem, repo, task);
        KND_TASK_ERR("failed to add a facet elem {err %d}", err);
        is_idx_updated = true;

        /* inheritance bottom reached */
        if (result_key == term_key) {
            break;
        }

        curr_key = result_key;
        count++;
    } while (curr_key);

    if (!is_idx_updated) {
        err = update_index(facet, elem, task);
        KND_TASK_ERR("failed to update a facet index");
    }
    return knd_OK;
}

int knd_facet_add(struct kndFacet *facet, void *elem, struct kndRepo *repo, struct kndTask *task)
{
    size_t cache_size;
    int err;

    assert (elem != NULL);

    if (facet->num_elems < KND_FACET_MAX_ELEM_CACHE) {
        facet->cache[facet->num_elems] = elem;
        facet->cache_size++;
        facet->num_elems++;
        return knd_OK;
    }

    if (!facet->num_children) {
        cache_size = facet->cache_size;
        facet->cache_size = 0;

        for (size_t i = 0; i < cache_size; i++) {
            err = facetize_elem(facet, facet->cache[i], repo, task);
            KND_TASK_ERR("failed to facetize a cached elem");
        }
    }

    err = facetize_elem(facet, elem, repo, task);
    KND_TASK_ERR("failed to facetize an elem");
    facet->num_elems++;

    return knd_OK;
}
