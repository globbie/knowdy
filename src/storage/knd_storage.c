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

int knd_storage_build_filepath(const char *path, size_t path_size,
                               const char *filename, size_t filename_size,
                               const char *ext, size_t ext_size,
                               char *result, size_t *result_size,
                               struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    out->reset(out);
    OUT(path, path_size);
    OUT(filename, filename_size);
    if (ext_size) {
        OUT(ext, ext_size);
    }

    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("storage file name too long");
    }

    memcpy(result, out->buf, out->buf_size);
    *result_size = out->buf_size;
    result[out->buf_size] = '\0';

    return knd_OK;
}

static int check_path_exists(const char *path, size_t path_size,
                             knd_storage_mode mode, struct kndTask *task)
{
    struct stat st;
    int err;

    switch (mode) {
    case KND_STORAGE_MODE_READ_WRITE:
        if (stat(path, &st)) {
            err = knd_mkpath((const char*)path, path_size, 0755, false);
            KND_TASK_ERR("failed to make {path %.*s}", path, path);
        }
        break;
    case KND_STORAGE_MODE_READ_ONLY:
        if (stat(path, &st)) {
            err = knd_IO_FAIL;
            KND_TASK_ERR("failed to open {path %.*s}", path_size, path);
        }
        break;
    default:
        break;
    }
    return knd_OK;
}

static int check_wal_path(struct kndRepoSnapshot *snapshot,
                          const char *agent_name, size_t agent_name_size,
                          knd_storage_mode mode, char *path, size_t *path_size,
                          struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    out->reset(out);

    /* global wal updates dir */
    OUT(snapshot->path, snapshot->path_size);
    OUT(KND_SUBMIT_DIR_NAME, strlen(KND_SUBMIT_DIR_NAME));
    OUT("/", 1);

    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("GSP path too long");
    }
    err = check_path_exists(out->buf, out->buf_size, mode, task);
    KND_TASK_ERR("failed to ensure {path %.*s}");

    /* agent local wal dir */
    OUT(KND_AGENT_DIR_NAME, strlen(KND_AGENT_DIR_NAME));
    OUT(agent_name, agent_name_size);
    OUT("/", 1);

    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("GSP path too long");
    }

    err = check_path_exists(out->buf, out->buf_size, mode, task);
    KND_TASK_ERR("failed to ensure {path %.*s}");

    memcpy(path, out->buf, out->buf_size);
    *path_size = out->buf_size;
    path[out->buf_size] = '\0';

    return knd_OK;
}

int knd_wal_fetch(struct kndRepoSnapshot *snapshot, size_t agent_id,
                  knd_storage_mode mode,
                  struct kndStorageWal **result, struct kndTask *task)
{
    char name[KND_SHORT_NAME_SIZE + 1];
    size_t name_size = 0;
    char path[KND_PATH_SIZE + 1];
    size_t path_size = 0;
    struct kndStorageWal *wal = NULL;
    struct stat st;
    int err;

    FOREACH (wal, task->repo_wals) {
        if (wal->agent_id != agent_id) {
            continue;
        }
        if (wal->snapshot != snapshot) continue;
        *result = wal;
        return knd_OK;
    }

    knd_uid_create(agent_id, name, &name_size);

    err = check_wal_path(snapshot, name, name_size, mode, path, &path_size, task);
    KND_TASK_ERR("failed to check WAL paths");

    err = knd_storage_wal_new(&wal, agent_id, name, name_size, path, path_size,
                              snapshot, KND_STORAGE_MODE_READ_WRITE);
    KND_TASK_ERR("failed to alloc a storage WAL");

    /* check wal state file: state.gsl */
    err = knd_storage_build_filepath((const char*)wal->path, wal->path_size,
                                     KND_WAL_STATE_INDEX_NAME, strlen(KND_WAL_STATE_INDEX_NAME),
                                     KND_GSL_FILE_EXT_NAME, strlen(KND_GSL_FILE_EXT_NAME),
                                     path, &path_size, task);
    KND_TASK_ERR("failed to build a WAL state file path");

