#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_storage.h"

void knd_storage_leaf_del(struct kndStorageLeaf *leaf)
{
    free(leaf);
}

int knd_storage_leaf_new(struct kndStorageLeaf **result, size_t numid)
{
    struct kndStorageLeaf *leaf;

    leaf = calloc(1, sizeof(struct kndStorageLeaf));
    if (!leaf) return knd_NOMEM;

    leaf->numid = numid;

    leaf->min_size = KND_SNAPSHOT_LEAF_MIN_THRESHOLD;
    leaf->max_size = KND_SNAPSHOT_LEAF_MAX_THRESHOLD;

    *result = leaf;
    return knd_OK;
}
