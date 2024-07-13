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
#include "knd_memblock.h"
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

void knd_repo_del(struct kndRepo *self)
{
    // del snapshots
    free(self);
}

static int present_latest_state_JSON(struct kndRepo *self, struct kndOutput *out)
{
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size;
    struct kndRepoSnapshot *snapshot = atomic_load_explicit(&self->snapshot, memory_order_relaxed);
    assert (snapshot != NULL);

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

static gsl_err_t present_repo_state(void *obj, const char *unused_var(name),
                                    size_t unused_var(name_size))
{
    struct kndTask *task = obj;
    struct kndRepo *repo = task->repo;
    struct kndOutput *out = task->out;
    // struct kndMemPool *mempool = task->mempool;
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

static int find_repo(struct kndRepo **result, const char *name, size_t name_size,
                     struct kndTask *task)
{
    struct kndRepo *repo;
    repo = knd_dict_get(task->repo_name_idx, name, name_size);
    if (!repo) return knd_NO_MATCH;
    *result = repo;
    return knd_OK;
}

static gsl_err_t run_select_repo(void *obj, const char *name, size_t name_size)
{
    struct kndTask *task = obj;
    struct kndRepoSnapshot *snapshot;
    int err;

    /* default system repo */
    struct kndRepo *repo = NULL;
    if (!name_size) return make_gsl_err(gsl_FAIL);

    /* special names */
    if (name_size == 1) {
        switch (*name) {
        case '/':
            repo = task->system_repo;
            break;
        case '~':
            repo = task->user_ctx->repo;
            break;
        default:
            err = find_repo(&repo, name, name_size, task);
            if (err) {
                return make_gsl_err(gsl_NO_MATCH);
            }
            break;
        }
    }
    if (!repo) {
        err = find_repo(&repo, name, name_size, task);
        if (err) {
            return make_gsl_err(gsl_NO_MATCH);
        }
    }
    task->repo = repo;
    task->user_ctx->repo = repo;

    snapshot = atomic_load_explicit(&repo->snapshot, memory_order_relaxed);
    assert (snapshot != NULL);

    task->snapshot = snapshot;
    task->idxs = &repo->snapshot->idxs;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_snapshot_task(void *obj, const char *unused_var(rec), size_t *total_size)
{
    struct kndTask *task = obj;
    int err;

    task->type = KND_SNAPSHOT_STATE;
    err = knd_repo_snapshot_create(task->repo, task);
    if (err) {
        KND_TASK_LOG("failed to build a snapshot of {repo %.*s}",
                     task->repo->name_size, task->repo->name);
        return *total_size = 0, make_gsl_err(gsl_FAIL);
    }
    return *total_size = 0, make_gsl_err(gsl_OK);
}

static gsl_err_t decode_seq(void *obj, const char *val, size_t val_size)
{
    struct kndTask *task = obj;
    struct kndCharSeq *seq;
    int err;

    err = knd_charseq_decode(val, val_size, &seq, task);
    if (err) {
        KND_TASK_LOG("failed to decode a text charseq %.*s", val_size, val);
        return make_gsl_err_external(err);
    }
    if (DEBUG_REPO_LEVEL_3)
        knd_log(">> text seq:%.*s", seq->val_size, seq->val);
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

    if (task->type != KND_BULK_LOAD_STATE) {
        task->type = KND_COMMIT_STATE;
        if (!task->ctx->commit) {
            err = knd_commit_new(task->mempool, &task->ctx->commit);
            if (err) return make_gsl_err_external(err);

            task->ctx->commit->orig_state_id =\
                atomic_load_explicit(&task->snapshot->num_commits, memory_order_relaxed);
        }
    }
    return knd_class_import(repo, rec, total_size, task);
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
        { .name = "state",
          .name_size = strlen("state"),
          .parse = parse_repo_state,
          .obj = task
        },
        { .name = "commit-from",
          .name_size = strlen("commit-from"),
          .parse = gsl_parse_size_t,
          .obj = &task->state_eq
        },
        { .type = GSL_SET_STATE,
          .name = "snapshot",
          .name_size = strlen("snapshot"),
          .parse = parse_snapshot_task,
          .obj = task
        },
        { .name = "seq-decode",
          .name_size = strlen("seq-decode"),
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

int knd_repo_index_proc_arg(struct kndRepo *repo, struct kndProc *proc,
                            struct kndProcArg *arg, struct kndTask *task)
{
    struct kndMemPool *mempool   = task->mempool;
    struct kndSet *arg_idx       = task->idxs->proc_arg_idx;
    struct kndSharedDict *arg_name_idx = task->idxs->proc_arg_name_idx;
    struct kndProcArgRef *ref, *arg_ref, *next_arg_ref;
    int err;

    /* generate unique attr id */
    arg->numid = atomic_fetch_add_explicit(&task->idxs->proc_arg_id_count, 1,
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
    case KND_BULK_LOAD_STATE:

        err = knd_proc_get_arg(proc, arg->name, arg->name_size, &ref, task);

        next_arg_ref = knd_shared_dict_get(arg_name_idx, arg->name, arg->name_size);
        arg_ref->next = next_arg_ref;

        err = knd_shared_dict_set(arg_name_idx, arg->name, arg->name_size, (void*)arg_ref);
        KND_TASK_ERR("failed to globally register {arg %.*s}", arg->name_size, arg->name);

        err = arg_idx->add(arg_idx, arg->id, arg->id_size, (void*)arg_ref);
        KND_TASK_ERR("failed to globally register numid of arg \"%.*s\"",
                     arg->name_size, arg->name);

        return knd_OK;
    default:
        break;
    }

    /* local task name idx */
    err = knd_dict_set(task->proc_arg_name_idx, arg->name, arg->name_size, (void*)arg_ref);
    KND_TASK_ERR("failed to register arg name %.*s", arg->name_size, arg->name);

    if (DEBUG_REPO_LEVEL_2)
        knd_log("++ new primary arg: \"%.*s\" (id:%.*s) of \"%.*s\" (repo:%.*s)",
                arg->name_size, arg->name, arg->id_size, arg->id,
                proc->name_size, proc->name, repo->name_size, repo->name);

    return knd_OK;
}

int knd_present_repo_state(struct kndRepo *self, struct kndTask *task)
{
    int err;

    // TODO: choose format
    err = present_latest_state_JSON(self, task->out);
    KND_TASK_ERR("failed to present repo state");
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

static int build_snapshot_path(struct kndRepoSnapshot *s, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndRepo *repo = s->repo;
    int err;

    out->reset(out);
    OUT(repo->path, repo->path_size);
    OUTF("snapshot_%zu/", s->numid);
    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("GSP path too long");
    }
    memcpy(s->path, out->buf, out->buf_size);
    s->path_size = out->buf_size;
    s->path[out->buf_size] = '\0';

    err = knd_mkpath((const char*)s->path, s->path_size, 0755, false);
    KND_TASK_ERR("mkpath %.*s failed", s->path_size, s->path);

    return knd_OK;
}

int knd_repo_snapshot_new(struct kndRepoSnapshot **result, size_t numid, size_t latest_commit_id,
                          struct kndRepo *repo, struct kndTask *task)
{
    struct kndRepoSnapshot *s;
    struct kndMemPool *mempool = task->mempool;
    int err;

    s = calloc(1, sizeof(struct kndRepoSnapshot));
    if (!s) return knd_NOMEM;
    s->numid = numid;
    s->repo = repo;
    s->start_from_commit_id = latest_commit_id;

    err = build_snapshot_path(s, task);
    KND_TASK_ERR("failed to build a snapshot path");

    err = knd_set_new(&s->commit_idx, mempool);
    if (err) return err;
    s->max_journals = KND_MAX_JOURNALS;
    s->max_journal_size = KND_MAX_JOURNAL_SIZE;

    /* indices for writing */
    err = knd_shared_set_new(&s->idxs.str_idx, mempool);
    if (err) return err;
    err = knd_shared_dict_new(&s->idxs.str_dict, KND_MEDIUM_DICT_SIZE, mempool, false);
    if (err) return err;

    err = knd_shared_set_new(&s->idxs.class_idx, mempool);
    if (err) return err;
    err = knd_shared_dict_new(&s->idxs.class_name_idx, KND_MEDIUM_DICT_SIZE, mempool, false);
    if (err) return err;

    err = knd_shared_set_new(&s->idxs.attr_idx, mempool);
    if (err) return err;
    err = knd_shared_dict_new(&s->idxs.attr_name_idx, KND_MEDIUM_DICT_SIZE, mempool, false);
    if (err) return err;

    err = knd_shared_set_new(&s->idxs.proc_idx, mempool);
    if (err) return err;
    err = knd_shared_dict_new(&s->idxs.proc_name_idx, KND_MEDIUM_DICT_SIZE, mempool, false);
    if (err) return err;

    *result = s;
    return knd_OK;
}

int knd_repo_snapshot_activate(struct kndRepo *repo, struct kndTask *task)
{
    struct kndRepoSnapshot *snapshot;
    int err;
    assert (repo->snapshot_temp != NULL);

    /* it is now safe to transfer all interim commits to new memory */
    err = knd_repo_transfer_commits(repo, task);
    KND_TASK_ERR("failed to transfer sys repo commits");

    /* time to switch snapshots */
    snapshot = atomic_load_explicit(&repo->snapshot, memory_order_relaxed);    
    atomic_store_explicit(&repo->snapshot, repo->snapshot_temp, memory_order_relaxed);
    repo->snapshot_temp = NULL;

    /* release prev resources */
    knd_repo_snapshot_del(snapshot);

    return knd_OK;
}

int knd_repo_snapshot_fetch_memblock(struct kndRepoSnapshot *self,
                                     size_t space_required,
                                     struct kndMemBlock **result,
                                     struct kndTask *task)
{
    struct kndMemBlock *block, *curr_block;
    int err;

    if (space_required >= KND_MEMBLOCK_BUF_SIZE) return knd_LIMIT;

    if (!self->blocks) {
        err = knd_memblock_new(&block, 0, KND_MEMBLOCK_BUF_SIZE);
        KND_TASK_ERR("failed to alloc a memblock");
        *result = block;
        return knd_OK;
    }

    curr_block = self->blocks;
    if ((curr_block->capacity - curr_block->buf_size) >= space_required) {
        *result = curr_block;
        return knd_OK;
    }

    err = knd_memblock_new(&block, 0, KND_MEMBLOCK_BUF_SIZE);
    KND_TASK_ERR("failed to alloc a memblock");
    block->next = curr_block;
    self->blocks = block;
    self->num_blocks++;
    *result = block;
    return knd_OK;
}

void knd_repo_snapshot_del(struct kndRepoSnapshot *snapshot)
{
    struct kndMemBlock *block, *next_block;

    for (block = snapshot->blocks; block; block = next_block) {
        next_block = block->next;
        if (block->buf)
            free(block->buf);
        free(block);
    }
    free(snapshot);
}

int knd_repo_new(struct kndRepo **repo, const char *name, size_t name_size,
                 const char *path, size_t path_size,
                 const char *schema_path, size_t schema_path_size)
{
    struct kndRepo *self;

    if (name_size >= (KND_NAME_SIZE - 1)) return knd_LIMIT;

    self = calloc(1, sizeof(struct kndRepo));
    if (!self) return knd_NOMEM;

    memcpy(self->name, name, name_size);
    self->name_size = name_size;

    if (path_size) {
        if (path_size >= (KND_PATH_SIZE - 1)) return knd_LIMIT;

        memcpy(self->path, path, path_size);
        self->path_size = path_size;
        if (path[path_size - 1] != '/') {
            self->path[path_size] = '/';
            self->path_size++;
        }
    }

    /* check special repo names */
    switch (self->name[0]) {
    case '/': // base repo
    case '~': // user repo
        break;
    default:
        if (self->path_size + name_size >= (KND_PATH_SIZE - 1)) return knd_LIMIT;

        memcpy(self->path + self->path_size, name, name_size);
        self->path_size += name_size;

        if (self->path[self->path_size - 1] != '/') {
            self->path[self->path_size] = '/';
            self->path_size++;
        }
    }
    self->schema_path = schema_path;
    self->schema_path_size = schema_path_size;
 
    *repo = self;
    return knd_OK;
}
