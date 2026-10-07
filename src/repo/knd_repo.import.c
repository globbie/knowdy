#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <stdatomic.h>

#include "knd_repo.h"
#include "knd_attr.h"
#include "knd_facet.h"
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
#include "knd_storage.h"
#include "knd_commit.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_REPO_IMPORT_LEVEL_0 0
#define DEBUG_REPO_IMPORT_LEVEL_1 0
#define DEBUG_REPO_IMPORT_LEVEL_2 0
#define DEBUG_REPO_IMPORT_LEVEL_3 0
#define DEBUG_REPO_IMPORT_LEVEL_TMP 1

static int add_cls_state_update(struct kndClassEntry *entry, struct kndTask *task)
{
    struct kndCommit *commit = task->ctx->commit;
    struct kndStateUpdate *update;
    int err;

    err = knd_state_update_new(&update, task->mempool);
    KND_TASK_ERR("failed to alloc a state update");

    update->oper_type = KND_CREATED;
    update->obj_type = KND_STATE_CLS;
    update->obj = entry;
    update->commit = commit;

    knd_commit_append_update(commit, update);

    return knd_OK;
}

int knd_repo_cls_import(const char *rec, size_t *total_size,
                        struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndClass *cls;
    struct kndClassEntry *entry;
    struct kndSet *cls_idx = task->idxs.cls_idx;
    int err;

    err = knd_class_import(rec, total_size, &cls, snapshot, task);
    KND_TASK_ERR("failed to import a cls");

    entry = cls->entry;
    assert (entry != NULL);

    /* assign a unique cls entry id */
    entry->numid = ++task->idxs.cls_id_count;
    knd_uid_create(entry->numid, entry->id, &entry->id_size);

    err = knd_set_add(cls_idx, entry->id, entry->id_size, (void*)entry, task);
    KND_TASK_ERR("failed to register {cls %.*s} in cls idx", entry->name_size, entry->name);

    if (DEBUG_REPO_IMPORT_LEVEL_3) {
        knd_log(">> registered {cls %.*s {id %.*s}}", entry->name_size, entry->name,
                entry->id_size, entry->id);
    }

    switch (task->type) {
    case KND_TASK_COMMIT:
        err = add_cls_state_update(entry, task);
        KND_TASK_ERR("failed to add a state update");
        break;
    default:
        break;
    }

    return knd_OK;
}
