#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "knd_mempool.h"
#include "knd_state.h"

extern int knd_state_new(struct kndMemPool *mempool, struct kndState **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                                sizeof(struct kndState), &page);                      RET_ERR();
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_TINY,
                                     sizeof(struct kndState), &page);                      RET_ERR();
    }
    *result = page;
    return knd_OK;
}

extern int knd_state_ref_new(struct kndMemPool *mempool, struct kndStateRef **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                                sizeof(struct kndStateRef), &page);                      RET_ERR();
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_TINY,
                                     sizeof(struct kndStateRef), &page);                      RET_ERR();
    }
    memset(page, 0, sizeof(struct kndStateRef));
    *result = page;
    return knd_OK;
}

extern int knd_state_val_new(struct kndMemPool *mempool,
                             struct kndStateVal **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                                sizeof(struct kndStateVal), &page);                      RET_ERR();
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_TINY,
                                     sizeof(struct kndStateVal), &page);                      RET_ERR();
    }
    *result = page;
    return knd_OK;
}
