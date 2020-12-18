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
#include "knd_shard.h"
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

#define DEBUG_REPO_LEVEL_0 0
#define DEBUG_REPO_LEVEL_1 0
#define DEBUG_REPO_LEVEL_2 0
#define DEBUG_REPO_LEVEL_3 0
#define DEBUG_REPO_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
};

static int update_indices(struct kndRepo *self, struct kndCommit *commit, struct kndTask *task);

static void free_blocks(struct kndRepo *repo)
{
    struct kndMemBlock *block, *next_block = NULL;
    for (block = repo->blocks; block; block = next_block) {
        next_block = block->next;
        if (block->buf)
            free(block->buf);
        free(block);
    }
    repo->total_block_size = 0;
    repo->num_blocks = 0;
}

void knd_repo_del(struct kndRepo *self)
{
    knd_shared_dict_del(self->class_name_idx);
    knd_shared_dict_del(self->attr_name_idx);
    knd_shared_dict_del(self->proc_name_idx);
    knd_shared_dict_del(self->proc_arg_name_idx);

    if (self->num_source_files) {
        for (size_t i = 0; i < self->num_source_files; i++)
            free(self->source_files[i]);
        free(self->source_files);
    }

    if (self->num_blocks)
        free_blocks(self);
    
    free(self);
}

static int write_filepath(struct kndOutput *out, struct kndConcFolder *folder)
{
    int err;
    if (folder->parent) {
        err = write_filepath(out, folder->parent);
        if (err) return err;
    }
    OUT(folder->name, folder->name_size);
    return knd_OK;
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
            
    err = knd_parse_num(buf, &numval);
    if (err) {
        return make_gsl_err_external(err);
    }

    commit->numid = (size_t)numval;
    knd_uid_create(commit->numid, commit->id, &commit->id_size);

    //knd_log("++ commit #%zu", commit->numid);
    
    return make_gsl_err(gsl_OK);
}

static int present_latest_state_JSON(struct kndRepo *self, struct kndOutput *out)
{
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size;
    struct kndRepoSnapshot *snapshot = atomic_load_explicit(&self->snapshots, memory_order_relaxed);
    size_t latest_commit_id = atomic_load_explicit(&snapshot->num_commits, memory_order_relaxed);
    struct kndCommit *commit;
    int err;

    out->reset(out);
    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->write(out, "\"repo\":", strlen("\"repo\":"));                      RET_ERR();
    err = out->writec(out, '"');                                                  RET_ERR();
    err = out->write(out,  self->name, self->name_size);                          RET_ERR();
    err = out->writec(out, '"');                                                  RET_ERR();

    err = out->write(out, ",\"_state\":", strlen(",\"_state\":"));                RET_ERR();
    err = out->writef(out, "%zu", latest_commit_id);                              RET_ERR();

    if (latest_commit_id) {
        knd_uid_create(latest_commit_id, idbuf, &idbuf_size);

        //
        err = knd_set_get(snapshot->commit_idx, idbuf, idbuf_size, (void**)&commit);  RET_ERR();
        err = out->write(out, ",\"_time\":", strlen(",\"_time\":"));              RET_ERR();
        err = out->writef(out, "%zu", (size_t)commit->timestamp);                 RET_ERR();
        //err = present_commit_JSON(commit, out);  RET_ERR();
    } else {
        err = out->write(out, ",\"_time\":", strlen(",\"_time\":"));              RET_ERR();
        err = out->writef(out, "%zu", (size_t)snapshot->timestamp);          RET_ERR();
    }
    err = out->writec(out, '}');                                                  RET_ERR();
    return knd_OK;
}

