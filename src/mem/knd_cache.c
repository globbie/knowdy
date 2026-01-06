#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "knd_config.h"
#include "knd_mempool.h"
#include "knd_cache.h"
#include "knd_utils.h"

int knd_cache_item_new(struct kndCacheItem **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndCacheItem));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndCacheItem));
    *result = page;
    return knd_OK;
}
