#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "knd_mempool.h"
#include "knd_shared_dict.h"
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

int knd_state_conflict_new(struct kndStateConflict **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndStateConflict));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndStateConflict));
    *result = page;
    return knd_OK;
}

int knd_state_conflict_ref_new(struct kndStateConflictRef **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndStateConflictRef));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndStateConflictRef));
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

int knd_state_update_new(struct kndStateUpdate **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndStateUpdate));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndStateUpdate));
    *result = page;
    return knd_OK;
}

int knd_state_ledger_new(struct kndStateLedger **result, size_t max_commits,
                         struct kndMemPool *shared_idx_mempool, struct kndMemPool *mempool)
{
    void *page;
    struct kndStateLedger *ledger;
    int err;
    assert(KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndStateLedger));

    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndStateLedger));

    ledger = page;
    ledger->commits = calloc(max_commits, sizeof(struct kndCommit*));
    if (!ledger->commits) return knd_NOMEM;
    ledger->max_commits = max_commits;

    err = knd_shared_dict_new(&ledger->cls_name_idx, KND_LARGE_DICT_SIZE,
                              KND_DICT_UNIQUE_VALUES, KND_DICT_STORE_MEMONLY, shared_idx_mempool);
    if (err) return err;

    *result = ledger;
    return knd_OK;
}
