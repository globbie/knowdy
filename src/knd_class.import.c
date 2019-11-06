#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

/* numeric conversion by strtol */
#include <errno.h>
#include <limits.h>

#include "knd_config.h"
#include "knd_mempool.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_user.h"
#include "knd_dict.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>

#define DEBUG_CLASS_IMPORT_LEVEL_1 0
#define DEBUG_CLASS_IMPORT_LEVEL_2 0
#define DEBUG_CLASS_IMPORT_LEVEL_3 0
#define DEBUG_CLASS_IMPORT_LEVEL_4 0
#define DEBUG_CLASS_IMPORT_LEVEL_5 0
#define DEBUG_CLASS_IMPORT_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
    struct kndClass *class;
    struct kndClassVar *class_var;
};


static gsl_err_t set_class_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndClass *self = ctx->class;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = ctx->repo;
    struct kndClassEntry *entry;
    struct kndClass *c;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2) {
        knd_log(".. set class name: \"%.*s\"..",
                name_size, name);
    }

    /* initial bulk load in progress */
    if (task->type == KND_LOAD_STATE) {
        entry = knd_shared_dict_get(repo->class_name_idx, name, name_size);
        if (!entry) {
            entry = self->entry;
            entry->name = name;
            entry->name_size = name_size;
            self->name = name;
            self->name_size = name_size;

            /* register globally */
            err = knd_shared_dict_set(repo->class_name_idx,
                                      name, name_size,
                                      (void*)entry,
                                      task->mempool,
                                      NULL, NULL, false);
            if (err) return make_gsl_err_external(err);
            return make_gsl_err(gsl_OK);
        }
        /* class entry has no class body */
        if (!entry->class) {
            entry->class =    self;
            self->entry =     entry;
            self->name =      name;
            self->name_size = name_size;
            // TODO release curr entry ?
            return make_gsl_err(gsl_OK);
        }
        KND_TASK_LOG("\"%.*s\" class name already exists", name_size, name);
        task->ctx->http_code = HTTP_CONFLICT;
        task->ctx->error = KND_CONFLICT;
        return make_gsl_err(gsl_FAIL);
    }

    /* commit in progress */
    err = knd_get_class(repo, name, name_size, &c, task);
    if (!err) {
        KND_TASK_LOG("\"%.*s\" class already exists in repo %.*s", name_size, name);
        task->ctx->http_code = HTTP_CONFLICT;
        task->ctx->error = KND_CONFLICT;
        return make_gsl_err(gsl_FAIL);
    }

    /* check user shared repo */
    if (task->user_ctx) {
        err = knd_get_class(task->user_ctx->base_repo, name, name_size, &c, task);
        if (!err) {
            KND_TASK_LOG("\"%.*s\" class already exists in a base repo: %.*s",
                         name_size, name,
                         task->user_ctx->base_repo->name_size,
                         task->user_ctx->base_repo->name);
            task->ctx->http_code = HTTP_CONFLICT;
            task->ctx->error = KND_CONFLICT;
            return make_gsl_err(gsl_FAIL);
        }
    }

    /* update local task idx */
    entry = knd_dict_get(task->class_name_idx, name, name_size);
    if (!entry) {
        entry = self->entry;
        entry->name = name;
        entry->name_size = name_size;
        self->name = name;
        self->name_size = name_size;
        err = knd_dict_set(task->class_name_idx,
                           name, name_size,
                           (void*)entry);
        if (err) return make_gsl_err_external(err);
        return make_gsl_err(gsl_OK);
    }

    KND_TASK_LOG("current commit already has a doublet of \"%.*s\" class", name_size, name);
    task->ctx->http_code = HTTP_CONFLICT;
    task->ctx->error = KND_CONFLICT;
    return make_gsl_err(gsl_FAIL);
}

