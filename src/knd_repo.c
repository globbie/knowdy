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
        // select system repo
        if (DEBUG_REPO_LEVEL_3)
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
    s->max_journal_size = KND_MAX_JOURNAL_SIZE;

    *result = s;
    return knd_OK;
}

int knd_repo_new(struct kndRepo **repo, const char *name, size_t name_size,
                 const char *path, size_t path_size,
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

    if (path_size) {
        memcpy(self->path, path, path_size);
        self->path_size = path_size;
        if (path[path_size - 1] != '/') {
            self->path[path_size] = '/';
            self->path_size++;
        }
    }

    /* special repo names */
    switch (self->name[0]) {
    case '/':
    case '~':
        break;
    default:
        memcpy(self->path + self->path_size, name, name_size);
        self->path_size += name_size;
        self->path[self->path_size] = '/';
        self->path_size++;
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
