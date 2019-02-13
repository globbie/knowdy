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

    /* self->class_name_idx->del(self->class_name_idx);
    self->class_inst_name_idx->del(self->class_inst_name_idx);
    self->attr_name_idx->del(self->attr_name_idx);
    self->proc_name_idx->del(self->proc_name_idx);
    self->proc_arg_name_idx->del(self->proc_arg_name_idx);
    */
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
    struct kndDict *class_name_idx = repo->class_name_idx;
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
                           struct kndOutput *out)
{
    size_t total_size = 0;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("  .. restoring repo \"%.*s\" in \"%s\"",
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

    parser_err = kndRepo_read_updates(self, out->buf, &total_size);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}


static int present_latest_state_JSON(struct kndRepo *self,
                                     struct kndOutput *out)
{
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size;
    size_t latest_update_id = atomic_load_explicit(&self->num_updates,
                                                   memory_order_relaxed);
    struct kndUpdate *update;
    int err;

    out->reset(out);
    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->write(out, "\"repo\":", strlen("\"repo\":"));                      RET_ERR();
    err = out->writec(out, '"');                                                  RET_ERR();
    err = out->write(out,  self->name, self->name_size);                          RET_ERR();
    err = out->writec(out, '"');                                                  RET_ERR();

    err = out->write(out, ",\"_state\":", strlen(",\"_state\":"));                RET_ERR();
    err = out->writef(out, "%zu", latest_update_id);                              RET_ERR();


    if (latest_update_id) {
        knd_uid_create(latest_update_id, idbuf, &idbuf_size);
        err = self->update_idx->get(self->update_idx,
                                    idbuf, idbuf_size,
                                    (void**)&update);                             RET_ERR();
        err = out->write(out, ",\"_time\":", strlen(",\"_time\":"));              RET_ERR();
        err = out->writef(out, "%zu", (size_t)update->timestamp);                 RET_ERR();
        //err = present_update_JSON(update, out);  RET_ERR();
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
#endif


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
        task->repo = task->user_ctx->repo;
        return make_gsl_err(gsl_OK);
    }

