#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>

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
#include "knd_rel.h"
#include "knd_mempool.h"
#include "knd_state.h"

#include <gsl-parser.h>
#include <glb-lib/output.h>

#define DEBUG_REPO_LEVEL_0 0
#define DEBUG_REPO_LEVEL_1 0
#define DEBUG_REPO_LEVEL_2 0
#define DEBUG_REPO_LEVEL_3 0
#define DEBUG_REPO_LEVEL_TMP 1

static void kndRepo_del(struct kndRepo *self)
{
    free(self);
}

static void kndRepo_str(struct kndRepo *self)
{
    knd_log("Repo: %p", self);
}

static gsl_err_t alloc_class_update(void *obj,
                                    const char *name,
                                    size_t name_size,
                                    size_t count __attribute__((unused)),
                                    void **item)
{
    struct kndUpdate    *self = obj;
    struct kndMemPool *mempool = self->repo->mempool;
    struct kndClassUpdate *class_update;
    int err;
    assert(name == NULL && name_size == 0);
    err = knd_class_update_new(mempool, &class_update);
    if (err) return make_gsl_err_external(err);
    class_update->update = self;
    *item = class_update;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t append_class_update(void *accu,
                                     void *item)
{
    struct kndUpdate *self = accu;
    struct kndClassUpdate *class_update = item;
    // TODO
    return make_gsl_err(gsl_OK);
}

static gsl_err_t get_class_by_id(void *obj, const char *name, size_t name_size)
{
    struct kndClassUpdate *self = obj;
    struct kndRepo *repo = self->update->repo;
    struct kndMemPool *mempool = repo->mempool;
    struct kndSet *class_idx = repo->class_idx;
    struct ooDict *class_name_idx = repo->class_name_idx;
    void *result;
    struct kndClassEntry *entry;
    struct kndClass *c;
    int err;

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
    struct kndClassUpdate *self = obj;
    struct kndClass *c;
    struct kndRepo *repo = self->update->repo;
    struct ooDict *class_name_idx = repo->class_name_idx;
    struct kndSet *class_idx = repo->class_idx;
    struct kndMemPool *mempool = repo->mempool;
    struct kndClassEntry *entry = self->entry;
    void *page;
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
        knd_log("== batch mode: %d",
                repo->root_class->batch_mode);
    }
    char *s = malloc(name_size);
    memcpy(s, name, name_size);

    entry->name = s;
    entry->name_size = name_size;

    /* get class */
    err = knd_get_class(repo->root_class, name, name_size, &c);
    if (err) {
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_NORMAL, sizeof(*c), &page);
        if (err) return make_gsl_err_external(err);
        c = page;
        kndClass_init(c);

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
    gsl_err_t err;

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

static gsl_err_t alloc_update(void *obj,
                              const char *name,
                              size_t name_size,
                              size_t count,
                              void **item)
{
    struct kndRepo *self = obj;
    struct kndMemPool *mempool = self->mempool;
    struct kndUpdate *update;
    int err;

    assert(name == NULL && name_size == 0);

    err = knd_update_new(mempool, &update);
    if (err) return make_gsl_err_external(err);

    update->repo = self;
    *item = update;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t append_update(void *accu,
                               void *item)
{
    struct kndRepo *self =   accu;
    struct kndUpdate *update = item;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t get_timestamp(void *obj, const char *name, size_t name_size)
{
    struct kndUpdate *update = obj;
    char buf[KND_NAME_SIZE] = {0};
    size_t buf_size = 0;
    struct tm tm_info = {0};

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("UPD: id:%.*s  time:\"%.*s\"",
                update->id_size, update->id, name_size, name);

    memcpy(buf, name, name_size);
    buf_size = name_size;

    /* parse date/time */
    if (!strptime(buf, "%Y-%m-%d %H:%M:%S", &tm_info)) {
        knd_log("-- incorrect date/time: %.*s?", buf_size, buf);
        return make_gsl_err_external(knd_FAIL);
    }
    tm_info.tm_isdst = -1;
    update->timestamp = mktime(&tm_info);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_update(void *obj,
                              const char *rec,
                              size_t *total_size)
{
    struct kndUpdate *update = obj;

    struct gslTaskSpec class_update_spec = {
        .is_list_item = true,
        .alloc  = alloc_class_update,
        .append = append_class_update,
        .parse  = parse_class_update,
        .accu = update
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = update->id,
          .buf_size = &update->id_size,
          .max_buf_size = sizeof update->id
        },
        { .name = "ts",
          .name_size = strlen("ts"),
          .run = get_timestamp,
          .obj = update
        },
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

static gsl_err_t kndRepo_read_updates(void *obj,
                                      const char *rec,
                                      size_t *total_size)
{
    struct kndRepo *repo = obj;
    struct gslTaskSpec update_spec = {
        .is_list_item = true,
        .alloc  = alloc_update,
        .append = append_update,
        .parse  = parse_update,
        .accu = repo
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
                           const char *filename)
{
    struct glbOutput *out = self->task->file_out;
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

static int kndRepo_open(struct kndRepo *self)
{
    struct glbOutput *out;
    struct kndClass *c;
    struct kndProc *proc;
    struct kndRel *rel;
    struct kndClassInst *inst;
    struct stat st;
    int err;

    out = self->out;

    memcpy(self->schema_path,
           self->user->shard->schema_path,
           self->user->shard->schema_path_size);
    self->schema_path_size = self->user->shard->schema_path_size;

    memcpy(self->path, self->user->shard->path, self->user->shard->path_size);
    self->path_size = self->user->shard->path_size;

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

    c = self->root_class;

    /* frozen DB exists? */
    if (!stat(out->buf, &st)) {
        /* try opening the frozen DB */
        err = c->open(c, (const char*)out->buf);
        if (err) {
            knd_log("-- failed to open a frozen DB");
            return err;
        }
    } else {
        if (!self->user_ctx) {
            /* read a system-wide schema */
            knd_log("-- no existing frozen DB was found, reading the original schema..");
            c->batch_mode = true;
            err = c->load(c, NULL, "index", strlen("index"));
            if (err) {
                knd_log("-- couldn't read any schemas :(");
                return err;
            }
            err = c->coordinate(c);
            if (err) {
                knd_log("-- concept coordination failed");
                return err;
            }


            proc = self->root_proc;
            //err = proc->coordinate(proc);                                     RET_ERR();
            rel = self->root_rel;
            err = rel->coordinate(rel);                                       RET_ERR();
            c->batch_mode = false;
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
        err = kndRepo_restore(self, out->buf);
        if (err) return err;
    }

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("++ \"%.*s\" repo opened in \"%.*s\"!",
                self->name_size, self->name,
                self->path_size, self->path);

    return knd_OK;
}

extern int kndRepo_init(struct kndRepo *self)
{
    int err;

    self->task     = self->user->task;
    self->out      = self->task->out;
    self->file_out = self->task->file_out;
    self->log      = self->task->log;

    err = kndRepo_open(self);
    if (err) return err;
    
    return knd_OK;
}

extern int kndRepo_new(struct kndRepo **repo,
                       struct kndMemPool *mempool)
{
    struct kndRepo *self;
    struct kndStateControl *state_ctrl;
    struct kndClass *c;
    struct kndProc *proc;
    struct kndRel *rel;
    int err;

    self = malloc(sizeof(struct kndRepo));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndRepo));

    err = kndStateControl_new(&state_ctrl);
    if (err) return err;
    //state_ctrl->updates =     mempool->update_idx;
    state_ctrl->repo = self;
    self->state_ctrl = state_ctrl;
   
    err = kndClass_new(&c, mempool);
    if (err) goto error;
    c->entry->repo = self;
    self->root_class = c;

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
    
    err = kndProc_new(&proc, self, mempool);
    if (err) goto error;
    proc->entry->name[0] = '/';
    proc->entry->name_size = 1;
    self->root_proc = proc;
    
    err = kndRel_new(&rel, mempool);
    if (err) goto error;
    rel->entry->repo = self;
    self->root_rel = rel;

    self->mempool = mempool;
    self->max_journal_size = KND_FILE_BUF_SIZE;

    self->del = kndRepo_del;
    self->str = kndRepo_str;
    self->init = kndRepo_init;

    *repo = self;

    return knd_OK;
 error:
    // TODO: release resources
    return err;
}
