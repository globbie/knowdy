#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <stdatomic.h>

#include "knd_repo.h"
#include "knd_set.h"
#include "knd_shared_set.h"
#include "knd_user.h"
#include "knd_query.h"
#include "knd_task.h"
#include "knd_dict.h"
#include "knd_shared_dict.h"
#include "knd_class.h"
#include "knd_attr.h"
#include "knd_class_inst.h"
#include "knd_proc.h"
#include "knd_mempool.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_REPO_OPEN_LEVEL_0 0
#define DEBUG_REPO_LEVEL_1 0
#define DEBUG_REPO_LEVEL_2 0
#define DEBUG_REPO_LEVEL_3 0
#define DEBUG_REPO_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
    const char *path;
    size_t path_size;

    struct kndStorageLeaf *leaves;
    struct kndStorageLeaf *leaf_tail;
    size_t num_leaves;
};

static inline void append_memblock(struct kndRepoSnapshot *self, struct kndMemBlock *block)
{
    block->next = self->blocks;
    self->blocks = block;
    self->num_blocks++;
    self->total_block_size += block->buf_size;
}

static inline void append_leaf(struct LocalContext *ctx, struct kndStorageLeaf *leaf)
{
    if (!ctx->leaves) {
        ctx->leaves = leaf;
        ctx->leaf_tail = leaf;
        ctx->num_leaves++;
        return;
    }

    ctx->leaf_tail->next = leaf;
    ctx->leaf_tail = leaf;
    ctx->num_leaves++;
}

static int build_path(char *path, size_t *path_size,
                      const char *snapshot_path, size_t snapshot_path_size,
                      const char *pref, size_t pref_size, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    out->reset(out);
    OUT(snapshot_path, snapshot_path_size);
    OUT(pref, pref_size);
    OUT("/", 1);
    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("GSP path too long");
    }

    memcpy(path, out->buf, out->buf_size);
    *path_size = out->buf_size;
    path[out->buf_size] = '\0';

    return knd_OK;
}

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

    if (DEBUG_REPO_LEVEL_2)
        knd_log("#%zu COMMIT: \"%.*s\" [size:%zu]",
                commit->numid, commit->rec_size, commit->rec, commit->rec_size);

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
    struct kndTask *task = ctx->task;
    struct kndUserContext *user_ctx = task->user_ctx;
    struct kndSet *idx = task->snapshot->commit_idx;
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
    commit->is_restored = true;

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
    err = knd_set_add(idx, commit->id, commit->id_size, (void*)commit);
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

static gsl_err_t parse_WAL(void *obj, const char *rec, size_t *total_size)
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

static int restore_commits(struct kndRepo *repo, struct kndMemBlock *memblock, struct kndTask *task)
{
    size_t total_size;

    struct LocalContext ctx = {
        .task = task,
        .repo = repo
    };
    struct gslTaskSpec specs[] = {
        { .name = "WAL",
          .name_size = strlen("WAL"),
          .parse = parse_WAL,
          .obj = &ctx
        }
    };
    gsl_err_t parser_err;

    task->type = KND_TASK_RESTORE;
    total_size = memblock->buf_size;

    parser_err = gsl_parse_task(memblock->buf, &total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        // knd_log("WAL parsing failed: %d", parser_err.code);
        return gsl_err_to_knd_err_codes(parser_err);
    }
    return knd_OK;
}

static int restore_journals(struct kndRepo *self, struct kndRepoSnapshot *snapshot,
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

    for (size_t i = 0; i < snapshot->max_journals; i++) {
        out->reset(out);
        OUT(path, path_size);
        err = out->writef(out, "journal_%zu.log", i);
        if (err) return err;
        if (out->buf_size >= KND_PATH_SIZE) return knd_LIMIT;
        memcpy(buf, out->buf, out->buf_size);
        buf_size = out->buf_size;
        buf[buf_size] = '\0';

        if (stat(buf, &st)) break;
        if (DEBUG_REPO_LEVEL_2) {
            knd_log(".. restoring the journal file: %.*s", buf_size, buf);
        }

        block_size = (size_t)st.st_size + footer_size;
        err = knd_memblock_new(&memblock, i, block_size);
        KND_TASK_ERR("failed to alloc a memblock");
        
        err = knd_memblock_read_file(memblock, buf, (size_t)st.st_size);
        KND_TASK_ERR("failed to read memblock from %s {size %zu}", out->buf, st.st_size);

        err = restore_commits(self, memblock, task);
        KND_TASK_ERR("failed to restore commits from %s", out->buf);

        append_memblock(snapshot, memblock);
        snapshot->num_journals[agent_id] = i;
    }
    return knd_OK;
}