    if (name_size == 1) {
        switch (*name) {
        case '/':
            knd_log("== system repo selected!");
            //task->repo = task->shard->repo;
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
        task->type = KND_UPDATE_STATE;

        if (!task->ctx->update) {
            err = knd_update_new(task->mempool, &task->ctx->update);
            if (err) return make_gsl_err_external(err);

            err = knd_dict_new(&task->ctx->class_name_idx, KND_SMALL_DICT_SIZE);
            if (err) return make_gsl_err_external(err);

            task->ctx->update->orig_state_id = atomic_load_explicit(&task->repo->num_updates,
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
    struct kndOutput *out = task->log;
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
    struct kndDict *arg_name_idx = task->ctx->proc_arg_name_idx;
    struct kndProcArgRef *arg_ref, *prev_arg_ref;
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
    prev_arg_ref = knd_dict_get(arg_name_idx,
                                arg->name, arg->name_size);
    arg_ref->next = prev_arg_ref;

    if (prev_arg_ref) {
        arg_ref->next = prev_arg_ref;
        prev_arg_ref->next = arg_ref;
    } else {
        err = knd_dict_set(arg_name_idx,
                           arg->name, arg->name_size,
                           (void*)arg_ref);                                       RET_ERR();
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
    struct kndDictItem *item;
    struct kndSet *class_idx = self->class_idx;
    struct kndDict *name_idx = task->ctx->class_name_idx;
    int err;

    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. resolving classes in \"%.*s\"..",
                self->name_size, self->name);

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

            if (!c->is_resolved) {
                err = knd_class_resolve(c, task);
                if (err) {
                    knd_log("-- couldn't resolve the \"%.*s\" class",
                            c->entry->name_size, c->entry->name);
                    return err;
                }
                c->is_resolved = true;
            }
            
            err = class_idx->add(class_idx,
                                 c->entry->id, c->entry->id_size,
                                 (void*)c->entry);
            if (err) return err;
            if (DEBUG_REPO_LEVEL_2) {
                c->str(c, 1);
            }
        }
    }
    return knd_OK;
}

static int resolve_procs(struct kndRepo *self,
                         struct kndTask *task)
{
    struct kndProcEntry *entry;
    struct kndDictItem *item;
    struct kndDict *name_idx = task->ctx->proc_name_idx;
    int err;

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log(".. resolving procs of repo \"%.*s\"..",
                self->name_size, self->name);

    for (size_t i = 0; i < name_idx->size; i++) {
        item = atomic_load_explicit(&name_idx->hash_array[i],
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

            task->type = KND_LOAD_STATE;
            err = read_GSL_file(self, NULL, "index", strlen("index"), task);
            if (err) {
                knd_log("-- couldn't read any schemas");
                return err;
            }

            err = resolve_classes(self, task);
            if (err) {
                knd_log("-- class coordination failed");
                return err;
            }

            err = resolve_procs(self, task);
            if (err) {
                knd_log("-- resolving of procs failed");
                return err;
            }
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

static int present_repo_update_JSON(struct kndTaskContext *ctx)
{    
    struct kndOutput *out = ctx->out;
    struct kndUpdate *update = ctx->update;
    int err;

    err = out->write(out,  "{\"state\":{", strlen("{\"state\":{"));             RET_ERR();
    err = out->writef(out, "\"id\":%zu", (size_t)update->numid);                  RET_ERR();
    err = out->write(out,  "\"time\":", strlen("\"time\":"));                     RET_ERR();
    err = out->writef(out, "%zu", (size_t)update->timestamp);                     RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();
    return knd_OK;
}

static int present_repo_update_GSL(struct kndTaskContext *ctx)
{    
    struct kndOutput *out = ctx->out;
    struct kndUpdate *update = ctx->update;
    int err;

    err = out->write(out,  "{state ", strlen("{state "));                       RET_ERR();
    err = out->writef(out, "%zu", (size_t)update->numid);                         RET_ERR();
    err = out->write(out,  "{time ", strlen("{time "));                           RET_ERR();
    err = out->writef(out, "%zu", (size_t)update->timestamp);                     RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();
    return knd_OK;
}

static int present_repo_update(struct kndTaskContext *ctx)
{
    ctx->out->reset(ctx->out);
    switch (ctx->format) {
    case KND_FORMAT_JSON:
        return present_repo_update_JSON(ctx);
    default:
        return present_repo_update_GSL(ctx);
    }
    return knd_FAIL;
}

static int deliver_task_report(void *obj,
                               const char *unused_var(task_id),
                               size_t unused_var(task_id_size),
                               void *ctx_obj)
{
    struct kndTask *task = obj;
    struct kndTaskContext *ctx = ctx_obj;
    int err;

    knd_log(".. worker:%zu (type:%d) / ctx:%zu  delivering report on task #%zu..",
            task->id, task->type, ctx->numid, ctx->update->numid);

    if (ctx->update) {
        ctx->update->timestamp = time(NULL);
        err = present_repo_update(ctx);                                           RET_ERR();
    }

    if (!ctx->out->buf_size) {
        err = ctx->out->write(ctx->out, "{}", strlen("{}"));
        if (err) return err;
    }

    ctx->phase = KND_COMPLETE;
    
    return knd_OK;
}

static int build_persistent_commit(void *obj,
                                   const char *unused_var(task_id),
                                   size_t unused_var(task_id_size),
                                   void *ctx_obj)
{
    struct kndTask *task = obj;
    struct kndTaskContext *ctx = ctx_obj;
    int err;

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("..  worker:#%zu / ctx:%zu    write commit #%zu ..",
                task->id, ctx->numid, ctx->update->numid);

    ctx->phase = KND_WAL_COMMIT;
    ctx->cb = deliver_task_report;
    err = knd_queue_push(task->storage->input_queue, (void*)ctx);                 RET_ERR();

    return knd_OK;
}

int knd_confirm_updates(struct kndRepo *self, struct kndTask *task)
{
    struct kndTaskContext *ctx = task->ctx;
    struct kndUpdate *update = ctx->update;
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

    for (ref = update->class_state_refs; ref; ref = ref->next) {
        entry = ref->obj;
        if (entry) {
            knd_log(".. repo %.*s to confirm updates in \"%.*s\"..",
                    self->name_size, self->name,
                    entry->name_size, entry->name);
        }
        err = knd_class_resolve(entry->class, task);                              RET_ERR();
        
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

    /* PROCS */
    for (ref = update->proc_state_refs; ref; ref = ref->next) {
        proc_entry = ref->obj;
        if (proc_entry) {
            knd_log(".. confirming updates in \"%.*s\"..",
                    self->name_size, self->name,
                    proc_entry->name_size, proc_entry->name);
        }
        /* proc resolving */
        err = knd_proc_resolve(proc_entry->proc, task);                     RET_ERR();
    }

    // TODO: serialize update

    ctx->phase = KND_WAL_WRITE;
    ctx->cb = build_persistent_commit;
    ctx->repo = self;

    err = knd_queue_push(task->storage->input_queue, (void*)ctx);
    if (err) return err;

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

int knd_repo_check_conflicts(struct kndRepo *self,
                             struct kndTaskContext *ctx)
{
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size;
    struct kndStateRef *ref;
    struct kndClassEntry *entry;
    struct kndUpdate *update = ctx->update, *curr_update;
    int err;

    if (DEBUG_REPO_LEVEL_2)
        knd_log("== orig update: #%zu ",
                update->orig_state_id);
    size_t latest_update_id = atomic_load_explicit(&self->num_updates,
                                                   memory_order_relaxed);

    for (size_t i = update->orig_state_id; i < latest_update_id; i++) {
        knd_uid_create(i + 1, idbuf, &idbuf_size);

        if (DEBUG_REPO_LEVEL_TMP)
            knd_log(".. any conflicts with prev update #%zu [id:%.*s]?",
                    i, idbuf_size, idbuf);

        err = self->update_idx->get(self->update_idx,
                                    idbuf, idbuf_size, (void**)&curr_update);
        if (err) {
            knd_log("-- no such update: #%zu", (i + 1));
            return err;
        }

        for (ref = update->class_state_refs; ref; ref = ref->next) {
            entry = ref->obj;

            knd_log(".. check class conflicts: %.*s..",
                    entry->name_size, entry->name);

            entry->class->str(entry->class, 1);
            /*       err = knd_dict_set(name_idx,
                     entry->name,  entry->name_size,
                     (void*)entry);
                     if (err) return err;
            */
            //update->confirm = KND_CONFLICT_STATE;

            /* no conflicts with this update */
            update->orig_state_id = i;
        }
    }
    
    update->confirm = KND_VALID_STATE;
    update->numid = atomic_fetch_add_explicit(&self->num_updates, 1,
                                              memory_order_relaxed);
    update->numid++;
    knd_uid_create(update->numid, update->id, &update->id_size);

    err = self->update_idx->add(self->update_idx,
                                update->id, update->id_size,
                                (void*)update);
    if (err) return err;

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("++ no conflicts found, update #%zu confirmed!",
                update->numid);

    return knd_OK;
}

int knd_repo_update_name_idx(struct kndRepo *self,
                             struct kndTaskContext *ctx)
{
    struct kndStateRef *ref;
    struct kndClassEntry *entry;
    struct kndDict *name_idx = self->class_name_idx;
    struct kndUpdate *update = ctx->update;
    int err;

    for (ref = update->class_state_refs; ref; ref = ref->next) {
        entry = ref->obj;

        if (DEBUG_REPO_LEVEL_TMP)
            knd_log(".. register class \"%.*s\"..",
                    entry->name_size, entry->name);
        err = knd_dict_set(name_idx,
                           entry->name,  entry->name_size,
                           (void*)entry);
        if (err) return err;
    }

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
    err = knd_dict_new(&self->class_name_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    /* attrs */
    err = knd_set_new(mempool, &self->attr_idx);
    if (err) goto error;
    err = knd_dict_new(&self->attr_name_idx, KND_MEDIUM_DICT_SIZE);
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

    err = knd_dict_new(&self->proc_name_idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    /* proc args */
    err = knd_set_new(mempool, &self->proc_arg_idx);
    if (err) goto error;
    err = knd_dict_new(&self->proc_arg_name_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    /* proc insts */
    err = knd_dict_new(&self->proc_inst_name_idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    /* updates */
    err = knd_set_new(mempool, &self->update_idx);
    if (err) goto error;

    self->max_journal_size = KND_FILE_BUF_SIZE;
    *repo = self;

    return knd_OK;
 error:
    // TODO: release resources
    return err;
}
