#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <stdatomic.h>
#include <unistd.h>

#include "knd_repo.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_output.h"
#include "knd_storage.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#define DEBUG_COMMIT_WAL_LEVEL_0 0
#define DEBUG_COMMIT_WAL_LEVEL_1 0
#define DEBUG_COMMIT_WAL_LEVEL_2 0
#define DEBUG_COMMIT_WAL_LEVEL_3 0
#define DEBUG_COMMIT_WAL_LEVEL_TMP 1

static int update_wal_leaf_list(struct kndStorageWal *wal, size_t indent_size, size_t depth,
                                struct kndTask *task)
{
    struct kndStorageLeaf *leaf;
    char tmp_name[KND_PATH_SIZE + 1];
    size_t tmp_name_size;
    char target_name[KND_PATH_SIZE + 1];
    size_t target_name_size;
    struct kndOutput *out = task->out;
    int err;

    out->reset(out);
    OUT(wal->path, wal->path_size);
    OUT(KND_WAL_STATE_INDEX_NAME, strlen(KND_WAL_STATE_INDEX_NAME));
    OUT(KND_FILE_TMP_EXT_NAME, strlen(KND_FILE_TMP_EXT_NAME));
    memcpy(tmp_name, out->buf, out->buf_size);
    tmp_name_size = out->buf_size;
    tmp_name[tmp_name_size] = '\0';

    out->reset(out);
    OUT(wal->path, wal->path_size);
    OUT(KND_WAL_STATE_INDEX_NAME, strlen(KND_WAL_STATE_INDEX_NAME));
    OUT(KND_GSL_FILE_EXT_NAME, strlen(KND_GSL_FILE_EXT_NAME));
    memcpy(target_name, out->buf, out->buf_size);
    target_name_size = out->buf_size;
    target_name[target_name_size] = '\0';

    out->reset(out);
    OUT("{wal ", strlen("{wal "));
    OUT(wal->name, wal->name_size);

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_indent(out, (depth + 1) * indent_size);
        KND_TASK_ERR("indent output failure");
    }

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_indent(out, (depth + 1) * indent_size);
        KND_TASK_ERR("indent output failure");
    }

    OUT("[leaf", strlen("[leaf"));
    FOREACH (leaf, wal->leaves) {
        if (indent_size) {
            OUT("\n", 1);
            err = knd_print_indent(out, (depth + 1) * indent_size);
            KND_TASK_ERR("indent output failure");
        }
        err = knd_storage_leaf_export_GSL(leaf, out, indent_size, depth + 1, task);
        KND_TASK_ERR("failed to export storage leaf GSL");
    }
    OUT("]", 1);
    OUT("}", 1);

    err = knd_write_file((const char*)tmp_name, out->buf, out->buf_size);
    KND_TASK_ERR("failed writing to {file %.*s}", tmp_name_size, tmp_name);

    err = rename((const char*)tmp_name, (const char*)target_name);
    KND_TASK_ERR("failed renaming {file %.*s} to {file %.*s}",
                 tmp_name_size, tmp_name, target_name, target_name_size);

    return knd_OK;
}

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
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size;
    struct kndStorageLeaf *leaf;
    int err;

    assert (wal->path_size != 0);

    err = knd_storage_leaf_new(&leaf, wal->num_leaves + 1, wal->path, wal->path_size,
                               0, KND_MAX_WAL_LEAF_SIZE, KND_LEAF_WAL, KND_STORAGE_MODE_READ_WRITE);
    KND_TASK_ERR("failed to alloc a storage wal leaf");

    knd_wal_append_leaf(wal, leaf);

    knd_uid_create(leaf->numid, idbuf, &idbuf_size);

    err = knd_storage_build_filepath((const char*)wal->path, wal->path_size,
                                     idbuf, idbuf_size,
                                     KND_WAL_FILE_EXT_NAME, strlen(KND_WAL_FILE_EXT_NAME),
                                     leaf->filename, &leaf->filename_size, task);
    KND_TASK_ERR("failed to build a WAL leaf file path");

    err = create_wal_leaf_file(leaf->filename, leaf->filename_size, task);
    KND_TASK_ERR("failed to create a WAL leaf");

    err = update_wal_leaf_list(wal, KND_INDENT_SIZE, 1, task);
    KND_TASK_ERR("failed to update a WAL list");


    *result = leaf;
    return knd_OK;
}

