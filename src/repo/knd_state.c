#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "knd_mempool.h"
#include "knd_state.h"

int knd_state_new(struct kndMemPool *mempool, struct kndState **result)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndState));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndState));
    *result = page;
    return knd_OK;
}

int knd_state_ref_new(struct kndMemPool *mempool, struct kndStateRef **result)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndStateRef));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndStateRef));
    *result = page;
    return knd_OK;
}

int knd_state_val_new(struct kndMemPool *mempool, struct kndStateVal **result)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndStateVal));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndStateVal));
    *result = page;
    return knd_OK;
}
