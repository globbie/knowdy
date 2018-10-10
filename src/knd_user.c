#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gsl-parser.h>
#include <glb-lib/output.h>

//#include "knd_policy.h"
#include "knd_user.h"
#include "knd_shard.h"
#include "knd_repo.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_proc.h"
#include "knd_rel.h"
#include "knd_set.h"
#include "knd_mempool.h"
#include "knd_state.h"

#define DEBUG_USER_LEVEL_0 0
#define DEBUG_USER_LEVEL_1 0
#define DEBUG_USER_LEVEL_2 0
#define DEBUG_USER_LEVEL_3 0
#define DEBUG_USER_LEVEL_TMP 1

static void del(struct kndUser *self)
{
    if (self->repo)
        self->repo->del(self->repo);
    free(self);
}

static void str(struct kndUser *self)
{
    knd_log("USER: %.*s", self->name_size, self->name);
}

static gsl_err_t parse_proc_import(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndUser *self = obj;
    struct kndProc *proc = self->repo->root_class->proc;

    self->task->type = KND_UPDATE_STATE;
    return knd_proc_import(proc, rec, total_size);
}

static gsl_err_t parse_proc_select(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndUser *self = obj;
    struct kndProc *proc = self->repo->root_class->proc;

    return knd_proc_select(proc, rec, total_size);
}

static gsl_err_t parse_rel_import(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndUser *self = obj;
    struct kndRel *rel = self->repo->root_rel;

    self->task->type = KND_UPDATE_STATE;
    return rel->import(rel, rec, total_size);
}

static gsl_err_t parse_class_import(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndUser *self = obj;
    struct kndClass *c = self->repo->root_class;
    struct kndUserContext *ctx = self->curr_ctx;   
    if (ctx)
        c = ctx->repo->root_class;

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. parsing the default class import: \"%.*s\"..", 64, rec);

    self->task->type = KND_UPDATE_STATE;
    return knd_class_import(c, rec, total_size);
}

static gsl_err_t parse_sync_task(void *obj,
                                 const char *rec __attribute__((unused)),
                                 size_t *total_size)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    struct kndUser *self = obj;
    //struct glbOutput *out = self->out;
    struct stat st;
    char *s, *n;
    size_t path_size;
    int err;

    if (DEBUG_USER_LEVEL_TMP)
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

    self->task->type = KND_SYNC_STATE;

    //err = knd_class_freeze(self->repo->root_class);
    /*if (err) {
        knd_log("-- failed to freeze class: \"%s\" :(", self->out->buf);
        return *total_size = 0, make_gsl_err_external(err);
        }*/

    /* bump frozen count */

    /* temp: simply rename the GSP file */
    self->out->reset(self->out);
    err = self->out->write(self->out, self->path, self->path_size);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    err = self->out->write(self->out, "/frozen.gsp", strlen("/frozen.gsp"));
    if (err) return *total_size = 0, make_gsl_err_external(err);

    /* null-termination is needed to call rename */
    self->out->buf[self->out->buf_size] = '\0';

    err = rename(self->path, self->out->buf);
    if (err) {
        knd_log("-- failed to rename GSP output file: \"%s\" :(", self->out->buf);
        return *total_size = 0, make_gsl_err_external(err);
    }

    /* TODO: inform retrievers */

    /* release resources */
    if (!stat(self->out->buf, &st)) {
        if (DEBUG_USER_LEVEL_TMP)
            knd_log("++ frozen DB file sync'ed OK, total bytes: %lu",
                    (unsigned long)st.st_size);
    }

    /*self->out->reset(self->out);
    err = self->out->write(self->out,
                           "{\"file_size\":",
                           strlen("{\"file_size\":"));
    if (err) return make_gsl_err_external(err);
    buf_size = sprintf(buf, "%lu", (unsigned long)st.st_size);
    err = self->out->write(self->out, buf, buf_size);
    if (err) return make_gsl_err_external(err);
    err = self->out->write(self->out, "}", 1);
    if (err) return make_gsl_err_external(err);
    */
    return *total_size = 0, make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_select(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndUser *self = obj;
    struct kndUserContext *ctx = self->curr_ctx;
    struct glbOutput *log = self->task->log;
    struct kndClass *c;

    if (!ctx) {
        knd_log("-- no user selected");
        log->writef(log, "no user selected");
        self->task->http_code = HTTP_BAD_REQUEST;
        return make_gsl_err(gsl_FAIL);
    }

    self->task->out->reset(self->task->out);

    c = self->repo->root_class;
    if (ctx) {
        c = ctx->repo->root_class;
    }

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. parsing the default class select: \"%.*s\"", 64, rec);

    return knd_class_select(c, rec, total_size);
}

