#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <stdatomic.h>

#include "knd_repo.h"
#include "knd_shard.h"
#include "knd_attr.h"
#include "knd_set.h"
#include "knd_user.h"
#include "knd_query.h"
#include "knd_task.h"
#include "knd_dict.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_proc.h"
#include "knd_mempool.h"
#include "knd_state.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_REPO_GSP_LEVEL_0 0
#define DEBUG_REPO_GSP_LEVEL_1 0
#define DEBUG_REPO_GSP_LEVEL_2 0
#define DEBUG_REPO_GSP_LEVEL_3 0
#define DEBUG_REPO_GSP_LEVEL_TMP 1


static int class_export_GSP(void *obj,
                            const char *unused_var(elem_id),
                            size_t unused_var(elem_id_size),
                            size_t unused_var(count),
                            void *elem)
{
    struct kndTask *task = obj;
    struct kndClassEntry *entry = elem;
    int err;

    /* class body to be fetched from another shard? */
    if (!entry->class) {
        // TODO: build an address
        return knd_OK;
    }

    err = knd_class_export(entry->class, KND_FORMAT_GSP, task);
    KND_TASK_ERR("failed to export class %.*s", entry->name_size, entry->name);

    // knd_log("GSP REC:%.*s", task->out->buf_size, task->out->buf);
    err = knd_append_file((const char*)task->filepath,
                          task->out->buf, task->out->buf_size);
    KND_TASK_ERR("GSP file append failed");

    return knd_OK;
}

int knd_repo_sync(struct kndRepo *self, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    size_t latest_commit_id = atomic_load_explicit(&self->snapshot.num_commits,
                                                   memory_order_relaxed);
    size_t total_size = 0;
    int err;

    knd_log("LAST COMMIT: %zu", latest_commit_id);

    if (!latest_commit_id) {
        err = knd_NO_MATCH;
        KND_TASK_ERR("nothing to sync: no new commits found to snapshot #%zu",
                     self->snapshot.numid);
    }

    knd_log(".. build GSP for repo \"%.*s\"", self->name_size, self->name);
    out->reset(out);
    err = out->write(out, task->path, task->path_size);
    KND_TASK_ERR("system path construction failed");

    if (self->path_size) {
        err = out->write(out, self->path, self->path_size);
        KND_TASK_ERR("repo path construction failed");
    }
    err = out->writef(out, "snapshot_%zu/", self->snapshot.numid);
    KND_TASK_ERR("snapshot path construction failed");

    err = out->writef(out, "agent_%d/", task->id);
    KND_TASK_ERR("agent path construction failed");

    err = knd_mkpath((const char*)out->buf, out->buf_size, 0755, false);
    KND_TASK_ERR("mkpath %.*s failed", out->buf_size, out->buf);

    err = out->write(out, "snapshot_new.gsp", strlen("snapshot_new.gsp"));
    KND_TASK_ERR("new snapshot filename construction failed");
    out->buf[out->buf_size] = '\0';

    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("GSP path too long");
    }

    memcpy(task->filepath, out->buf, out->buf_size);
    task->filepath_size = out->buf_size;
    task->filepath[task->filepath_size] = '\0';

    // knd_log("new snapshot filename: %.*s", task->filepath_size, task->filepath);

    /* build GSP repo header */
    out->reset(out);
    err = out->write(out, "GSP Repo", strlen("GSP Repo"));
    KND_TASK_ERR("repo header construction failed");

    err = knd_write_file((const char*)task->filepath, out->buf, out->buf_size);
    KND_TASK_ERR("failed writing to file %.*s", task->filepath_size, task->filepath);

    /* class concs */
    err = knd_set_sync(self->class_idx, class_export_GSP, &total_size, task);
    KND_TASK_ERR("failed to sync class idx");

    // save class GSP total_size

    // 
    /* TODO: proc */
    // err = knd_set_sync(self->proc_idx, proc_export_GSP, task);
    // KND_TASK_ERR("failed to sync proc idx");

    
    return knd_OK;

    
}
