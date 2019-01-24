#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <stdatomic.h>

#include "knd_repo.h"
#include "knd_shard.h"
#include "knd_attr.h"
#include "knd_set.h"
#include "knd_user.h"
#include "knd_query.h"
#include "knd_task.h"
#include "knd_dict.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_proc.h"
#include "knd_mempool.h"
#include "knd_state.h"

#include <gsl-parser.h>
#include <glb-lib/output.h>

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

    self->class_name_idx->del(self->class_name_idx);
    self->class_inst_name_idx->del(self->class_inst_name_idx);
    self->attr_name_idx->del(self->attr_name_idx);
    self->proc_name_idx->del(self->proc_name_idx);
    self->proc_arg_name_idx->del(self->proc_arg_name_idx);

    free(self);
}

__attribute__((unused))
static gsl_err_t alloc_class_update(void *obj,
                                    const char *name __attribute__((unused)),
                                    size_t name_size __attribute__((unused)),
                                    size_t unused_var(count),
                                    void **item)
{
    struct kndUpdate    *self = obj;
    struct kndMemPool *mempool = NULL; // = self->repo->mempool;
    struct kndClassUpdate *class_update;
    int err;

    assert(mempool);
    return make_gsl_err_external(knd_FAIL);

    assert(name == NULL && name_size == 0);
    err = knd_class_update_new(mempool, &class_update);
    if (err) return make_gsl_err_external(err);
    class_update->update = self;
    *item = class_update;

    return make_gsl_err(gsl_OK);
}

/*static gsl_err_t append_class_update(void *accu,
                                     void *item)
{
    struct kndUpdate *self = accu;
    struct kndClassUpdate *class_update = item;
    // TODO
    return make_gsl_err(gsl_OK);
}
*/

