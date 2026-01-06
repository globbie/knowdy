/* fixed size LRU cache */

#pragma once

#include "knd_config.h"

typedef enum knd_cache_item_t { KND_CACHE_WRITABLE,
                                KND_CACHE_READ_ONLY } knd_cache_item_t;
struct kndCacheItem
{
    knd_cache_item_t type;
    size_t num_hits;
    void *data;

    struct kndCacheItem *prev;
    struct kndCacheItem *next;
};

int knd_cache_item_new(struct kndCacheItem **result, struct kndMemPool *mempool);
