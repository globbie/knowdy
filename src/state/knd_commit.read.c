#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <stdatomic.h>

#include "knd_repo.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_proc.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_COMMIT_READ_LEVEL_0 0
#define DEBUG_COMMIT_READ_LEVEL_1 0
#define DEBUG_COMMIT_READ_LEVEL_2 0
#define DEBUG_COMMIT_READ_LEVEL_3 0
#define DEBUG_COMMIT_READ_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndStorageWal *wal;
    struct kndStorageLeaf *leaf;
    struct kndStateLedger *ledger;
    struct kndStateRange *range;
    struct kndCommit *commit;
    struct kndStateUpdate *update;
};

static gsl_err_t cls_update_parse(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndCommit *commit = ctx->commit;
    struct kndTask *task = ctx->task;
    struct kndStateUpdate *update = ctx->update;
    struct kndMemBlock *memblock;
    struct kndClassEntry *entry;
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    gsl_err_t parser_err;
    int err;

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .buf = buf,
            .buf_size = &buf_size,
            .max_buf_size = KND_NAME_SIZE
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code != gsl_OK) {
        KND_TASK_LOG("failed to parse commit rec");
        return parser_err;
    }

    knd_log("{cls-update %.*s}", buf_size, buf);
    err = knd_memblock_fetch(&memblock, buf_size, task);
    if (err) {
        KND_TASK_LOG("failed to fetch a memblock");
        return *total_size = 0, make_gsl_err_external(err);
    }

    err = knd_class_entry_new(&entry, task->mempool);
    if (err) {
        KND_TASK_LOG("failed to alloc a cls entry");
        return *total_size = 0, make_gsl_err_external(err);
    }

    err = knd_memblock_write(memblock, buf, buf_size, false, &entry->name);
    if (err) {
        KND_TASK_LOG("failed to to save {cls-name %.*s}", buf_size, buf);
        return *total_size = 0, make_gsl_err_external(err);
    }

    entry->name_size = buf_size;
    update->oper_type = KND_CREATED;
    update->obj_type = KND_STATE_CLS;
    update->obj = entry;

    return parser_err;
}

static gsl_err_t commit_update_parse(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndCommit *commit = ctx->commit;
    struct kndTask *task = ctx->task;
    struct kndStateUpdate *update;
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    gsl_err_t parser_err;
    int err;

    err = knd_state_update_new(&update, task->mempool);
    if (err) {
        KND_TASK_LOG("failed to alloc a state update");
        return *total_size = 0, make_gsl_err_external(err);
    }
    ctx->update = update;

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .buf = buf,
            .buf_size = &buf_size,
            .max_buf_size = KND_NAME_SIZE
        },
        {   .name = "cls",
            .name_size = strlen("cls"),
            .parse = cls_update_parse,
            .obj = ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code != gsl_OK) {
        KND_TASK_LOG("failed to parse commit rec");
        return parser_err;
    }

    update->commit = commit;
    knd_commit_append_update(commit, update);

    return parser_err;
}

