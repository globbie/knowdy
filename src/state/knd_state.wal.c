#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <stdatomic.h>
#include <unistd.h>

#include "knd_task.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_output.h"
#include "knd_storage.h"
#include "knd_utils.h"
#include "knd_config.h"

#include <gsl-parser.h>

#define DEBUG_STATE_WAL_LEVEL_0 0
#define DEBUG_STATE_WAL_LEVEL_1 0
#define DEBUG_STATE_WAL_LEVEL_2 0
#define DEBUG_STATE_WAL_LEVEL_3 0
#define DEBUG_STATE_WAL_LEVEL_TMP 1

static void calc_confirmed_commits(struct kndStorageLeaf *leaf, size_t *result)
{
    struct kndCommit *commit;
    size_t total_confirmed = 0;

    FOREACH (commit, leaf->commits) {
        switch (commit->phase) {
        case KND_CONFIRMED_STATE:
            total_confirmed++;
            break;
        default:
            continue;
        }
    }
    *result = total_confirmed;
}

static int write_confirmed_commits(struct kndStorageLeaf *leaf, struct kndOutput *out)
{
    struct kndCommit *commit;

    OUT("[incl", strlen("[incl"));

    FOREACH (commit, leaf->commits) {
        switch (commit->phase) {
        case KND_CONFIRMED_STATE:
            OUT("{ %zu }", commit->numid);
            break;
        default:            
            continue;
        }
    }
    OUT("]", 1);

    return knd_OK;
}

static int write_unconfirmed_commits(struct kndStorageLeaf *leaf, struct kndOutput *out)
{
    struct kndCommit *commit;

    OUT("[excl", strlen("[excl"));

    FOREACH (commit, leaf->commits) {
        switch (commit->phase) {
        case KND_CONFIRMED_STATE:
            break;
        default:            
            OUT("{ %zu }", commit->numid);
            continue;
        }
    }
    OUT("]", 1);

    return knd_OK;
}

static int update_state_log(struct kndStorageWal *wal, struct kndStateLedger *ledger, struct kndTask *task)
{
    struct kndStorageLeaf *leaf;    
    struct kndOutput *out;
    const char *rec;
    size_t rec_size;
    char filename[KND_PATH_SIZE + 1];
    size_t filename_size;
    unsigned char val_buf[KND_PACK_INT_BUF_SIZE];
    size_t val_size;
    size_t num_confirmed_commits;
    int err;

    if (DEBUG_STATE_WAL_LEVEL_TMP) {   
        knd_log(".. {collector #%zu} to update {wal %zu}", wal->agent_id);
    }

    out = task->file_out;
    out->reset(out);
    OUT("{state %zu", ledger->latest_state_id + 1);
    OUT("[leaf", strlen("[leaf"));

    FOREACH (leaf, wal->leaves) {
        OUT("{ %zu", leaf->numid);

        calc_confirmed_commits(leaf, &num_confirmed_commits);

        if (num_confirmed_commits < (leaf->num_commits / 2)) {
            err = write_confirmed_commits(leaf, out);
            KND_TASK_ERR("failed to write confirmed commit refs");
        } else {
            err = write_unconfirmed_commits(leaf, out);
            KND_TASK_ERR("failed to write unconfirmed commit refs");
        }
        OUT("}", 1);
    }
    OUT("]", 1);
    OUT("}", 1);

    rec = out->buf;
    rec_size = out->buf_size;

    /* build output file name for commit refs */
    out = task->out;
    out->reset(out);
    OUT(wal->path, wal->path_size);
    OUT(KND_WAL_STATE_INDEX_NAME, strlen(KND_WAL_STATE_INDEX_NAME));
    OUT(KND_LOG_FILE_EXT_NAME, strlen(KND_LOG_FILE_EXT_NAME));
    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("file path name size %zu exceeds limit", out->buf_size);
    }

    memcpy(filename, out->buf, out->buf_size);
    filename_size = out->buf_size;
    filename[filename_size] = '\0';

    err = knd_append_file((const char*)filename, rec, rec_size);
    KND_TASK_ERR("WAL state log append failed");

    /* write idx */
    out->reset(out);
    OUT(wal->path, wal->path_size);
    OUT(KND_WAL_STATE_INDEX_NAME, strlen(KND_WAL_STATE_INDEX_NAME));
    OUT(KND_IDX_FILE_EXT_NAME, strlen(KND_IDX_FILE_EXT_NAME));
    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("file path name size %zu exceeds limit", out->buf_size);
    }
    memcpy(filename, out->buf, out->buf_size);
    filename_size = out->buf_size;
    filename[filename_size] = '\0';

    out->reset(out);
    val_size = knd_min_bytes(wal->snapshot->max_wal_commits);
    knd_pack_int(val_buf, ledger->latest_state_id + 1, val_size);
    OUT((const char*)val_buf, val_size);

    val_size = knd_min_bytes(wal->snapshot->max_wal_leaf_size);
    knd_pack_int(val_buf, rec_size, val_size);
    OUT((const char*)val_buf, val_size);

    err = knd_append_file((const char*)filename, out->buf, out->buf_size);
    KND_TASK_ERR("WAL state log idx append failed");

    return knd_OK;
}

int knd_collector_update_state_logs(struct kndStateLedger *ledger, struct kndTask *task)
{
    char name[KND_SHORT_NAME_SIZE + 1];
    size_t name_size = 0;
    char path[KND_PATH_SIZE + 1];
    size_t path_size = 0;
    struct kndOutput *file_out = task->file_out;
    struct kndOutput *out = task->out;
    struct kndStorageWal *wal;
    int err;

    if (DEBUG_STATE_WAL_LEVEL_TMP) {   
        knd_log(".. {collector #%zu} to save confirmed commit refs", task->id);
    }

    FOREACH (wal, task->repo_wals) {
        err = update_state_log(wal, ledger, task);
        KND_TASK_ERR("failed to update state log %zu", wal->agent_id);
    }
   
    return knd_OK;
}