static gsl_err_t get_class_by_id(void *obj, const char *name, size_t name_size)
{
    struct kndClassUpdate *self = obj;
    struct kndRepo *repo = self->update->repo;
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
        //err = mempool->new_class_entry(mempool, &entry);
        //if (err) return make_gsl_err_external(err);
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
    struct kndClassUpdate *self = obj;
    struct kndClass *c;
    struct kndRepo *repo = self->update->repo;
    struct ooDict *class_name_idx = repo->class_name_idx;
    struct kndSet *class_idx = repo->class_idx;
    struct kndMemPool *mempool = task->mempool;
    struct kndClassEntry *entry = self->entry;
    int err;

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log(".. check or set class name: %.*s", name_size, name);
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

        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("++ class already exists: %.*s!", name_size, name);
        self->class = entry->class;
        self->entry = entry;

        return make_gsl_err(gsl_OK);
    }

    if (DEBUG_REPO_LEVEL_TMP) {
        knd_log("\n\n== batch mode: %d", task->batch_mode);
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

    /* register class entry */
    err = class_idx->add(class_idx,
                         entry->id, entry->id_size, (void*)entry);
    if (err) return make_gsl_err_external(err);

    err = class_name_idx->set(class_name_idx,
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
    struct kndClassUpdate *self = obj;
    struct kndClass *c = self->class;

    return knd_read_class_state(c, self, rec, total_size);
}

static gsl_err_t parse_class_update(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndClassUpdate *class_update = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = get_class_by_id,
          .obj = class_update
        },
        { .name = "_n",
          .name_size = strlen("_n"),
          .run = set_class_name,
          .obj = class_update
        },
        { .name = "_st",
          .name_size = strlen("_st"),
          .parse = parse_class_state,
          .obj = class_update
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    return make_gsl_err(gsl_OK);
}

__attribute__((unused))
static gsl_err_t alloc_update(void *obj,
                              const char *name __attribute__((unused)),
                              size_t name_size __attribute__((unused)),
                              size_t unused_var(count),
                              void **item)
{
    struct kndRepo *self = obj;
    struct kndMemPool *mempool = NULL;//self->mempool;
    struct kndUpdate *update;
    int err;

    assert(mempool);
    return make_gsl_err_external(knd_FAIL);

    assert(name == NULL && name_size == 0);

    err = knd_update_new(mempool, &update);
    if (err) return make_gsl_err_external(err);

    update->repo = self;
    *item = update;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_update(void *unused_var(obj),
                              const char *rec,
                              size_t *total_size)
{
    //struct kndUpdate *update = obj;

    struct gslTaskSpec class_update_spec = {
        .is_list_item = true,
        //.alloc  = alloc_class_update,
        //.append = append_class_update,
        .parse  = parse_class_update,
        //.accu = update
    };

    struct gslTaskSpec specs[] = {
        /*{ .is_implied = true,
          .buf = update->id,
          .buf_size = &update->id_size,
          .max_buf_size = sizeof update->id
          },*/
        /*{ .name = "ts",
          .name_size = strlen("ts"),
          .run = get_timestamp,
          .obj = update
          },*/
        { .type = GSL_SET_ARRAY_STATE,
          .name = "c",
          .name_size = strlen("c"),
          .parse = gsl_parse_array,
          .obj = &class_update_spec
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t kndRepo_read_updates(void *unused_var(obj),
                                      const char *rec,
                                      size_t *total_size)
{
    //struct kndRepo *repo = obj;
    struct gslTaskSpec update_spec = {
        .is_list_item = true,
        //.alloc  = alloc_update,
        //.append = append_update,
        .parse  = parse_update,
        //.accu = repo
    };

    struct gslTaskSpec specs[] = {
        { .type = GSL_SET_ARRAY_STATE,
          .name = "update",
          .name_size = strlen("update"),
          .parse = gsl_parse_array,
          .obj = &update_spec
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    return make_gsl_err(gsl_OK);
}

static int kndRepo_restore(struct kndRepo *self,
                           const char *filename,
                           struct glbOutput *out)
{
    size_t total_size = 0;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("  .. restoring repo \"%.*s\" in \"%s\" repo:%p",
                self->name_size, self->name, filename, self);

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

    parser_err = kndRepo_read_updates(self, out->buf, &total_size);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}


static int present_latest_state_JSON(struct kndRepo *self,
                                     struct glbOutput *out)
{
    size_t latest_update_id = atomic_load_explicit(&self->num_updates,
                                                   memory_order_relaxed);
    //struct kndUpdate *update;
    int err;

    out->reset(out);
    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->write(out, "\"repo\":", strlen("\"repo\":"));                      RET_ERR();
    err = out->writec(out, '"');                                                  RET_ERR();
    err = out->write(out,  self->name, self->name_size);                          RET_ERR();
    err = out->writec(out, '"');                                                  RET_ERR();

    err = out->write(out, ",\"_state\":", strlen(",\"_state\":"));                RET_ERR();
    err = out->writef(out, "%zu", latest_update_id);                              RET_ERR();

    /*    if (self->updates) {
        update = self->updates;
        err = out->write(out, ",\"_modif\":", strlen(",\"_modif\":"));            RET_ERR();
        err = out->writef(out, "%zu", (size_t)update->timestamp);                 RET_ERR();

        err = present_update_JSON(update, out);  RET_ERR();
    } else {
        err = out->write(out, ",\"_modif\":", strlen(",\"_modif\":"));            RET_ERR();
        err = out->writef(out, "%zu", (size_t)self->timestamp);                   RET_ERR();
        }*/

    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

static int present_latest_state_GSL(struct kndRepo *self,
                                    struct glbOutput *out)
{
    size_t latest_update_id = atomic_load_explicit(&self->num_updates,
                                                   memory_order_relaxed);
    //struct kndUpdate *update;
    int err;

    out->reset(out);
    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->write(out, "repo ", strlen("repo "));                              RET_ERR();
    err = out->write(out,  self->name, self->name_size);                          RET_ERR();

    err = out->write(out, "{_state ", strlen("{_state "));                        RET_ERR();
    err = out->writef(out, "%zu", latest_update_id);                              RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    /*    if (self->updates) {
        update = self->updates;
        err = out->write(out, "{modif ", strlen("{modif "));                      RET_ERR();
        err = out->writef(out, "%zu", (size_t)update->timestamp);                 RET_ERR();
        err = out->writec(out, '}');                                              RET_ERR();
        }*/

    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

#if 0
static int select_update_range(struct kndRepo *self,
                               size_t gt, size_t lt,
                               size_t unused_var(eq),
                               struct kndSet *set)
{
    struct kndUpdate *update;
    struct kndStateRef *ref;
    int err;

    /*    for (update = self->updates; update; update = update->next) {
        if (update->numid >= lt) continue;
        if (update->numid <= gt) continue;

        for (ref = update->class_state_refs; ref; ref = ref->next) {
            err = knd_retrieve_class_updates(ref, set);                           RET_ERR();
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
    struct glbOutput *out = task->out;
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

    /* restore:    if (!repo->updates) goto show_curr_state;
    update = repo->updates;
    if (task->state_gt >= update->numid) goto show_curr_state;
    */

    // TODO: handle lt and eq cases
    //if (task->state_lt && task->state_lt < task->state_gt) goto show_curr_state;
    task->state_lt = repo->num_updates + 1;

    if (DEBUG_REPO_LEVEL_TMP) {
        knd_log(".. select repo delta:  gt %zu  lt %zu  eq:%zu..",
                task->state_gt, task->state_lt, task->state_eq);
    }

    err = knd_set_new(mempool, &set);
    if (err) return make_gsl_err_external(err);
    set->mempool = mempool;

    /*err = select_update_range(repo,
                              task->state_gt, task->state_lt,
                              task->state_eq, set);
    if (err) return make_gsl_err_external(err);
    */

    // export
    task->show_removed_objs = true;

    if (task->curr_locale_size) {
        task->locale = task->curr_locale;
        task->locale_size = task->curr_locale_size;
    }

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
        task->repo = task->shard->user->repo;
        return make_gsl_err(gsl_OK);
    }

    if (name_size == 1) {
        switch (*name) {
        case '/':
            knd_log("== system repo selected!");
            task->repo = task->shard->repo;
            return make_gsl_err(gsl_OK);
        default:
            break;
        }
    }

    // TODO: name match
    knd_log("== shared repo selected!");
    task->repo = task->shard->user->repo;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_select(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndUserContext *ctx = task->user_ctx;
    struct kndRepo *repo;

    if (!ctx) {
        struct glbOutput *log = task->log;
        knd_log("-- no user selected");
        log->writef(log, "no user selected");
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

static gsl_err_t parse_class_import(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndUserContext *ctx = task->user_ctx;
    struct kndRepo *repo = task->repo;
    struct kndClass *c;

    /*if (!ctx) {
        struct glbOutput *log = task->log;
        knd_log("-- no user selected");
        log->writef(log, "no user selected");
        task->http_code = HTTP_BAD_REQUEST;
        return make_gsl_err(gsl_FAIL);
        } */

    if (ctx) {
        repo = ctx->repo;
        task->repo = repo;
    }

    assert(repo != NULL);

    c = repo->root_class;
    task->class = c;
    task->type = KND_UPDATE_STATE;

    return knd_class_import(repo, rec, total_size, task);
}

extern gsl_err_t knd_parse_repo(void *obj, const char *rec, size_t *total_size)
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

/*static gsl_err_t parse_class_import(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndTask *task = obj;

    task->type = KND_UPDATE_STATE;

    return knd_class_import(task->repo, rec, total_size, task);
}
*/

static gsl_err_t parse_proc_import(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndTask *task = obj;
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

static int write_filepath(struct glbOutput *out,
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
    struct glbOutput *out = task->out;
    struct glbOutput *file_out = task->file_out;
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
    struct kndMemPool *mempool = task->mempool;
    struct kndSet *arg_idx = repo->proc_arg_idx;
    struct ooDict *arg_name_idx = repo->proc_arg_name_idx;
    struct kndProcArgRef *arg_ref, *prev_arg_ref;
    int err;

    repo->num_proc_args++;
    arg->numid = repo->num_proc_args;
    knd_uid_create(arg->numid, arg->id, &arg->id_size);

    err = knd_proc_arg_ref_new(mempool, &arg_ref);
    if (err) {
        return err;
    }
    arg_ref->arg = arg;
    arg_ref->proc = proc;

    /* global indices */
    prev_arg_ref = arg_name_idx->get(arg_name_idx,
                                     arg->name, arg->name_size);
    arg_ref->next = prev_arg_ref;

    if (prev_arg_ref) {
        //knd_log("-- dict remove");
        err = arg_name_idx->remove(arg_name_idx,
                                   arg->name, arg->name_size);           RET_ERR();
    }

    err = arg_name_idx->set(arg_name_idx,
                             arg->name, arg->name_size,
                             (void*)arg_ref);                              RET_ERR();

    err = arg_idx->add(arg_idx,
                        arg->id, arg->id_size,
                        (void*)arg_ref);                                   RET_ERR();

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("++ new primary arg: \"%.*s\" (id:%.*s)",
                arg->name_size, arg->name, arg->id_size, arg->id);

    return knd_OK;
}

static int resolve_procs(struct kndRepo *self,
                         struct kndTask *task)
{
    struct kndProcEntry *entry;
    const char *key;
    void *val;
    int err;

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log(".. resolving procs of repo \"%.*s\"..",
                self->name_size, self->name);

    key = NULL;
    self->proc_name_idx->rewind(self->proc_name_idx);
    do {
        self->proc_name_idx->next_item(self->proc_name_idx, &key, &val);
        if (!key) break;

        entry = (struct kndProcEntry*)val;
        if (entry->proc->is_resolved) {
            continue;
        }
        err = knd_proc_resolve(entry->proc, task);
        if (err) {
            knd_log("-- couldn't resolve the \"%.*s\" proc",
                    entry->proc->name_size, entry->proc->name);
            return err;
        }
    } while (key);

    return knd_OK;
}

int knd_repo_open(struct kndRepo *self, struct kndTask *task)
{
    struct glbOutput *out;
    struct kndClass *c;
    struct kndClassInst *inst;
    struct stat st;
    int err;

    out = task->out;
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
    out->buf[out->buf_size] = '\0';


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
            knd_log("-- no existing frozen DB was found, "
                    " reading the original schema..");

            task->batch_mode = true;
            err = read_GSL_file(self, NULL, "index", strlen("index"), task);
            if (err) {
                knd_log("-- couldn't read any schemas");
                return err;
            }

            c = self->root_class;
            err = knd_class_coordinate(c, task);
            if (err) {
                knd_log("-- class coordination failed");
                return err;
            }

            err = resolve_procs(self, task);
            if (err) {
                knd_log("-- resolving of procs failed");
                return err;
            }
            task->batch_mode = false;
        }
    }

    /* restore the journal? */
    out->reset(out);
    err = out->write(out, self->path, self->path_size);
    if (err) return err;
    err = out->write(out, "/journal.log", strlen("/journal.log"));
    if (err) return err;
     out->buf[out->buf_size] = '\0';

    /* read any existing updates to the frozen DB (failure recovery) */
    if (!stat(out->buf, &st)) {
        err = kndRepo_restore(self, out->buf, task->file_out);
        if (err) return err;
    }

    self->timestamp = time(NULL);

    return knd_OK;
}

#if 0
static int check_conflicts(struct kndRepo *self,
                           struct kndUpdate *update)
{

    // make sure there are no conflicts
    // from origin_id to latest_id

    // try to push yourself
    // to a queue of latests updates    
    
    return knd_OK;
}
#endif

int knd_confirm_updates(struct kndRepo *self, struct kndTask *task)
{
    struct kndUpdate *update = task->update;
    struct glbOutput *out = task->out;
    struct kndStateRef *ref, *child_ref;
    struct kndState *state;
    struct kndClassEntry *entry;
    struct kndProcEntry *proc_entry;
    int err;

    if (DEBUG_REPO_LEVEL_TMP) {
        knd_log("\n.. \"%.*s\" repo to confirm updates..",
                self->name_size, self->name);
    }

    update->repo = self;

    for (ref = task->class_state_refs; ref; ref = ref->next) {
        entry = ref->obj;
        if (entry) {
            knd_log(".. repo %.*s to confirm updates in \"%.*s\"..",
                    self->name_size, self->name,
                    entry->name_size, entry->name);
        }

        err = knd_class_resolve(entry->class, task);  RET_ERR();
        
        state = ref->state;
        state->update = update;
        if (!state->children) continue;

        for (child_ref = state->children; child_ref; child_ref = child_ref->next) {
            entry = child_ref->obj;
            /*if (entry) {
                knd_log("  == class:%.*s", entry->name_size, entry->name);
                }*/
            state = child_ref->state;
            state->update = update;
        }
    }

    update->class_state_refs = task->class_state_refs;
    task->class_state_refs = NULL;

    /* PROCS */
    for (ref = task->proc_state_refs; ref; ref = ref->next) {
        proc_entry = ref->obj;
        if (proc_entry) {
            knd_log(".. confirming updates in \"%.*s\"..",
                    self->name_size, self->name,
                    proc_entry->name_size, proc_entry->name);
        }
        /* proc resolving */
        err = knd_proc_resolve(proc_entry->proc, task);                     RET_ERR();
    }


    update->numid = atomic_fetch_add_explicit(&self->update_id_count, 1,
                                              memory_order_relaxed);

    // TODO: build update GSP

    // TODO: any conflicts?
    //err = check_conflicts(self, update);
    //if (err) {
        // release resources
        // build err msg
    //  return err;
    //}

    update->timestamp = time(NULL);

    /* build a reply */
    switch (task->format) {
    case KND_FORMAT_JSON:
        err = present_latest_state_JSON(self, out);   RET_ERR();
        break;
    default:
        err = present_latest_state_GSL(self, out);    RET_ERR();
        break;
    }
    return knd_OK;
}

int knd_present_repo_state(struct kndRepo *self,
                           struct kndTask *task)
{
    int err;

    // TODO: choose format
    err = present_latest_state_JSON(self,
                                    task->out);     RET_ERR();

    return knd_OK;
}

int knd_conc_folder_new(struct kndMemPool *mempool,
                               struct kndConcFolder **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                            sizeof(struct kndConcFolder), &page);  RET_ERR();
    *result = page;
    return knd_OK;
}

int kndRepo_new(struct kndRepo **repo,
                struct kndMemPool *mempool)
{
    struct kndRepo *self;
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndClassInst *inst;
    struct kndProc *proc;
    struct kndProcEntry *proc_entry;
    int err;

    self = malloc(sizeof(struct kndRepo));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndRepo));

    err = knd_class_entry_new(mempool, &entry);  RET_ERR();
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

    /* root class instance */
    err = knd_class_inst_new(mempool, &inst);
    if (err) goto error;
    inst->base = c;
    self->root_inst = inst;

    /* global name indices */
    err = knd_set_new(mempool, &self->class_idx);
    if (err) goto error;
    err = ooDict_new(&self->class_name_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    /* attrs */
    err = knd_set_new(mempool, &self->attr_idx);
    if (err) goto error;
    err = ooDict_new(&self->attr_name_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    /* class insts */
    err = ooDict_new(&self->class_inst_name_idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    /*** PROC ***/
    err = knd_proc_entry_new(mempool, &proc_entry);  RET_ERR();
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

    err = ooDict_new(&self->proc_name_idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    /* proc args */
    err = knd_set_new(mempool, &self->proc_arg_idx);
    if (err) goto error;
    err = ooDict_new(&self->proc_arg_name_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    /* proc insts */
    err = ooDict_new(&self->proc_inst_name_idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    self->max_journal_size = KND_FILE_BUF_SIZE;
    *repo = self;

    return knd_OK;
 error:
    // TODO: release resources
    return err;
}
