#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gsl-parser.h>

#include "knd_user.h"
#include "knd_utils.h"
#include "knd_shard.h"
#include "knd_repo.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_proc.h"
#include "knd_text.h"
#include "knd_set.h"
#include "knd_mempool.h"
#include "knd_cache.h"
#include "knd_state.h"
#include "knd_commit.h"
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

static void free_user_ctx(void *obj)
{
    struct kndUserContext *ctx = obj;

    atomic_store_explicit(&ctx->inst->entry->cache_cell_num, 0, memory_order_relaxed);

    knd_log(".. freeing user ctx \"%.*s\"", ctx->inst->name_size, ctx->inst->name);
}

static gsl_err_t parse_proc_import(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    task->type = KND_COMMIT_STATE;

    if (!task->ctx->commit->orig_state_id)
        task->ctx->commit->orig_state_id = atomic_load_explicit(&task->repo->snapshots->num_commits,
                                                                memory_order_relaxed);
    return knd_proc_import(task->repo, rec, total_size, task);
}

static gsl_err_t parse_proc_select(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    return knd_proc_select(task->repo, rec, total_size, task);
}

int knd_create_user_repo(struct kndTask *task)
{
    struct kndUserContext *ctx = task->user_ctx;
    struct kndRepo *repo;
    int err;

    assert(ctx->repo == NULL);

    err = knd_repo_new(&repo, "~", 1, NULL, 0, ctx->mempool);
    KND_TASK_ERR("failed to alloc new repo");
    repo->base = ctx->base_repo;
    ctx->repo = repo;

    err = knd_repo_open(repo, task);
    if (err) {
        KND_TASK_LOG("failed to open repo: %.*s", repo->name_size, repo->name);
        ctx->repo = NULL;
        knd_repo_del(repo);
        return err;
    }
    /* restore task */
    knd_task_reset(task);
    task->user_ctx = ctx;
    return knd_OK;
}

static gsl_err_t parse_class_import(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndUserContext *user_ctx = task->user_ctx;
    int err;

    assert(user_ctx->repo);

    if (DEBUG_USER_LEVEL_TMP)
        knd_log(".. parsing user class import: \"%.*s\"..", 64, rec);

    task->type = KND_COMMIT_STATE;
    if (!task->ctx->commit) {
        err = knd_commit_new(task->mempool, &task->ctx->commit);
        if (err) return make_gsl_err_external(err);
        
        task->ctx->commit->orig_state_id = atomic_load_explicit(&task->repo->snapshots->num_commits, memory_order_relaxed);
    }

    return knd_class_import(user_ctx->repo, rec, total_size, task);
}

static gsl_err_t parse_class_select(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    gsl_err_t parser_err;
    if (!task->user_ctx) {
        KND_TASK_LOG("no user selected");
        task->http_code = HTTP_BAD_REQUEST;
        return make_gsl_err(gsl_FAIL);
    }
    /* check private repo first */
    if (task->user_ctx->repo) {
        parser_err = knd_class_select(task->user_ctx->repo, rec, total_size, task);
        if (parser_err.code == gsl_OK) {
            return parser_err;
        }
        // failed import? 
        if (task->type == KND_COMMIT_STATE) {
            return make_gsl_err(gsl_FAIL);
        }
    }
    knd_log(".. check user base repo");
    /* shared read-only repo */
    return knd_class_select(task->user_ctx->base_repo, rec, total_size, task);
}

static gsl_err_t parse_text_search(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    if (!task->user_ctx) {
        KND_TASK_LOG("no user selected");
        task->http_code = HTTP_BAD_REQUEST;
        return make_gsl_err(gsl_FAIL);
    }
    return knd_text_search(task->user_ctx->repo, rec, total_size, task);
}

static int build_user_ctx(struct kndUser *self, struct kndClassInst *inst,
                          struct kndUserContext **result, struct kndTask *task)
{
    struct kndUserContext *ctx;
    struct kndOutput *out = task->out;
    int err;
    err = knd_user_context_new(&ctx);
    KND_TASK_ERR("failed to alloc user ctx");
    ctx->inst = inst;
    ctx->base_repo = self->repo;
    ctx->mempool = self->mempool;

    out->reset(out);
    OUT("users/", strlen("users/"));

    /* user dir prefix */
    if (inst->name_size >= KND_USER_PATH_PREFIX_SIZE) {
        OUT(inst->name, KND_USER_PATH_PREFIX_SIZE);
        OUT("/", 1);
    }
    OUT(inst->name, inst->name_size);
    OUT("/", 1);

