
extern int knd_permit_new(struct kndMemPool *mempool,
                          struct kndPermit **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndPermit), &page);                      RET_ERR();
    *result = page;
    return knd_OK;
}