int knd_repo_restore(struct kndRepo *self, struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    char path[KND_PATH_SIZE + 1];
    size_t path_size;
    struct kndOutput *out = task->file_out;
    struct stat st;
    int err;

    if (DEBUG_REPO_LEVEL_TMP) {
        const char *owner_name = "/";
        size_t owner_name_size = 1;
        switch (task->user_ctx->type) {
        case KND_USER_AUTHENTICATED:
            owner_name = task->user_ctx->inst->name;
            owner_name_size =  task->user_ctx->inst->name_size;
            break;
        default:
            break;
        }
        knd_log(".. restoring the latest {snapshot #%zu of repo \"%.*s\" (owner:%.*s) ",
                snapshot->numid, self->name_size, self->name, owner_name_size, owner_name);
    }

    // restore recent commits
    for (size_t i = 0; i < KND_MAX_TASKS; i++) {
        out->reset(out);
        OUT(snapshot->repo->path, snapshot->repo->path_size);
        OUTF("snapshot_%zu/", snapshot->numid);
        OUTF("agent_%zu/", i);

        if (stat(out->buf, &st)) {
            if (DEBUG_REPO_LEVEL_TMP)
                knd_log("-- no such folder: \"%.*s\"", out->buf_size, out->buf);

            // sys agent 0 folder is optional
            if (i == 0) continue;
            break;
        }
        if (out->buf_size > KND_PATH_SIZE) return knd_LIMIT;
        memcpy(path, out->buf, out->buf_size);
        path_size = out->buf_size;
        path[path_size] = '\0';

        err = restore_journals(self, snapshot, path, path_size, i, task);
        KND_TASK_ERR("failed to restore journals in \"%.*s\"", path_size, path);
    }

    if (snapshot->commit_idx->num_elems == 0) {
        knd_log("-- no commits to restore in repo \"%.*s\"",
                self->name_size, self->name);
        return knd_OK;
    }

    if (DEBUG_REPO_LEVEL_3) {
        knd_log("== total commits to restore in {repo %.*s}: %zu",
                self->name_size, self->name, snapshot->commit_idx->num_elems);
    }

    /* all commits are there in the idx,
       let's apply them in timely order */

    // TODO use set_reduce to aggregate
    task->repo = self;
    err = knd_set_map(snapshot->commit_idx, NULL, NULL, NULL, knd_apply_commit, (void*)task);
    KND_TASK_ERR("failed to apply commits");

    atomic_store_explicit(&snapshot->num_commits, snapshot->commit_idx->num_elems,
                          memory_order_relaxed);

    if (DEBUG_REPO_LEVEL_TMP) {
        knd_log("== {repo %.*s} {total-commits %zu}",
                self->name_size, self->name, snapshot->num_commits);
    }
    return knd_OK;
}

static int read_class_name_idx(struct kndSharedDict *class_name_idx, struct kndTask *task)
{
    struct kndSet *idx = class_name_idx->idx;
    struct kndStorageLeaf *leaf;
    int err;

    assert (idx != NULL);

    if (DEBUG_REPO_LEVEL_TMP) {
        knd_log(".. unmarshalling {class-name-idx}");
    }

    FOREACH (leaf, idx->leaves) {
        err = knd_set_leaf_open(idx, leaf, knd_class_name_unmarshall, task);
        KND_TASK_ERR("failed to read an idx {leaf %.*s}", leaf->name_size, leaf->name);
    }
    return knd_OK;
}

