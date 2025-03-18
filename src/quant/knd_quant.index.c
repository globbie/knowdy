#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_quant.h"
#include "knd_class.h"
#include "knd_attr.h"
#include "knd_attr_stm.h"
#include "knd_facet.h"
#include "knd_set.h"
#include "knd_task.h"
#include "knd_utils.h"

#define DEBUG_QUANT_INDEX_LEVEL_0 0
#define DEBUG_QUANT_INDEX_LEVEL_1 0
#define DEBUG_QUANT_INDEX_LEVEL_2 0
#define DEBUG_QUANT_INDEX_LEVEL_3 0
#define DEBUG_QUANT_INDEX_LEVEL_TMP 1

static int create_subfacets(struct kndFacet *parent, const char *seq, size_t seq_size,
                            struct kndTask *task);

/*
static int add_elem(struct kndFacet *parent, const char *seq, size_t seq_size,
                    struct kndAttrFacetElem *elem, struct kndTask *task)
{
    struct kndAttrFacet *f;
    int pos = 0;
    char c = '/';
    size_t depth = 0;
    struct kndSet *idx;
    int err;

    assert (seq_size >= 1 && seq != NULL);

    if (DEBUG_QUANT_INDEX_LEVEL_3) {
        knd_log(".. add elem {seq %.*s {size %zu}} to {facet {type %d} {depth %zu}}",
                seq_size, seq, seq_size, parent->type, parent->depth);
    }

    switch (parent->type) {
    case KND_ATTR_FACET_SEQ_LENGTH:
        pos = seq_size - 1;
        if (pos >= KND_MAX_FACETS) {
            err = knd_LIMIT;
            KND_TASK_ERR("uint seq limit exceeded");
        }
        
        break;
    case KND_ATTR_FACET_ACCUM:
        c = seq[seq_size - 1];
        pos = obj_id_base[(size_t)c];
        if (pos < 0) {
            err = knd_FORMAT;
            KND_TASK_ERR("invalid seq char");
        }
        depth = parent->depth + 1;
        seq_size--;
        break;
    default:
        err = knd_FORMAT;
        KND_TASK_ERR("unrecognized facet type %d", parent->type);
        break;
    }

    f = parent->children[pos];
    if (!f) {
        err = knd_attr_facet_new(&f, KND_ATTR_FACET_ACCUM, task->mempool);
        KND_TASK_ERR("failed to alloc attr facet value");
        f->depth = depth;
        parent->children[pos] = f;
        parent->num_children++;
    }

    parent->num_elems++;

    if (f->num_elems < KND_FACET_MAX_ELEM_CACHE) {
        f->elems->cache[f->num_elems] = elem;
        f->num_elems++;
        return knd_OK;
    }

    if (!f->num_children) {
        err = create_subfacets(f, &c, 1, task);
        KND_TASK_ERR("failed to create subfacets");
    }

    if (seq_size) {
        err = add_elem(f, seq, seq_size, elem, task);
        KND_TASK_ERR("failed to fetch a facet");
        return knd_OK;
    }

    idx = f->elems->idx;
    if (!f->elems->idx) {
        err = knd_set_new(&idx, task->mempool);
        KND_TASK_ERR("failed to alloc a set");
        f->elems->idx = idx;
    }

    err = knd_set_add(idx, elem->entry->id, elem->entry->id_size, (void*)elem);
    KND_TASK_ERR("failed to add elem to a set");
    f->num_elems++;
       return knd_OK;
}
*/

int knd_quant_uint_index(struct kndFacet *facet, struct kndClassEntry *topic,
                         struct kndAttrStm *stm, struct kndTask *task)
{
    //struct kndAttrFacetElem *elem;
    struct kndQuantAttrStm *quant_attr_stm = stm->subtype;

    assert (quant_attr_stm != NULL);

    struct kndQuantUInt *uint = quant_attr_stm->uint;
    //int err;

    if (DEBUG_QUANT_INDEX_LEVEL_2) {
        knd_log(".. {class %.*s} to index uint attr {%.*s %.*s {numval %lu {seq %.*s}}}",
                topic->name_size, topic->name,
                stm->name_size, stm->name, stm->val_size, stm->val, uint->numval,
                uint->seq_size, uint->seq);
    }

    //    err = knd_attr_facet_elem_new(&elem, task->mempool);
    //KND_TASK_ERR("failed to alloc attr facet elem");
    // elem->entry = topic;
    //elem->stm = stm;

    /* no need to apply a hash func for a small set */
    /*if (facet->num_elems < KND_FACET_MAX_ELEM_CACHE) {
        facet->elems->cache[facet->num_elems] = elem;
        facet->num_elems++;
        return knd_OK;
    }

    if (!facet->num_children) {
        if (DEBUG_QUANT_INDEX_LEVEL_3) {
            knd_log("-- NB: root facet buf capacity exceeded (%zu elems), creating subfacets",
                    KND_FACET_MAX_ELEM_CACHE);
        }

        err = create_subfacets(facet, "/", 1, task);
        KND_TASK_ERR("failed to create subfacets");
    }

    err = add_elem(facet, uint->seq, uint->seq_size, elem, task);
    KND_TASK_ERR("failed to fetch a facet");
    */
    return knd_OK;
}

int knd_quant_ureal_index(struct kndFacet *facet, struct kndClassEntry *topic,
                          struct kndAttrStm *stm, struct kndTask *unused_var(task))
{
    //struct kndAttrStm *stm;
    struct kndQuantAttrStm *quant_attr_stm = stm->subtype;
    struct kndQuantUReal *ureal = quant_attr_stm->ureal;
    //int err;

    if (DEBUG_QUANT_INDEX_LEVEL_2) {
        knd_log(".. {class %.*s} to index ureal attr {%.*s %.*s {real %.2Lf}}}",
                topic->name_size, topic->name,
                stm->name_size, stm->name, stm->val_size, stm->val, ureal->numval);
    }

    return knd_OK;
}

int knd_quant_seq_len_hash(void *curr_val, void *obj, void **val, size_t *numval, struct kndTask *task)
{
    struct kndAttrStm *stm = obj;
    struct kndQuantAttrStm *quant_attr_stm = stm->subtype;
    struct kndQuantUInt *uint = quant_attr_stm->uint;
    size_t pos = 0;

    if (DEBUG_QUANT_INDEX_LEVEL_TMP) {
        knd_log(".. hash quant {seq %.*s {len %zu}}", uint->seq_size, uint->seq, uint->seq_size);
    }

    *numval = uint->seq_size;
    *val = uint;
    return knd_OK;
}

int knd_quant_hash(void *curr_val, void *obj, void **val, size_t *numval, struct kndTask *task)
{
    struct kndAttrStm *stm = obj;
    struct kndQuantAttrStm *quant_attr_stm = stm->subtype;
    struct kndQuantUInt *uint = quant_attr_stm->uint;
    size_t pos = 0;

    if (DEBUG_QUANT_INDEX_LEVEL_TMP) {
        knd_log(".. hash seq {uint %zu}", uint->numval);
    }

    *numval = pos;
    *val = uint;
    return knd_OK;
}
