#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gsl-parser.h>

#include "knd_user.h"
#include "knd_utils.h"
#include "knd_steward.h"
#include "knd_repo.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_proc.h"
#include "knd_text.h"
#include "knd_set.h"
#include "knd_shared_set.h"
#include "knd_mempool.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_output.h"

#define DEBUG_USER_LEVEL_0 0
#define DEBUG_USER_LEVEL_1 0
#define DEBUG_USER_LEVEL_2 0
#define DEBUG_USER_LEVEL_3 0
#define DEBUG_USER_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndQuery *query;
    struct kndRepo *repo;
    struct kndRepoSnapshot *snapshot;
};

void knd_user_del(struct kndUser *self)
{
    if (self->repo)
        knd_repo_del(self->repo);
    free(self);
}

static gsl_err_t parse_proc_import(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndRepoSnapshot *snapshot = ctx->snapshot;
    struct kndTask *task = ctx->task;
    struct kndUserContext *user_ctx = task->user_ctx;
    struct kndRepoAccess *acl = user_ctx->acls;
    int err;

    assert(user_ctx->snapshot != NULL);
    assert(acl != NULL);

    if (DEBUG_USER_LEVEL_3) {
        knd_log(".. parsing user proc import: \"%.*s\"..", 64, rec);
    }

    if (!acl->allow_write) {
        KND_TASK_LOG("writing not allowed");
        err = knd_ACCESS;
        if (err) return make_gsl_err_external(err);
    }

    //if (!task->ctx->commit->orig_state_id)
    //    task->ctx->commit->orig_state_id = atomic_load_explicit(&task->snapshot->num_commits,
    //                                                            memory_order_relaxed);
    return knd_proc_import(rec, total_size, snapshot, task);
}

static gsl_err_t parse_proc_select(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepoSnapshot *snapshot = ctx->snapshot;
    return knd_proc_select(rec, total_size, snapshot, task);
}

