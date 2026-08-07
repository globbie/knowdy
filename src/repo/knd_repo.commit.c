#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <stdatomic.h>

#include "knd_repo.h"
#include "knd_attr.h"
#include "knd_set.h"
#include "knd_shared_set.h"
#include "knd_user.h"
#include "knd_query.h"
#include "knd_task.h"
#include "knd_dict.h"
#include "knd_shared_dict.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_proc.h"
#include "knd_mempool.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_REPO_COMMIT_LEVEL_0 0
#define DEBUG_REPO_COMMIT_LEVEL_1 0
#define DEBUG_REPO_COMMIT_LEVEL_2 0
#define DEBUG_REPO_COMMIT_LEVEL_3 0
#define DEBUG_REPO_COMMIT_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
};

static gsl_err_t save_task_body(void *obj, const char *rec, size_t *total_size)
{
    struct kndCommit *commit = obj;
    size_t rec_size = commit->rec_size;
    size_t remainder_size = rec_size - strlen("{task");
    int err;

    if (!rec_size) {
        err = knd_FAIL;
        knd_log("no rec size specified in commit #%zu", commit->numid);
        return make_gsl_err_external(err);
    }

    commit->rec = malloc(rec_size + 1);
    if (!commit->rec) return make_gsl_err_external(knd_NOMEM);

    memcpy(commit->rec, "{task", strlen("{task"));
    memcpy(commit->rec + strlen("{task"), rec, remainder_size);
    commit->rec[rec_size] = '\0';

    if (DEBUG_REPO_COMMIT_LEVEL_2) {
        knd_log("#%zu COMMIT: \"%.*s\" [size:%zu]",
                commit->numid, commit->rec_size, commit->rec, commit->rec_size);
    }

    *total_size = remainder_size - 1; // without closing brace 
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_commit_numid(void *obj, const char *val, size_t val_size)
{
    struct kndCommit *commit = obj;
    char buf[KND_NAME_SIZE];
    long numval;
    int err;

    if (val_size >= KND_NAME_SIZE)
        return make_gsl_err(gsl_FAIL);

    memcpy(buf, val, val_size);
    buf[val_size] = '\0';
            
    err = knd_parse_int(buf, &numval);
    if (err) {
        return make_gsl_err_external(err);
    }

    commit->numid = (size_t)numval;
    knd_uid_create(commit->numid, commit->id, &commit->id_size);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_commit(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndRepo *repo = ctx->repo;
    struct kndTask *task = ctx->task;
    struct kndUserContext *user_ctx = task->user_ctx;
    struct kndSet *idx = repo->snapshot->commit_idx;
    struct kndMemPool *mempool = task->mempool;
    size_t ts = 0;
    int err;

    struct kndCommit *commit = malloc(sizeof(struct kndCommit));
    if (!commit) {
        err = knd_NOMEM;
        KND_TASK_LOG("failed to alloc kndCommit");
        return make_gsl_err_external(err);
    }
    memset(commit, 0, sizeof(struct kndCommit));
    //commit->is_restored = true;

    task->mempool = NULL;
    knd_task_reset(task);

    task->ctx->commit = commit;
    task->user_ctx = user_ctx;
    task->mempool = mempool;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_commit_numid,
          .obj = commit
        },
        { .name = "_ts",
          .name_size = strlen("_ts"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &ts
        },
        { .name = "_size",
          .name_size = strlen("_size"),
          .parse = gsl_parse_size_t,
          .obj = &commit->rec_size
        },
        { .name = "task",
          .name_size = strlen("task"),
          .parse = save_task_body,
          .obj = commit
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        KND_TASK_LOG("failed to parse commit rec \"%.*s...\"", 32, rec);
        return parser_err;
    }

    err = knd_set_add(idx, commit->id, commit->id_size, (void*)commit, task);
    if (err) {
        if (err == knd_CONFLICT) {
            KND_TASK_LOG("commit #%zu already exists", commit->numid);
        } else {
            KND_TASK_LOG("failed to index commit #%zu", commit->numid);
        }
        return make_gsl_err_external(err);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_task_wal(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;

    struct gslTaskSpec specs[] = {
        { .name = "commit",
          .name_size = strlen("commit"),
          .parse = parse_commit,
          .obj = obj
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        KND_TASK_LOG("failed to parse commits in \"%.*s...\"", 32, rec);
        return parser_err;
    }
    return make_gsl_err(gsl_OK);
}

static int restore_commits(struct kndRepo *repo, struct kndMemBlock *memblock,
                           struct kndTask *task)
{
    size_t total_size;

    struct LocalContext ctx = {
        .task = task,
        .repo = repo
    };
    struct gslTaskSpec specs[] = {
        { .name = "wal",
          .name_size = strlen("wal"),
          .parse = parse_task_wal,
          .obj = &ctx
        }
    };
    gsl_err_t parser_err;

    task->type = KND_TASK_RESTORE;
    total_size = memblock->buf_size;

    parser_err = gsl_parse_task(memblock->buf, &total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        // knd_log("task WAL parsing failed: %d", parser_err.code);
        return gsl_err_to_knd_err_codes(parser_err);
    }
    return knd_OK;
}

int knd_repo_restore_logs(struct kndRepo *self, struct kndRepoSnapshot *snapshot,
                          const char *path, size_t path_size, size_t agent_id,
                          struct kndTask *task)
{
    struct kndOutput *out = task->file_out;
    char buf[KND_PATH_SIZE + 1];
    size_t buf_size;
    struct stat st;
    struct kndMemBlock *memblock;
    size_t block_size;
    size_t footer_size = strlen("}") + 1; // closing brace + null-termination
    int err;


#if 0
    for (size_t i = 0; i < snapshot->max_WALs; i++) {
        out->reset(out);
        OUT(path, path_size);
        err = out->writef(out, "journal_%zu.log", i);
        if (err) return err;
        if (out->buf_size >= KND_PATH_SIZE) return knd_LIMIT;
        memcpy(buf, out->buf, out->buf_size);
        buf_size = out->buf_size;
        buf[buf_size] = '\0';

        if (stat(buf, &st)) break;
        if (DEBUG_REPO_COMMIT_LEVEL_2) {
            knd_log(".. restoring the journal file: %.*s", buf_size, buf);
        }

        // TODO fetch memblock

        block_size = (size_t)st.st_size + footer_size;
        err = knd_memblock_new(&memblock, i, block_size);
        KND_TASK_ERR("failed to alloc a memblock");

        err = knd_memblock_read_file(memblock, buf, (size_t)st.st_size, true, &task->input);
        KND_TASK_ERR("failed to read memblock from %s {size %zu}", out->buf, st.st_size);
        task->input_size = (size_t)st.st_size;

        err = restore_commits(self, memblock, task);
        KND_TASK_ERR("failed to restore commits from %s", out->buf);

        //append_memblock(snapshot, memblock);
    }
#endif
    return knd_OK;
}

int knd_repo_transfer_commits(struct kndRepo *repo, struct kndTask *unused_var(task))
{
    //struct kndCommit *commit = NULL;
    //struct kndRepoSnapshot *snapshot = repo->snapshot;

    if (DEBUG_REPO_COMMIT_LEVEL_TMP) {
        knd_log(".. transfer the remaining delta of latest commits in {repo %.*s}",
                repo->name_size, repo->name);
    }

    // TODO
    return knd_OK;
}

int knd_repo_build_updates_path(struct kndRepoSnapshot *snapshot, size_t agent_id,
                                char *result, size_t *result_size, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    const char *path;
    size_t path_size;
    int err;

    out->reset(out);
    OUT(snapshot->path, snapshot->path_size);
    OUT(KND_UPDATES_DIR_NAME, strlen(KND_UPDATES_DIR_NAME));
    OUT("/", 1);    
    OUT(KND_AGENT_DIR_NAME, strlen(KND_AGENT_DIR_NAME));
    OUTF("%zu", agent_id);
    OUT("/", 1);

    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("WAL dir path too long");
    }
    path = out->buf;
    path_size = out->buf_size;

    memcpy(result, path, path_size);
    *result_size = path_size;
    result[path_size] = '\0';

    if (DEBUG_REPO_COMMIT_LEVEL_3) {
        knd_log("{agent-WAL-path %.*s}", path_size, path);
    }
    return knd_OK;
}