static gsl_err_t present_repo_state(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct kndTask *task = obj;
    struct kndRepo *repo = task->repo;
    struct kndOutput *out = task->out;
    // struct kndMemPool *mempool = task->mempool;
    int err;

    knd_log(".. present repo state..");

    if (!repo) {
        knd_log("-- no repo selected");
        out->reset(out);
        err = out->writec(out, '{');
        if (err) return make_gsl_err_external(err);
        err = out->writec(out, '}');
        if (err) return make_gsl_err_external(err);
        return make_gsl_err(gsl_OK);
    }

    
    task->type = KND_SELECT_STATE;

    /* restore:    if (!repo->commits) goto show_curr_state;
    commit = repo->commits;
    if (task->state_gt >= commit->numid) goto show_curr_state;
    */

    // TODO: handle lt and eq cases
    //if (task->state_lt && task->state_lt < task->state_gt) goto show_curr_state;

    // TODO
    // size_t latest_commit_id = atomic_load_explicit(&repo->snapshots->num_commits, memory_order_relaxed);
    // task->state_lt = latest_commit_id + 1;

    switch (task->ctx->format) {
    default:
        err = present_latest_state_JSON(repo, out);  
        if (err) return make_gsl_err_external(err);
        break;
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_repo_state(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = knd_set_curr_state,
          .obj = task
        },
        { .name = "gt",
          .name_size = strlen("gt"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_gt
        },
        { .name = "gte",
          .name_size = strlen("gte"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_gte
        },
        { .name = "lt",
          .name_size = strlen("lt"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_lt
        },
        { .name = "lte",
          .name_size = strlen("lte"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->state_lte
        },
        { .is_default = true,
          .run = present_repo_state,
          .obj = task
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t run_select_repo(void *obj, const char *name, size_t name_size)
{
    struct kndTask *task = obj;
    struct kndRepo *repo = task->repo;

    if (!name_size) return make_gsl_err(gsl_FAIL);

    switch (*name) {
    case '/':
        task->user_ctx->repo = repo;
        // default system repo stays
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("== system repo selected: %.*s", repo->name_size, repo->name);
        return make_gsl_err(gsl_OK);
    case '~':
        repo = task->user_ctx->repo;
        if (DEBUG_REPO_LEVEL_2)
            knd_log("== user base repo selected: %.*s",
                    repo->name_size, repo->name);
        task->repo = repo;
        return make_gsl_err(gsl_OK);
    default:
        task->repo = task->user_ctx->repo;
        break;
    }

    /*repo = knd_shared_dict_get(task->shard->repo_name_idx, name, name_size);
    if (!repo) {
        KND_TASK_LOG("no such repo: %.*s", name_size, name);
        return make_gsl_err(gsl_FAIL);
    }
    task->repo = repo;
    */
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_select(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndUserContext *ctx = task->user_ctx;
    struct kndRepo *repo = ctx->repo ? ctx->repo : task->repo;
    return knd_class_select(repo, rec, total_size, task);
}

static gsl_err_t parse_class_import(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndUserContext *ctx = task->user_ctx;
    struct kndRepo *repo = ctx->repo ? ctx->repo : task->repo;
    int err;

    if (task->type != KND_LOAD_STATE) {
        task->type = KND_COMMIT_STATE;
        if (!task->ctx->commit) {
            err = knd_commit_new(task->mempool, &task->ctx->commit);
            if (err) return make_gsl_err_external(err);

            task->ctx->commit->orig_state_id = atomic_load_explicit(&task->repo->snapshots->num_commits,
                                                                    memory_order_relaxed);
        }
    }
    return knd_class_import(repo, rec, total_size, task);
}

static gsl_err_t parse_snapshot_task(void *obj, const char *unused_var(rec), size_t *total_size)
{
    struct kndTask *task = obj;
    int err;

    task->type = KND_SNAPSHOT_STATE;
    err = knd_repo_snapshot(task->repo, task);
    if (err) {
        KND_TASK_LOG("failed to build a snapshot of repo %.*s", task->repo->name_size, task->repo->name);
        return *total_size = 0, make_gsl_err(gsl_FAIL);
    }
    return *total_size = 0, make_gsl_err(gsl_OK);
}

static gsl_err_t decode_seq(void *obj, const char *val, size_t val_size)
{
    struct kndTask *task = obj;
    struct kndCharSeq *seq;
    int err;

    err = knd_charseq_decode(task->repo, val, val_size, &seq, task);
    if (err) {
        KND_TASK_LOG("failed to decode a text charseq %.*s", val_size, val);
        return make_gsl_err_external(err);
    }
    if (DEBUG_REPO_LEVEL_3)
        knd_log(">> text seq:%.*s", seq->val_size, seq->val);
    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_parse_repo(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
            .is_selector = true,
            .run = run_select_repo,
            .obj = task
        },
        { .type = GSL_SET_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_import,
          .obj = task
        },
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_select,
          .obj = task
        },
        { .name = "_state",
          .name_size = strlen("_state"),
          .parse = parse_repo_state,
          .obj = task
        },
        { .name = "_commit_from",
          .name_size = strlen("_commit_from"),
          .parse = gsl_parse_size_t,
          .obj = &task->state_eq
        },
        { .name = "_snapshot",
          .name_size = strlen("_snapshot"),
          .parse = parse_snapshot_task,
          .obj = task
        },
        { .name = "_seq",
          .name_size = strlen("_seq"),
          .run = decode_seq,
          .obj = task
        },
        { .is_default = true,
          .run = present_repo_state,
          .obj = task
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t run_read_include(void *obj, const char *name, size_t name_size)
{
    struct kndTask *task = obj;
    struct kndConcFolder *folder;
    struct kndMemPool *mempool = task->mempool;
    int err;

    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. include file name: \"%.*s\" [%zu]", (int)name_size, name, name_size);
    if (!name_size) return make_gsl_err(gsl_FORMAT);

    err = knd_conc_folder_new(mempool, &folder);
    if (err) {
        knd_log("failed to alloc a conc folder");
        return make_gsl_err_external(knd_NOMEM);
    }
    folder->name = name;
    folder->name_size = name_size;

    folder->next = task->folders;
    task->folders = folder;
    task->num_folders++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_proc_import(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndUserContext *ctx = task->user_ctx;
    struct kndRepo *repo = task->repo;
    int err;

    if (ctx) {
        repo = ctx->repo;
        task->repo = repo;
    }

    if (task->type != KND_LOAD_STATE) {
        task->type = KND_COMMIT_STATE;

        if (!task->ctx->commit) {
            err = knd_commit_new(task->mempool, &task->ctx->commit);
            if (err) return make_gsl_err_external(err);

            task->ctx->commit->orig_state_id = atomic_load_explicit(&task->repo->snapshots->num_commits,
                                                                    memory_order_relaxed);
        }
    }
    return knd_proc_import(task->repo, rec, total_size, task);
}

static gsl_err_t run_get_schema(void *obj, const char *name, size_t name_size)
{
    struct kndTask *self = obj;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    /* set current schema */
    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. select repo schema: \"%.*s\"..",
                name_size, name);

    self->repo->schema_name = name;
    self->repo->schema_name_size = name_size;
    return make_gsl_err(gsl_OK);
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

static gsl_err_t parse_commit(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = ctx->repo;
    struct kndUserContext *user_ctx = task->user_ctx;
    struct kndSet *idx = repo->snapshots->commit_idx;
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
    err = idx->add(idx, commit->id, commit->id_size, (void*)commit);
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

    task->type = KND_RESTORE_STATE;
    total_size = memblock->buf_size;

    parser_err = gsl_parse_task(memblock->buf, &total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        // knd_log("WAL parsing failed: %d", parser_err.code);
        return gsl_err_to_knd_err_codes(parser_err);
    }
    return knd_OK;
}

static int restore_journals(struct kndRepo *self, struct kndRepoSnapshot *snapshot,
                            const char *path, size_t path_size, struct kndTask *task)
{
    struct kndOutput *out = task->file_out;
    char buf[KND_PATH_SIZE + 1];
    size_t buf_size;
    struct stat st;
    struct kndMemBlock *memblock;
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
        if ((size_t)st.st_size >= snapshot->max_journal_size) {
            err = knd_LIMIT;
            KND_TASK_ERR("journal size limit reached");
        }

        if (DEBUG_REPO_LEVEL_2)
            knd_log(".. restoring the journal file: %.*s", buf_size, buf);

        err = knd_task_read_file_block(task, buf, (size_t)st.st_size, &memblock);
        KND_TASK_ERR("failed to read memblock from %s", out->buf);

        err = restore_commits(self, memblock, task);
        KND_TASK_ERR("failed to restore commits from %s", out->buf);

        /* restore prev path */
        out->rtrim(out, buf_size);
    }

    return knd_OK;
}

static int fetch_str_idx(struct kndRepo *self, const char *path, size_t path_size, struct kndTask *task)
{
    struct kndOutput *out = task->file_out;
    struct stat st;
    const char *filename = "strings.gsp";
    size_t filename_size = strlen(filename);
    int err;

    out->reset(out);
    OUT(path, path_size);
    OUT(filename, filename_size);
    if (stat(out->buf, &st)) {
        return knd_NO_MATCH;
    }
    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. reading str idx \"%.*s\" [%zu]", out->buf_size, out->buf, (size_t)st.st_size);

    err = knd_shared_set_unmarshall_file(self->str_idx, out->buf, out->buf_size,
                                         (size_t)st.st_size, knd_charseq_unmarshall, task);
    KND_TASK_ERR("failed to unmarshall str idx file");

    atomic_store_explicit(&self->num_strs, self->str_idx->num_elems, memory_order_relaxed);
    return knd_OK;
}

static int fetch_class_storage(struct kndRepo *self, const char *path, size_t path_size, struct kndTask *task)
{
    struct kndOutput *out = task->file_out;
    struct stat st;
    const char *filename = "classes.gsp";
    size_t filename_size = strlen(filename);
    int err;

    out->reset(out);
    OUT(path, path_size);
    OUT(filename, filename_size);
    if (stat(out->buf, &st)) {
        return knd_NO_MATCH;
    }
    if (DEBUG_REPO_LEVEL_TMP)
        knd_log(".. reading class storage: %.*s [%zu]", out->buf_size, out->buf, (size_t)st.st_size);

    err = knd_shared_set_unmarshall_file(self->class_idx, out->buf, out->buf_size,
                                         (size_t)st.st_size, knd_class_entry_unmarshall, task);
    KND_TASK_ERR("failed to unmarshall class storage GSP file");
    return knd_OK;
}

static int apply_commit(void *obj, const char *unused_var(elem_id), size_t unused_var(elem_id_size),
                        size_t unused_var(count), void *elem)
{
    struct kndTask *task = obj;
    struct kndUserContext *user_ctx = task->user_ctx;
    struct kndMemPool *mempool = task->mempool;
    struct kndCommit *commit = elem;
    struct kndCommit *head_commit;
    struct kndRepo *repo = task->repo;
    struct kndRepoSnapshot *snapshot = atomic_load_explicit(&repo->snapshots, memory_order_relaxed);
    gsl_err_t parser_err;
    size_t total_size = commit->rec_size;
    int err;

    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. applying commit #%zu: %.*s", commit->numid, commit->rec_size, commit->rec);

    task->mempool = NULL;
    knd_task_reset(task);

    task->type = KND_RESTORE_STATE;
    task->ctx->commit = commit;
    task->user_ctx = user_ctx;
    task->mempool = mempool;

    struct gslTaskSpec specs[] = {
        { .name = "task",
          .name_size = strlen("task"),
          .parse = knd_parse_task,
          .obj = task
        }
    };

    parser_err = gsl_parse_task(commit->rec, &total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    err = knd_resolve_commit(commit, task);
    KND_TASK_ERR("failed to resolve commit #%zu", commit->numid);

    err = update_indices(repo, commit, task);
    KND_TASK_ERR("index update failed");

    do {
        head_commit = atomic_load_explicit(&snapshot->commits, memory_order_acquire);
        commit->prev = head_commit;
    } while (!atomic_compare_exchange_weak(&snapshot->commits, &head_commit, commit));
    /* restore repo ref */
    task->repo = repo;
    return knd_OK;
}

static int resolve_classes(struct kndRepo *self, struct kndTask *task)
{
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndSharedDictItem *item;
    struct kndSharedDict *name_idx = self->class_name_idx;
    int err;

    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. resolving classes in \"%.*s\"..", self->name_size, self->name);

    // TODO: iterate func in kndSharedDict
    for (size_t i = 0; i < name_idx->size; i++) {
        item = atomic_load_explicit(&name_idx->hash_array[i], memory_order_relaxed);
        for (; item; item = item->next) {
            entry = item->data;
            if (!entry->class) {
                knd_log("-- unresolved class entry: %.*s", entry->name_size, entry->name);
                return knd_FAIL;
            }
            c = entry->class;
            if (c->is_resolved) continue;

            err = knd_class_resolve(c, task);
            KND_TASK_ERR("failed to resolve a class: \"%.*s\"", c->entry->name_size, c->entry->name);
            if (DEBUG_REPO_LEVEL_2)
                c->str(c, 1);
        }
    }
    return knd_OK;
}

static int index_classes(struct kndRepo *self, struct kndTask *task)
{
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndSharedDictItem *item;
    struct kndSharedDict *name_idx = self->class_name_idx;
    struct kndSharedSet *class_idx = self->class_idx;
    int err;

    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. indexing classes in \"%.*s\"..", self->name_size, self->name);

    // TODO iterate func
    for (size_t i = 0; i < name_idx->size; i++) {
        item = atomic_load_explicit(&name_idx->hash_array[i], memory_order_relaxed);
        for (; item; item = item->next) {
            entry = item->data;
            c = atomic_load_explicit(&entry->class, memory_order_relaxed);
            if (!c) {
                knd_log("-- unresolved class entry: %.*s", entry->name_size, entry->name);
                return knd_FAIL;
            }
            err = knd_class_index(c, task);
            if (err) {
                knd_log("failed to index the \"%.*s\" class", c->entry->name_size, c->entry->name);
                return err;
            }
            err = knd_shared_set_add(class_idx, entry->id, entry->id_size, (void*)entry);
            if (err) return err;
        }
    }
    return knd_OK;
}

static int resolve_procs(struct kndRepo *self, struct kndTask *task)
{
    struct kndProcEntry *entry;
    struct kndSharedDictItem *item;
    struct kndSharedDict *proc_name_idx = self->proc_name_idx;
    struct kndSet *proc_idx = self->proc_idx;
    int err;

    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. resolving procs of repo \"%.*s\"..",
                self->name_size, self->name);

    for (size_t i = 0; i < proc_name_idx->size; i++) {
        item = atomic_load_explicit(&proc_name_idx->hash_array[i], memory_order_relaxed);
        for (; item; item = item->next) {
            entry = item->data;

            if (entry->proc->is_resolved) {
                continue;
            }

            err = knd_proc_resolve(entry->proc, task);
            if (err) {
                knd_log("-- couldn't resolve the \"%.*s\" proc",
                        entry->proc->name_size, entry->proc->name);
                return err;
            }

            entry->numid = atomic_fetch_add_explicit(&self->proc_id_count, 1, \
                                                     memory_order_relaxed);
            entry->numid++;
            knd_uid_create(entry->numid, entry->id, &entry->id_size);
            
            err = proc_idx->add(proc_idx,
                                entry->id, entry->id_size, (void*)entry);
            if (err) return err;
            if (DEBUG_REPO_LEVEL_2) {
                knd_proc_str(entry->proc, 1);
            }
        }
    }

    /*for (size_t i = 0; i < proc_name_idx->size; i++) {
        item = atomic_load_explicit(&proc_name_idx->hash_array[i],
                                    memory_order_relaxed);
        for (; item; item = item->next) {
            entry = item->data;
            if (!entry->proc->is_computed) {
                err = knd_proc_compute(entry->proc, task);
                if (err) {
                    knd_log("-- couldn't compute the \"%.*s\" proc",
                            entry->proc->name_size, entry->proc->name);
                    return err;
                }
            }
        }
        }*/

    return knd_OK;
}

static gsl_err_t parse_schema(void *obj,
                              const char *rec,
                              size_t *total_size)
{
    struct kndTask *task = obj;

    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. parse schema REC: \"%.*s\"..", 64, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_get_schema,
          .obj = task
        },
        { .type = GSL_SET_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_import,
          .obj = task
        },
        { .type = GSL_SET_STATE,
          .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_import,
          .obj = task
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_include(void *obj,
                               const char *rec,
                               size_t *total_size)
{
    struct kndTask *task = obj;

    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. parse include REC: \"%.*s\"..", 64, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_read_include,
          .obj = task
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int parse_GSL(struct kndTask *task, const char *rec, size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        { .name = "schema",
          .name_size = strlen("schema"),
          .parse = parse_schema,
          .obj = task
        },
        { .name = "include",
          .name_size = strlen("include"),
          .parse = parse_include,
          .obj = task
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}

static int read_GSL_file(struct kndRepo *repo, struct kndConcFolder *parent_folder,
                         const char *filename, size_t filename_size, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndOutput *file_out = task->file_out;
    struct kndConcFolder *folder, *folders;
    const char *c;
    char *rec;
    char **recs;
    size_t folder_name_size;
    const char *index_folder_name = "index";
    size_t index_folder_name_size = strlen("index");
    const char *file_ext = ".gsl";
    size_t file_ext_size = strlen(".gsl");
    size_t chunk_size = 0;
    int err;

    out->reset(out);
    OUT(repo->schema_path, repo->schema_path_size);
    OUT("/", 1);
    if (parent_folder) {
        err = write_filepath(out, parent_folder);
        KND_TASK_ERR("failed to write a filepath");
    }

    OUT(filename, filename_size);
    OUT(file_ext, file_ext_size);

    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. reading GSL file: %.*s", out->buf_size, out->buf);

    file_out->reset(file_out);
    err = file_out->write_file_content(file_out, (const char*)out->buf);
    if (err) {
        knd_log("-- couldn't read GSL class file \"%s\"", out->buf);
        return err;
    }

    // TODO: find another place for storage
    rec = malloc(file_out->buf_size + 1);
    if (!rec) return knd_NOMEM;
    memcpy(rec, file_out->buf, file_out->buf_size);
    rec[file_out->buf_size] = '\0';

    recs = (char**)realloc(repo->source_files, (repo->num_source_files + 1) * sizeof(char*));
    if (!recs) return knd_NOMEM;
    recs[repo->num_source_files] = rec;

    repo->source_files = recs;
    repo->num_source_files++;

    if (DEBUG_REPO_LEVEL_3)
        knd_log("== total GSL files: %zu", repo->num_source_files);

    task->input = rec;
    task->input_size = file_out->buf_size;

    /* actual parsing */
    err = parse_GSL(task, (const char*)rec, &chunk_size);
    if (err) {
        knd_log("-- parsing of \"%.*s\" failed, err: %d", out->buf_size, out->buf, err);
        return err;
    }

    /* high time to read our folders */
    folders = task->folders;
    task->folders = NULL;
    task->num_folders = 0;

    for (folder = folders; folder; folder = folder->next) {
        folder->parent = parent_folder;

        /* reading a subfolder */
        if (folder->name_size > index_folder_name_size) {
            folder_name_size = folder->name_size - index_folder_name_size;
            c = folder->name + folder_name_size;
            if (!memcmp(c, index_folder_name, index_folder_name_size)) {
                /* right trim the folder's name */
                folder->name_size = folder_name_size;

                err = read_GSL_file(repo, folder, index_folder_name, index_folder_name_size, task);
                if (err) {
                    knd_log("-- failed to read file: %.*s",
                            index_folder_name_size, index_folder_name);
                    return err;
                }
                continue;
            }
        }
        err = read_GSL_file(repo, parent_folder, folder->name, folder->name_size, task);
        if (err) {
            knd_log("-- failed to read GSL file: %.*s", folder->name_size, folder->name);
            return err;
        }
    }
    return knd_OK;
}

static int read_source_files(struct kndRepo *self, struct kndTask *task)
{
    int err;

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log(".. initial loading of schema GSL files");

    /* read a system-wide schema */
    task->type = KND_LOAD_STATE;
    err = read_GSL_file(self, NULL, "index", strlen("index"), task);
    KND_TASK_ERR("schema import failed");
        
    err = resolve_classes(self, task);
    KND_TASK_ERR("class resolving failed");
        
    err = resolve_procs(self, task);
    KND_TASK_ERR("proc resolving failed");

    err = index_classes(self, task);
    KND_TASK_ERR("class indexing failed");
    
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
        knd_log(".. restoring the latest snapshot #%zu of repo \"%.*s\" (owner:%.*s) ",
                snapshot->numid, self->name_size, self->name, owner_name_size, owner_name);
    }

    // restore recent commits
    for (size_t i = 0; i < KND_MAX_TASKS; i++) {
        out->reset(out);
        OUT(snapshot->path, snapshot->path_size);
        err = out->writef(out, "agent_%zu/", i);
        if (err) return err;

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

        err = restore_journals(self, snapshot, path, path_size, task);
        KND_TASK_ERR("failed to restore journals in \"%.*s\"", path_size, path);
    }
    if (snapshot->commit_idx->num_elems == 0) {
        knd_log("-- no commits to restore in repo \"%.*s\"", self->name_size, self->name);
        return knd_OK;
    }

    if (DEBUG_REPO_LEVEL_3)
        knd_log("== total commits to restore in repo \"%.*s\": %zu", self->name_size, self->name,
                snapshot->commit_idx->num_elems);

    /* all commits are there in the idx,
       time to apply them in timely order */
    task->repo = self;
    err = snapshot->commit_idx->map(snapshot->commit_idx, apply_commit, (void*)task);
    KND_TASK_ERR("failed to apply commits");
    atomic_store_explicit(&snapshot->num_commits, snapshot->commit_idx->num_elems, memory_order_relaxed);

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("== repo \"%.*s\", total commits applied: %zu", self->name_size, self->name, snapshot->num_commits);

    return knd_OK;
}

int knd_repo_open(struct kndRepo *self, struct kndTask *task)
{
    struct kndOutput *out = task->file_out;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndRepoSnapshot *snapshot;
    char buf[KND_PATH_SIZE];
    size_t buf_size;
    struct stat st;
    int latest_snapshot_id = -1;
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
        knd_log(".. open \"%.*s\" Repo (owner:%.*s  open mode:%d acls:%p)",
                self->name_size, self->name, owner_name_size, owner_name,
                task->role, task->user_ctx->acls);

        out->reset(out);
        mempool->present(mempool, out);
        knd_log("** Repo Mempool\n%.*s", out->buf_size, out->buf);
    }
    out->reset(out);
    OUT(task->path, task->path_size);

    if (task->user_ctx && task->user_ctx->path_size) {
        OUT(task->user_ctx->path, task->user_ctx->path_size);
    }
    if (self->path_size) {
        OUT(self->path, self->path_size);
    }

    for (size_t i = 0; i < KND_MAX_SNAPSHOTS; i++) {
        buf_size = snprintf(buf, KND_TEMP_BUF_SIZE, "snapshot_%zu/", i);
        OUT(buf, buf_size);

        if (DEBUG_REPO_LEVEL_2)
            knd_log(".. try snapshot path: %.*s", out->buf_size, out->buf);

        if (stat(out->buf, &st)) {
            out->rtrim(out, buf_size);
            break;
        }
        latest_snapshot_id = (int)i;
        out->rtrim(out, buf_size);
    }

    if (latest_snapshot_id < 0) {
        knd_log("no snapshots of \"%.*s\" found", self->name_size, self->name);
        switch (task->user_ctx->type) {
        case KND_USER_DEFAULT:
            err = read_source_files(self, task);
            KND_TASK_ERR("failed to read GSL source files");
            break;
        default:
            break;
        }
        return knd_OK;
    }

    if (DEBUG_REPO_LEVEL_2)
        knd_log("== the latest snapshot of \"%.*s\" is #%d", self->name_size, self->name, latest_snapshot_id);

    err = out->writef(out, "snapshot_%d/", latest_snapshot_id);
    KND_TASK_ERR("snapshot path construction failed");

    if (out->buf_size >= KND_PATH_SIZE) return knd_LIMIT;

    err = knd_repo_snapshot_new(mempool, &snapshot);
    KND_TASK_ERR("failed to alloc a repo snapshot");
    snapshot->numid = latest_snapshot_id;
    memcpy(snapshot->path, out->buf, out->buf_size);
    snapshot->path_size = out->buf_size;

    atomic_store_explicit(&self->snapshots, snapshot, memory_order_relaxed);

    /* decode string names */
    err = fetch_str_idx(self, snapshot->path, snapshot->path_size, task);
    if (err && err != knd_NO_MATCH) return err;

    err = fetch_class_storage(self, snapshot->path, snapshot->path_size, task);
    switch (err) {
    case knd_NO_MATCH:
        switch (task->user_ctx->type) {
        case KND_USER_DEFAULT:
            err = read_source_files(self, task);
            KND_TASK_ERR("failed to read GSL source files");
            break;
        default:
            break;
        }
        break;
    case knd_OK:
        break;
    default:
        return err;
    }

    switch (task->role) {
    case KND_READER:
        return knd_OK;
    default:
        break;
    }
    snapshot->role = task->role;
    err = knd_repo_restore(self, snapshot, task);
    KND_TASK_ERR("failed to restore repo \"%.*s\"", self->name_size, self->name);
    return knd_OK;
}

int knd_repo_index_proc_arg(struct kndRepo *repo, struct kndProc *proc,
                            struct kndProcArg *arg, struct kndTask *task)
{
    struct kndMemPool *mempool   = task->mempool;
    struct kndSet *arg_idx       = repo->proc_arg_idx;
    struct kndSharedDict *arg_name_idx = repo->proc_arg_name_idx;
    struct kndProcArgRef *ref, *arg_ref, *next_arg_ref;
    int err;

    /* generate unique attr id */
    arg->numid = atomic_fetch_add_explicit(&repo->proc_arg_id_count, 1,
                                           memory_order_relaxed);
    arg->numid++;
    knd_uid_create(arg->numid, arg->id, &arg->id_size);

    err = knd_proc_arg_ref_new(mempool, &arg_ref);
    if (err) {
        return err;
    }
    arg_ref->arg = arg;
    arg_ref->proc = proc;

    switch (task->type) {
    case KND_RESTORE_STATE:
        // fall through
    case KND_LOAD_STATE:

        err = knd_proc_get_arg(proc, arg->name, arg->name_size, &ref);

        next_arg_ref = knd_shared_dict_get(arg_name_idx, arg->name, arg->name_size);
        arg_ref->next = next_arg_ref;

        err = knd_shared_dict_set(arg_name_idx, arg->name, arg->name_size,
                                  (void*)arg_ref, mempool, NULL, &arg->item, true);
        KND_TASK_ERR("failed to globally register arg name \"%.*s\"", arg->name_size, arg->name);

        err = arg_idx->add(arg_idx, arg->id, arg->id_size, (void*)arg_ref);
        KND_TASK_ERR("failed to globally register numid of arg \"%.*s\"", arg->name_size, arg->name);

        return knd_OK;
    default:
        break;
    }

    /* local task name idx */
    err = knd_dict_set(task->proc_arg_name_idx, arg->name, arg->name_size, (void*)arg_ref);
    KND_TASK_ERR("failed to register arg name %.*s", arg->name_size, arg->name);

    if (DEBUG_REPO_LEVEL_2)
        knd_log("++ new primary arg: \"%.*s\" (id:%.*s) of \"%.*s\" (repo:%.*s)",
                arg->name_size, arg->name, arg->id_size, arg->id, proc->name_size, proc->name, repo->name_size, repo->name);

    return knd_OK;
}

static int export_commit_GSL(struct kndRepo *self, struct kndCommit *commit, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndStateRef *ref;
    struct kndState *state;
    struct kndClassEntry *entry;
    struct kndClassInst *user_inst;
    int err;

    out->reset(out);
    task->ctx->max_depth = KND_MAX_DEPTH;
    OUT("{task", strlen("{task"));

    switch (task->user_ctx->type) {
    case KND_USER_AUTHENTICATED:
        user_inst = task->user_ctx->inst;
        OUT("{user ", strlen("{user "));
        OUT(user_inst->name, user_inst->name_size);
        break;
    default:
        break;
    }

    OUT("{repo ", strlen("{repo "));
    OUT(self->name, self->name_size);

    for (ref = commit->class_state_refs; ref; ref = ref->next) {
        entry = ref->obj;
        if (!entry) continue;

        state = ref->state;

        err = out->writec(out, '{');                                              RET_ERR();
        if (state->phase == KND_CREATED) {
            err = out->writec(out, '!');                                          RET_ERR();
        }

        OUT("class ", strlen("class "));
        OUT(entry->name, entry->name_size);

        if (state->phase == KND_REMOVED) {
            OUT("_rm ", strlen("_rm"));
            err = out->writec(out, '}');                                          RET_ERR();
            continue;
        }

        if (state->phase == KND_SELECTED) {
            err = knd_class_inst_export_commit(state->children, task);
            KND_TASK_ERR("failed to export class inst commit");
        }
        err = out->writec(out, '}');                                              RET_ERR();
    }    
    
    err = out->writec(out, '}');                                                  RET_ERR();

    if (task->user_ctx) {
        err = out->writec(out, '}');                                                  RET_ERR();
    }

    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

static int update_indices(struct kndRepo *self, struct kndCommit *commit, struct kndTask *task)
{
    struct kndStateRef *ref;
    struct kndClassEntry *entry;
    struct kndProcEntry *proc_entry;
    struct kndSharedDictItem *item = NULL;
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    struct kndRepo *repo = self;
    struct kndSharedDict *name_idx = repo->class_name_idx;
    int err;

    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. commit #%zu to update the indices of %.*s [shard role:%d]",
                commit->numid, self->name_size, self->name, task->role);

    if (task->user_ctx) {
        repo = task->user_ctx->repo;
        name_idx = repo->class_name_idx;
    }

    FOREACH (ref, commit->class_state_refs) {
        entry = ref->obj;
        if (DEBUG_REPO_LEVEL_2)
            knd_log(".. idx update of class \"%.*s\" (phase:%d)",
                    entry->name_size, entry->name, ref->state->phase);

        switch (ref->state->phase) {
        case KND_CREATED:
            if (DEBUG_REPO_LEVEL_3)
                knd_log(".. class name idx of repo \"%.*s\" to register class \"%.*s\"",
                        self->name_size, self->name, entry->name_size, entry->name);

            /* register new class */
            err = knd_shared_dict_set(name_idx, entry->name,  entry->name_size,
                                      (void*)entry, mempool, commit, &item, false);
            KND_TASK_ERR("failed to register class %.*s", entry->name_size, entry->name);
            entry->dict_item = item;
            continue;
        case KND_REMOVED:
            entry->phase = KND_REMOVED;
            continue;
        case KND_UPDATED:
            entry->phase = KND_UPDATED;

            err = knd_class_update_indices(self, entry, ref->state, task);
            KND_TASK_ERR("failed to update indices of class %.*s",
                         entry->name_size, entry->name);
            continue;
        default:
            // KND_SELECTED
            if (ref->state->children != NULL) {
                err = knd_class_inst_update_indices(self, entry, ref->state->children, task);
                KND_TASK_ERR("failed to update inst indices of class \"%.*s\"",
                             entry->name_size, entry->name);
            }
            break;
        }
    }

    name_idx = self->proc_name_idx;
    for (ref = commit->proc_state_refs; ref; ref = ref->next) {
        proc_entry = ref->obj;
        switch (ref->state->phase) {
        case KND_REMOVED:
            proc_entry->phase = KND_REMOVED;
            err = knd_shared_dict_remove(name_idx, proc_entry->name, proc_entry->name_size);       RET_ERR();
            continue;
        case KND_UPDATED:
            proc_entry->phase = KND_UPDATED;
            continue;
        default:
            break;
        }
        err = knd_shared_dict_set(name_idx, proc_entry->name,  proc_entry->name_size,
                                  (void*)proc_entry, task->mempool, commit, &item, false);        RET_ERR();
        proc_entry->dict_item = item;
    }
    return knd_OK;
}

static int check_class_conflicts(struct kndRepo *unused_var(self),
                                 struct kndCommit *new_commit,
                                 struct kndTask *task)
{
    struct kndStateRef *ref;
    struct kndClassEntry *entry;
    struct kndState *state;
    knd_commit_confirm confirm;
    int err;

    for (ref = new_commit->class_state_refs; ref; ref = ref->next) {
        entry = ref->obj;
        state = ref->state;

        if (DEBUG_REPO_LEVEL_2)
            knd_log(".. checking \"%.*s\" class conflicts, state phase: %d",
                    entry->name_size, entry->name, state->phase);

        switch (state->phase) {
        case KND_SELECTED:
            // TODO: check instances
            break;
        case KND_CREATED:

            // TODO: check class name idx
            // check dedups

            // knd_log(".. any new states in class name idx?");

            state = atomic_load_explicit(&entry->dict_item->states, memory_order_acquire);

            for (; state; state = state->next) {
                if (state->commit == new_commit) continue;

                confirm = atomic_load_explicit(&state->commit->confirm, memory_order_acquire);
                switch (confirm) {
                case KND_VALID_STATE:
                case KND_PERSISTENT_STATE:
                    atomic_store_explicit(&new_commit->confirm, KND_CONFLICT_STATE, memory_order_release);
                    err = knd_FAIL;
                    KND_TASK_ERR("%.*s class already registered", entry->name_size, entry->name);
                default:
                    break;
                }
            }
            break;
        default:
            break;
        }
    }
    return knd_OK;
}

static int check_commit_conflicts(struct kndRepo *self, struct kndCommit *commit, struct kndTask *task)
{
    struct kndCommit *head_commit = NULL;
    struct kndRepoSnapshot *snapshot;
    int err;

    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. new commit #%zu (%p) to check any commit conflicts since state #%zu..",
                commit->numid, commit, commit->orig_state_id);

    snapshot = atomic_load_explicit(&self->snapshots, memory_order_relaxed);
    do {
        head_commit = atomic_load_explicit(&snapshot->commits, memory_order_relaxed);
        if (head_commit) {
            commit->prev = head_commit;
            commit->numid = head_commit->numid + 1;
        }

        err = check_class_conflicts(self, commit, task);
        KND_TASK_ERR("class level conflicts detected");

    } while (!atomic_compare_exchange_weak(&snapshot->commits, &head_commit, commit));

    atomic_store_explicit(&commit->confirm, KND_VALID_STATE, memory_order_release);
    atomic_fetch_add_explicit(&snapshot->num_commits, 1, memory_order_relaxed);

    if (DEBUG_REPO_LEVEL_2)
        knd_log("++ no conflicts found, commit #%zu confirmed!", commit->numid);

    return knd_OK;
}

static int build_commit_WAL(struct kndRepo *self, struct kndCommit *commit, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndOutput *file_out = task->file_out;
    struct kndRepoSnapshot *snapshot = atomic_load_explicit(&self->snapshots, memory_order_relaxed);
    char filename[KND_PATH_SIZE + 1];
    size_t filename_size;
    size_t planned_journal_size = 0;
    struct stat st;
    int err;
    
    commit->timestamp = time(NULL);
    if (DEBUG_REPO_LEVEL_2) {
        knd_log(".. kndTask #%zu to build a WAL entry (path:%.*s)",
                task->id, task->path_size, task->path);
    }
    out->reset(out);
    err = out->write(out, task->path, task->path_size);
    KND_TASK_ERR("system path construction failed");

    if (task->user_ctx && task->user_ctx->path_size) {
        OUT(task->user_ctx->path, task->user_ctx->path_size);
    }

    if (self->path_size) {
        err = out->write(out, self->path, self->path_size);
        KND_TASK_ERR("repo path construction failed");
    }
    err = out->writef(out, "snapshot_%zu/", snapshot->numid);
    KND_TASK_ERR("snapshot path construction failed");

    err = out->writef(out, "agent_%d/", task->id);
    KND_TASK_ERR("agent path construction failed");

    err = knd_mkpath((const char*)out->buf, out->buf_size, 0755, false);
    KND_TASK_ERR("mkpath %.*s failed", out->buf_size, out->buf);

    err = out->writef(out, "journal_%zu.log", snapshot->num_journals[task->id]);
    KND_TASK_ERR("log filename construction failed");

    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("journal filename too long");
    }
    memcpy(filename, out->buf, out->buf_size);
    filename_size = out->buf_size;
    filename[filename_size] = '\0';

    if (stat(out->buf, &st)) {
        if (DEBUG_REPO_LEVEL_3)
            knd_log(".. initializing the journal: \"%.*s\"", filename_size, filename);
        err = knd_write_file((const char*)filename, "{WAL\n", strlen("{WAL\n"));
        KND_TASK_ERR("failed writing to file %.*s", filename, filename_size);
        goto append_wal_rec;
    }

    planned_journal_size = st.st_size + out->buf_size;
    if (planned_journal_size >= snapshot->max_journal_size) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("!NB: journal size limit reached!");
        snapshot->num_journals[task->id]++;

        out->reset(out);
        err = out->write(out, task->path, task->path_size);
        if (err) return err;

        if (self->path_size) {
            err = out->write(out, self->path, self->path_size);
            if (err) return err;
        }

        //err = out->writef(out, "journal%zu.log", ctx->self->num_journals);
        //if (err) return err;
        //out->buf[out->buf_size] = '\0';

        //if (DEBUG_REPO_LEVEL_TMP)
        //    knd_log(".. switching to a new journal: \"%.*s\"",
        //            out->buf_size, out->buf);

        // err = knd_write_file((const char*)out->buf,
        //                     "{WAL\n", strlen("{WAL\n"));
        // if (err) return err;
    }

 append_wal_rec:

    err = export_commit_GSL(self, commit, task);
    KND_TASK_ERR("failed to export commit");

    file_out->reset(file_out);
    err = file_out->writef(file_out, "{commit %zu{_ts %zu}{_size %zu}",
                           commit->numid, (size_t)commit->timestamp, out->buf_size);
    KND_TASK_ERR("commit header output failed");

    err = file_out->write(file_out, out->buf, out->buf_size);
    KND_TASK_ERR("commit body output failed");

    err = file_out->write(file_out, "}\n", strlen("}\n"));
    KND_TASK_ERR("commit output failed");

    if (task->keep_local_WAL) {
        err = knd_append_file((const char*)filename, file_out->buf, file_out->buf_size);
        KND_TASK_ERR("WAL file append failed");
        atomic_store_explicit(&commit->confirm, KND_PERSISTENT_STATE, memory_order_relaxed);
    }
    return knd_OK;
}

int knd_confirm_commit(struct kndRepo *self, struct kndTask *task)
{
    struct kndTaskContext *ctx = task->ctx;
    struct kndCommit *commit = ctx->commit;
    int err;
    assert(commit != NULL);

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log(">> \"%.*s\" repo to confirm commit #%zu", self->name_size, self->name, commit->numid);

    commit->repo = self;

    err = knd_resolve_commit(commit, task);
    KND_TASK_ERR("failed to resolve commit #%zu", commit->numid);

    err = knd_dedup_commit(commit, task);
    KND_TASK_ERR("failed to dedup commit #%zu", commit->numid);

    switch (task->role) {
    case KND_ARBITER:
        err = update_indices(self, commit, task);
        KND_TASK_ERR("index update failed");

        err = check_commit_conflicts(self, commit, task);
        KND_TASK_ERR("commit conflicts detected, please get the latest repo updates");

        err = build_commit_WAL(self, commit, task);
        KND_TASK_ERR("WAL build failed");
        break;
    default:
        /* delegate commit confirmation to an Arbiter */
        err = export_commit_GSL(self, commit, task);
        KND_TASK_ERR("failed to export commit");
        ctx->phase = KND_CONFIRM_COMMIT;
    }
    return knd_OK;
}

int knd_present_repo_state(struct kndRepo *self, struct kndTask *task)
{
    int err;

    // TODO: choose format
    err = present_latest_state_JSON(self, task->out);                                   RET_ERR();
    return knd_OK;
}

int knd_conc_folder_new(struct kndMemPool *mempool, struct kndConcFolder **result)
{
    void *page;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndConcFolder));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndConcFolder));
    *result = page;
    return knd_OK;
}

