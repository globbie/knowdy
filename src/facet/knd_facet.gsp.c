#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "knd_facet.h"
#include "knd_class.h"
#include "knd_attr.h"
#include "knd_utils.h"
#include "knd_memblock.h"
#include "knd_mempool.h"
#include "knd_shared_set.h"
#include "knd_repo.h"
#include "knd_task.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#define DEBUG_FACET_GSP_LEVEL_0 0
#define DEBUG_FACET_GSP_LEVEL_1 0
#define DEBUG_FACET_GSP_LEVEL_2 0
#define DEBUG_FACET_GSP_LEVEL_3 0
#define DEBUG_FACET_GSP_LEVEL_4 0
#define DEBUG_FACET_GSP_LEVEL_TMP 1

static int write_subfacets_footer(size_t *subfacet_block_sizes, size_t cell_size,
                                  struct kndStorageLeaf *leaf,
                                  struct kndRepo *unused_var(repo), struct kndTask *task)
{
    unsigned char buf[KND_NAME_SIZE];
    struct kndOutput *out = task->out;
    size_t block_size;
    size_t num_subfacets = 0;
    int err;

    out->reset(out);
    for (size_t i = 0; i < KND_MAX_FACETS; i++) {
        block_size = subfacet_block_sizes[i];
        if (!block_size) continue;

        OUTC(obj_id_seq[i]);

        knd_pack_int(buf, block_size, cell_size);
        OUT((const char*)buf, cell_size);
        num_subfacets++;
    }
    OUTC((char)num_subfacets);
    OUTC((char)cell_size);

    switch (task->mode) {
    case KND_TASK_TRACE_MODE:
        knd_log(".. write subfacets footer to {filepath %.*s}", leaf->filepath_size, leaf->filepath);
        break;
    default:
        err = knd_append_file((const char*)leaf->filepath, out->buf, out->buf_size);
        KND_TASK_ERR("facet elems footer write failure");
    }
    
    return knd_OK;
}

static int write_elems_footer(size_t *elem_block_sizes, size_t num_elems, size_t cell_size,
                              size_t *result_size, struct kndStorageLeaf *leaf, struct kndTask *task)
{
    unsigned char buf[KND_NAME_SIZE];
    struct kndOutput *out = task->out;
    size_t block_size;
    int err;

    out->reset(out);
    for (size_t i = 0; i < num_elems; i++) {
        block_size = elem_block_sizes[i];
        if (!block_size) continue;

        knd_pack_int(buf, block_size, cell_size);
        OUT((const char*)buf, cell_size);
    }
    OUTC((int)num_elems);
    OUTC((int)cell_size);

    switch (task->mode) {
    case KND_TASK_TRACE_MODE:
        knd_log(".. write facet elems footer to {filepath %.*s}",
                leaf->filepath_size, leaf->filepath);
        break;
    default:
        err = knd_append_file((const char*)leaf->filepath, out->buf, out->buf_size);
        KND_TASK_ERR("facet elems footer write failure");
    }

    *result_size = out->buf_size;
    return knd_OK;
}

static int marshall_elems(struct kndFacet *facet, knd_set_elem_marshall_cb_t cb, void *ctx,
                          size_t *total_size, struct kndStorageLeaf *leaf,
                          struct kndSetRange *range, struct kndTask *task)
{
    size_t elem_block_sizes[KND_FACET_MAX_ELEM_CACHE] = { 0 };
    void *elem;
    size_t cell_max_val = 0;
    size_t cell_size = 1;
    int err;

    if (facet->idx) {
        err = knd_set_leaf_marshall(facet->idx, range, leaf, cb, ctx, task);
        KND_TASK_ERR("failed to marshall facet elems idx");

        // facet footer
        return knd_OK;
    }

    for (size_t i = 0; i < facet->cache_size; i++) {
        elem = facet->cache[i];
        if (!elem) continue;

        // TODO: filter out elems by given range

        err = cb(elem, range, leaf, &elem_block_sizes[i], task);
        KND_TASK_ERR("failed to apply GSP cb to a cached elem");

        if (elem_block_sizes[i] > cell_max_val) cell_max_val = elem_block_sizes[i];
    }

