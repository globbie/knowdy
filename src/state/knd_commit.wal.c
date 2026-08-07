#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <stdatomic.h>
#include <unistd.h>

#include "knd_repo.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_proc.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_output.h"
#include "knd_storage.h"

#include <gsl-parser.h>

#define DEBUG_COMMIT_WAL_LEVEL_0 0
#define DEBUG_COMMIT_WAL_LEVEL_1 0
#define DEBUG_COMMIT_WAL_LEVEL_2 0
#define DEBUG_COMMIT_WAL_LEVEL_3 0
#define DEBUG_COMMIT_WAL_LEVEL_TMP 1

static int create_wal_leaf_file(const char *filename, size_t filename_size, struct kndTask *task)
{
    int fd;
    int err;

    fd = open(filename, O_WRONLY | O_TRUNC | O_CREAT, 0644);
    err = (fd < 0) ? knd_IO_FAIL : knd_OK;
    close(fd);
    KND_TASK_ERR("failed creating {wal-file %.*s}", filename, filename_size);

    return knd_OK;
}

static int add_wal_leaf(struct kndStorageWal *wal, struct kndStorageLeaf **result, struct kndTask *task)
{
    struct kndStorageLeaf *leaf;
    int err;

    wal->num_leaves++;

    err = knd_storage_leaf_new(&leaf, wal->num_leaves, wal->path, wal->path_size,
                               0, KND_MAX_WAL_SIZE, KND_STORAGE_MODE_READ_WRITE);
    KND_TASK_ERR("failed to alloc a storage WAL");

    err = knd_storage_build_wal_leaf_filename((const char*)wal->path, wal->path_size, leaf->numid,
                                              leaf->filename, &leaf->filename_size, task);
    KND_TASK_ERR("failed to build WAL filename");
    
    err = create_wal_leaf_file(leaf->filename, leaf->filename_size, task);
    KND_TASK_ERR("failed to create a WAL leaf");

    //err = update_wals_meta(s, task);
    //KND_TASK_ERR("failed to update WAL meta");

    *result = leaf;

    return knd_OK;
}

static int fetch_wal_leaf(struct kndRepoSnapshot *snapshot, size_t rec_size,
                          size_t writer_id, struct kndStorageLeaf **result, struct kndTask *task)
{
    struct kndStorageWal *wal = task->wal;
    struct kndStorageLeaf *leaf;
    char path[KND_PATH_SIZE + 1];
    size_t path_size = 0;
    size_t planned_WAL_size = rec_size;
    struct stat st;
    int err;

    if (DEBUG_COMMIT_WAL_LEVEL_TMP) {
        knd_log(".. {writer #%zu} to fetch a WAL leaf for {snapshot %zu}",
                writer_id, snapshot->numid);
    }

    if (!wal) {
        err = knd_repo_build_updates_path(snapshot, writer_id, path, &path_size, task);
        KND_TASK_ERR("failed to build a path for a storage WAL");

        err = knd_storage_wal_new(&wal, path, path_size, KND_MAX_WAL_SIZE, KND_STORAGE_MODE_READ_WRITE);
        KND_TASK_ERR("failed to alloc a storage WAL");
        task->wal = wal;

        err = add_wal_leaf(wal, &leaf, task);
        KND_TASK_ERR("failed to add a WAL leaf");

        *result = leaf;
        return knd_OK;
    }
    
    leaf = wal->leaves;
    if (stat(leaf->filename, &st)) return knd_IO_FAIL;
    planned_WAL_size += st.st_size;

    if (planned_WAL_size < wal->max_size) {
        *result = leaf;
        return knd_OK;
    }

    if (DEBUG_COMMIT_WAL_LEVEL_TMP) {
        knd_log("{planned-log-size %zu} exceeds {wal-max-size %zu}",
                planned_WAL_size, wal->max_size);
    }

    /* switch to a new leaf */
    err = add_wal_leaf(wal, &leaf, task);
    KND_TASK_ERR("failed to add a WAL leaf");
    
    *result = leaf;
    return knd_OK;
}

static int update_wal_leaf_idx(struct kndCommit *commit, size_t rec_size, struct kndStorageLeaf *leaf,
                               struct kndTask *task)
{
    struct kndOutput *out = task->out;
    const char *filename;
    size_t filename_size;
    int err;

    out->reset(out);
    OUT(leaf->filename, leaf->filename_size);
    OUT(KND_IDX_FILE_EXT_NAME, strlen(KND_IDX_FILE_EXT_NAME));
    filename = out->buf;
    filename_size = out->buf_size;

    out = task->file_out;
    out->reset(out);

    OUTF("{commit %zu {size %zu}}", commit->numid, rec_size);

    err = knd_append_file(filename, out->buf, out->buf_size);
    KND_TASK_ERR("WAL idx append failed");

    return knd_OK;
}

int knd_commit_update_task_wal(struct kndCommit *commit, struct kndRepoSnapshot *snapshot,
                               size_t writer_id, struct kndTask *task)
{
    struct kndOutput *out;
    struct kndStorageLeaf *leaf;
    size_t total_size;
    int err;

    commit->timestamp = time(NULL);

    if (DEBUG_COMMIT_WAL_LEVEL_TMP) {
        knd_log(">> {writer #%zu} to build a WAL entry {snapshot %zu}", task->id, snapshot->numid);
    }

    err = knd_commit_calc_GSL_size(commit, &total_size, task);
    KND_TASK_ERR("failed to calculate GSL object size");

    err = fetch_wal_leaf(snapshot, total_size, writer_id, &leaf, task);
    KND_TASK_ERR("failed to fetch a WAL leaf of {writer %zu}", writer_id);

    err = knd_commit_export_GSL(commit, &total_size, task);
    KND_TASK_ERR("failed to export commit as GSL");

    out = task->file_out;
    err = knd_append_file((const char*)leaf->filename, out->buf, out->buf_size);
    KND_TASK_ERR("WAL leaf file append failed");

    err = update_wal_leaf_idx(commit, out->buf_size, leaf, task);
    KND_TASK_ERR("WAL leaf idx append failed");

    return knd_OK;
}
