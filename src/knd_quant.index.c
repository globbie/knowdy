#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_quant.h"
#include "knd_class.h"
#include "knd_attr.h"
#include "knd_attr_stm.h"
#include "knd_task.h"
#include "knd_utils.h"

#define DEBUG_QUANT_INDEX_LEVEL_0 0
#define DEBUG_QUANT_INDEX_LEVEL_1 0
#define DEBUG_QUANT_INDEX_LEVEL_2 0
#define DEBUG_QUANT_INDEX_LEVEL_3 0
#define DEBUG_QUANT_INDEX_LEVEL_TMP 1

static int fetch_maxpos_facet(struct kndAttrFacet *parent, size_t max_pos,
                              struct kndAttrFacet **result, struct kndTask *task)
{
    struct kndAttrFacet *f;
    int err;

    if (max_pos >= KND_MAX_FACETS) {
        err = knd_LIMIT;
        KND_TASK_ERR("uint seq limit exceeded");
    }

    f = parent->children[max_pos];
    if (!f) {
        err = knd_attr_facet_new(&f, task->mempool);
        KND_TASK_ERR("failed to alloc attr facet value");
        parent->children[max_pos] = f;
        parent->num_children++;
    }
    return knd_OK;
}

static int fetch_accum_facet(struct kndAttrFacet *facet, const char *seq, size_t seq_size,
                             struct kndAttrFacet **result, struct kndTask *task)
{
    struct kndAttrFacet *f;
    struct kndQuantUIntFacet *uint_facet;

    assert (seq_size > 0);
    const unsigned char c = seq[seq_size - 1];
    int err;

    size_t idx_pos = obj_id_base[c];

    knd_log(".. fetch a facet for {seq %.*s} {curr-pos %zu}", seq_size, seq, idx_pos);

#if 0
    FOREACH (f, facet->children) {
        assert (f->val != NULL);
        uint_facet = f->val;

        if (uint_facet->code != c) continue;

        knd_log("++ got a facet with {num-elems %zu}", f->num_elems);

        if (!f->children) {
            *result = f;
            return knd_OK;
        }

        /* more symbols to facetize */
        if (seq_size > 1) {
            return fetch_accum_facet(f, seq, seq_size - 1, result, task);
        }
        *result = f;
        return knd_OK;
    }

    err = knd_quant_uint_facet_new(&uint_facet, task->mempool);
    KND_TASK_ERR("failed to alloc a uint facet");
    uint_facet->code = c;
    uint_facet->pos = seq_size;

    err = knd_attr_facet_new(&f, task->mempool);
    KND_TASK_ERR("failed to alloc attr facet value");
    f->val = uint_facet;

    f->next = facet->children;
    facet->children = f;
    facet->num_children++;

    *result = f;
#endif
    return knd_OK;
}

static int add_uint_stm(struct kndAttrFacet *facet,
                        const char *seq, size_t seq_size,
                        struct kndAttrStm *stm, struct kndTask *unused_var(task))
{
    struct kndQuantUIntFacet *uint_facet = facet->val;
    //int err;

    if (DEBUG_QUANT_INDEX_LEVEL_TMP) {
        knd_log("== {facet {code %c} {pos %zu} {num-elems %zu}} {seq %.*s}",
                uint_facet->code, uint_facet->pos,
                facet->num_elems, seq_size, seq);
    }

    if (facet->num_elems < KND_FACET_MAX_THRESHOLD) {
        facet->elems->cache[facet->num_elems] = stm;
        facet->num_elems++;
        return knd_OK;
    }

    /*if (!facet->children) {
        err = create_subfacets(facet, task);
        KND_TASK_ERR("failed to create subfacets");
    }

    err = fetch_facet(facet, uint->seq, uint->seq_size, &f, task);
    KND_TASK_ERR("failed to fetch a facet");

    err = add_uint_stm(f, uint->seq, uint->seq_size, stm, task);
    KND_TASK_ERR("failed to update a facet");
    */
    return knd_OK;
}

static int create_subfacets(struct kndAttrFacet *facet, struct kndTask *task)
{
    struct kndAttrStm *stm;
    struct kndAttrFacet *mpf, *f;
    struct kndQuantUInt *uint;
    int err;

    for (size_t i = 0; i < KND_FACET_MAX_THRESHOLD; i++) {
        stm = facet->elems->cache[i];
        if (!stm) break;

        uint = stm->val_subtype;

        switch (facet->type) {
        case KND_ATTR_FACET_SEQ_SIZE:
            err = fetch_maxpos_facet(facet, uint->seq_size, &mpf, task);
            KND_TASK_ERR("failed to fetch a maxpos facet");
            break;
        case KND_ATTR_FACET_ACCUM:
            err = fetch_accum_facet(facet, uint->seq, uint->seq_size, &f, task);
            KND_TASK_ERR("failed to fetch an accum facet");
            break;
        default:
            err = knd_FORMAT;
            KND_TASK_ERR("unrecognized facet type %d", facet->type);
        }

        err = add_uint_stm(f, uint->seq, uint->seq_size, stm, task);
        KND_TASK_ERR("failed to update a facet");
    }
    return knd_OK;
}

int knd_quant_uint_index(struct kndAttrFacet *facet, struct kndClassEntry *topic,
                         struct kndAttrStm *stm, struct kndTask *task)
{
    struct kndAttrFacet *f;
    struct kndQuantUInt *uint = stm->val_subtype;
    int err;

    if (DEBUG_QUANT_INDEX_LEVEL_TMP) {
        knd_log(".. {class %.*s} to index uint attr {%.*s %.*s {numval %lu {seq %.*s}}}",
                topic->name_size, topic->name,
                stm->name_size, stm->name, stm->val_size, stm->val, uint->numval,
                uint->seq_size, uint->seq);
    }

    /* no need to apply a hash func for a small set */
    if (facet->num_elems < KND_FACET_MAX_THRESHOLD) {
        facet->elems->cache[facet->num_elems] = stm;
        facet->num_elems++;
        return knd_OK;
    }

    if (!facet->num_children) {
        knd_log("-- NB: root facet buf is full, creating subfacets");

        err = create_subfacets(facet, task);
        KND_TASK_ERR("failed to create maxpos subfacets");
    }

    err = fetch_maxpos_facet(facet, uint->seq_size, &f, task);
    KND_TASK_ERR("failed to fetch a positional facet");

    err = add_uint_stm(f, uint->seq, uint->seq_size, stm, task);
    KND_TASK_ERR("failed to update a facet");
 
    return knd_OK;
}

int knd_quant_ureal_index(struct kndAttrFacet *unused_var(facet), struct kndClassEntry *topic,
                          struct kndAttrStm *stm, struct kndTask *unused_var(task))
{
    //struct kndAttrStm *stm;
    struct kndQuantUReal *ureal = stm->val_subtype;
    //int err;

    if (DEBUG_QUANT_INDEX_LEVEL_TMP) {
        knd_log(".. {class %.*s} to index ureal attr {%.*s %.*s {real %.2Lf}}}",
                topic->name_size, topic->name,
                stm->name_size, stm->name, stm->val_size, stm->val, ureal->numval);
    }

    return knd_OK;
}


