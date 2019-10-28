#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gsl-parser.h>

#include "knd_user.h"
#include "knd_shard.h"
#include "knd_repo.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_proc.h"
#include "knd_set.h"
#include "knd_mempool.h"
#include "knd_state.h"
#include "knd_output.h"

#define DEBUG_USER_LEVEL_0 0
#define DEBUG_USER_LEVEL_1 0
#define DEBUG_USER_LEVEL_2 0
#define DEBUG_USER_LEVEL_3 0
#define DEBUG_USER_LEVEL_TMP 1

void knd_user_del(struct kndUser *self)
{
    if (self->repo)
        knd_repo_del(self->repo);
    free(self);
}

static gsl_err_t parse_proc_import(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndTask *task = obj;
    task->type = KND_COMMIT_STATE;

    if (!task->ctx->commit->orig_state_id)
        task->ctx->commit->orig_state_id = atomic_load_explicit(&task->repo->snapshot.num_commits,
                                                                memory_order_relaxed);
    return knd_proc_import(task->repo, rec, total_size, task);
}

static gsl_err_t parse_proc_select(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndTask *task = obj;

    return knd_proc_select(task->repo, rec, total_size, task);
}

static gsl_err_t parse_class_import(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndUserContext *ctx = task->user_ctx;
    struct kndRepo *repo;
    int err;

    repo = ctx->repo;
    if (task->repo)
        repo = task->repo;
    else
        task->repo = repo;

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. parsing user class import: \"%.*s\"..", 64, rec);

    task->type = KND_COMMIT_STATE;

    if (!task->ctx->commit) {
        err = knd_commit_new(task->mempool, &task->ctx->commit);
        if (err) return make_gsl_err_external(err);
        
        task->ctx->commit->orig_state_id = atomic_load_explicit(&task->repo->snapshot.num_commits,
                                                                memory_order_relaxed);
    }
    
    return knd_class_import(repo, rec, total_size, task);
}