int knd_repo_snapshot_new(struct kndMemPool *mempool, struct kndRepoSnapshot **result)
{
    struct kndRepoSnapshot *s;
    int err;

    s = malloc(sizeof(struct kndRepoSnapshot));
    if (!s) return knd_NOMEM;
    memset(s, 0, sizeof(struct kndRepoSnapshot));

    err = knd_set_new(mempool, &s->commit_idx);
    if (err) return err;
    s->max_journals = KND_MAX_JOURNALS;
    s->max_journal_size = KND_FILE_BUF_SIZE;

    *result = s;
    return knd_OK;
}

int knd_repo_new(struct kndRepo **repo, const char *name, size_t name_size,
                 const char *schema_path, size_t schema_path_size, struct kndMemPool *mempool)
{
    struct kndRepo *self;
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndProc *proc;
    struct kndProcEntry *proc_entry;
    struct kndRepoSnapshot *snapshot;
    int err;

    self = malloc(sizeof(struct kndRepo));
    if (!self) return knd_NOMEM;
    memset(self, 0, sizeof(struct kndRepo));

    if (name_size >= (KND_NAME_SIZE - 1)) return knd_LIMIT;

    memcpy(self->name, name, name_size);
    self->name_size = name_size;

    /* special repo names */
    switch (self->name[0]) {
    case '/':
    case '~':
        break;
    default:
        memcpy(self->path, name, name_size);
        self->path[name_size] = '/';
        self->path[name_size + 1] = '\0';
        self->path_size = name_size + 1;
    }

    self->schema_path = schema_path;
    self->schema_path_size = schema_path_size;

    err = knd_class_entry_new(mempool, &entry);
    if (err) goto error;
    entry->name = "/";
    entry->name_size = 1;

    err = knd_class_new(mempool, &c);
    if (err) goto error;
    c->name = entry->name;
    c->name_size = 1;
    entry->class = c;
    c->entry = entry;
    c->state_top = true;

    c->entry->repo = self;
    self->root_class = c;

    err = knd_shared_set_new(NULL, &self->str_idx);
    if (err) goto error;
    err = knd_shared_dict_new(&self->str_dict, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    err = knd_shared_set_new(NULL, &self->class_idx);
    if (err) goto error;

    /* global name indices */
    err = knd_shared_dict_new(&self->class_name_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    /* attrs */
    err = knd_set_new(mempool, &self->attr_idx);
    if (err) goto error;
    err = knd_shared_dict_new(&self->attr_name_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    /*** PROC ***/
    err = knd_proc_entry_new(mempool, &proc_entry);                               RET_ERR();
    proc_entry->name = "/";
    proc_entry->name_size = 1;

    err = knd_proc_new(mempool, &proc);
    if (err) goto error;
    proc->name = proc_entry->name;
    proc->name_size = 1;
    proc_entry->proc = proc;
    proc->entry = proc_entry;

    proc->entry->repo = self;
    self->root_proc = proc;

    err = knd_set_new(mempool, &self->proc_idx);
    if (err) goto error;
    err = knd_shared_dict_new(&self->proc_name_idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    /* proc args */
    err = knd_set_new(mempool, &self->proc_arg_idx);
    if (err) goto error;
    err = knd_shared_dict_new(&self->proc_arg_name_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    /* proc insts */
    err = knd_shared_dict_new(&self->proc_inst_name_idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    err = knd_repo_snapshot_new(mempool, &snapshot);
    if (err) goto error;
    atomic_store_explicit(&self->snapshots, snapshot, memory_order_relaxed);
    
    *repo = self;
    return knd_OK;
 error:
    // TODO: release resources
    return err;
}