    if (out->buf_size > KND_PATH_SIZE) return knd_LIMIT;
    memcpy(ctx->path, out->buf, out->buf_size);
    ctx->path_size = out->buf_size;

    task->user_ctx = ctx;
    err = knd_create_user_repo(task);
    KND_TASK_ERR("failed to create user repo");

    *result = ctx;
    return knd_OK;
}

static gsl_err_t run_get_user(void *obj, const char *name, size_t name_size)
{
    struct kndTask *task = obj;
    struct kndUser *self = task->shard->user;
    struct kndUserContext *ctx;
    struct kndClassInst *inst;
    size_t cell_num = 0;
    int err;

    if (task->user_ctx) return make_gsl_err(gsl_OK);

    assert(name_size != 0);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_get_class_inst(self->class, name, name_size, task, &inst);
    if (err) {
        KND_TASK_LOG("no such user: %.*s", name_size, name);
        return make_gsl_err_external(err);
    }

    do {
        cell_num = atomic_load_explicit(&inst->entry->cache_cell_num, memory_order_relaxed);
        if (DEBUG_USER_LEVEL_3)
            knd_log("== user inst %.*s (cache cell:%zu)", name_size, name, cell_num);
        if (!cell_num) {
            err = build_user_ctx(self, inst, &ctx, task);
            if (err) {
                knd_log("failed to build user ctx");
                return make_gsl_err_external(err);
            }
            err = knd_cache_set(self->cache, (void*)ctx, &cell_num);
            if (err) {
                knd_log("failed to set a cache cell: %d", err);
                if (err == knd_CONFLICT) {
                    // free resources
                    free_user_ctx(ctx);
                    ctx = NULL;
                    // make another attempt to read / create ctx
                    continue;
                }
                return make_gsl_err_external(err);
            }
            // 0 cell num denotes NULL so add 1 to store the cache cell idx
            cell_num++;
            atomic_store_explicit(&inst->entry->cache_cell_num, cell_num, memory_order_relaxed);
            ctx->cache_cell_num = cell_num;
        }

        // make sure we mark the cache cell as engaged by another reader
        err = knd_cache_get(self->cache, cell_num - 1, (void**)&ctx);
        if (err) {
            knd_log("failed to mark cell num %zu as read mode", cell_num - 1);
            return make_gsl_err_external(err);
        }

        // stale cache cell?
        if (ctx->inst != inst) {
            knd_log("cell num %zu contains ref to some other ctx, retrying..", cell_num - 1);
            continue;
        }
        task->user_ctx = ctx;
        break;
    } while (1);

    if (DEBUG_USER_LEVEL_TMP)
        knd_log("== user %.*s (cache cell:%zu) snapshot #%zu role:%d", inst->name_size, inst->name,
                cell_num, ctx->repo->snapshots->numid, ctx->repo->snapshots->role);

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

    user_inst = task->user_ctx->inst;
    err = knd_class_inst_export(user_inst, task->ctx->format, false, task);
    if (err) return make_gsl_err_external(err);

    err = user_footer_export(task);
    if (err) return make_gsl_err_external(err);
    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_present_state(void *obj, const char *unused_var(val), size_t unused_var(val_size))
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

static gsl_err_t parse_class_array(void *obj, const char *rec, size_t *total_size)
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

static gsl_err_t parse_snapshot_task(void *obj, const char *unused_var(rec), size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndRepo *repo = task->user_ctx ? task->user_ctx->repo : task->shard->user->repo;
    int err;

    task->type = KND_SNAPSHOT_STATE;
    err = knd_repo_snapshot(repo, task);
    if (err) {
        KND_TASK_LOG("failed to build a snapshot of user repo");
        return *total_size = 0, make_gsl_err(gsl_FAIL);
    }
    return *total_size = 0, make_gsl_err(gsl_OK);
}

gsl_err_t knd_parse_select_user(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    gsl_err_t parser_err;

    switch (task->type) {
    case KND_RESTORE_STATE:
        break;
    default:
        task->user_ctx   = NULL;
        task->type = KND_GET_STATE;
    }
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
        },
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
        { .name = "text",
          .name_size = strlen("text"),
          .parse = parse_text_search,
          .obj = task
        },
        { .name = "_state",
          .name_size = strlen("_state"),
          .run = run_present_state,
          .obj = task
        },
        { .name = "_snapshot",
          .name_size = strlen("_snapshot"),
          .parse = parse_snapshot_task,
          .obj = task
        },
        { .is_default = true,
          .run = run_present_user,
          .obj = task
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    switch (parser_err.code) {
    case gsl_NO_MATCH:
        KND_TASK_LOG("user area got an unrecognized tag \"%.*s\"", parser_err.val_size, parser_err.val);
        break;
    default:
        break;
    }

    switch (task->type) {
    case KND_RESTORE_STATE:
        return parser_err;
    default:
        break;
    }
    return parser_err;
}

gsl_err_t knd_create_user(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndUser *self = task->shard->user;
    int err;

    if (!task->ctx->commit) {
        err = knd_commit_new(task->mempool, &task->ctx->commit);
        if (err) return make_gsl_err_external(err);

        task->ctx->commit->orig_state_id = atomic_load_explicit(&task->repo->snapshots->num_commits, memory_order_relaxed);
    }
    err = knd_import_class_inst(self->class->entry, rec, total_size, task);
    if (err) {
        return *total_size = 0, make_gsl_err_external(err);
    }
    err = knd_class_inst_commit_state(self->class, task->ctx->class_inst_state_refs,
                                      task->ctx->num_class_inst_state_refs, task);
    if (err) {
        return make_gsl_err_external(err);
    }
    return make_gsl_err(gsl_OK);
}

int knd_user_new(struct kndUser **user, const char *classname, size_t classname_size,
                 const char *path, size_t path_size, const char *reponame, size_t reponame_size,
                 const char *schema_path, size_t schema_path_size, struct kndShard *shard, struct kndTask *task)
{
    struct kndUser *self;
    struct kndMemPool *mempool = NULL;
    struct kndRepo *repo = task->repo;
    int err;

    self = malloc(sizeof(struct kndUser));                                        ALLOC_ERR(self);
    memset(self, 0, sizeof(struct kndUser));
    self->classname = classname;
    self->classname_size = classname_size;

    err = knd_get_class(repo, classname, classname_size, &self->class, task);
    if (err) {
        knd_log("no such user class: %.*s", classname_size, classname);
        goto error;
    }

    self->schema_path = schema_path;
    self->schema_path_size = schema_path_size;

    if (strlen("users/") + path_size >= KND_PATH_SIZE) {
        knd_log("path limit exceeded");
        goto error;
    }

    self->path_size = path_size + strlen("users/");
    memcpy(self->path, path, path_size);
    memcpy(self->path + path_size, "users/", strlen("users/"));

    err = knd_mkpath(self->path, self->path_size, 0755, false);
    if (err != knd_OK) {
        knd_log("-- failed to make path: \"%.*s\"", self->path_size, self->path);
        goto error;
    }

    err = knd_mempool_new(&mempool, KND_ALLOC_SHARED, 0);
    if (err) goto error;
    mempool->num_pages = shard->mem_config.num_pages;
    mempool->num_small_x4_pages = shard->mem_config.num_small_x4_pages;
    mempool->num_small_x2_pages = shard->mem_config.num_small_x2_pages;
    mempool->num_small_pages = shard->mem_config.num_small_pages;
    mempool->num_tiny_pages = shard->mem_config.num_tiny_pages;
    err = mempool->alloc(mempool);
    if (err) goto error;
    self->mempool = mempool;

    /* read-only base repo for all users */
    self->reponame = reponame;
    self->reponame_size = reponame_size;
    err = knd_repo_new(&self->repo, reponame, reponame_size, schema_path, schema_path_size, shard->mempool);
    if (err) goto error;

    task->repo = self->repo;
    task->mempool = shard->mempool;
    err = knd_repo_open(self->repo, task);
    if (err) goto error;
    err = knd_set_new(shard->mempool, &self->user_idx);
    if (err) goto error;

    err = knd_cache_new(&self->cache, KND_CACHE_NUM_CELLS, KND_CACHE_MAX_MEM_SIZE, free_user_ctx);
    if (err) goto error;
    
    *user = self;
    return knd_OK;
 error:
    free(self);
    return err;
}

int knd_user_context_new(struct kndUserContext **result)
{
    struct kndUserContext *self;
    self = calloc(1, sizeof(struct kndUserContext));
    if (!self) return knd_NOMEM;
    *result = self;
    return knd_OK;
}
