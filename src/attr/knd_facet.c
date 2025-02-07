#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_facet.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_proc_call.h"
#include "knd_task.h"
#include "knd_class.h"
#include "knd_text.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_set.h"
#include "knd_mempool.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_FACET_LEVEL_1 0
#define DEBUG_FACET_LEVEL_2 0
#define DEBUG_FACET_LEVEL_3 0
#define DEBUG_FACET_LEVEL_4 0
#define DEBUG_FACET_LEVEL_5 0
#define DEBUG_FACET_LEVEL_TMP 1

int knd_attr_facet_elem_new(struct kndAttrFacetElem **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndAttrFacetElem));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndAttrFacetElem));
    *result = page;
    return knd_OK;
}

int knd_attr_facet_elem_idx_new(struct kndAttrFacetElemIdx **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->small_x4_page_size >= sizeof(struct kndAttrFacetElemIdx));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X4, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndAttrFacetElemIdx));
    *result = page;
    return knd_OK;
}

int knd_attr_facet_new(struct kndAttrFacet **result, knd_attr_facet_type type, struct kndMemPool *mempool)
{
    struct kndAttrFacetElemIdx *elems;
    void *page;
    int err;

    err = knd_attr_facet_elem_idx_new(&elems, mempool);
    if (err) return err;

    assert(mempool->page_size >= sizeof(struct kndAttrFacet));
    err = knd_mempool_page(mempool, KND_MEMPAGE_BASE, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndAttrFacet));

    *result = page;
    (*result)->type = type;
    (*result)->elems = elems;
    return knd_OK;
}