static int read_attr_name_idx(struct kndSharedDict *attr_name_idx, struct kndTask *task)
{
    struct kndSet *idx = attr_name_idx->idx;
    struct kndStorageLeaf *leaf;
    int err;

    assert (idx != NULL);

    if (DEBUG_REPO_LEVEL_TMP) {
        knd_log(".. reading {attr-name-idx}");
    }

    FOREACH (leaf, idx->leaves) {
        err = knd_set_leaf_open(idx, leaf, knd_attr_name_unmarshall, task);
        KND_TASK_ERR("failed to read attr name idx");
    }
    return knd_OK;
}

static int read_str_idx(struct kndSharedSet *idx, struct kndTask *task)
{
    struct kndStorageLeaf *leaf;
    int err;

    assert (idx != NULL);

    if (DEBUG_REPO_LEVEL_TMP) {
        knd_log(".. reading {str-idx}");
    }

    FOREACH (leaf, idx->leaves) {
        err = knd_shared_set_leaf_open(idx, leaf, NULL, task);
        KND_TASK_ERR("failed to read str idx");
    }
    return knd_OK;
}

static int read_class_idx(struct kndSharedSet *idx, struct kndTask *task)
{
    struct kndStorageLeaf *leaf;
    int err;

    if (DEBUG_REPO_LEVEL_2) {
        knd_log(".. reading {class-idx}");
    }

    FOREACH (leaf, idx->leaves) {
        err = knd_shared_set_leaf_open(idx, leaf, NULL, task);
        KND_TASK_ERR("failed to read class idx");
    }
    return knd_OK;
}

static gsl_err_t check_repo_name(void *obj, const char *val, size_t val_size)
{
    struct kndRepoSnapshot *snapshot = obj;
    struct kndRepo *repo = snapshot->repo;

    if (val_size != repo->name_size)  return make_gsl_err(gsl_FAIL);
    if (memcmp(repo->name, val, val_size)) return make_gsl_err(gsl_FAIL);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_snapshot_numid(void *obj, const char *val, size_t val_size)
{
    struct kndTask *task = obj;
    struct kndRepoSnapshot *snapshot = task->snapshot;
    int err;

    knd_calc_num_id(val, val_size, &snapshot->numid);

    err = knd_snapshot_build_path(snapshot, task);
    if (err) {
        KND_TASK_LOG("failed to build a snapshot path");
        return make_gsl_err_external(err);
    }

    if (DEBUG_REPO_LEVEL_TMP) {
        knd_log("{snapshot %.*s}", snapshot->path_size, snapshot->path);
    }
    return make_gsl_err(gsl_OK);
}

static int build_leaf_filepath(struct kndStorageLeaf *leaf, const char *path, size_t path_size,
                               struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    out->reset(out);
    OUT(path, path_size);
    OUT(leaf->name, leaf->name_size);
    OUT(KND_GSP_FILE_EXT_NAME, strlen(KND_GSP_FILE_EXT_NAME));

    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("GSP path too long");
    }

    memcpy(leaf->filepath, out->buf, out->buf_size);
    leaf->filepath[out->buf_size] = '\0';
    leaf->filepath_size = out->buf_size;

    return knd_OK;
}