static gsl_err_t commit_repo_parse(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndCommit *commit = ctx->commit;
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .buf = buf,
            .buf_size = &buf_size,
            .max_buf_size = KND_NAME_SIZE
        },
        {   .name = "upd",
            .name_size = strlen("upd"),
            .parse = commit_update_parse,
            .obj = obj
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t commit_parse(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndCommit *commit = ctx->commit;

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .buf = commit->id,
            .buf_size = &commit->id_size,
            .max_buf_size = KND_ID_SIZE
        },
        {   .name = "repo",
            .name_size = strlen("repo"),
            .parse = commit_repo_parse,
            .obj = obj
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

int knd_commit_parse(struct kndCommit *commit, const char *rec, size_t rec_size, struct kndTask *task)
{
    gsl_err_t parser_err;

    struct LocalContext ctx = {
       .task = task,
       .commit = commit
    };

    struct gslTaskSpec specs[] = {
        {
            .name = "commit",
            .name_size = strlen("commit"),
            .parse = commit_parse,
            .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, &rec_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code != gsl_OK) {
        KND_TASK_LOG("failed to parse commit rec");
        return gsl_err_to_knd_err_codes(parser_err);
    }
    
    return knd_OK;
}

int knd_commit_register(size_t rec_numid, const char *rec, size_t rec_size,
                        void *obj_ctx, size_t *result_size, void **result, struct kndTask *task)
{
    struct LocalContext *ctx = obj_ctx;
    struct kndCommit *commit;
    int err;

    if (DEBUG_COMMIT_READ_LEVEL_2) {
        knd_log(">> register {commit %zu {rec %.*s}}", rec_numid, rec_size, rec);
    }

    err = knd_commit_new(&commit, task->mempool);
    KND_TASK_ERR("failed to alloc a commit");

    err = knd_commit_parse(commit, rec, rec_size, task);
    KND_TASK_ERR("failed to parse commit");

    if (task->max_collector_commits < task->num_collector_commits + 1) {
        err = knd_LIMIT;
        KND_TASK_ERR("collector limit reached {max-commits %zu}", task->max_collector_commits);
    }

    commit->numid = task->num_collector_commits;

    task->collector_commits[task->num_collector_commits] = commit;
    task->num_collector_commits++;
    return knd_OK;
}

static int collect_wal_leaf(size_t collector_id,
                            struct kndStorageWal *wal, struct kndStorageLeaf *leaf,
                            size_t rec_id_size, size_t max_rec_size,
                            struct kndStateLedger *ledger,
                            struct kndStateRange *unused_var(range), struct kndTask *task)
{
    int err;

    struct LocalContext ctx = {
        .task = task,
        .wal = wal,
        .leaf = leaf,
        .ledger = ledger
    };

    if (DEBUG_COMMIT_READ_LEVEL_2) {
        knd_log(">> {collector %zu} to read {wal {leaf %zu}}", collector_id, leaf->numid);
    }

    err = knd_wal_leaf_read(leaf, rec_id_size, max_rec_size, NULL, knd_commit_register, &ctx, task);
    KND_TASK_ERR("failed to read {wal-leaf %zu}", leaf->numid);
    
    return knd_OK;
}

static int collect_commits(struct kndRepoSnapshot *snapshot, size_t collector_id,
                           struct kndStorageWal *wal, struct kndStateRange *range,
                           struct kndStateLedger *ledger, struct kndTask *task)
{
    struct kndStorageLeaf *leaf;    
    size_t rec_id_size = knd_min_bytes(snapshot->max_wal_commits);
    size_t max_rec_size = knd_min_bytes(snapshot->max_wal_leaf_size);
    int err;

    if (DEBUG_COMMIT_READ_LEVEL_TMP) {
        knd_log(">> {collector %zu} to read commits from {WAL %.*s {path %.*s}}",
                task->id, wal->name_size, wal->name, wal->path_size, wal->path);
    }

    FOREACH (leaf, wal->leaves) {
        // TODO check if this leaf is within range

        err = collect_wal_leaf(collector_id, wal, leaf, rec_id_size, max_rec_size, ledger, range, task);
        KND_TASK_ERR("failed to read a WAL leaf with commits");
    }

    return knd_OK;
}

int knd_collect_commits(struct kndRepoSnapshot *snapshot, size_t collector_id,
                        size_t *writer_ids, size_t num_writers, 
                        struct kndStateRange *range, struct kndStateLedger *ledger, struct kndTask *task)
{
    struct kndRepo *repo = snapshot->repo;
    size_t writer_id;
    struct kndStorageWal *wal;
    int err;

    if (DEBUG_COMMIT_READ_LEVEL_TMP) {
        knd_log(">> {collector %zu} to read commits from writers of {repo %.*s {num-writers %zu}}",
                task->id, repo->name_size, repo->name, num_writers);
    }

    for (size_t i = 0; i < num_writers; i++) {
        writer_id = writer_ids[i];

        err = knd_wal_fetch(repo->snapshot, writer_id, KND_STORAGE_MODE_READ_ONLY, &wal, task);        
        KND_TASK_ERR("failed to fetch a storage WAL");

        err = collect_commits(snapshot, collector_id, wal, range, ledger, task);
        KND_TASK_ERR("failed to read commits from {writer %zu}", writer_id);
    }

    return knd_OK;
}

int knd_commit_restore(void *elem, void *unused_var(ctx), struct kndTask *task)
{
    struct kndCommit *commit = elem;
    struct kndUserContext *user_ctx = task->user_ctx;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    size_t total_size = commit->rec_size;

    if (DEBUG_COMMIT_READ_LEVEL_TMP) {
        knd_log(".. restoring {commit #%zu}", commit->numid);
    }

    task->mempool = NULL;
    knd_task_reset(task);
    task->type = KND_TASK_RESTORE;
    task->ctx->commit = commit;
    task->user_ctx = user_ctx;
    task->mempool = mempool;

    struct gslTaskSpec specs[] = {
        { .name = "commit",
          .name_size = strlen("commit"),
          .parse = knd_commit_process,
          .obj = task
        }
    };

    parser_err = gsl_parse_task(commit->rec, &total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    // TODO
    return knd_OK;
}