static gsl_err_t set_class_var(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx      = obj;
    struct kndTask *task          = ctx->task;
    struct kndMemPool *mempool    = task->mempool;
    struct kndClassVar *self      = ctx->class_var;
    struct kndRepo *repo          = task->repo;
    struct kndDict *class_name_idx = task->class_name_idx;
    struct kndClassEntry *entry;
    void *result;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. repo \"%.*s\" to check class var name: %.*s [task id:%zu]",
                repo->name_size,
                repo->name,
                name_size, name, task->id);

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    result = knd_dict_get(class_name_idx, name, name_size);
    if (result) {
        self->entry = result;
        return make_gsl_err(gsl_OK);
    }

    err = knd_class_entry_new(mempool, &entry);
    if (err) return make_gsl_err_external(err);

    entry->name = name;
    entry->name_size = name_size;

    err = knd_dict_set(class_name_idx,
                              entry->name, name_size,
                              (void*)entry);
    if (err) return make_gsl_err_external(err);

    entry->repo = repo;
    self->entry = entry;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_state_top_option(void *obj,
                                      const char *unused_var(name),
                                      size_t unused_var(name_size) )
{
    struct kndClass *self = obj;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log("NB: set class state top option!");

    self->state_top = true;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_attr(void *obj,
                            const char *name, size_t name_size,
                            const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndClass *self = ctx->class;
    struct kndTask *task = ctx->task;
    struct kndAttr *attr;
    struct kndMemPool *mempool = task->mempool;
    const char *c;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. parsing attr: \"%.*s\" rec:\"%.*s\"",
                name_size, name, 32, rec);

    err = knd_attr_new(mempool, &attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;

    for (size_t i = 0; i < sizeof(knd_attr_names) / sizeof(knd_attr_names[0]); i++) {
        c = knd_attr_names[i];
        if (!memcmp(c, name, name_size)) 
            attr->type = (knd_attr_type)i;
    }

    if (attr->type == KND_ATTR_NONE) {
        knd_log("-- \"%.*s\" attr is not supported (imported class:%.*s)",
                name_size, name, self->name_size, self->name);
        //return *total_size = 0, make_gsl_err_external(err);
    }

    parser_err = knd_import_attr(attr, task, rec, total_size);
    if (parser_err.code) {
        if (DEBUG_CLASS_IMPORT_LEVEL_TMP)
            knd_log("-- failed to parse the attr field: %d", parser_err.code);
        return parser_err;
    }

    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    } else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }
    self->num_attrs++;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        attr->str(attr, 1);

    if (attr->is_implied)
        self->implied_attr = attr;
   
    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_attr_var(void *obj,
                                 const char *name, size_t name_size,
                                 const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    int err;

    err = knd_import_attr_var(ctx->class_var, name, name_size,
                              rec, total_size, ctx->task);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_attr_var_list(void *obj,
                                      const char *name, size_t name_size,
                                      const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    int err;

    err = knd_import_attr_var_list(ctx->class_var, name, name_size,
                                   rec, total_size, ctx->task);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_var(const char *rec,
                                 size_t *total_size,
                                 struct LocalContext *ctx)
{
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_var,
          .obj = ctx
        },
        { .type = GSL_SET_STATE,
          .validate = import_attr_var,
          .obj = ctx
        },
        { .validate = import_attr_var,
          .obj = ctx
        },
        { .type = GSL_SET_ARRAY_STATE,
          .validate = import_attr_var_list,
          .obj = ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = import_attr_var_list,
          .obj = ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_import_class_var(struct kndClassVar *self,
                               const char *rec,
                               size_t *total_size,
                               struct kndTask *task)
{
    gsl_err_t parser_err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. import class var: %.*s", 32, rec);

    struct LocalContext ctx = {
        .task = task,
        .class_var = self
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_var,
          .obj = &ctx
        },
        { .type = GSL_SET_STATE,
          .validate = import_attr_var,
          .obj = &ctx
        },
        { .validate = import_attr_var,
          .obj = &ctx
        },
        { .type = GSL_SET_ARRAY_STATE,
          .validate = import_attr_var_list,
          .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_baseclass(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClass *self = ctx->class;
    struct kndClassVar *class_var;
    struct kndMemPool *mempool = task->mempool;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log(".. parsing the base class: \"%.*s\"", 32, rec);

    err = knd_class_var_new(mempool, &class_var);
    if (err) {
        KND_TASK_LOG("failed to alloc a class var");
        return *total_size = 0, make_gsl_err_external(err);
    }
    class_var->parent = self;

    ctx->class_var = class_var;
    parser_err = parse_class_var(rec, total_size, ctx);
    if (parser_err.code) return parser_err;

    class_var->next = self->baseclass_vars;

    // TODO: atomic
    self->baseclass_vars = class_var;
    self->num_baseclass_vars++;

    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_class_import(struct kndRepo *repo,
                           const char *rec,
                           size_t *total_size,
                           struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndClass *c;
    struct kndClassEntry *entry;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CLASS_IMPORT_LEVEL_2)
        knd_log("..worker \"%zu\" to import class: \"%.*s\"",
                task->id, 128, rec);

    err = knd_class_new(mempool, &c);
    if (err) {
        KND_TASK_LOG("mempool failed to alloc kndClass");
        return make_gsl_err_external(err);
    }
    err = knd_class_entry_new(mempool, &entry);
    if (err) {
        KND_TASK_LOG("mempool failed to alloc kndClassEntry");
        return make_gsl_err_external(err);
    }
    entry->repo = repo;
    entry->class = c;
    c->entry = entry;

    struct LocalContext ctx = {
        .task = task,
        .repo = repo,
        .class = c
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_name,
          .obj = &ctx
        },
        { .name = "is",
          .name_size = strlen("is"),
          .parse = parse_baseclass,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = knd_parse_gloss_array,
          .obj = task
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "_summary",
          .name_size = strlen("_summary"),
          .parse = knd_parse_summary_array,
          .obj = task
        },
        { .name = "_state_top",
          .name_size = strlen("_state_top"),
          .run = set_state_top_option,
          .obj = c
        },
        { .validate = parse_attr,
          .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        KND_TASK_LOG("\"%.*s\" class parsing error",
                     c->name_size, c->name);
        goto final;
    }

    if (!c->name_size) {
        KND_TASK_LOG("no class name specified");
        task->http_code = HTTP_BAD_REQUEST;
        parser_err = make_gsl_err(gsl_FAIL);
        goto final;
    }

    /* reassign glosses */
    if (task->ctx->tr) {
        c->tr = task->ctx->tr;
        task->ctx->tr = NULL;
    }

    if (DEBUG_CLASS_IMPORT_LEVEL_2) {
        knd_log("++  \"%.*s\" class import completed!",
                c->name_size, c->name);
        c->str(c, 1);
    }

    switch (task->type) {
    case KND_RESTORE_STATE:
        // fall through
    case KND_COMMIT_STATE:
        err = knd_class_commit_state(c, KND_CREATED, task);
        if (err) {
            return make_gsl_err_external(err);
        }
        break;
    default:
        break;
    }

    return make_gsl_err(gsl_OK);

 final:

    // TODO free resources
    
    return parser_err;
}
