#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_storage.h"
#include "knd_utils.h"

void knd_storage_leaf_del(struct kndStorageLeaf *leaf)
{
    free(leaf);
}

int knd_storage_leaf_new(struct kndStorageLeaf **result, size_t numid,
                         const char *path, size_t path_size,
                         size_t min_size, size_t max_size)
{
    struct kndStorageLeaf *leaf;
    char buf[KND_ID_SIZE];
    size_t buf_size;
    char *b;
    int err;

    assert (path_size != 0);

    leaf = calloc(1, sizeof(struct kndStorageLeaf));
    if (!leaf) return knd_NOMEM;

    if (numid > KND_MAX_STORAGE_LEAVES) {
        err = knd_LIMIT;
        goto error;
    }

    leaf->numid = numid;
    knd_uid_create(numid, buf, &buf_size);

    if (path[path_size - 1] != '/') {
        leaf->filepath_size = path_size + 1 + buf_size + strlen(".gsp");
    } else {
        leaf->filepath_size = path_size + buf_size + strlen(".gsp");
    }

    if (leaf->filepath_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        goto error;
    }

    memcpy(leaf->filepath, path, path_size);
    b = leaf->filepath + path_size;

    if (path[path_size - 1] != '/') {
        *b = '/';
        b++;
    }

    memcpy(b, buf, buf_size);
    b += buf_size;

    memcpy(b, ".gsp", strlen(".gsp"));
    //knd_log("{leaf-path %.*s}", leaf->filepath_size, leaf->filepath);

    leaf->min_size = min_size ? min_size : KND_SNAPSHOT_LEAF_MIN_THRESHOLD;
    leaf->max_size = max_size ? max_size : KND_SNAPSHOT_LEAF_MAX_THRESHOLD;

    if (max_size < min_size) {
        free (leaf);
        knd_log("max storage leaf {size %zu} is less that min {size %zu}", max_size, min_size);
        return knd_LIMIT;
    }

    *result = leaf;
    return knd_OK;

 error:
    free(leaf);
    return err;
}