static gsl_err_t parse_sync_task(void *obj,
                                 const char *unused_var(rec),
                                 size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndUser *self = task->user;
    struct kndOutput *out = task->out;
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    struct stat st;
    char *s, *n;
    size_t path_size;
    int err;

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. got sync task..");

    s = self->path;
    memcpy(s, self->path, self->path_size);
    s += self->path_size;
    self->path_size += self->path_size;

    path_size =  strlen("/frozen_merge.gsp");
    memcpy(s, "/frozen_merge.gsp", path_size);
    self->path_size += path_size;
    self->path[self->path_size] = '\0';

    /* file exists, remove it */
    if (!stat(self->path, &st)) {
        err = remove(self->path);
        if (err) return *total_size = 0, make_gsl_err_external(err);
        knd_log("-- existing frozen DB file removed..");
    }

    /* name IDX */
    n = buf;
    buf_size = 0;
    memcpy(n, self->path, self->path_size);
    n += self->path_size;
    buf_size += self->path_size;
    path_size =  strlen("/frozen_name.gsi");
    memcpy(n, "/frozen_name.gsi", path_size);
    buf_size += path_size;
    buf[buf_size] = '\0';

    task->type = KND_SYNC_STATE;

    //err = knd_class_freeze(self->repo->root_class);
    /*if (err) {
        knd_log("-- failed to freeze class: \"%s\" :(", out->buf);
        return *total_size = 0, make_gsl_err_external(err);
        }*/

    /* bump frozen count */

    /* temp: simply rename the GSP file */
    out->reset(out);
    err = out->write(out, self->path, self->path_size);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    err = out->write(out, "/frozen.gsp", strlen("/frozen.gsp"));
    if (err) return *total_size = 0, make_gsl_err_external(err);

    /* null-termination is needed to call rename */
    out->buf[out->buf_size] = '\0';

    err = rename(self->path, out->buf);
    if (err) {
        knd_log("-- failed to rename GSP output file: \"%s\" :(", out->buf);
        return *total_size = 0, make_gsl_err_external(err);
    }

    /* TODO: inform retrievers */

    /* release resources */
    if (!stat(out->buf, &st)) {
        if (DEBUG_USER_LEVEL_TMP)
            knd_log("++ frozen DB file sync'ed OK, total bytes: %lu",
                    (unsigned long)st.st_size);
    }

    /*out->reset(out);
    err = out->write(out,
                           "{\"file_size\":",
                           strlen("{\"file_size\":"));
    if (err) return make_gsl_err_external(err);
    buf_size = sprintf(buf, "%lu", (unsigned long)st.st_size);
    err = out->write(out, buf, buf_size);
    if (err) return make_gsl_err_external(err);
    err = out->write(out, "}", 1);
    if (err) return make_gsl_err_external(err);
    */
    return *total_size = 0, make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_select(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndUserContext *ctx = task->user_ctx;
    struct kndRepo *repo;

    if (!ctx) {
        KND_TASK_LOG("no user selected");
        task->http_code = HTTP_BAD_REQUEST;
        return make_gsl_err(gsl_FAIL);
    }

    repo = ctx->repo;
    if (task->repo)
        repo = task->repo;
    else
        task->repo = repo;

    return knd_class_select(repo, rec, total_size, task);
}

static gsl_err_t run_get_user(void *obj, const char *name, size_t name_size)
{
    struct kndTask *task = obj;
    struct kndUser *self = task->user;
    struct kndUserContext *ctx;
    struct kndClassInst *inst;
    struct kndMemPool *mempool = task->mempool;
    struct kndShard *shard = task->shard;
    //struct kndRepo *repo = task->shard->repo;
    int err;

    assert(name_size != 0);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_get_class_inst(self->class, name, name_size, task, &inst);
    if (err) {
        KND_TASK_LOG("no such user: %.*s", name_size, name);
        return make_gsl_err_external(err);
    }

    err = shard->user_idx->get(shard->user_idx,
                               inst->entry->id, inst->entry->id_size,
                               (void**)&ctx);
    if (err) {
        err = knd_user_context_new(mempool, &ctx);
        if (err) return make_gsl_err_external(err);
        ctx->user_inst = inst;

        /*err = knd_repo_new(&repo, name, name_size, task->mempool);
        if (err) return make_gsl_err_external(err);
        repo->user = self;
        repo->user_ctx = ctx;

        // TODO
        repo->base = self->repo;
        ctx->repo = repo;

        err = knd_repo_open(repo, task);
        if (err) return make_gsl_err_external(err);
        */

        err = shard->user_idx->add(shard->user_idx,
                                  inst->entry->id, inst->entry->id_size,
                                  (void*)ctx);
        if (err) return make_gsl_err_external(err);
    }

    task->user_ctx = ctx;
    // task->repo = repo;

    return make_gsl_err(gsl_OK);
}

static int user_header_export(struct kndTask *task)
{
    knd_format format = task->ctx->format;
    struct kndOutput *out = task->out;
    int err;
    switch (format) {
        case KND_FORMAT_JSON:
            break;
        default:
            err = out->write(out, "{user ", strlen("{user "));      RET_ERR();
    }
    return knd_OK;
}

static int user_footer_export(struct kndTask *task)
{
    knd_format format = task->ctx->format;
    struct kndOutput *out = task->out;
    int err;
    switch (format) {
        case KND_FORMAT_JSON:
            break;
        default:
            err = out->writec(out, '}');      RET_ERR();
    }
    return knd_OK;
}

static gsl_err_t run_present_user(void *obj,
                                  const char *unused_var(val),
                                  size_t unused_var(val_size))
{
    struct kndTask *task = obj;
    struct kndClassInst *user_inst;
    struct kndOutput *out = task->out;
    int err;

    if (!task->user_ctx) {
        KND_TASK_LOG("no user selected");
        return make_gsl_err(gsl_FAIL);
    }

    out->reset(out);

    err = user_header_export(task);
    if (err) return make_gsl_err_external(err);

    user_inst = task->user_ctx->user_inst;
    err = knd_class_inst_export(user_inst, task->ctx->format, task);
    if (err) return make_gsl_err_external(err);

    err = user_footer_export(task);
    if (err) return make_gsl_err_external(err);
    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_present_state(void *obj,
                                   const char *unused_var(val),
                                   size_t unused_var(val_size))
{
    struct kndTask *task = obj;
    struct kndRepo *repo;
    int err;

    if (!task->user_ctx) {
        KND_TASK_LOG("no user selected");
        return make_gsl_err(gsl_FAIL);
    }

    repo = task->user_ctx->repo;
    err = knd_present_repo_state(repo, task);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_array_item(void *obj,
                                        const char *rec,
                                        size_t *total_size)
{
    struct kndTask *task = obj;

    return knd_class_import(task->repo, rec, total_size, task);
}

static gsl_err_t parse_class_array(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndTask *task = obj;

    task->type = KND_COMMIT_STATE;

    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .parse = parse_class_array_item,
        .obj = task
    };

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. import class array..");

    return gsl_parse_array(&item_spec, rec, total_size);
}

gsl_err_t knd_parse_select_user(void *obj,
                                const char *rec,
                                size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndRepo *repo;
    gsl_err_t parser_err;
    int err;

    /* reset defaults */
    task->user_ctx   = NULL;
    task->type = KND_GET_STATE;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_user,
          .obj = task
        },
        { .name = "_depth",
          .name_size = strlen("_depth"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->ctx->max_depth
        },
        { .name = "repo",
          .name_size = strlen("repo"),
          .parse = knd_parse_repo,
          .obj = task
        },
        { .type = GSL_SET_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_import,
          .obj = task
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_array,
          .obj = task
        }/* TODO ,
        { .type = GSL_GET_ARRAY_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_array,
          .obj = task
          }*/,
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_select,
          .obj = task
        },
        { .type = GSL_SET_STATE,
          .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_import,
          .obj = task
        },
        { .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_select,
          .obj = task
        },
        { .name = "_sync",
          .name_size = strlen("_sync"),
          .parse = parse_sync_task,
          .obj = task
        },
        { .name = "_state",
          .name_size = strlen("_state"),
          .run = run_present_state,
          .obj = task
        },
        { .is_default = true,
          .run = run_present_user,
          .obj = task
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        struct kndOutput *log = task->log;
        knd_log("-- user task parse failure: \"%.*s\"",
                log->buf_size, log->buf);
        if (!log->buf_size) {
            err = log->write(log, "internal server error",
                             strlen("internal server error"));
            if (err) {
                parser_err = make_gsl_err_external(err);
                goto cleanup;
            }
        }
        goto cleanup;
    }

    assert(task->user_ctx);
    repo = task->user_ctx->repo;

    switch (task->type) {
    case KND_COMMIT_STATE:
        err = knd_confirm_commit(repo, task);
        if (err) return make_gsl_err_external(err);
        break;
    default:
        break;
    }
    return make_gsl_err(gsl_OK);

 cleanup:

    // TODO release resources 
    return parser_err;
}

gsl_err_t knd_create_user(void *obj,
                          const char *rec,
                          size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndUser *self = task->user;
    int err;

    if (!task->ctx->commit) {
        err = knd_commit_new(task->mempool, &task->ctx->commit);
        if (err) return make_gsl_err_external(err);

        task->ctx->commit->orig_state_id = atomic_load_explicit(&task->repo->snapshot.num_commits,
                                                                memory_order_relaxed);
    }

    err = knd_parse_import_class_inst(self->class, rec, total_size, task);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    err = knd_class_inst_commit_state(self->class,
                                      task->ctx->class_inst_state_refs,
                                      task->ctx->num_class_inst_state_refs,
                                      task);
    if (err) {
        return make_gsl_err_external(err);
    }
    
    return make_gsl_err(gsl_OK);
}

int knd_user_new(struct kndUser **user)
{
    struct kndUser *self;

    self = malloc(sizeof(struct kndUser));                                        ALLOC_ERR(self);
    memset(self, 0, sizeof(struct kndUser));
    memset(self->id, '0', KND_ID_SIZE);
    memset(self->last_uid, '0', KND_ID_SIZE);
    memset(self->db_state, '0', KND_ID_SIZE);

    *user = self;

    return knd_OK;
}

int knd_user_context_new(struct kndMemPool *mempool,
                         struct kndUserContext **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                                sizeof(struct kndUserContext), &page);                RET_ERR();
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_SMALL,
                                     sizeof(struct kndUserContext), &page);           RET_ERR();
    }
    *result = page;
    return knd_OK;
}
