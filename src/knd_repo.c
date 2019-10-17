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

__attribute__((unused))
static gsl_err_t alloc_class_commit(void *obj,
                                    const char *name __attribute__((unused)),
                                    size_t name_size __attribute__((unused)),
                                    size_t unused_var(count),
                                    void **item)
{
    struct kndCommit    *self = obj;
    struct kndMemPool *mempool = NULL; // = self->repo->mempool;
    struct kndClassCommit *class_commit;
    int err;

    assert(mempool);
    return make_gsl_err_external(knd_FAIL);

    assert(name == NULL && name_size == 0);
    err = knd_class_commit_new(mempool, &class_commit);
    if (err) return make_gsl_err_external(err);
    class_commit->commit = self;
    *item = class_commit;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t get_class_by_id(void *obj, const char *name, size_t name_size)
{
    struct kndClassCommit *self = obj;
    struct kndRepo *repo = self->commit->repo;
    struct kndMemPool *mempool = NULL; // repo->mempool;
    struct kndSet *class_idx = repo->class_idx;
    void *result;
    struct kndClassEntry *entry;
    int err;

    assert(mempool);
    return make_gsl_err_external(knd_FAIL);

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. get class by id:%.*s", name_size, name);

    err = class_idx->get(class_idx, name, name_size, &result);
    if (err) {
        err = knd_class_entry_new(mempool, &entry);
        if (err) return make_gsl_err_external(err);
        memcpy(entry->id, name, name_size);
        entry->id_size = name_size;
        entry->repo = repo;

        if (DEBUG_REPO_LEVEL_2)
            knd_log("!! new entry:%.*s", name_size, name);
        self->entry = entry;

        return make_gsl_err(gsl_OK);
    }

    entry = result;
    self->entry = entry;
    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_class_name(void *obj, const char *name, size_t name_size)
{
    struct kndTask *task = obj;
    struct kndClassCommit *self = obj;
    struct kndClass *c;
    struct kndRepo *repo = self->commit->repo;
    struct kndDict *class_name_idx = task->class_name_idx;
    struct kndMemPool *mempool = task->mempool;
    struct kndClassEntry *entry = self->entry;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);

    if (entry->name_size) {
        if (entry->name_size != name_size) {
            knd_log("-- class name mismatch: %.*s", name_size, name);
            return make_gsl_err(gsl_FAIL);
        }
        if (memcmp(entry->name, name, name_size)) {
            knd_log("-- class name mismatch: %.*s", name_size, name);
            return make_gsl_err(gsl_FAIL);
        }

        if (DEBUG_REPO_LEVEL_2)
            knd_log("++ class already exists: %.*s!", name_size, name);
        self->class = entry->class;
        self->entry = entry;

        return make_gsl_err(gsl_OK);
    }

    entry->name = name;
    entry->name_size = name_size;

    /* get class */
    err = knd_get_class(repo, name, name_size, &c, task);
    if (err) {
        err = knd_class_new(mempool, &c);
        if (err) return make_gsl_err_external(err);

        c->entry = entry;
        entry->class = c;
        c->name = c->entry->name;
        c->name_size = name_size;
    }
    entry->class = c;

    err = knd_dict_set(class_name_idx,
                       entry->name, name_size,
                       (void*)entry);
    if (err) return make_gsl_err_external(err);

    self->class = c;
    self->entry = entry;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_state(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndClassCommit *self = obj;
    struct kndClass *c = self->class;

    return knd_read_class_state(c, self, rec, total_size);
}

static gsl_err_t parse_class_commit(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndClassCommit *class_commit = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = get_class_by_id,
          .obj = class_commit
        },
        { .name = "_n",
          .name_size = strlen("_n"),
          .run = set_class_name,
          .obj = class_commit
        },
        { .name = "_st",
          .name_size = strlen("_st"),
          .parse = parse_class_state,
          .obj = class_commit
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    return make_gsl_err(gsl_OK);
}

__attribute__((unused))
static gsl_err_t alloc_commit(void *obj,
                              const char *name __attribute__((unused)),
                              size_t name_size __attribute__((unused)),
                              size_t unused_var(count),
                              void **item)
{
    struct kndRepo *self = obj;
    struct kndMemPool *mempool = NULL;//self->mempool;
    struct kndCommit *commit;
    int err;

    assert(mempool);
    return make_gsl_err_external(knd_FAIL);

    assert(name == NULL && name_size == 0);

    err = knd_commit_new(mempool, &commit);
    if (err) return make_gsl_err_external(err);

    commit->repo = self;
    *item = commit;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_commit(void *unused_var(obj),
                              const char *rec,
                              size_t *total_size)
{
    //struct kndCommit *commit = obj;

    struct gslTaskSpec class_commit_spec = {
        .is_list_item = true,
        .parse  = parse_class_commit,
    };

    struct gslTaskSpec specs[] = {
        /*{ .is_implied = true,
          .buf = commit->id,
          .buf_size = &commit->id_size,
          .max_buf_size = sizeof commit->id
          },*/
        /*{ .name = "ts",
          .name_size = strlen("ts"),
          .run = get_timestamp,
          .obj = commit
          },*/
        { .type = GSL_SET_ARRAY_STATE,
          .name = "c",
          .name_size = strlen("c"),
          .parse = gsl_parse_array,
          .obj = &class_commit_spec
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t kndRepo_read_commits(void *unused_var(obj),
                                      const char *rec,
                                      size_t *total_size)
{
    //struct kndRepo *repo = obj;
    struct gslTaskSpec commit_spec = {
        .is_list_item = true,
        .parse  = parse_commit,
    };

    struct gslTaskSpec specs[] = {
        { .type = GSL_SET_ARRAY_STATE,
          .name = "commit",
          .name_size = strlen("commit"),
          .parse = gsl_parse_array,
          .obj = &commit_spec
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    return make_gsl_err(gsl_OK);
}

static int restore_state(struct kndRepo *self,
                         const char *filename,
                         struct kndOutput *out)
{
    size_t total_size = 0;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("  .. restoring repo \"%.*s\" in \"%s\"..",
                self->name_size, self->name, filename);

    out->reset(out);
    err = out->write_file_content(out, filename);
    if (err) {
        knd_log("-- failed to open journal: \"%s\"", filename);
        return err;
    }

    /* a closing bracket is needed */
    err = out->writec(out, ']');                                                  RET_ERR();

    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. restoring the journal file: %.*s", out->buf_size, out->buf);

    parser_err = kndRepo_read_commits(self, out->buf, &total_size);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}


static int present_latest_state_JSON(struct kndRepo *self,
                                     struct kndOutput *out)
{
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size;
    size_t latest_commit_id = atomic_load_explicit(&self->num_commits,
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
        err = self->commit_idx->get(self->commit_idx,
                                    idbuf, idbuf_size,
                                    (void**)&commit);                             RET_ERR();
        err = out->write(out, ",\"_time\":", strlen(",\"_time\":"));              RET_ERR();
        err = out->writef(out, "%zu", (size_t)commit->timestamp);                 RET_ERR();
        //err = present_commit_JSON(commit, out);  RET_ERR();
    } else {
        err = out->write(out, ",\"_time\":", strlen(",\"_time\":"));              RET_ERR();
        err = out->writef(out, "%zu", (size_t)self->timestamp);                   RET_ERR();
    }

    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

#if 0
static int present_latest_state_GSL(struct kndRepo *self,
                                    struct kndOutput *out)
{
    size_t latest_commit_id = atomic_load_explicit(&self->num_commits,
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


#if 0
static int select_commit_range(struct kndRepo *self,
                               size_t gt, size_t lt,
                               size_t unused_var(eq),
                               struct kndSet *set)
{
    struct kndCommit *commit;
    struct kndStateRef *ref;
    int err;

    /*    for (commit = self->commits; commit; commit = commit->next) {
        if (commit->numid >= lt) continue;
        if (commit->numid <= gt) continue;

        for (ref = commit->class_state_refs; ref; ref = ref->next) {
            err = knd_retrieve_class_commits(ref, set);                           RET_ERR();
        }
    }
    */
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
    task->state_lt = repo->num_commits + 1;

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

            err = knd_dict_new(&task->class_name_idx, task->mempool, KND_SMALL_DICT_SIZE);
            if (err) return make_gsl_err_external(err);

            err = knd_dict_new(&task->attr_name_idx, task->mempool, KND_SMALL_DICT_SIZE);
            if (err) return make_gsl_err_external(err);

            task->ctx->commit->orig_state_id = atomic_load_explicit(&task->repo->num_commits,
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

            err = knd_dict_new(&task->proc_name_idx, task->mempool, KND_SMALL_DICT_SIZE);
            if (err) return make_gsl_err_external(err);

            err = knd_dict_new(&task->proc_arg_name_idx, task->mempool, KND_SMALL_DICT_SIZE);
            if (err) return make_gsl_err_external(err);

            task->ctx->commit->orig_state_id = atomic_load_explicit(&task->repo->num_commits,
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

    /* global indices */
    prev_arg_ref = knd_shared_dict_get(arg_name_idx,
                                       arg->name, arg->name_size);
    if (prev_arg_ref) {
        arg_ref->next = prev_arg_ref->next;
        prev_arg_ref->next = arg_ref;
    } else {
        err = knd_shared_dict_set(arg_name_idx,
                                  arg->name, arg->name_size,
                                  (void*)arg_ref,
                                  mempool,
                                  task->ctx->commit, &item);                                       RET_ERR();
    }

    err = arg_idx->add(arg_idx,
                        arg->id, arg->id_size,
                        (void*)arg_ref);                                          RET_ERR();

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

    out = task->log;
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

    /* restore the recent state */
    out->reset(out);
    err = out->write(out, self->path, self->path_size);
    if (err) return err;
    err = out->write(out, "/state.db", strlen("/state.db"));
    if (err) return err;

    /* read any existing commits to the frozen DB (failure recovery) */
    if (!stat(out->buf, &st)) {
        err = restore_state(self, out->buf, task->file_out);
        if (err) return err;
    }
    self->timestamp = time(NULL);

    return knd_OK;
}

static int export_commit_GSL(struct kndRepo *self,
                             struct kndCommit *commit,
                             struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndStateRef *ref;
    struct kndClassEntry *entry;
    // struct kndProcEntry *proc_entry;
    int err;

    err = out->write(out, "{task", strlen("{task"));                              RET_ERR();
    err = out->write(out, "{repo ", strlen("{repo "));                            RET_ERR();
    err = out->write(out, self->name, self->name_size);                           RET_ERR();
    err = out->writef(out, "{_commit_from %zu}", commit->orig_state_id);            RET_ERR();

    for (ref = commit->class_state_refs; ref; ref = ref->next) {
        entry = ref->obj;
        if (!entry) continue;

        err = out->writec(out, '{');                                                  RET_ERR();
        if (ref->state->phase == KND_CREATED) {
            err = out->writec(out, '!');                                                  RET_ERR();
        }
        err = out->write(out, "class ", strlen("class "));                            RET_ERR();
        err = out->write(out, entry->name, entry->name_size);                           RET_ERR();
        
        if (ref->state->phase == KND_REMOVED) {
            err = out->write(out, "_rm ", strlen("_rm"));                              RET_ERR();
            err = out->writec(out, '}');                                                  RET_ERR();
            continue;
        }
        err = out->writec(out, '}');                                                  RET_ERR();
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
        case KND_REMOVED:
            entry->phase = KND_REMOVED;
            continue;
        case KND_UPDATED:
            entry->phase = KND_UPDATED;
            continue;
        default:
            break;
        }
        
        err = knd_shared_dict_set(name_idx,
                                  entry->name,  entry->name_size,
                                  (void*)entry,
                                  task->mempool,
                                  commit, &item);
        KND_TASK_ERR("failed to register class %.*s", entry->name_size, entry->name);
        entry->dict_item = item;
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
                                  commit, &item);                                    RET_ERR();
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

        if (DEBUG_REPO_LEVEL_TMP)
            knd_log(".. checking \"%.*s\" class conflicts, state phase: %d",
                    entry->name_size, entry->name, state->phase);

        switch (state->phase) {
        case KND_CREATED:
            /* check class name idx */
            knd_log(".. any new states in class name idx?");

            state = atomic_load_explicit(&entry->dict_item->states,
                                         memory_order_relaxed);

            for (; state; state = state->next) {
                if (state->commit == new_commit) continue;

                confirm = atomic_load_explicit(&state->commit->confirm,
                                               memory_order_relaxed);
                if (confirm == KND_VALID_STATE) {
                    atomic_store_explicit(&new_commit->confirm,
                                          KND_CONFLICT_STATE, memory_order_relaxed);
                    err = knd_FAIL;
                    KND_TASK_ERR("%.*s class already registered",
                                 entry->name_size, entry->name);
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

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log(".. new commit #%zu (%p) to check any commit conflicts since state #%zu..",
                new_commit->numid, new_commit, new_commit->orig_state_id);

    do {
        head_commit = atomic_load_explicit(&self->commits,
                                           memory_order_acquire);
        new_commit->prev = head_commit;
        if (head_commit)
            new_commit->numid = head_commit->numid + 1;

        err = check_class_conflicts(self, new_commit, task);
        KND_TASK_ERR("class level conflicts detected");

    } while (!atomic_compare_exchange_weak(&self->commits, &head_commit, new_commit));

    atomic_store_explicit(&new_commit->confirm, KND_VALID_STATE, memory_order_relaxed);
    atomic_fetch_add_explicit(&self->num_commits, 1, memory_order_relaxed);

    // knd_uid_create(new_commit->numid, new_commit->id, &new_commit->id_size);
    // err = self->commit_idx->add(self->commit_idx,
    //                            commit->id, commit->id_size,
    //                            (void*)commit);
    //if (err) return err;

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("++ no conflicts found, commit #%zu confirmed!",
                new_commit->numid);

    return knd_OK;
}

int knd_confirm_commit(struct kndRepo *self, struct kndTask *task)
{
    struct kndTaskContext *ctx = task->ctx;
    struct kndCommit *commit = ctx->commit;
    struct kndStateRef *ref, *child_ref;
    struct kndState *state;
    struct kndClassEntry *entry;
    struct kndProcEntry *proc_entry;
    int err;

    assert(commit != NULL);

    if (DEBUG_REPO_LEVEL_TMP) {
        knd_log(".. \"%.*s\" repo to apply commit #%zu..",
                self->name_size, self->name, commit->numid);
    }
    commit->repo = self;

    for (ref = commit->class_state_refs; ref; ref = ref->next) {
        if (ref->state->phase == KND_REMOVED) {
            continue;
        }
        entry = ref->obj;
        if (entry) {
            knd_log(".. repo %.*s to resolve commits in \"%.*s\"..",
                    self->name_size, self->name,
                    entry->name_size, entry->name);
        }

        if (!entry->class->is_resolved) {
            err = knd_class_resolve(entry->class, task);                              RET_ERR();
        }
        state = ref->state;
        state->commit = commit;
        if (!state->children) continue;

        for (child_ref = state->children; child_ref; child_ref = child_ref->next) {
            entry = child_ref->obj;
            /*if (entry) {
                knd_log("  == class:%.*s", entry->name_size, entry->name);
                }*/
            state = child_ref->state;
            state->commit = commit;
        }
    }

    /* PROCS */
    for (ref = commit->proc_state_refs; ref; ref = ref->next) {
        if (ref->state->phase == KND_REMOVED) {
            knd_log(".. proc to be removed");
            continue;
        }
        proc_entry = ref->obj;
        if (proc_entry) {
            knd_log(".. confirming proc commits in \"%.*s\"..",
                    self->name_size, self->name,
                    proc_entry->name_size, proc_entry->name);
        }

        /* proc resolving */
        if (!proc_entry->proc->is_resolved) {
            err = knd_proc_resolve(proc_entry->proc, task);                       RET_ERR();
        }
    }

    switch (task->role) {
    case KND_WRITER:

        err = update_indices(self, commit, task);
        KND_TASK_ERR("index update failed");

        knd_log(".. Writer #%d to confirm commit #%zu..", task->id, commit->numid);

        err = check_commit_conflicts(self, commit, task);
        KND_TASK_ERR("commit conflicts detected, please get the latest repo updates");

        task->ctx->repo = self;
        err = knd_save_commit_WAL(task, commit);
        KND_TASK_ERR("WAL saving failed");

        // err = build_reply(task, commit);
        // KND_TASK_ERR("index update failed");
        task->out->write(task->out, "OK", 2);
        
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
    err = knd_set_new(mempool, &self->commit_idx);
    if (err) goto error;

    self->max_journal_size = KND_FILE_BUF_SIZE;
    *repo = self;

    return knd_OK;
 error:
    // TODO: release resources
    return err;
}
