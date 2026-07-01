#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "knd_mempool.h"
#include "knd_state.h"

int knd_state_new(struct kndState **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndState));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndState));
    *result = page;
    return knd_OK;
}

int knd_state_ref_new(struct kndStateRef **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndStateRef));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndStateRef));
    *result = page;
    return knd_OK;
}

int knd_state_val_new(struct kndStateVal **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndStateVal));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndStateVal));
    *result = page;
    return knd_OK;
}