    cell_size = knd_min_bytes(cell_max_val);

    err = write_elems_footer(elem_block_sizes, facet->cache_size, cell_size, total_size, leaf, task);
    KND_TASK_ERR("failed to build elems GSP footer");
    
    return knd_OK;
}

static int write_facet_footer(struct kndFacet *facet,
                              size_t elems_block_size, size_t subfacets_block_size,
                              size_t *result_size,
                              struct kndStorageLeaf *leaf,
                              struct kndRepo *repo, struct kndTask *task)
{
    unsigned char buf[KND_NAME_SIZE];
    struct kndFacetHashSpec *spec = facet->hash_specs;
    struct kndOutput *out = task->out;
    size_t cell_size = 1;
    int err;

    out->reset(out);
    err = spec->key_encode_cb(facet->key, repo, task);
    KND_TASK_ERR("failed to encode facet key GSP");

    if (subfacets_block_size > elems_block_size) {
        cell_size = knd_min_bytes(subfacets_block_size);
    } else {
        cell_size = knd_min_bytes(elems_block_size);
    }

    knd_pack_int(buf, elems_block_size, cell_size);
    OUT((const char*)buf, cell_size);

    knd_pack_int(buf, subfacets_block_size, cell_size);
    OUT((const char*)buf, cell_size);

    switch (task->mode) {
    case KND_TASK_TRACE_MODE:
        knd_log(".. write facet footer to {filepath %.*s}",
                leaf->filepath_size, leaf->filepath);
        break;
    default:
        err = knd_append_file((const char*)leaf->filepath, out->buf, out->buf_size);
        KND_TASK_ERR("facet elems footer write failure");
    }

    *result_size = out->buf_size;
    return knd_OK;
}

static int facet_marshall(struct kndFacet *facet, struct kndStorageLeaf *leaf,
                          struct kndSetRange *range,
                          knd_set_elem_marshall_cb_t cb, void *cb_ctx, size_t *result_size,
                          struct kndRepo *repo, struct kndTask *task)
{
    //struct kndFacetHashSpec *spec = facet->hash_specs;
    struct kndFacet *f;
    size_t elem_block_size = 0;
    size_t subfacet_block_sizes[KND_MAX_FACETS] = { 0 };
    size_t subfacets_total_size = 0;
    size_t facet_footer_size = 0;
    size_t cell_max_val = 1;
    int err;

    err = marshall_elems(facet, cb, cb_ctx, &elem_block_size, leaf, range, task);
    KND_TASK_ERR("failed to marshall stored elems");

    if (facet->num_children) {
        for (size_t i = 0; i < KND_MAX_FACETS; i++) {
            if (!facet->children[i]) continue;
            f = facet->children[i];

            err = facet_marshall(f, leaf, range, cb, cb_ctx, &subfacet_block_sizes[i], repo, task);
            KND_TASK_ERR("failed to marshall a subfacet");

            if (subfacet_block_sizes[i] > cell_max_val) cell_max_val = subfacet_block_sizes[i];

            subfacets_total_size += subfacet_block_sizes[i];
        }
    }

    err = write_subfacets_footer(subfacet_block_sizes, subfacets_total_size, leaf, repo, task);
    KND_TASK_ERR("failed to write subfacets footer");

    err = write_facet_footer(facet, elem_block_size, subfacets_total_size, &facet_footer_size,
                             leaf, repo, task);
    KND_TASK_ERR("failed to write a facet footer");

    *result_size = elem_block_size + subfacets_total_size + facet_footer_size;
    return knd_OK;
}

int knd_facet_leaf_marshall(struct kndFacet *facet, knd_attr_type attr_type,
                            struct kndStorageLeaf *leaf, struct kndSetRange *range,
                            size_t *output_size,
                            struct kndRepo *repo, struct kndTask *task)
{
    int err;

    switch (attr_type) {
    case KND_ATTR_CLS_REF:
        err = facet_marshall(facet, leaf, range, knd_attr_stm_subj_GSP, NULL, output_size,
                             repo, task);
        KND_TASK_ERR("failed to traverse attr facet to build GSP");
    default:
        break;
    }
    return knd_OK;
}