int knd_create_user_repo(struct kndTask *task)
{
    struct kndUserContext *ctx = task->user_ctx;
    struct kndRepo *repo;
    int err;
    assert(ctx->snapshot == NULL);

    err = knd_repo_new(&repo, "~", 1, "", 0);
    KND_TASK_ERR("failed to alloc new repo");
    repo->base = ctx->base_repo;
    ctx->repo = repo;

    err = knd_repo_read(repo, task);
    if (err) {
        KND_TASK_LOG("failed to open {repo %.*s}", repo->name_size, repo->name);
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
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepoSnapshot *snapshot = ctx->snapshot;
    struct kndUserContext *user_ctx = task->user_ctx;
    struct kndRepoAccess *acl = user_ctx->acls;
    struct kndClass *cls;
    int err;

    assert(acl != NULL);

    if (DEBUG_USER_LEVEL_3) {
        knd_log(".. parsing user class import: \"%.*s\"..", 64, rec);
    }

    if (!acl->allow_write) {
        KND_TASK_LOG("writing not allowed");
        err = knd_ACCESS;
        if (err) return make_gsl_err_external(err);
    }

    if (!task->ctx->commit) {
        err = knd_commit_new(&task->ctx->commit, task->mempool);
        if (err) return make_gsl_err_external(err);
        
        //task->ctx->commit->orig_state_id = atomic_load_explicit(&task->snapshot->num_commits,
        //                                                        memory_order_relaxed);
    }

    err = knd_class_import(rec, total_size, &cls, snapshot, task);
    if (err) return make_gsl_err_external(err);

    /* assign a unique class entry id */
    //entry->numid = ++task->idxs.cls_id_count;
    //knd_uid_create(entry->numid, entry->id, &entry->id_size);

    return make_gsl_err(gsl_OK);
}

#if 0
static gsl_err_t parse_class_select(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepo *snapshot = ctx->snapshot;
    gsl_err_t parser_err;
    if (!task->user_ctx) {
        KND_TASK_LOG("no user selected");
        return make_gsl_err(gsl_FAIL);
    }

    /* check private repo first */
    if (task->user_ctx->snapshot) {
        parser_err = knd_class_select(rec, total_size, snapshot, task);
        if (parser_err.code == gsl_OK) {
            return parser_err;
        }
        // failed import? 
        if (task->type == KND_TASK_COMMIT) {
            return make_gsl_err(gsl_FAIL);
        }
    }
    /* shared read-only repo */
    return knd_class_select(rec, total_size, snapshot, task);
}

static gsl_err_t parse_text_search(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    if (!task->user_ctx) {
        KND_TASK_LOG("no user selected");
        return make_gsl_err(gsl_FAIL);
    }
    return knd_text_search(task->user_ctx->snapshot, rec, total_size, task);
}
#endif

static int build_user_ctx(struct kndUser *self, struct kndClassInst *inst,
                          struct kndUserContext **result, struct kndTask *task)
{
    struct kndUserContext *ctx;
    struct kndOutput *out = task->out;
    int err;
    err = knd_user_context_new(&ctx);
    KND_TASK_ERR("failed to alloc user ctx");
    ctx->type =  KND_USER_AUTHENTICATED;
    ctx->inst = inst;
    ctx->base_repo = self->repo;
    ctx->mempool = self->mempool_write;

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
    struct kndUser *self = task->user;
    struct kndUserContext *ctx;
    struct kndClassInst *inst;
    int err;

    if (task->user_ctx) return make_gsl_err(gsl_OK);
    
    assert(name_size != 0);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    /* default anonymous user */
    if (name_size == 1 && name[0] == '_') {
        err = knd_user_context_new(&ctx);
        if (err) return make_gsl_err_external(err);
        ctx->repo = self->repo;
        ctx->base_repo = self->repo;
        ctx->acls = self->default_acls;
        ctx->mempool = self->mempool_write;

        task->user_ctx = ctx;
        return make_gsl_err(gsl_OK);
    }

    err = knd_get_class_inst(self->class, name, name_size, task, &inst);
    if (err) {
        KND_TASK_LOG("no such user: %.*s", name_size, name);
        return make_gsl_err_external(err);
    }

    err = build_user_ctx(self, inst, &ctx, task);
    if (err) {
        KND_TASK_LOG("failed to build user ctx %.*s", inst->name_size, inst->name);
        return make_gsl_err_external(err);
    }

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

static gsl_err_t run_present_user(void *obj, const char *unused_var(val), size_t unused_var(val_size))
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    //struct kndRepoSnapshot *snapshot = ctx->snapshot;
    struct kndClassInst *user_inst;
    struct kndOutput *out = task->out;
    int err;

    if (!task->user_ctx) {
        KND_TASK_LOG("no user selected");
        // return make_gsl_err(gsl_FAIL);

        // TODO: check filters

        // choose export format
        
        /*err = knd_shared_set_map(self->class->inst_idx,
                knd_class_inst_iterate_export_JSON, (void*)task);
        if (err) {
            knd_log("export map failed: %d", err);
            return make_gsl_err_external(err);
        }
        */
        return make_gsl_err(gsl_OK);
    }

    if (task->user_ctx->type != KND_USER_AUTHENTICATED)
        return make_gsl_err(gsl_OK);

    out->reset(out);
    err = user_header_export(task);
    if (err) return make_gsl_err_external(err);

    user_inst = task->user_ctx->inst;

    err = knd_class_inst_export(user_inst, task->ctx->format, false, KND_SELECTED, task);
    if (err) return make_gsl_err_external(err);

    err = user_footer_export(task);
    if (err) return make_gsl_err_external(err);
    
    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_parse_select_user(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;

    gsl_err_t parser_err;

    switch (task->type) {
    case KND_TASK_RESTORE:
        break;
    default:
        task->user_ctx   = NULL;
        task->type = KND_TASK_QUERY;
    }
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_user,
          .obj = obj
        },
        { .name = "_depth",
          .name_size = strlen("_depth"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &task->ctx->max_depth
        },
        { .name = "repo",
          .name_size = strlen("repo"),
          .parse = knd_parse_repo_select,
          .obj = obj
        },
        { .type = GSL_SET_STATE,
          .name = "cls",
          .name_size = strlen("cls"),
          .parse = parse_class_import,
          .obj = obj
        },
        { .type = GSL_SET_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_import,
          .obj = obj
        }/*,
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_select,
          .obj = obj
          }*/,
        { .type = GSL_SET_STATE,
          .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_import,
          .obj = obj
        },
        { .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_select,
          .obj = obj
        }/*,
        { .name = "text",
          .name_size = strlen("text"),
          .parse = parse_text_search,
          .obj = obj
          }*/,
        { .is_default = true,
          .run = run_present_user,
          .obj = obj
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    switch (parser_err.code) {
    case gsl_NO_MATCH:
        KND_TASK_LOG("user area got an unrecognized {tag %.*s}",
                     parser_err.val_size, parser_err.val);
        break;
    default:
        break;
    }

    switch (task->type) {
    case KND_TASK_RESTORE:
        return parser_err;
    default:
        break;
    }
    return parser_err;
}

gsl_err_t knd_create_user(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndRepoSnapshot *snapshot = ctx->snapshot;
    struct kndUser *self = task->user;
    int err;

    if (!task->ctx->commit) {
        err = knd_commit_new(&task->ctx->commit, task->mempool);
        if (err) return make_gsl_err_external(err);
        //task->ctx->commit->orig_state_id = atomic_load_explicit(&task->snapshot->num_commits,
        //                                                        memory_order_relaxed);
    }
    err = knd_import_class_inst(self->class->entry, rec, total_size, snapshot, task);
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

static int init_mempool(struct kndSteward *steward, knd_mempool_t memtype, size_t numid,
                        struct kndMemPool **result)
{
    struct kndMemPool *mempool;
    struct kndOutput *out = steward->out;
    struct kndOutput *log = steward->log;
    int err;

    err = knd_mempool_new(&mempool, memtype, numid);
    KND_STEWARD_ERR("failed to create a regular mempool");

    mempool->num_large_pages = steward->mem_user_config.num_large_pages;
    mempool->num_base_pages = steward->mem_user_config.num_base_pages;
    mempool->num_small_x4_pages = steward->mem_user_config.num_small_x4_pages;
    mempool->num_small_x2_pages = steward->mem_user_config.num_small_x2_pages;
    mempool->num_small_pages = steward->mem_user_config.num_small_pages;
    mempool->num_tiny_pages = steward->mem_user_config.num_tiny_pages;

    err = knd_mempool_alloc(mempool);
    KND_STEWARD_ERR("failed to alloc a regular mempool");

    *result = mempool;
    return knd_OK;
}

int knd_user_new(struct kndUser **result,
                 const char *classname,   size_t classname_size,
                 const char *path,        size_t path_size,
                 const char *repo_name,    size_t repo_name_size,
                 const char *schema_path, size_t schema_path_size,
                 struct kndSteward *steward, struct kndTask *task)
{
    struct kndUser *user;
    struct kndRepoAccess *acl;
    struct kndMemPool *mempool;
    struct kndOutput *out = steward->out;
    struct kndOutput *log = steward->log;
    // TODO
    struct kndRepo *repo = steward->repo;
    struct kndRepoSnapshot *snapshot = repo->snapshot;
    struct kndClassEntry *entry;
    int err;

    user = calloc(1, sizeof(struct kndUser));
    ALLOC_ERR(user);
    user->classname = classname;
    user->classname_size = classname_size;

    err = knd_get_cls_entry_by_name(snapshot, classname, classname_size, &entry, task);
    if (err) {
        KND_TASK_LOG("no such user {cls %.*s}", classname_size, classname);
        goto error;
    }

    err = knd_class_acquire(entry, &user->class, snapshot, task);
    KND_TASK_ERR("failed to acquire {cls %.*s}", entry->name_size, entry->name);

    user->schema_path = schema_path;
    user->schema_path_size = schema_path_size;

    if (strlen(KND_USERSPACE_DIR_NAME) + path_size >= KND_PATH_SIZE) {
        knd_log("path limit exceeded");
        goto error;
    }

    user->path_size = path_size + strlen(KND_USERSPACE_DIR_NAME);
    memcpy(user->path, path, path_size);
    memcpy(user->path + path_size, KND_USERSPACE_DIR_NAME, strlen(KND_USERSPACE_DIR_NAME));

    err = knd_mkpath(user->path, user->path_size, 0755, false);
    if (err != knd_OK) {
        knd_log("-- failed to make {path %.*s}", user->path_size, user->path);
        goto error;
    }

    err = init_mempool(steward, KND_ALLOC_INCR, 1, &user->mempool_read);
    KND_STEWARD_ERR("failed to init a read mempool");

    err = init_mempool(steward, KND_ALLOC_INCR, 2, &user->mempool_read_temp);
    KND_STEWARD_ERR("failed to init a temp read mempool");

    err = init_mempool(steward, KND_ALLOC_SHARED, 3, &user->mempool_write);
    KND_STEWARD_ERR("failed to init a shared mempool");

    err = init_mempool(steward, KND_ALLOC_SHARED, 4, &user->mempool_write_temp);
    KND_STEWARD_ERR("failed to init a shared mempool");

    /* base repo for all users */
    mempool = user->mempool_write;
    user->repo_name = repo_name;
    user->repo_name_size = repo_name_size;
    err = knd_repo_new(&user->repo, repo_name, repo_name_size,
                       schema_path, schema_path_size);
    if (err) goto error;

    err = knd_dict_set(steward->repo_name_idx, repo_name, repo_name_size, (void*)user->repo, task);
    KND_TASK_ERR("failed to register {repo %.*s}", repo_name_size, repo_name);

    /* default acl */
    err = knd_repo_access_new(&acl, mempool);
    KND_TASK_ERR("failed to alloc repo acl");
    acl->repo = user->repo;
    acl->allow_read = true;
    acl->allow_write = true;
    user->default_acls = acl;

    task->user_ctx->repo = user->repo;
    task->user_ctx->mempool = user->mempool_write;
    task->user_ctx->acls = user->default_acls;
    task->mempool = mempool;

    //err = knd_repo_read(user->repo, task);
    //if (err) goto error;

    err = knd_set_new(&user->user_idx, KND_SET_STORE_MEMONLY, mempool);
    if (err) goto error;

    *result = user;
    return knd_OK;
 error:
    free(user);
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

int knd_repo_access_new(struct kndRepoAccess **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndRepoAccess));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndRepoAccess));
    *result = page;
    return knd_OK;
}