static gsl_err_t parse_rel_select(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndUser *self = obj;
    struct kndRel *rel = self->repo->root_rel;
    gsl_err_t err;

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. User %.*s:  parsing default Rel select: \"%.*s\"",
                self->name_size, self->name, 64, rec);

    err = rel->select(rel, rec, total_size);
    if (err.code) {
        /* TODO: release resources */
        //rel->reset_inbox(rel);
        knd_log("-- rel select failed :(");
        return err;
    }

    return make_gsl_err(gsl_OK);
}


/*static gsl_err_t parse_liquid_updates(void *obj,
                                      const char *rec,
                                      size_t *total_size)
{
    struct kndUser *self = (struct kndUser*)obj;

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. parse and apply liquid updates..");

    self->task->type = KND_LIQUID_STATE;

    return self->repo->root_class->apply_liquid_updates(self->repo->root_class, rec, total_size);
}
*/

static gsl_err_t run_get_user(void *obj, const char *name, size_t name_size)
{
    struct kndUser *self = obj;
    struct kndClass *c;
    struct kndUserContext *ctx;
    struct kndClassInst *inst;
    struct kndMemPool *mempool = self->mempool;
    struct glbOutput *log = self->task->log;
    struct kndTask *task = self->task;
    struct kndRepo *repo = self->shard->repo;
    void *result = NULL;
    int e, err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    if (DEBUG_USER_LEVEL_TMP)
        knd_log(".. select user: \"%.*s\" system repo:%.*s..",
                name_size, name,
                repo->name_size,
                repo->name);

    err = knd_get_class(repo,
                        self->class_name,
                        self->class_name_size,
                        &c);
    if (err) {
        knd_log("-- no such class: %.*s",
                self->class_name_size,
                self->class_name);
        log->reset(log);
        e = log->write(log,   "no such class: ",
                       strlen("no such class: "));
        if (e) return make_gsl_err_external(e);
        e = log->write(log, name, name_size);
        if (e) return make_gsl_err_external(e);
        task->http_code = HTTP_NOT_FOUND;
        return make_gsl_err_external(err);
    }

    err = knd_get_class_inst(c, name, name_size, &inst);
    if (err) {
        knd_log("-- no such user: %.*s", name_size, name);
        log->reset(log);
        e = log->write(log,   "no such user: ",
                       strlen("no such user: "));
        if (e) return make_gsl_err_external(e);
        e = log->write(log, name, name_size);
        if (e) return make_gsl_err_external(e);
        task->http_code = HTTP_NOT_FOUND;
        return make_gsl_err_external(err);
    }

    err = self->user_idx->get(self->user_idx,
                              inst->entry->id, inst->entry->id_size,
                              &result);
    ctx = result;
    if (err) {
        err = knd_user_context_new(mempool, &ctx);
        if (err) return make_gsl_err_external(err);

        ctx->user_inst = inst;

        err = kndRepo_new(&repo, self->mempool);
        if (err) return make_gsl_err_external(err);

        memcpy(repo->name, name, name_size);
        repo->name_size = name_size;
        repo->user = self;
        repo->user_ctx = ctx;

        // TODO
        repo->base = self->repo;
        ctx->repo = repo;
        repo->task = self->shard->task;

        err = repo->init(repo);
        if (err) return make_gsl_err_external(err);

        err = self->user_idx->add(self->user_idx,
                                  inst->entry->id, inst->entry->id_size,
                                  (void*)ctx);
        if (err) return make_gsl_err_external(err);
    }

    self->curr_ctx = ctx;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_present_user(void *data,
                                  const char *val __attribute__((unused)),
                                  size_t val_size __attribute__((unused)))
{
    struct kndUser *self = data;
    struct kndClassInst *user;
    struct glbOutput *out = self->task->out;
    int err;

    if (!self->curr_ctx) {
        knd_log("-- no user selected");
        return make_gsl_err(gsl_FAIL);
    }
    out->reset(out);
    user = self->curr_ctx->user_inst;
    user->max_depth = self->max_depth;

    err = user->export(user, KND_FORMAT_JSON, out);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_present_state(void *data,
                                   const char *val __attribute__((unused)),
                                   size_t val_size __attribute__((unused)))
{
    struct kndUser *self = data;
    struct kndRepo *repo;
    int err;

    if (!self->curr_ctx) {
        knd_log("-- no user selected");
        return make_gsl_err(gsl_FAIL);
    }

    knd_log(".. present state of %p..", self);

    repo = self->curr_ctx->repo;
    err = knd_present_repo_state(repo, self->task->out);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t alloc_class_item(void *obj,
                                  const char *name,
                                  size_t name_size,
                                  size_t count  __attribute__((unused)),
                                  void **item)
{
    struct kndClass *self = obj;

    assert(name == NULL && name_size == 0);

    *item = self;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t append_class_item(void *accu __attribute__((unused)),
                                   void *item  __attribute__((unused)))
{
    //struct kndClass *self = accu;

    knd_log(".. append class item..");
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_item(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndClass *c = obj;
    return knd_class_import(c, rec, total_size);
}

static gsl_err_t parse_class_array(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndUser *self = obj;
    struct kndClass *c = self->repo->root_class;
    struct kndUserContext *ctx = self->curr_ctx;   
    if (ctx)
        c = ctx->repo->root_class;

    self->task->type = KND_UPDATE_STATE;

    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .alloc = alloc_class_item,
        .append = append_class_item,
        .accu = c,
        .parse = parse_class_item
    };

    if (DEBUG_USER_LEVEL_TMP)
        knd_log(".. import class array..");

    return gsl_parse_array(&item_spec, rec, total_size);
}

static gsl_err_t parse_select_user(struct kndUser *self,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndClass *root_class = NULL;
    struct glbOutput *out = self->task->out;
    struct kndRepo *repo;
    gsl_err_t parser_err;
    int err;

    /* reset defaults */
    self->max_depth  = 0;
    self->curr_ctx   = NULL;
    self->task->type = KND_GET_STATE;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_user,
          .obj = self
        },
        { .name = "_depth",
          .name_size = strlen("_depth"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &self->max_depth
        },
        { .type = GSL_SET_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_import,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_array,
          .obj = self
        }/* TODO ,
        { .type = GSL_GET_ARRAY_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_array,
          .obj = self
          }*/,
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_select,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_import,
          .obj = self
        },
        { .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_select,
          .obj = self
        }/*,
        { .type = GSL_SET_STATE,
          .name = "rel",
          .name_size = strlen("rel"),
          .parse = parse_rel_import,
          .obj = self
        },
        { .name = "rel",
          .name_size = strlen("rel"),
          .parse = parse_rel_select,
          .obj = self
        },
        { .name = "_rel",
          .name_size = strlen("_rel"),
          .parse = select_user_rels,
          .obj = self
          }*/,
        { .name = "_sync",
          .name_size = strlen("_sync"),
          .parse = parse_sync_task,
          .obj = self
        },
        { .name = "_state",
          .name_size = strlen("_state"),
          .run = run_present_state,
          .obj = self
        },
        { .is_default = true,
          .run = run_present_user,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- user task parse failure: \"%.*s\" :(",
                self->log->buf_size, self->log->buf);

        if (!self->log->buf_size) {
            err = self->log->write(self->log, "internal server error",
                                   strlen("internal server error"));
            if (err) {
                parser_err = make_gsl_err_external(err);
                goto cleanup;
            }
        }
        goto cleanup;
    }

    assert(self->curr_ctx);
    repo = self->curr_ctx->repo;

    switch (self->task->type) {
    case KND_UPDATE_STATE:
        err = knd_confirm_state(repo);
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

static int kndUser_init(struct kndUser *self)
{
    struct kndRepo *repo = self->repo;
    int err;

    memcpy(self->path, self->shard->path, self->shard->path_size);
    self->path_size = self->shard->path_size;

    char *p = self->path + self->path_size;
    memcpy(p, "/users", strlen("/users"));
    self->path_size += strlen("/users");

    err = knd_mkpath((const char*)self->path, self->path_size, 0755, false);
    if (err) return err;

    repo->task = self->task;
    repo->schema_path = self->schema_path;
    repo->schema_path_size = self->schema_path_size;

    memcpy(repo->name, self->repo_name, self->repo_name_size);
    repo->name_size = self->repo_name_size;

    repo->task = self->task;
    repo->out = self->out;
    repo->log = self->log;

    err = self->repo->init(self->repo);
    if (err) return err;

    return knd_OK;
}

extern int kndUser_new(struct kndUser **user, struct kndMemPool *mempool)
{
    struct kndUser *self;
    struct kndRepo *repo;
    int err = knd_OK;

    self = malloc(sizeof(struct kndUser));                                        ALLOC_ERR(self);
    memset(self, 0, sizeof(struct kndUser));
    memset(self->id, '0', KND_ID_SIZE);
    memset(self->last_uid, '0', KND_ID_SIZE);
    memset(self->db_state, '0', KND_ID_SIZE);

    err = kndRepo_new(&repo, mempool);                                            RET_ERR();
    repo->user = self;
    self->repo = repo;

    err = knd_set_new(mempool, &self->user_idx);                                  RET_ERR();
    self->mempool = mempool;

    self->del = del;
    self->str = str;
    self->init = kndUser_init;
    self->select = parse_select_user;

    *user = self;

    return knd_OK;
}

extern int knd_user_context_new(struct kndMemPool *mempool,
                                struct kndUserContext **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                            sizeof(struct kndUserContext), &page);                RET_ERR();
    *result = page;
    return knd_OK;
}