    switch (mode) {
    case KND_STORAGE_MODE_READ_WRITE:    
        if (stat(path, &st)) {
            /* check writing permissions */
            if (access(wal->path, W_OK)) {
                err = knd_IO_FAIL;
                KND_TASK_ERR("no write access to wal {path %.*s}", wal->path_size, wal->path);
            }
            break;
        }
        err = knd_wal_state_read(wal, path, path_size, NULL, task);
        KND_TASK_ERR("failed to read WAL config {file %.*s}", path_size, path);

        break;
    case KND_STORAGE_MODE_READ_ONLY:
        if (stat(path, &st)) {
            err = knd_IO_FAIL;
            KND_TASK_ERR("failed to open {file %.*s}", path_size, path);
        }
        err = knd_wal_state_read(wal, path, path_size, NULL, task);
        KND_TASK_ERR("failed to read WAL config {file %.*s}", path_size, path);
        break;
    default:
        break;
    }

    wal->next = task->repo_wals;
    task->repo_wals = wal;
    task->num_repo_wals++;

    *result = wal;
    return knd_OK;
}

int knd_storage_leaf_new(struct kndStorageLeaf **result, size_t numid,
                         const char *path, size_t path_size,
                         size_t min_size, size_t max_size,
                         knd_leaf_file_t file_type, knd_storage_mode mode)
{
    struct kndStorageLeaf *leaf;
    const char *file_ext;
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

    switch (file_type) {
    case KND_LEAF_WAL:
        file_ext = KND_WAL_FILE_EXT_NAME;
        break;
    default:
        file_ext = KND_GSP_FILE_EXT_NAME;
        break;
    }
    
    if (path[path_size - 1] != '/') {
        leaf->filename_size = path_size + 1 + leaf->name_size + strlen(file_ext);
    } else {
        leaf->filename_size = path_size + leaf->name_size + strlen(file_ext);
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

    memcpy(b, file_ext, strlen(file_ext));

    leaf->min_size = min_size;
    leaf->max_size = max_size;

    if (max_size < min_size) {
        free(leaf);
        knd_log("max storage leaf {size %zu} is less that min {size %zu}", max_size, min_size);
        return knd_LIMIT;
    }

    switch (mode) {
    case KND_STORAGE_MODE_READ_WRITE:
        if (stat(leaf->filename, &st)) {
            fd = open(leaf->filename, O_WRONLY | O_TRUNC | O_CREAT, 0644);
            if (fd < 0) return knd_IO_FAIL;
            close(fd);
            break;
        }
        /* check writing permissions */
        if (access(leaf->filename, W_OK)) {
            knd_log("no write access to leaf {file %.*s}", leaf->filename_size, leaf->filename);
            return knd_IO_FAIL;
        }
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

int knd_storage_wal_new(struct kndStorageWal **result,
                        size_t agent_id, const char *name, size_t name_size,
                        const char *path, size_t path_size,
                        struct kndRepoSnapshot *snapshot,
                        knd_storage_mode mode)
{
    struct kndStorageWal *wal;
    char *b;
    int err;

    assert (name_size != 0);
    assert (path_size != 0);

    wal = calloc(1, sizeof(struct kndStorageWal));
    if (!wal) return knd_NOMEM;

    /* check constraints */
    if (name_size >= KND_SHORT_NAME_SIZE) {
        err = knd_LIMIT;
        goto error;
    }
    if (path_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        goto error;
    }

    memcpy(wal->name, name, name_size);
    wal->name_size = name_size;

    memcpy(wal->path, path, path_size);
    wal->path_size = path_size;
    b = wal->path + path_size;

    if (path[path_size - 1] != '/') {
        *b = '/';
        b++;
        wal->path_size++;
    }

    wal->mode = mode;
    wal->agent_id = agent_id;
    wal->snapshot = snapshot;

    *result = wal;
    return knd_OK;

 error:
    free(wal);
    return err;
}
