#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_rel.h"
#include "knd_mempool.h"

int knd_rel_pred_new(struct kndMemPool *mempool, struct kndRelPred **result)
{
    void *page;
    int err;
    assert(mempool->small_x2_page_size >= sizeof(struct kndRelPred));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X2, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndRelPred));
    *result = page;
    return knd_OK;
}

int knd_rel_new(struct kndMemPool *mempool, struct kndRel **result)
{
    void *page;
    int err;
    assert(mempool->small_x2_page_size >= sizeof(struct kndRel));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X2, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndRel));
    *result = page;
    return knd_OK;
}