static int fetch_wal_leaf(struct kndStorageWal *wal,
                          struct kndRepoSnapshot *snapshot, size_t rec_size,
                          size_t writer_id, struct kndStorageLeaf **result, struct kndTask *task)
{
    struct kndStorageLeaf *leaf;
    size_t planned_leaf_size = rec_size;
    size_t max_wal_leaf_size = wal->snapshot->max_wal_leaf_size;
    struct stat st;
    int err;

    if (DEBUG_COMMIT_WAL_LEVEL_2) {
        knd_log(".. {writer #%zu} to fetch a WAL leaf for {snapshot %zu}",
                writer_id, snapshot->numid);
    }

    if (wal->num_leaves == 0 || !wal->leaves) {
        err = add_wal_leaf(wal, &leaf, task);
        KND_TASK_ERR("failed to add a WAL leaf");
        *result = leaf;
        return knd_OK;
    }

    leaf = wal->leaves;
    if (stat(leaf->filename, &st)) return knd_IO_FAIL;
    planned_leaf_size += st.st_size;

    if (planned_leaf_size < max_wal_leaf_size) {
        *result = leaf;
        return knd_OK;
    }

    if (DEBUG_COMMIT_WAL_LEVEL_3) {
        knd_log("{planned-leaf-size %zu} exceeds {max-leaf-size %zu}, switching to a new leaf",
                planned_leaf_size, max_wal_leaf_size);
    }

    err = add_wal_leaf(wal, &leaf, task);
    KND_TASK_ERR("failed to add a WAL leaf");
    
    *result = leaf;
    return knd_OK;
}

static int update_wal_leaf_idx(struct kndStorageWal *wal, struct kndStorageLeaf *leaf,
                               struct kndCommit *commit, size_t rec_size,
                               struct kndTask *task)
{
    struct kndOutput *out = task->out;
    unsigned char val_buf[KND_PACK_INT_BUF_SIZE];
    size_t val_size;
    char buf[KND_PATH_SIZE + 1];
    size_t buf_size;
    int err;

    if (DEBUG_COMMIT_WAL_LEVEL_2) {
        knd_log("{wal-leaf-idx {commit %zu {rec-size %zu}}}", commit->numid, rec_size);
    }

    out->reset(out);
    OUT(leaf->filename, leaf->filename_size);
    OUT(KND_IDX_FILE_EXT_NAME, strlen(KND_IDX_FILE_EXT_NAME));

    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("file path name size %zu exceeds limit", out->buf_size);
    }

    memcpy(buf, out->buf, out->buf_size);
    buf_size = out->buf_size;
    buf[buf_size] = '\0';

    out->reset(out);
    val_size = knd_min_bytes(wal->snapshot->max_wal_commits);
    knd_pack_int(val_buf, commit->numid, val_size);
    OUT((const char*)val_buf, val_size);

    val_size = knd_min_bytes(leaf->max_size);
    knd_pack_int(val_buf, rec_size, val_size);
    OUT((const char*)val_buf, val_size);

    err = knd_append_file((const char*)buf, out->buf, out->buf_size);
    KND_TASK_ERR("WAL idx append failed");

    return knd_OK;
}

int knd_commit_update_wal(struct kndCommit *commit,
                          struct kndStorageWal *wal, struct kndRepoSnapshot *snapshot,
                          size_t writer_id, struct kndTask *task)
{
    struct kndOutput *out = task->file_out;
    struct kndStorageLeaf *leaf;
    size_t total_size;
    int err;

    if (DEBUG_COMMIT_WAL_LEVEL_2) {
        knd_log(">> {writer #%zu} to update a WAL of {snapshot %zu}", task->id, snapshot->numid);
    }

    out->reset(out);
    err = knd_commit_export_GSL(commit, out, &total_size, task);
    KND_TASK_ERR("failed to export commit as GSL");

    err = fetch_wal_leaf(wal, snapshot, total_size, writer_id, &leaf, task);
    KND_TASK_ERR("failed to fetch a WAL leaf of {writer %zu}", writer_id);

    err = knd_append_file((const char*)leaf->filename, out->buf, out->buf_size);
    KND_TASK_ERR("WAL leaf file append failed");

    err = update_wal_leaf_idx(wal, leaf, commit, out->buf_size, task);
    KND_TASK_ERR("WAL leaf idx append failed");

    return knd_OK;
}
