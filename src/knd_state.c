#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "knd_mempool.h"
#include "knd_state.h"
#include "knd_user.h"
#include "knd_repo.h"
#include "knd_utils.h"

#define DEBUG_STATE_LEVEL_0 0
#define DEBUG_STATE_LEVEL_1 0
#define DEBUG_STATE_LEVEL_2 0
#define DEBUG_STATE_LEVEL_3 0
#define DEBUG_STATE_LEVEL_TMP 1

extern int knd_state_new(struct kndMemPool *mempool,
                         struct kndState **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndState), &page);                      RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_state_ref_new(struct kndMemPool *mempool,
                             struct kndStateRef **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndStateRef), &page);                      RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_state_val_new(struct kndMemPool *mempool,
                             struct kndStateVal **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndStateVal), &page);                      RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_update_new(struct kndMemPool *mempool,
                          struct kndUpdate **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                            sizeof(struct kndUpdate), &page);                     RET_ERR();
    *result = page;
    return knd_OK;
}
