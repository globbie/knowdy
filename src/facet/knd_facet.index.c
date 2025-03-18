#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_facet.h"
#include "knd_task.h"
#include "knd_mempool.h"
#include "knd_output.h"
#include "knd_utils.h"

#define DEBUG_FACET_IDX_LEVEL_1 0
#define DEBUG_FACET_IDX_LEVEL_2 0
#define DEBUG_FACET_IDX_LEVEL_3 0
#define DEBUG_FACET_IDX_LEVEL_4 0
#define DEBUG_FACET_IDX_LEVEL_5 0
#define DEBUG_FACET_IDX_LEVEL_TMP 1

static int create_subfacets(struct kndFacet *parent, struct kndTask *task)
{
    struct kndFacet *f;
    size_t numval;
    void *elem;
    void *hashval;
    struct kndFacetHashSpec *spec;
    knd_facet_hash_fn hash_fn;
    int err;

    if (!parent->num_hash_specs) {
        err = knd_LIMIT;
        KND_TASK_ERR("no facet hash specs available");
    }

    spec = parent->hash_specs;

    if (DEBUG_FACET_IDX_LEVEL_TMP) {
        knd_log(".. creating subfacets of {facet {num-elems %zu}}",
                parent->num_elems);
    }

    hash_fn = spec->hash_fn;

    for (size_t i = 0; i < KND_FACET_MAX_ELEM_CACHE; i++) {
        elem = parent->cache[i];
        if (!elem) break;

        err = hash_fn(parent->val, elem, &hashval, &numval, task);
        if (err) {
            // check knd_LIMIT

            // try next hash spec?

            knd_log("failed to hash elem");
            break;
        }

        f = parent->children[numval];
        if (!f) {
            err = knd_facet_new(&f, hashval, parent->hash_specs, parent->num_hash_specs,
                                parent->elem_key_fn, task->mempool);
            KND_TASK_ERR("failed to alloc a subfacet");
            parent->children[numval] = f;
            parent->num_children++;
        }

        //err = knd_facet_add(f, elem, task);
        //KND_TASK_ERR("failed to add elem to facet");
    }

    return knd_OK;
}

int knd_facet_add(struct kndFacet *facet, void *elem, struct kndTask *task)
{
    struct kndFacet *f;
    struct kndFacetHashSpec *spec = facet->hash_specs;
    size_t numval;
    void *payload;
    int err;

    /* no need to apply a hash func for a small set */
    if (facet->num_elems < KND_FACET_MAX_ELEM_CACHE) {
        facet->cache[facet->num_elems] = elem;
        facet->num_elems++;
        return knd_OK;
    }


    if (!facet->num_children) {

        // no more hash specs?

        err = create_subfacets(facet, task);
        KND_TASK_ERR("failed to create subfacets");

    }

    err = spec->hash_fn(facet->val, elem, &payload, &numval, task);
    KND_TASK_ERR("failed to apply a facet hash func");

    /*f = facet->children[numval];
    err = knd_facet_add(f, elem, task);
    KND_TASK_ERR("failed to add a facet elem");
    */
    return knd_OK;
}
