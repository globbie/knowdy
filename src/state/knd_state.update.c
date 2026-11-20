#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "knd_mempool.h"
#include "knd_output.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_utils.h"

#define DEBUG_STATE_UPDATE_LEVEL_1 0
#define DEBUG_STATE_UPDATE_LEVEL_2 0
#define DEBUG_STATE_UPDATE_LEVEL_3 0
#define DEBUG_STATE_UPDATE_LEVEL_4 0
#define DEBUG_STATE_UPDATE_LEVEL_5 0
#define DEBUG_STATE_UPDATE_LEVEL_TMP 1

int knd_state_apply_upstream_updates(struct kndRepoSnapshot *unused_var(snapshot), struct kndTask *unused_var(task))
{
    knd_log(".. apply upstream updates");

    // TODO
    return knd_OK;
}

int knd_state_advance(struct kndRepoSnapshot *snapshot, struct kndStateLedger *ledger,
                      struct kndTask *task)
{
    char filename[KND_PATH_SIZE + 1];
    size_t filename_size = 0;
    unsigned char val_buf[KND_PACK_INT_BUF_SIZE];
    size_t val_size;
    struct kndOutput *out = task->out;
    struct stat st;
    int err;

    knd_log("++ advance global state");

    out->reset(out);
    OUT(snapshot->path, snapshot->path_size);
    OUT(KND_SUBMIT_DIR_NAME, strlen(KND_SUBMIT_DIR_NAME));
    OUT("/", 1);

    OUT(KND_WAL_STATE_INDEX_NAME, strlen(KND_WAL_STATE_INDEX_NAME));
    OUT(KND_IDX_FILE_EXT_NAME, strlen(KND_IDX_FILE_EXT_NAME));
    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("file path name size %zu exceeds limit", out->buf_size);
    }
    memcpy(filename, out->buf, out->buf_size);
    filename_size = out->buf_size;
    filename[filename_size] = '\0';
    
    if (stat(filename, &st)) {
        err = knd_IO_FAIL;
        KND_TASK_ERR("failed to open {file %.*s}", filename_size, filename);
    }

    out->reset(out);
    val_size = knd_min_bytes(snapshot->max_state_commits);
    knd_pack_int(val_buf, ledger->latest_state_id + 1, val_size);
    OUT((const char*)val_buf, val_size);

    err = knd_append_file((const char*)filename, out->buf, out->buf_size);
    KND_TASK_ERR("global state log append failed");

    return knd_OK;
}