static gsl_err_t set_leaf_name(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndStorageLeaf *leaf = ctx->leaf_tail;
    int err;

    assert (leaf != NULL);

    if (val_size >= KND_SHORT_NAME_SIZE) {
        err = knd_LIMIT;
        KND_TASK_LOG("failed to build a snapshot path");
        return make_gsl_err_external(err);
    }
    memcpy(leaf->name, val, val_size);
    leaf->name_size = val_size;

    err = build_leaf_filepath(leaf, ctx->path, ctx->path_size, task);
    if (err) {
        KND_TASK_LOG("failed to build a leaf path");
        return make_gsl_err_external(err);
    }

    if (DEBUG_REPO_LEVEL_TMP) {
        knd_log("{leaf %.*s {filepath %.*s}}",
                leaf->name_size, leaf->name, leaf->filepath_size, leaf->filepath);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_idx_leaf(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndStorageLeaf *leaf;
    int err;
 
    err = knd_storage_leaf_new(&leaf, 0);
    if (err) {
        KND_TASK_LOG("failed to alloc a storage leaf");
        return *total_size = 0, make_gsl_err_external(err);
    }

    append_leaf(ctx, leaf);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_leaf_name,
          .obj = ctx
        },
        {   .name = "from-addr",
            .name_size = strlen("from-addr"),
            .buf = leaf->range_from_addr,
            .buf_size = &leaf->range_from_addr_size,
            .max_buf_size = KND_ID_SIZE
        },
        {   .name = "to-addr",
            .name_size = strlen("to-addr"),
            .buf = leaf->range_to_addr,
            .buf_size = &leaf->range_to_addr_size,
            .max_buf_size = KND_ID_SIZE
        },
        {   .name = "num-elems",
            .name_size = strlen("num-elems"),
            .parse = gsl_parse_size_t,
            .obj = &leaf->num_elems
        },
        {   .name = "file-size",
            .name_size = strlen("file-size"),
            .parse = gsl_parse_size_t,
            .obj = &leaf->curr_size
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_idx_leaf_array(void *obj, const char *rec, size_t *total_size)
{
    struct gslTaskSpec spec = {
        .is_list_item = true,
        .parse = parse_idx_leaf,
        .obj = obj
    };
    return gsl_parse_array(&spec, rec, total_size);
}

static gsl_err_t parse_class_name_idx(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndRepoSnapshot *snapshot = task->snapshot;
    struct kndSharedDict *class_name_idx = snapshot->idxs.class_name_idx;
    const char *pref = "class-name-idx";
    size_t pref_size = strlen(pref);
    struct kndSet *idx;
    char path[KND_PATH_SIZE + 1];
    size_t path_size;
    gsl_err_t parser_err;
    int err;

    err = knd_set_new(&idx, KND_SET_MULTIPLE_VALUES, task->mempool);
    if (err) {
        KND_TASK_LOG("failed to alloc a set");
        return *total_size = 0, make_gsl_err_external(err);
    }

    err = build_path(path, &path_size, snapshot->path, snapshot->path_size, pref, pref_size, task);
    if (err) {
        KND_TASK_LOG("failed to build a path for {idx %.*s}", pref_size, pref);
        return *total_size = 0, make_gsl_err_external(err);
    }

    class_name_idx->idx = idx;

    struct LocalContext ctx = {
          .task = task,
          .path = path,
          .path_size = path_size
    };

    struct gslTaskSpec specs[] = {
        {   .name = "leaf",
            .name_size = strlen("leaf"),
            .type = GSL_GET_ARRAY_STATE,
            .parse = parse_idx_leaf_array,
            .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- config parse error: %d", parser_err.code);
        return parser_err;
    }

    idx->leaves = ctx.leaves;
    idx->leaf_tail = ctx.leaf_tail;
    idx->num_leaves = ctx.num_leaves;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_attr_name_idx(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndRepoSnapshot *snapshot = task->snapshot;
    struct kndSharedDict *attr_name_idx = snapshot->idxs.attr_name_idx;
    const char *pref = "attr-name-idx";
    size_t pref_size = strlen(pref);
    char path[KND_PATH_SIZE + 1];
    size_t path_size;
    struct kndSet *idx;
    gsl_err_t parser_err;
    int err;

    err = knd_set_new(&idx, KND_SET_MULTIPLE_VALUES, task->mempool);
    if (err) {
        KND_TASK_LOG("failed to alloc a shared set");
        return *total_size = 0, make_gsl_err_external(err);
    }

    err = build_path(path, &path_size, snapshot->path, snapshot->path_size, pref, pref_size, task);
    if (err) {
        KND_TASK_LOG("failed to build a path for {idx %.*s}", pref_size, pref);
        return *total_size = 0, make_gsl_err_external(err);
    }

    attr_name_idx->idx = idx;

    struct LocalContext ctx = {
        .task = task,
        .path = path,
        .path_size = path_size
    };

    struct gslTaskSpec specs[] = {
        {   .name = "leaf",
            .name_size = strlen("leaf"),
            .type = GSL_GET_ARRAY_STATE,
            .parse = parse_idx_leaf_array,
            .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- config parse error: %d", parser_err.code);
        return parser_err;
    }

    idx->leaves = ctx.leaves;
    idx->leaf_tail = ctx.leaf_tail;
    idx->num_leaves = ctx.num_leaves;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_idx(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndRepoSnapshot *snapshot = task->snapshot;
    struct kndSharedSet *idx = snapshot->idxs.class_idx;
    const char *pref = "classes";
    size_t pref_size = strlen(pref);
    char path[KND_PATH_SIZE + 1];
    size_t path_size;
    gsl_err_t parser_err;
    int err;

    err = build_path(path, &path_size, snapshot->path, snapshot->path_size, pref, pref_size, task);
    if (err) {
        KND_TASK_LOG("failed to build a path for %.*s idx", pref_size, pref);
        return *total_size = 0, make_gsl_err_external(err);
    }

    struct LocalContext ctx = {
        .task = task,
        .path = path,
        .path_size = path_size
    };

    struct gslTaskSpec specs[] = {
        {   .name = "leaf",
            .name_size = strlen("leaf"),
            .type = GSL_GET_ARRAY_STATE,
            .parse = parse_idx_leaf_array,
            .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- config parse error: %d", parser_err.code);
        return parser_err;
    }

    idx->leaves = ctx.leaves;
    idx->leaf_tail = ctx.leaf_tail;
    idx->num_leaves = ctx.num_leaves;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_string_idx(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndRepoSnapshot *snapshot = task->snapshot;
    struct kndSharedSet *idx = snapshot->idxs.str_idx;
    const char *pref = "strings";
    size_t pref_size = strlen(pref);
    char path[KND_PATH_SIZE + 1];
    size_t path_size;
    gsl_err_t parser_err;
    int err;

    err = build_path(path, &path_size, snapshot->path, snapshot->path_size, pref, pref_size, task);
    if (err) {
        KND_TASK_LOG("failed to build a path for {idx %.*s}", pref_size, pref);
        return *total_size = 0, make_gsl_err_external(err);
    }

    struct LocalContext ctx = {
        .task = task,
        .path = path,
        .path_size = path_size
    };

    struct gslTaskSpec specs[] = {
        {   .name = "leaf",
            .name_size = strlen("leaf"),
            .type = GSL_GET_ARRAY_STATE,
            .parse = parse_idx_leaf_array,
            .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- config parse error: %d", parser_err.code);
        return parser_err;
    }

    idx->leaves = ctx.leaves;
    idx->leaf_tail = ctx.leaf_tail;
    idx->num_leaves = ctx.num_leaves;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_snapshot(void *obj, const char *rec, size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .run = set_snapshot_numid,
            .obj = obj
        },
        {   .name = "class-name-idx",
            .name_size = strlen("class-name-idx"),
            .parse = parse_class_name_idx,
            .obj = obj
        },
        {   .name = "attr-name-idx",
            .name_size = strlen("attr-name-idx"),
            .parse = parse_attr_name_idx,
            .obj = obj
        },
        {   .name = "classes",
            .name_size = strlen("classes"),
            .parse = parse_class_idx,
            .obj = obj
        },
        {   .name = "strings",
            .name_size = strlen("strings"),
            .parse = parse_string_idx,
            .obj = obj
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_config(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .run = check_repo_name,
            .obj = task->snapshot
        },
        {   .name = "snapshot",
            .name_size = strlen("snapshot"),
            .parse = parse_snapshot,
            .obj = task
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

static int read_repo_meta(struct kndRepo *repo, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndOutput *file_out = task->file_out;
    const char *filename;
    struct stat st;
    size_t total_parsed;
    gsl_err_t parser_err;
    int err;

    struct gslTaskSpec specs[] = {
        {
            .name = "repo",
            .name_size = strlen("repo"),
            .parse = parse_config,
            .obj = task
        }
    };

    out->reset(out);
    OUT(repo->path, repo->path_size);
    OUT("repo.gsl", strlen("repo.gsl"));
    filename = out->buf;

    if (stat(filename, &st)) {
        knd_log("-- no repo meta found, assume new empty repo");
        return knd_OK;
    }

    file_out->reset(file_out);
    err = file_out->write_file_content(file_out, filename);
    if (err) {
        knd_log("failed to read {file %s}", filename);
        return err;
    }
    total_parsed = file_out->buf_size;

    parser_err = gsl_parse_task(file_out->buf, &total_parsed, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code != gsl_OK) {
        KND_TASK_LOG("failed to read configuration file");
        return gsl_err_to_knd_err_codes(parser_err);
    }
    task->snapshot->state = KND_SNAPSHOT_FULL;
    return knd_OK;
}

static int fetch_latest_snapshot(struct kndRepo *repo, struct kndRepoSnapshot **result,
                                 struct kndTask *task)
{
    struct kndRepoSnapshot *snapshot;
    int err;

    err = knd_repo_snapshot_new(&snapshot, 0, 0, repo, task);
    KND_TASK_ERR("failed to alloc a repo snapshot");

    atomic_store_explicit(&repo->snapshot, snapshot, memory_order_relaxed);
    task->snapshot = snapshot;

    err = read_repo_meta(repo, task);
    KND_TASK_ERR("failed to read repo metadata");

    snapshot->role = task->role;
    *result = snapshot;
    return knd_OK;
}

int knd_repo_snapshot_read(struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndSharedDict *class_name_idx = snapshot->idxs.class_name_idx;
    struct kndSharedSet *class_idx = snapshot->idxs.class_idx;
    struct kndSharedDict *attr_name_idx = snapshot->idxs.attr_name_idx;
    struct kndSharedSet *str_idx = snapshot->idxs.str_idx;
    int err;

    if (DEBUG_REPO_LEVEL_TMP) {
        knd_log(".. reading {latest-snapshot %.*s}",
                snapshot->path_size, snapshot->path);
    }

    err = read_class_name_idx(class_name_idx, task);
    KND_TASK_ERR("failed to read class name idx in {snapshot #%zu {path %.*s}}",
                 snapshot->numid, snapshot->path_size, snapshot->path);

    err = read_attr_name_idx(attr_name_idx, task);
    KND_TASK_ERR("failed to read attr name idx in {snapshot #%zu {path %.*s}}",
                 snapshot->numid, snapshot->path_size, snapshot->path);

    err = read_str_idx(str_idx, task);
    KND_TASK_ERR("failed to read strings idx in {snapshot #%zu {path %.*s}}",
                 snapshot->numid, snapshot->path_size, snapshot->path);

    err = read_class_idx(class_idx, task);
    KND_TASK_ERR("failed to read class idx in {snapshot #%zu {path %.*s}}",
                 snapshot->numid, snapshot->path_size, snapshot->path);

    err = knd_repo_cache_update(snapshot, task);
    KND_TASK_ERR("failed to update a repo cache");

    return knd_OK;
}

int knd_repo_read(struct kndRepo *self, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndRepoSnapshot *snapshot;
    int err;

    assert(mempool != NULL);

    if (DEBUG_REPO_LEVEL_TMP) {
        const char *owner_name = "/";
        size_t owner_name_size = 1;
        switch (task->user_ctx->type) {
        case KND_USER_AUTHENTICATED:
            owner_name = task->user_ctx->inst->name;
            owner_name_size =  task->user_ctx->inst->name_size;
            break;
        default:
            break;
        }
        const char *agent_role_name = knd_agent_role_names[task->role];
        knd_log(">> open {repo %.*s {owner %.*s}  {open-mode %s}  {system-path %.*s}",
                self->name_size, self->name, owner_name_size, owner_name,
                agent_role_name, self->path_size, self->path);
    }
    err = fetch_latest_snapshot(self, &snapshot, task);
    KND_TASK_ERR("failed to fetch any repo snapshots");
    task->snapshot = snapshot;
    task->idxs = &snapshot->idxs;

    switch (snapshot->state) {
    case KND_SNAPSHOT_INIT:
        switch (task->user_ctx->type) {
        case KND_USER_DEFAULT:
            err = knd_repo_read_sources(self, task);
            KND_TASK_ERR("failed to read GSL sources");
            break;
        default:
            break;
        }
        return knd_OK;
    default:
        break;
    }

    task->type = KND_TASK_READ_SNAPSHOT;
    err = knd_repo_snapshot_read(snapshot, task);
    KND_TASK_ERR("failed to read the latest snapshot");

    task->type = KND_TASK_RESTORE;
    err = knd_repo_restore(self, snapshot, task);
    KND_TASK_ERR("failed to restore {repo %.*s}", self->name_size, self->name);

    return knd_OK;
}
