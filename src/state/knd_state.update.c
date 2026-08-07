#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "knd_mempool.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_utils.h"

#define DEBUG_STATE_UPDATE_LEVEL_1 0
#define DEBUG_STATE_UPDATE_LEVEL_2 0
#define DEBUG_STATE_UPDATE_LEVEL_3 0
#define DEBUG_STATE_UPDATE_LEVEL_4 0
#define DEBUG_STATE_UPDATE_LEVEL_5 0
#define DEBUG_STATE_UPDATE_LEVEL_TMP 1

#if 0
static int write_updates_meta_file(struct kndRepoSnapshot *s, size_t writer_id,
                                   const char *rec, size_t rec_size,
                                   struct kndTask *task)
{
    char path[KND_PATH_SIZE + 1];
    size_t path_size;
    char tmp_name[KND_PATH_SIZE + 1];
    size_t tmp_name_size;
    char target_name[KND_PATH_SIZE + 1];
    size_t target_name_size;
    int err;

    err = knd_repo_build_updates_path(s, writer_id, path, path_size, task);
    KND_TASK_ERR("failed to build repo updates path for {writer %zu}", writer_id);

    err = knd_write_file((const char*)tmp_name, rec, rec_size);
    KND_TASK_ERR("failed writing to {file %.*s}", tmp_name_size, tmp_name);

    err = rename((const char*)tmp_name, (const char*)target_name);
    KND_TASK_ERR("failed renaming {file %.*s} to {file %.*s}",
                 tmp_name_size, tmp_name, target_name, target_name_size);

    return knd_OK;
}

static int build_update_log_filename(struct kndRepoSnapshot *snapshot,
                                     char *filename, size_t *filename_size, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    const char *path;
    size_t path_size;
    int err;

    out->reset(out);
    OUT(snapshot->path, snapshot->path_size);
    OUT(KND_UPDATES_DIR_NAME, strlen(KND_UPDATES_DIR_NAME));
    OUT("/", 1);    

    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("updates log dir path too long");
    }

    path = out->buf;
    path_size = out->buf_size;

    err = knd_mkpath(path, path_size, 0755, false);
    KND_TASK_ERR("mkpath %.*s failed", path_size, path);

    OUTF("%zu", snapshot->num_update_logs);
    OUT(KND_LOG_FILE_EXT_NAME, strlen(KND_LOG_FILE_EXT_NAME));

    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("updates log file name too long");
    }

    memcpy(filename, out->buf, out->buf_size);
    *filename_size = out->buf_size;
    filename[out->buf_size] = '\0';

    if (DEBUG_STATE_UPDATE_LEVEL_TMP) {
        knd_log("{update-log %.*s}", *filename_size, filename);
    }
    return knd_OK;
}
#endif

int knd_state_apply_upstream_updates(struct kndRepoSnapshot *unused_var(snapshot), struct kndTask *unused_var(task))
{
    knd_log(".. apply upstream updates");

    // TODO
    return knd_OK;
}
