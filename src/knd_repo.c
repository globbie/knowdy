#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <stdatomic.h>

#include "knd_repo.h"
#include "knd_attr.h"
#include "knd_set.h"
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
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_REPO_LEVEL_0 0
#define DEBUG_REPO_LEVEL_1 0
#define DEBUG_REPO_LEVEL_2 0
#define DEBUG_REPO_LEVEL_3 0
#define DEBUG_REPO_LEVEL_TMP 1

static int update_indices(struct kndRepo *self,
                          struct kndCommit *commit,
                          struct kndTask *task);
static int resolve_commit(struct kndCommit *commit, struct kndTask *task);

void knd_repo_del(struct kndRepo *self)
{
    char *rec;
    if (self->num_source_files) {
        for (size_t i = 0; i < self->num_source_files; i++) {
            rec = self->source_files[i];
            free(rec);
        }
        free(self->source_files);
    }

    knd_shared_dict_del(self->class_name_idx);
    knd_shared_dict_del(self->attr_name_idx);
    knd_shared_dict_del(self->proc_name_idx);
    knd_shared_dict_del(self->proc_arg_name_idx);
    free(self);
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

static gsl_err_t save_commit_body(void *obj, const char *rec, size_t *total_size)
{
    struct kndCommit *commit = obj;
    size_t rec_size;
    int err;

    if (!commit->rec_size) {
        err = knd_FAIL;
        return make_gsl_err_external(err);
    }

    /* task header not needed anymore */
    rec_size = commit->rec_size - strlen("{task");

    commit->rec = malloc(rec_size);
    if (!commit->rec) {
        err = knd_NOMEM;
        return make_gsl_err_external(err);
    }
    memcpy(commit->rec, rec, rec_size);
    commit->rec[rec_size] = '\0';

    commit->rec_size = rec_size;
    //knd_log("COMMIT REC: \"%.*s\"", commit->rec_size, commit->rec);

    *total_size = rec_size - 1; // closing brace
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_commit(void *obj,
                              const char *rec,
                              size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndRepo *repo = task->repo;
    size_t ts = 0;
    int err;

    struct kndCommit *commit = malloc(sizeof(struct kndCommit));
    if (!commit) {
        err = knd_NOMEM;
        KND_TASK_LOG("failed to alloc kndCommit");
        return make_gsl_err_external(err);
    }
    memset(commit, 0, sizeof(struct kndCommit));
    task->ctx->commit = commit;

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
          .parse = save_commit_body,
          .obj = commit
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    err = repo->snapshot.commit_idx->add(repo->snapshot.commit_idx,
                                         commit->id, commit->id_size,
                                         (void*)commit);
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

static gsl_err_t parse_WAL(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        { .name = "commit",
          .name_size = strlen("commit"),
          .parse = parse_commit,
          .obj = obj
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;
    return make_gsl_err(gsl_OK);
}

static int restore_commits(struct kndMemBlock *memblock,
                           struct kndTask *task)
{
    size_t total_size;

    struct gslTaskSpec specs[] = {
        { .name = "WAL",
          .name_size = strlen("WAL"),
          .parse = parse_WAL,
          .obj = task
        }
    };
    gsl_err_t parser_err;

    task->type = KND_RESTORE_STATE;
    total_size = memblock->buf_size;

    parser_err = gsl_parse_task(memblock->buf, &total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}

static int restore_journals(struct kndRepo *self,
                            struct kndTask *task)
{
    struct kndOutput *file_out = task->file_out;
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    struct stat st;
    struct kndMemBlock *memblock;
    int err;

    for (size_t i = 0; i < self->snapshot.max_journals; i++) {
        buf_size = snprintf(buf, KND_TEMP_BUF_SIZE, "journal_%zu.log", i);

        err = file_out->write(file_out, buf, buf_size);
        KND_TASK_ERR("log filename construction failed");

        if (stat(file_out->buf, &st)) {
            file_out->rtrim(file_out, buf_size);
            break;
        }
        if ((size_t)st.st_size >= self->snapshot.max_journal_size) {
            err = knd_LIMIT;
            KND_TASK_ERR("journal size limit reached");
        }

        if (DEBUG_REPO_LEVEL_TMP)
            knd_log(".. restoring the journal file: %.*s",
                    file_out->buf_size, file_out->buf);

        err = knd_task_read_file_block(task, file_out->buf, (size_t)st.st_size,
                                       &memblock);
        KND_TASK_ERR("failed to read memblock from %s", file_out->buf);

        task->repo = self;
        err = restore_commits(memblock, task);
        KND_TASK_ERR("failed to restore commits from %s", file_out->buf);

        /* restore prev path */
        file_out->rtrim(file_out, buf_size);
    }

    return knd_OK;
}

static int apply_commit(void *obj,
                        const char *unused_var(elem_id),
                        size_t unused_var(elem_id_size),
                        size_t unused_var(count),
                        void *elem)
{
    struct kndTask *task = obj;
    struct kndCommit *commit = elem;
    struct kndCommit *head_commit;
    struct kndRepo *repo = task->repo;
    gsl_err_t parser_err;
    size_t total_size = commit->rec_size;
    int err;

    task->type = KND_RESTORE_STATE;
    task->ctx->commit = commit;

    struct gslTaskSpec specs[] = {
        { .name = "repo",
          .name_size = strlen("repo"),
          .parse = knd_parse_repo,
          .obj = task
        }
    };

    parser_err = gsl_parse_task(commit->rec, &total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    err = resolve_commit(commit, task);
    KND_TASK_ERR("failed to resolve commit #%zu", commit->numid);
    
    err = update_indices(repo, commit, task);
    KND_TASK_ERR("index update failed");

    do {
        head_commit = atomic_load_explicit(&repo->snapshot.commits,
                                           memory_order_acquire);
        commit->prev = head_commit;
    } while (!atomic_compare_exchange_weak(&repo->snapshot.commits,
                                           &head_commit, commit));
    
    return knd_OK;
}

static int restore_state(struct kndRepo *self,
                         struct kndTask *task)
{
    struct kndOutput *file_out = task->file_out;
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    struct stat st;
    int latest_snapshot_id = -1;
    int err;

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("  .. restoring the latest valid state of repo \"%.*s\"..",
                self->name_size, self->name);

    file_out->reset(file_out);
    err = file_out->write(file_out, task->path, task->path_size);
    KND_TASK_ERR("system path construction failed");

    if (self->path_size) {
        err = file_out->write(file_out, self->path, self->path_size);
        KND_TASK_ERR("repo path construction failed");
    }

    for (size_t i = 0; i < KND_MAX_SNAPSHOTS; i++) {
        buf_size = snprintf(buf, KND_TEMP_BUF_SIZE,
                            "snapshot_%zu/", i);
        err = file_out->write(file_out, buf, buf_size);
        KND_TASK_ERR("snapshot path construction failed");
        if (stat(file_out->buf, &st)) {
            file_out->rtrim(file_out, buf_size);
            break;
        }
        latest_snapshot_id = (int)i;
        file_out->rtrim(file_out, buf_size);
    }

    if (latest_snapshot_id < 0) {
        knd_log("no snapshots of \"%.*s\" found", self->name_size, self->name);
        return knd_OK;
    }

    knd_log("== the latest snapshot of \"%.*s\" is #%d",
            self->name_size, self->name, latest_snapshot_id);

    err = file_out->writef(file_out, "snapshot_%d/", latest_snapshot_id);
    KND_TASK_ERR("snapshot path construction failed");

    // NB: agent count starts from 1
    for (size_t i = 1; i < KND_MAX_TASKS; i++) {
        buf_size = snprintf(buf, KND_TEMP_BUF_SIZE,
                            "agent_%zu/", i);
        err = file_out->write(file_out, buf, buf_size);
        KND_TASK_ERR("agent path construction failed");

        if (stat(file_out->buf, &st)) {
            knd_log("-- no such file: %.*s", file_out->buf_size, file_out->buf);
            break;
        }
        err = restore_journals(self, task);
        KND_TASK_ERR("failed to restore journals in %.*s", file_out->buf_size, file_out->buf);
        file_out->rtrim(file_out, buf_size);
    }

    /* all commits are there in the idx,
       time to apply them in timely order */
    err = self->snapshot.commit_idx->map(self->snapshot.commit_idx,
                                         apply_commit,
                                         (void*)task);
    KND_TASK_ERR("failed to apply commits");

    atomic_store_explicit(&self->snapshot.num_commits,
                          self->snapshot.commit_idx->num_elems, memory_order_relaxed);

    knd_log("== total commits applied: %zu", self->snapshot.commit_idx->num_elems);
    knd_task_free_blocks(task);

    return knd_OK;
}

static int present_latest_state_JSON(struct kndRepo *self,
                                     struct kndOutput *out)
{
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size;
    size_t latest_commit_id = atomic_load_explicit(&self->snapshot.num_commits,
                                                   memory_order_relaxed);
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
        err = self->snapshot.commit_idx->get(self->snapshot.commit_idx,
                                    idbuf, idbuf_size,
                                    (void**)&commit);                             RET_ERR();
        err = out->write(out, ",\"_time\":", strlen(",\"_time\":"));              RET_ERR();
        err = out->writef(out, "%zu", (size_t)commit->timestamp);                 RET_ERR();
        //err = present_commit_JSON(commit, out);  RET_ERR();
    } else {
        err = out->write(out, ",\"_time\":", strlen(",\"_time\":"));              RET_ERR();
        err = out->writef(out, "%zu", (size_t)self->snapshot.timestamp);          RET_ERR();
    }

    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

#if 0
static int present_latest_state_GSL(struct kndRepo *self,
                                    struct kndOutput *out)
{
    size_t latest_commit_id = atomic_load_explicit(&self->snapshot.num_commits,
                                                   memory_order_relaxed);
    //struct kndCommit *commit;
    int err;

    out->reset(out);
    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->write(out, "repo ", strlen("repo "));                              RET_ERR();
    err = out->write(out,  self->name, self->name_size);                          RET_ERR();

    err = out->write(out, "{_state ", strlen("{_state "));                        RET_ERR();
    err = out->writef(out, "%zu", latest_commit_id);                              RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    /*    if (self->commits) {
        commit = self->commits;
        err = out->write(out, "{modif ", strlen("{modif "));                      RET_ERR();
        err = out->writef(out, "%zu", (size_t)commit->timestamp);                 RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
        }*/

    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}
#endif

static gsl_err_t present_repo_state(void *obj,
                                    const char *unused_var(name),
                                    size_t unused_var(name_size))
{
    struct kndTask *task = obj;
    struct kndRepo *repo = task->repo;
    struct kndOutput *out = task->out;
    struct kndMemPool *mempool = task->mempool;
    struct kndSet *set;
    int err;

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

    size_t latest_commit_id = atomic_load_explicit(&repo->snapshot.num_commits,
                                                   memory_order_relaxed);
    task->state_lt = latest_commit_id + 1;

    if (DEBUG_REPO_LEVEL_TMP) {
        knd_log(".. select repo delta:  gt %zu  lt %zu  eq:%zu..",
                task->state_gt, task->state_lt, task->state_eq);
    }

    err = knd_set_new(mempool, &set);
    if (err) return make_gsl_err_external(err);
    set->mempool = mempool;

    /*err = select_commit_range(repo,
                              task->state_gt, task->state_lt,
                              task->state_eq, set);
    if (err) return make_gsl_err_external(err);
    */

    // export
    task->show_removed_objs = true;

    // TODO: formats
    err = knd_class_set_export_JSON(set, task);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
    
    /*show_curr_state:

    switch (task->format) {
    case KND_FORMAT_JSON:
        err = present_latest_state_JSON(repo, out);  
        if (err) return make_gsl_err_external(err);
        break;
    default:
        err = present_latest_state_GSL(repo, out);  
        if (err) return make_gsl_err_external(err);
        break;
    }
    */
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_repo_state(void *obj,
                                  const char *rec,
                                  size_t *total_size)
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

    if (!name_size)  return make_gsl_err(gsl_FAIL);

    if (task->user_ctx) {
        switch (*name) {
        case '~':
            knd_log("== user home repo selected!");
            task->repo = task->user_ctx->repo;
            return make_gsl_err(gsl_OK);
        default:
            break;
        }
        knd_log("== user base repo selected!");
        task->repo = task->user_ctx->repo;
        return make_gsl_err(gsl_OK);
    }

    if (name_size == 1) {
        switch (*name) {
        case '/':
            return make_gsl_err(gsl_OK);
        default:
            break;
        }
    }

    // TODO: repo name match
    task->repo = task->user->repo;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_select(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndUserContext *ctx = task->user_ctx;
    struct kndRepo *repo;

    repo = task->repo;
    if (ctx) {
        repo = ctx->repo;
        task->repo = repo;
    }

    return knd_class_select(repo, rec, total_size, task);
}

static gsl_err_t parse_class_import(void *obj,
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

            task->ctx->commit->orig_state_id = atomic_load_explicit(&task->repo->snapshot.num_commits,
                                                                    memory_order_relaxed);
        }
    }
    return knd_class_import(repo, rec, total_size, task);
}

gsl_err_t knd_parse_repo(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;

    struct gslTaskSpec specs[] = {
        {   .is_implied = true,
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

    if (DEBUG_REPO_LEVEL_1)
        knd_log(".. running include file func.. name: \"%.*s\" [%zu]",
                (int)name_size, name, name_size);
    if (!name_size) return make_gsl_err(gsl_FORMAT);

    err = knd_conc_folder_new(mempool, &folder);
    if (err) return make_gsl_err_external(knd_NOMEM);

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

            task->ctx->commit->orig_state_id = atomic_load_explicit(&task->repo->snapshot.num_commits,
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

static int parse_GSL(struct kndTask *task,
                     const char *rec,
                     size_t *total_size)
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

static int write_filepath(struct kndOutput *out,
                          struct kndConcFolder *folder)
{
    int err;

    if (folder->parent) {
        err = write_filepath(out, folder->parent);
        if (err) return err;
    }

    err = out->write(out, folder->name, folder->name_size);
    if (err) return err;

    return knd_OK;
}

static int read_GSL_file(struct kndRepo *repo,
                         struct kndConcFolder *parent_folder,
                         const char *filename,
                         size_t filename_size,
                         struct kndTask *task)
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
    err = out->write(out, repo->schema_path,
                     repo->schema_path_size);                        RET_ERR();
    err = out->write(out, "/", 1);                                                RET_ERR();

    if (parent_folder) {
        err = write_filepath(out, parent_folder);                                 RET_ERR();
    }

    err = out->write(out, filename, filename_size);                               RET_ERR();
    err = out->write(out, file_ext, file_ext_size);                               RET_ERR();

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

    recs = (char**)realloc(repo->source_files,
                           (repo->num_source_files + 1) * sizeof(char*));
    if (!recs) return knd_NOMEM;
    recs[repo->num_source_files] = rec;

    repo->source_files = recs;
    repo->num_source_files++;

    task->input = rec;
    task->input_size = file_out->buf_size;

    /* actual parsing */
    err = parse_GSL(task, (const char*)rec, &chunk_size);
    if (err) {
        knd_log("-- parsing of \"%.*s\" failed, err: %d",
                out->buf_size, out->buf, err);
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

                err = read_GSL_file(repo, folder,
                                    index_folder_name, index_folder_name_size,
                                    task);
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
            knd_log("-- failed to read file: %.*s",
                    folder->name_size, folder->name);
            return err;
        }
    }
    return knd_OK;
}

int knd_repo_index_proc_arg(struct kndRepo *repo,
                            struct kndProc *proc,
                            struct kndProcArg *arg,
                            struct kndTask *task)
{
    struct kndMemPool *mempool   = task->mempool;
    struct kndSet *arg_idx       = repo->proc_arg_idx;
    struct kndSharedDict *arg_name_idx = repo->proc_arg_name_idx;
    struct kndProcArgRef *arg_ref, *prev_arg_ref;
    struct kndSharedDictItem *item;
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

    // if (task->type == KND_LOAD_STATE) {

    /* global indices */
    prev_arg_ref = knd_shared_dict_get(arg_name_idx,
                                       arg->name, arg->name_size);
    arg_ref->next = prev_arg_ref;
    err = knd_shared_dict_set(arg_name_idx,
                              arg->name, arg->name_size,
                              (void*)arg_ref,
                              mempool,
                              task->ctx->commit, &item, true);                                       RET_ERR();

    err = arg_idx->add(arg_idx,
                        arg->id, arg->id_size,
                        (void*)arg_ref);                                          RET_ERR();

    // TODO: local task register

    if (DEBUG_REPO_LEVEL_2)
        knd_log("++ new primary arg: \"%.*s\" (id:%.*s)",
                arg->name_size, arg->name, arg->id_size, arg->id);

    return knd_OK;
}

static int resolve_classes(struct kndRepo *self,
                           struct kndTask *task)
{
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndSharedDictItem *item;
    struct kndSharedDict *name_idx = self->class_name_idx;
    int err;

    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. resolving classes in \"%.*s\"..",
                self->name_size, self->name);

    // TODO: iterate func in kndSharedDict
    for (size_t i = 0; i < name_idx->size; i++) {
        item = atomic_load_explicit(&name_idx->hash_array[i],
                                    memory_order_relaxed);
        for (; item; item = item->next) {
            entry = item->data;
            if (!entry->class) {
                knd_log("-- unresolved class entry: %.*s",
                        entry->name_size, entry->name);
                return knd_FAIL;
            }
            c = entry->class;

            if (c->is_resolved) continue;

            err = knd_class_resolve(c, task);
            if (err) {
                knd_log("-- couldn't resolve the \"%.*s\" class",
                        c->entry->name_size, c->entry->name);
                return err;
            }

            if (DEBUG_REPO_LEVEL_2) {
                c->str(c, 1);
            }

        }
    }
    return knd_OK;
}

static int index_classes(struct kndRepo *self,
                         struct kndTask *task)
{
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndSharedDictItem *item;
    struct kndSharedDict *name_idx = self->class_name_idx;
    struct kndSet *class_idx = self->class_idx;
    int err;

    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. indexing classes in \"%.*s\"..",
                self->name_size, self->name);

    // TODO iterate func
    for (size_t i = 0; i < name_idx->size; i++) {
        item = atomic_load_explicit(&name_idx->hash_array[i],
                                    memory_order_relaxed);
        for (; item; item = item->next) {
            entry = item->data;
            if (!entry->class) {
                knd_log("-- unresolved class entry: %.*s",
                        entry->name_size, entry->name);
                return knd_FAIL;
            }
            c = entry->class;

            err = knd_class_index(c, task);
            if (err) {
                knd_log("-- couldn't index the \"%.*s\" class",
                        c->entry->name_size, c->entry->name);
                return err;
            }

            err = class_idx->add(class_idx,
                                 entry->id, entry->id_size, (void*)entry);
            if (err) return err;
        }
    }
    return knd_OK;
}

static int resolve_procs(struct kndRepo *self,
                         struct kndTask *task)
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
        item = atomic_load_explicit(&proc_name_idx->hash_array[i],
                                    memory_order_relaxed);
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

    for (size_t i = 0; i < proc_name_idx->size; i++) {
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
    }

    return knd_OK;
}

int knd_repo_open(struct kndRepo *self, struct kndTask *task)
{
    struct kndOutput *out;
    struct kndClassInst *inst;
    struct stat st;
    int err;

    out = task->file_out;
    task->repo = self;

    /* extend user DB path */
    if (self->user_ctx) {
        memcpy(self->path, self->user->path, self->user->path_size);
        self->path_size = self->user->path_size;

        inst = self->user_ctx->user_inst;
        
        char *p = self->path + self->path_size;
        memcpy(p, "/", 1);
        p++;
        self->path_size++;

        memcpy(p, inst->entry->id, inst->entry->id_size);
        self->path_size += inst->entry->id_size;
        self->path[self->path_size] = '\0';

        err = knd_mkpath((const char*)self->path, self->path_size, 0755, false);
        if (err) return err;
    }

    out->reset(out);
    err = out->write(out, self->path, self->path_size);
    if (err) return err;
    err = out->write(out, "/frozen.gsp", strlen("/frozen.gsp"));
    if (err) return err;

    /* frozen DB exists? */
    if (!stat(out->buf, &st)) {
        // TODO:
        // try opening the frozen DB
        /*err = c->open(c, (const char*)out->buf);
        if (err) {
            knd_log("-- failed to open a frozen DB");
            return err;
            }*/
    } else {
        if (!self->user_ctx) {
            /* read a system-wide schema */
            task->type = KND_LOAD_STATE;
            err = read_GSL_file(self, NULL, "index", strlen("index"), task);
            KND_TASK_ERR("schema import failed");

            err = resolve_classes(self, task);
            KND_TASK_ERR("class resolving failed");

            err = resolve_procs(self, task);
            if (err) {
                knd_log("-- resolving of procs failed");
                return err;
            }

            err = index_classes(self, task);
            if (err) {
                knd_log("-- class indexing failed");
                return err;
            }
        }
    }

    /* read any existing commits from the WAL
       to the latest snapshot */
    err = restore_state(self, task);
    KND_TASK_ERR("failed to restore the latest state of %.*s", self->name_size, self->name);

    return knd_OK;
}

static int export_commit_GSL(struct kndRepo *self,
                             struct kndCommit *commit,
                             struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndStateRef *ref;
    struct kndState *state;
    struct kndClassEntry *entry;
    // struct kndProcEntry *proc_entry;
    int err;

    err = out->write(out, "{task", strlen("{task"));                              RET_ERR();
    err = out->write(out, "{repo ", strlen("{repo "));                            RET_ERR();
    err = out->write(out, self->name, self->name_size);                           RET_ERR();

    for (ref = commit->class_state_refs; ref; ref = ref->next) {
        entry = ref->obj;
        if (!entry) continue;

        state = ref->state;

        err = out->writec(out, '{');                                              RET_ERR();
        if (state->phase == KND_CREATED) {
            err = out->writec(out, '!');                                          RET_ERR();
        }

        err = out->write(out, "class ", strlen("class "));                        RET_ERR();
        err = out->write(out, entry->name, entry->name_size);                     RET_ERR();

        if (state->phase == KND_REMOVED) {
            err = out->write(out, "_rm ", strlen("_rm"));                         RET_ERR();
            err = out->writec(out, '}');                                          RET_ERR();
            continue;
        }

        if (state->phase == KND_SELECTED) {

            knd_log(".. export class inst GSL..");

            err = knd_class_inst_export_commit(state->children, task);
            KND_TASK_ERR("failed to export class inst commit");
        }

        err = out->writec(out, '}');                                              RET_ERR();
    }    
    
    err = out->writec(out, '}');                                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

static int update_indices(struct kndRepo *self,
                          struct kndCommit *commit,
                          struct kndTask *task)
{
    struct kndStateRef *ref;
    struct kndClassEntry *entry;
    struct kndProcEntry *proc_entry;
    struct kndSharedDict *name_idx = self->class_name_idx;
    struct kndSharedDictItem *item = NULL;
    int err;

    for (ref = commit->class_state_refs; ref; ref = ref->next) {
        entry = ref->obj;
        switch (ref->state->phase) {
        case KND_CREATED:
            /* register new class */
            err = knd_shared_dict_set(name_idx,
                                      entry->name,  entry->name_size,
                                      (void*)entry,
                                      task->mempool,
                                      commit, &item, false);
            KND_TASK_ERR("failed to register class %.*s", entry->name_size, entry->name);
            entry->dict_item = item;
            continue;
        case KND_REMOVED:
            entry->phase = KND_REMOVED;
            continue;
        case KND_UPDATED:
            entry->phase = KND_UPDATED;

            err = knd_class_update_indices(entry, ref->state, task);
            KND_TASK_ERR("failed to update indices of class %.*s",
                         entry->name_size, entry->name);
            continue;
        default:

            err = knd_class_inst_update_indices(entry, ref->state->children, task);
            KND_TASK_ERR("failed to update inst idx of class %.*s",
                         entry->name_size, entry->name);
            
            break;
        }

    }

    name_idx = self->proc_name_idx;
    for (ref = commit->proc_state_refs; ref; ref = ref->next) {
        proc_entry = ref->obj;
        switch (ref->state->phase) {
        case KND_REMOVED:
            proc_entry->phase = KND_REMOVED;
            err = knd_shared_dict_remove(name_idx,
                                  proc_entry->name, proc_entry->name_size);       RET_ERR();
            continue;
        case KND_UPDATED:
            proc_entry->phase = KND_UPDATED;
            continue;
        default:
            break;
        }
        err = knd_shared_dict_set(name_idx,
                                  proc_entry->name,  proc_entry->name_size,
                                  (void*)proc_entry,
                                  task->mempool,
                                  commit, &item, false);                                    RET_ERR();
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
            /* check class name idx */
            // knd_log(".. any new states in class name idx?");

            state = atomic_load_explicit(&entry->dict_item->states,
                                         memory_order_acquire);

            for (; state; state = state->next) {
                if (state->commit == new_commit) continue;

                confirm = atomic_load_explicit(&state->commit->confirm,
                                               memory_order_acquire);
                switch (confirm) {
                case KND_VALID_STATE:
                case KND_PERSISTENT_STATE:
                    atomic_store_explicit(&new_commit->confirm,
                                          KND_CONFLICT_STATE, memory_order_release);
                    err = knd_FAIL;
                    KND_TASK_ERR("%.*s class already registered",
                                 entry->name_size, entry->name);
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

static int check_commit_conflicts(struct kndRepo *self,
                                  struct kndCommit *new_commit,
                                  struct kndTask *task)
{
    struct kndCommit *head_commit = NULL;
    int err;

    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. new commit #%zu (%p) to check any commit conflicts since state #%zu..",
                new_commit->numid, new_commit, new_commit->orig_state_id);

    do {
        head_commit = atomic_load_explicit(&self->snapshot.commits,
                                           memory_order_acquire);
        if (head_commit) {
            new_commit->prev = head_commit;
            new_commit->numid = head_commit->numid + 1;
        }

        err = check_class_conflicts(self, new_commit, task);
        KND_TASK_ERR("class level conflicts detected");

    } while (!atomic_compare_exchange_weak(&self->snapshot.commits, &head_commit, new_commit));

    atomic_store_explicit(&new_commit->confirm, KND_VALID_STATE, memory_order_release);
    atomic_fetch_add_explicit(&self->snapshot.num_commits, 1, memory_order_relaxed);

    if (DEBUG_REPO_LEVEL_2)
        knd_log("++ no conflicts found, commit #%zu confirmed!",
                new_commit->numid);

    return knd_OK;
}

static int save_commit_WAL(struct kndRepo *self, struct kndCommit *commit, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndOutput *file_out = task->file_out;
    size_t planned_journal_size = 0;
    struct stat st;
    int err;

    commit->timestamp = time(NULL);
    if (DEBUG_REPO_LEVEL_2) {
        knd_log(".. kndTask #%zu to write a WAL entry: %.*s (path:%.*s)",
                task->id,
                task->input_size, task->input, task->path_size, task->path);
    }

    out->reset(out);
    err = out->writef(out, "{commit %zu{_ts %zu}{_size %zu}",
                      commit->numid, (size_t)commit->timestamp, task->input_size);
    KND_TASK_ERR("commit header output failed");
    err = out->write(out, task->input, task->input_size);
    KND_TASK_ERR("commit body output failed");
    err = out->write(out, "}\n", strlen("}\n"));
    KND_TASK_ERR("commit footer output failed");
    

    file_out->reset(file_out);
    err = file_out->write(file_out, task->path, task->path_size);
    KND_TASK_ERR("system path construction failed");

    if (self->path_size) {
        err = file_out->write(file_out, self->path, self->path_size);
        KND_TASK_ERR("repo path construction failed");
    }
    err = file_out->writef(file_out, "snapshot_%zu/", self->snapshot.numid);
    KND_TASK_ERR("snapshot path construction failed");

    err = file_out->writef(file_out, "agent_%d/", task->id);
    KND_TASK_ERR("agent path construction failed");

    err = knd_mkpath((const char*)file_out->buf, file_out->buf_size, 0755, false);
    KND_TASK_ERR("mkpath %.*s failed", file_out->buf_size, file_out->buf);

    err = file_out->writef(file_out, "journal_%zu.log", self->snapshot.num_journals[task->id]);
    KND_TASK_ERR("log filename construction failed");
    file_out->buf[file_out->buf_size] = '\0';

    // knd_log("WAL filename: %.*s", file_out->buf_size, file_out->buf);

    if (stat(file_out->buf, &st)) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log(".. initializing the journal: \"%.*s\"",
                    file_out->buf_size, file_out->buf);
        err = knd_write_file((const char*)file_out->buf,
                             "{WAL\n", strlen("{WAL\n"));
        KND_TASK_ERR("failed writing to file %.*s", file_out->buf_size, file_out->buf);
        goto append_wal_rec;
    }

    planned_journal_size = st.st_size + out->buf_size;
    if (planned_journal_size >= self->snapshot.max_journal_size) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("!NB: journal size limit reached!");
        self->snapshot.num_journals[task->id]++;

        file_out->reset(file_out);
        err = file_out->write(file_out, task->path, task->path_size);
        if (err) return err;

        if (self->path_size) {
            err = file_out->write(file_out, self->path, self->path_size);
            if (err) return err;
        }

        //err = out->writef(file_out, "journal%zu.log", ctx->self->num_journals);
        //if (err) return err;
        //file_out->buf[file_out->buf_size] = '\0';

        //if (DEBUG_REPO_LEVEL_TMP)
        //    knd_log(".. switching to a new journal: \"%.*s\"",
        //            file_out->buf_size, file_out->buf);

        // err = knd_write_file((const char*)file_out->buf,
        //                     "{WAL\n", strlen("{WAL\n"));
        // if (err) return err;
    }

 append_wal_rec:

    // TODO: bufferize WAL

    // knd_log(".. appending to WAL: %.*s", out->buf_size, out->buf);
    err = knd_append_file((const char*)file_out->buf,
                          out->buf, out->buf_size);
    KND_TASK_ERR("WAL file append failed");

    return knd_OK;
}

static int resolve_class_inst_commit(struct kndStateRef *state_refs,
                                     struct kndCommit *commit, struct kndTask *unused_var(task))
{
    struct kndState *state;
    struct kndClassInstEntry *entry;
    struct kndStateRef *ref;
    // int err;

    for (ref = state_refs; ref; ref = ref->next) {
        entry = ref->obj;
        state = ref->state;
        state->commit = commit;

        knd_log(".. resolve inst entry: %.*s", entry->name_size, entry->name);
        // TODO: resolve class inst
    }
    return knd_OK;
}

static int resolve_commit(struct kndCommit *commit, struct kndTask *task)
{
    struct kndState *state;
    struct kndClassEntry *entry;
    struct kndProcEntry *proc_entry;
    struct kndStateRef *ref;
    int err;

    for (ref = commit->class_state_refs; ref; ref = ref->next) {
        if (ref->state->phase == KND_REMOVED) {
            continue;
        }
        entry = ref->obj;
        if (!entry->class->is_resolved) {
            err = knd_class_resolve(entry->class, task);
            KND_TASK_ERR("failed to resolve class \"%.*s\"",
                         entry->name_size, entry->name);
        }

        state = ref->state;
        state->commit = commit;
        if (!state->children) continue;

        err = resolve_class_inst_commit(state->children, commit, task);
        KND_TASK_ERR("failed to resolve commit of class insts");

    }

    /* PROCS */
    for (ref = commit->proc_state_refs; ref; ref = ref->next) {
        if (ref->state->phase == KND_REMOVED) {
            knd_log(".. proc to be removed");
            continue;
        }
        proc_entry = ref->obj;
        /* proc resolving */
        if (!proc_entry->proc->is_resolved) {
            err = knd_proc_resolve(proc_entry->proc, task);                       RET_ERR();
        }
    }
    return knd_OK;
}

int knd_confirm_commit(struct kndRepo *self, struct kndTask *task)
{
    struct kndTaskContext *ctx = task->ctx;
    struct kndCommit *commit = ctx->commit;
    int err;

    assert(commit != NULL);

    if (DEBUG_REPO_LEVEL_TMP) {
        knd_log(".. \"%.*s\" repo to apply commit #%zu..",
                self->name_size, self->name, commit->numid);
    }
    commit->repo = self;

    err = resolve_commit(commit, task);
    KND_TASK_ERR("failed to resolve commit #%zu", commit->numid);

    switch (task->role) {
    case KND_WRITER:
        err = update_indices(self, commit, task);
        KND_TASK_ERR("index update failed");

        err = check_commit_conflicts(self, commit, task);
        KND_TASK_ERR("commit conflicts detected, please get the latest repo updates");

        err = save_commit_WAL(self, commit, task);
        KND_TASK_ERR("WAL saving failed");
        break;
    default:
        /* delegate commit confirmation to a Writer */
        err = export_commit_GSL(self, commit, task);                                  RET_ERR();
        ctx->phase = KND_CONFIRM_COMMIT;
    }
    return knd_OK;
}

int knd_present_repo_state(struct kndRepo *self,
                           struct kndTask *task)
{
    int err;

    // TODO: choose format
    err = present_latest_state_JSON(self,
                                    task->out);                                   RET_ERR();
    return knd_OK;
}

int knd_conc_folder_new(struct kndMemPool *mempool,
                        struct kndConcFolder **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                            sizeof(struct kndConcFolder), &page);                 RET_ERR();
    *result = page;
    return knd_OK;
}

int knd_repo_new(struct kndRepo **repo,
                 const char *name,
                 size_t name_size,
                 struct kndMemPool *mempool)
{
    struct kndRepo *self;
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndProc *proc;
    struct kndProcEntry *proc_entry;
    int err;

    self = malloc(sizeof(struct kndRepo));
    if (!self) return knd_NOMEM;
    memset(self, 0, sizeof(struct kndRepo));

    if (name_size >= (KND_NAME_SIZE - 1)) return knd_LIMIT;

    memcpy(self->name, name, name_size);
    self->name_size = name_size;

    knd_log("new REPO: %.*s %.*s",
            self->path_size, self->path, name_size, name);

    if (self->name[0] != '/') {
        memcpy(self->path, name, name_size);
        self->path[name_size] = '/';
        self->path[name_size + 1] = '\0';
        self->path_size = name_size + 1;
    }

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

    /* global name indices */
    err = knd_set_new(mempool, &self->class_idx);
    if (err) goto error;

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

    /* commits */
    err = knd_set_new(mempool, &self->snapshot.commit_idx);
    if (err) goto error;

    self->snapshot.max_journals = KND_MAX_JOURNALS;
    self->snapshot.max_journal_size = KND_FILE_BUF_SIZE;
    *repo = self;

    return knd_OK;
 error:
    // TODO: release resources
    return err;
}
