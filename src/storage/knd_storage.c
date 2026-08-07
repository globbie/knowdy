#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "knd_task.h"
#include "knd_storage.h"
#include "knd_utils.h"

void knd_storage_leaf_del(struct kndStorageLeaf *leaf)
{
    free(leaf);
}

int knd_storage_new(struct kndStorage **result)
{
    struct kndStorage *s = calloc(1, sizeof(struct kndStorage));
    if (!s) return knd_NOMEM;
    s->snapshot_threshold_ratio = KND_SNAPSHOT_MEM_THRESHOLD_RATIO;
    *result = s;
    return knd_OK;
}

int knd_storage_build_wal_leaf_filename(const char *path, size_t path_size, size_t wal_id,
                                        char *filename, size_t *filename_size, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    out->reset(out);
    OUT(path, path_size);
    OUTF("%zu", wal_id);
    OUT(KND_WAL_FILE_EXT_NAME, strlen(KND_WAL_FILE_EXT_NAME));

    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("WAL file name too long");
    }

    memcpy(filename, out->buf, out->buf_size);
    *filename_size = out->buf_size;
    filename[out->buf_size] = '\0';

    return knd_OK;
}

int knd_storage_leaf_new(struct kndStorageLeaf **result, size_t numid,
                         const char *path, size_t path_size,
                         size_t min_size, size_t max_size, knd_storage_mode mode)
{
    struct kndStorageLeaf *leaf;
    char *b;
    int fd;
    struct stat st;
    int err;

    assert (path_size != 0);

    leaf = calloc(1, sizeof(struct kndStorageLeaf));
    if (!leaf) return knd_NOMEM;
    leaf->mode = mode;

    if (numid > KND_MAX_STORAGE_LEAVES) {
        err = knd_LIMIT;
        goto error;
    }

    leaf->numid = numid;
    knd_uid_create(numid, leaf->name, &leaf->name_size);

    if (path[path_size - 1] != '/') {
        leaf->filename_size = path_size + 1 + leaf->name_size + strlen(".gsp");
    } else {
        leaf->filename_size = path_size + leaf->name_size + strlen(".gsp");
    }

    if (leaf->filename_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        goto error;
    }

    memcpy(leaf->filename, path, path_size);
    b = leaf->filename + path_size;

    if (path[path_size - 1] != '/') {
        *b = '/';
        b++;
    }

    memcpy(b, leaf->name, leaf->name_size);
    b += leaf->name_size;

    memcpy(b, ".gsp", strlen(".gsp"));

    leaf->min_size = min_size ? min_size : KND_SNAPSHOT_LEAF_MIN_THRESHOLD;
    leaf->max_size = max_size ? max_size : KND_SNAPSHOT_LEAF_MAX_THRESHOLD;

    if (max_size < min_size) {
        free (leaf);
        knd_log("max storage leaf {size %zu} is less that min {size %zu}", max_size, min_size);
        return knd_LIMIT;
    }

    switch (mode) {
    case KND_STORAGE_MODE_READ_WRITE:
        fd = open(leaf->filename, O_WRONLY | O_TRUNC | O_CREAT, 0644);
        if (fd < 0) return knd_IO_FAIL;
        close(fd);
        break;
    default:
        if (stat(leaf->filename, &st)) return knd_IO_FAIL;
        leaf->curr_size = (size_t)st.st_size;

        // TODO calc file hash for integrity checks
        break;
    };

    *result = leaf;
    return knd_OK;

 error:
    free(leaf);
    return err;
}

int knd_storage_wal_new(struct kndStorageWal **result, const char *path, size_t path_size,
                        size_t max_size, knd_storage_mode mode)
{
    struct kndStorageWal *wal;
    char *b;
    int fd;
    struct stat st;
    int err;

    assert (path_size != 0);

    wal = calloc(1, sizeof(struct kndStorageWal));
    if (!wal) return knd_NOMEM;
    wal->mode = mode;

    if (path_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        goto error;
    }

    memcpy(wal->path, path, path_size);
    b = wal->path + path_size;

    if (path[path_size - 1] != '/') {
        *b = '/';
        b++;
    }

    wal->max_size = max_size ? max_size : KND_MAX_WAL_THRESHOLD;

    /* make sure this path exists */
    switch (mode) {
    case KND_STORAGE_MODE_READ_WRITE:
        err = knd_mkpath(path, path_size, 0755, false);
        if (err) {
            knd_log("mkpath %.*s failed", path_size, path);
            return err;
        }
        break;
    default:
        break;
    }
    
    *result = wal;
    return knd_OK;

 error:
    free(wal);
    return err;
}
