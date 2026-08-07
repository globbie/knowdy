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
    struct kndStateRange *range;
};

static inline void append_leaf(struct kndStorageWal *wal, struct kndStorageLeaf *leaf)
{
    if (!wal->leaf_tail) {
        wal->leaf_tail  = leaf;
        wal->leaves = leaf;
    }
    else {
        wal->leaf_tail->next = leaf;
        wal->leaf_tail = leaf;
    }
    wal->num_leaves++;
}

static gsl_err_t add_wal_leaf(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndStorageWal *wal = ctx->wal;
    struct kndStorageLeaf *leaf;
    char buf[KND_SHORT_NAME_SIZE];
    long numval;
    int err;

    if (val_size >= KND_SHORT_NAME_SIZE) {
        err = knd_LIMIT;
        KND_TASK_LOG("failed to build a snapshot path");
        return make_gsl_err_external(err);
    }
    memcpy(buf, val, val_size);
    buf[val_size] = '\0';

    err = knd_parse_int(buf, &numval);
    if (err) {
        return make_gsl_err_external(err);
    }

    err = knd_storage_leaf_new(&leaf, numval, wal->path, wal->path_size,
                               0, 0, KND_STORAGE_MODE_READ_ONLY);
    if (err) {
        KND_TASK_LOG("failed to alloc a storage wal");
        return make_gsl_err_external(err);
    }

    append_leaf(wal, leaf);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_wal_leaf(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndStorageLeaf *leaf;
    size_t num_elems = 0;
    char range_from[KND_ID_SIZE];
    size_t range_from_size = 0;
    char range_to[KND_ID_SIZE];
    size_t range_to_size = 0;
    size_t idx_file_size = 0;
    
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = add_wal_leaf,
          .obj = obj
        },
        {   .name = "range-from",
            .name_size = strlen("range-from"),
            .buf = range_from,
            .buf_size = &range_from_size,
            .max_buf_size = KND_ID_SIZE
        },
        {   .name = "range-to",
            .name_size = strlen("range-to"),
            .buf = range_to,
            .buf_size = &range_to_size,
            .max_buf_size = KND_ID_SIZE
        },
        {   .name = "num-commits",
            .name_size = strlen("num-commits"),
            .parse = gsl_parse_size_t,
            .obj = &num_elems
        },
        {   .name = "file-size",
            .name_size = strlen("file-size"),
            .parse = gsl_parse_size_t,
            .obj = &idx_file_size
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    leaf = ctx->wal->leaves;
    assert (leaf != NULL);

    leaf->num_elems = num_elems;
    // TODO other params

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_wal_leaf_array(void *obj, const char *rec, size_t *total_size)
{
    struct gslTaskSpec spec = {
        .is_list_item = true,
        .parse = parse_wal_leaf,
        .obj = obj
    };
    return gsl_parse_array(&spec, rec, total_size);
}

static gsl_err_t check_wal_name(void *unused_var(obj), const char *unused_var(val), size_t unused_var(val_size))
{
    //struct LocalContext *ctx = obj;
    //struct kndTask *task = ctx->task;
    // TODO    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_wal(void *obj, const char *rec, size_t *total_size)
{
    size_t wal_max_size = 0;

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .run = check_wal_name,
            .obj = obj
        },
        {   .name = "max-size",
            .name_size = strlen("max-size"),
            .parse = gsl_parse_size_t,
            .obj = &wal_max_size
        },
        {   .name = "leaf",
            .name_size = strlen("leaf"),
            .type = GSL_GET_ARRAY_STATE,
            .parse = parse_wal_leaf_array,
            .obj = obj
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- config parse error: %d", parser_err.code);
        return parser_err;
    }
    return make_gsl_err(gsl_OK);
}
static int read_wal_leaf(struct kndStorageLeaf *unused_var(leaf),
                         struct kndStateLedger *unused_var(ledger), struct kndTask *task)
{
    knd_log(".. reading wal leaf {collector %zu}", task->id);
    return knd_OK;
}

static int read_commits(struct kndRepoSnapshot *unused_var(snapshot),
                        struct kndStorageWal *wal, struct kndStateRange *unused_var(range),
                        struct kndStateLedger *ledger, struct kndTask *task)
{
    struct kndStorageLeaf *leaf;    
    int err;

    if (DEBUG_COMMIT_READ_LEVEL_TMP) {
        knd_log(">> {collector %zu} to read commits from {WAL %.*s {path %.*s}}",
                task->id, wal->name_size, wal->name, wal->path_size, wal->path);
    }

    FOREACH (leaf, wal->leaves) {
        // TODO check if this leaf is within range
        
        err = read_wal_leaf(leaf, ledger, task);
        KND_TASK_ERR("failed to read a WAL leaf with commits");

    }

    return knd_OK;
}

static int read_wal(struct kndRepoSnapshot *snapshot, size_t writer_id,
                    struct kndStorageWal **result, struct kndStateRange *range, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndOutput *file_out = task->file_out;
    struct kndStorageWal *wal = task->wal;
    char path[KND_PATH_SIZE + 1];
    size_t path_size = 0;
    const char *filename;
    size_t filename_size;
    struct stat st;
    size_t total_parsed;
    gsl_err_t parser_err;
    int err;

    if (!task->wal) {
        err = knd_repo_build_updates_path(snapshot, writer_id, path, &path_size, task);
        KND_TASK_ERR("failed to build a path for a storage WAL");

        err = knd_storage_wal_new(&wal, path, path_size, KND_MAX_WAL_SIZE, KND_STORAGE_MODE_READ_ONLY);
        KND_TASK_ERR("failed to alloc a storage WAL");
        task->wal = wal;
    }

    out->reset(out);
    OUT(wal->path, wal->path_size);
    OUT(KND_WAL_STATE_INDEX_NAME, strlen(KND_WAL_STATE_INDEX_NAME));
    OUT(KND_GSL_FILE_EXT_NAME, strlen(KND_GSL_FILE_EXT_NAME));
    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("WAL dir index name too long");
    }

    filename = out->buf;
    filename_size = out->buf_size;

    if (stat(filename, &st)) {
        knd_log("failed to open {file %.*s}", filename_size, filename);
        return knd_IO_FAIL;
    }

    file_out->reset(file_out);
    err = file_out->write_file_content(file_out, filename);
    if (err) {
        knd_log("failed to read {file %s}", filename);
        return err;
    }
    total_parsed = file_out->buf_size;

    struct LocalContext ctx = {
       .task = task,
       .wal = wal,
       .range = range
    };

    struct gslTaskSpec specs[] = {
        {
            .name = "WAL",
            .name_size = strlen("WAL"),
            .parse = parse_wal,
            .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(file_out->buf, &total_parsed, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code != gsl_OK) {
        KND_TASK_LOG("failed to read repo state config");
        return gsl_err_to_knd_err_codes(parser_err);
    }

    *result = wal;
    return knd_OK;
}

int knd_commit_collect(struct kndRepoSnapshot *snapshot, 
                       size_t *writer_ids, size_t num_writers, struct kndStateRange *range,
                       struct kndStateLedger *ledger, struct kndTask *task)
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

        err = read_wal(snapshot, writer_id, &wal, range, task);
        KND_TASK_ERR("failed to read a WAL of {writer %zu}", writer_id);

        err = read_commits(snapshot, wal, range, ledger, task);
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
