#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "knd_repo.h"
#include "knd_shard.h"
#include "knd_object.h"
#include "knd_attr.h"
#include "knd_set.h"
#include "knd_user.h"
#include "knd_query.h"
#include "knd_task.h"
#include "knd_dict.h"
#include "knd_class.h"
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

static void
kndRepo_del(struct kndRepo *self)
{
    free(self);
}

static void
kndRepo_str(struct kndRepo *self __attribute__((unused)))
{
    knd_log("REPO");
}



static int
kndRepo_open(struct kndRepo *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    const char *repo_dir = "/repos";
    size_t repo_dir_size = strlen(repo_dir);
    int err;

    struct glbOutput *out = self->path_out;

    out->reset(out);
    //err = out->write(out, self->user->path, self->user->path_size);
    //if (err) return err;
    
    
    err = out->write(out, repo_dir, repo_dir_size);
    if (err) return err;

    err = knd_make_id_path(buf, NULL, self->id, NULL);
    if (err) return err;
    buf_size = strlen(buf);

    err = out->write(out, buf, buf_size);
    if (err) return err;

    err = out->write(out, "/", 1);
    if (err) return err;

    if (out->buf_size >= KND_TEMP_BUF_SIZE) return knd_LIMIT;
    memcpy(self->path, out->buf, out->buf_size);
    self->path[out->buf_size] = '\0';
    self->path_size = out->buf_size;

    if (DEBUG_REPO_LEVEL_2)
        knd_log("..opening repo:  ID:\"%s\" REPO PATH:%s",
                self->id, self->path);

    err = out->write(out, "repo.gsl", strlen("repo.gsl"));
    if (err) return err;

    self->out->reset(self->out);
    err = self->out->write_file_content(self->out,
                               (const char*)out->buf);
    if (err) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("-- failed to open repo: \"%s\" :(",
                    out->buf);
        return err;
    }

    self->out->buf[self->out->buf_size] = '\0';

    /*if (self->restore_mode) {
        err = kndRepo_parse_config(self, self->out->buf, &chunk_size);
        if (err) return err;

        if (!self->name_size) {
            knd_log("-- repo %s full name is not set :(",
                    self->id);
            return knd_FAIL;
        }
        }*/

    if (DEBUG_REPO_LEVEL_2)
        knd_log("++ REPO open success: \"%s\" PATH: \"%s\"",
                self->out->buf, self->path);

    return knd_OK;
}


static int
kndRepo_update_state(struct kndRepo *self)
{
    char new_state_path[KND_TEMP_BUF_SIZE];
    char state_path[KND_TEMP_BUF_SIZE];

    struct glbOutput *out;
    const char *state_header = "{STATE{repo";
    size_t state_header_size = strlen(state_header);

    const char *state_close = "}}";
    size_t state_close_size = strlen(state_close);
    int err;

    out = self->out;
    out->reset(out);

    err = out->write(out, state_header, state_header_size);
    if (err) return err;

    /* last repo id */
    /*err = out->write(out, "{last ", strlen("{last "));
    if (err) return err;
    err = out->write(out, self->last_id, KND_ID_SIZE);
    if (err) return err;
    err = out->write(out, "}", 1);
    if (err) return err;
    */
    err = out->write(out, state_close, state_close_size);
    if (err) return err;


    /*err = knd_write_file((const char*)self->path,
                         "state_new.gsl",
                         out->buf, out->buf_size);
    if (err) return err;

    buf_size = self->dbpath_size;
    memcpy(state_path, (const char*)self->dbpath, buf_size);
    memcpy(state_path + buf_size, "/state.gsl", strlen("/state.gsl"));
    buf_size += strlen("/state.gsl");
    state_path[buf_size] = '\0';

    buf_size = self->dbpath_size;
    memcpy(new_state_path, (const char*)self->dbpath, buf_size);
    memcpy(new_state_path + buf_size, "/state_new.gsl", strlen("/state_new.gsl"));
    buf_size += strlen("/state_new.gsl");
    new_state_path[buf_size] = '\0';
    */
    
    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. renaming \"%s\" -> \"%s\"..",
                new_state_path, state_path);

    err = rename(new_state_path, state_path);
    if (err) return err;

    return knd_OK;
}


static int
kndRepo_add_repo(struct kndRepo *self, const char *name, size_t name_size)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size = 0;
    char path[KND_TEMP_BUF_SIZE];
    size_t path_size = 0;
    struct kndRepo *repo;
    int err;

    /* check if repo's name is unique */
    repo = self->repo_idx->get(self->repo_idx, name, name_size);
    if (repo) {
        knd_log("-- \"%.*s\" REPO name already taken?", name_size, name);
        return knd_FAIL;
    }

    err = kndRepo_new(&repo, self->mempool);
    if (err) goto final;

    repo->user = self->user;
    repo->out = self->out;

    /* get new repo id */
    /*memcpy(repo->id, self->last_id, KND_ID_SIZE);
    repo->id[KND_ID_SIZE] = '\0';
    knd_inc_id(repo->id);
    */

    memcpy(repo->name, name, name_size);
    repo->name_size = name_size;
    repo->name[name_size] = '\0';

    /* check repo's existence: must be an error */
    err = kndRepo_open(repo);
    if (!err) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("-- \"%s\" REPO already exists?", repo->id);
        return knd_FAIL;
    }

    /*  sprintf(buf, "%s/repos", self->user->dbpath);
    err = knd_make_id_path(path, buf, repo->id, NULL);
    if (err) goto final;
    */

    /* TODO: check if DIR already exists */
    err = knd_mkpath(path, path_size, 0755, false);
    if (err) goto final;

    path_size = strlen(path);
    memcpy(self->path, path, path_size);
    self->path_size = path_size;
    self->path[path_size] = '\0';

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("  .. create new repo:  ID:\"%s\"  N:%.*s  PATH:%s",
                repo->id, repo->name_size, repo->name, path);

    /* in batch mode:
          ignore incoming tasks
       in non-batch mode (default):
          append incoming tasks to the inbox queue */

    sprintf(buf, "%s/inbox", path);
    err = knd_mkpath(buf, buf_size, 0755, false);
    if (err) goto final;

    /* reflecting class scheme changes */
    sprintf(buf, "%s/migrations", path);
    err = knd_mkpath(buf, buf_size, 0755, false);
    if (err) goto final;

    /*    err = kndRepo_export(repo, KND_FORMAT_GSL);
    if (err) goto final;
    */

    err = knd_write_file((const char*)path,
                         "repo.gsl",
                         repo->out->buf, repo->out->buf_size);
    if (err) goto final;

    err = self->repo_idx->set(self->repo_idx,
                              repo->name, repo->name_size,
                              repo);
    if (err) goto final;

    /* increment id */
    //memcpy(self->last_id, repo->id, KND_ID_SIZE);

    err = kndRepo_update_state(self);
    if (err) goto final;

    return knd_OK;

 final:
    repo->del(repo);

    return err;
}


static gsl_err_t
kndRepo_run_add_repo(void *obj, const char *name, size_t name_size)
{
    struct kndRepo *repo;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);

    repo = (struct kndRepo*)obj;

    err = kndRepo_add_repo(repo, name, name_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}



static gsl_err_t
kndRepo_run_get_repo(void *obj, const char *name, size_t name_size)
{
    struct kndRepo *self, *curr_repo;

    if (!name_size) return make_gsl_err(gsl_FORMAT);

    self = (struct kndRepo*)obj;
    self->curr_repo = NULL;

    curr_repo = self->repo_idx->get(self->repo_idx,
                                    name, name_size);
    if (!curr_repo) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("-- no such repo: \"%.*s\" :(", name_size, name);
        return make_gsl_err(gsl_FAIL);
    }

    if (DEBUG_REPO_LEVEL_2) {
        knd_log("++ got repo: \"%s\" PATH: %s",
                name, curr_repo->path);
    }

    /* assign task */
    curr_repo->task = self->task;
    curr_repo->log = self->log;
    self->curr_repo = curr_repo;

    return make_gsl_err(gsl_OK);
}


//static int
//kndRepo_run_get_obj(void *obj, struct kndTaskArg *args, size_t num_args)
//{
//    struct kndRepo *self;
//    struct kndTaskArg *arg;
//    const char *name = NULL;
//    size_t name_size = 0;
//    int err;
//
//    for (size_t i = 0; i < num_args; i++) {
//        arg = &args[i];
//        if (!strncmp(arg->name, "n", strlen("n"))) {
//            name = arg->val;
//            name_size = arg->val_size;
//        }
//    }
//    if (!name_size) return knd_FAIL;
//
//    self = (struct kndRepo*)obj;
//
//    if (DEBUG_REPO_LEVEL_1)
//        knd_log(".. repo %s to get OBJ: \"%s\"", self->name, name);
//
//    err = kndRepo_get_obj(self, name, name_size);
//    if (err) return err;
//
//    return knd_OK;
//}


//static int
//kndRepo_run_import_obj(void *obj, struct kndTaskArg *args, size_t num_args)
//{
//    struct kndRepo *self;
//    struct kndTaskArg *arg;
//    const char *name = NULL;
//    size_t chunk_size = 0;
//    size_t name_size = 0;
//    int err;
//
//    for (size_t i = 0; i < num_args; i++) {
//        arg = &args[i];
//        if (!strncmp(arg->name, "obj", strlen("obj"))) {
//            name = arg->val;
//            name_size = arg->val_size;
//        }
//    }
//    if (!name_size) return knd_FAIL;
//
//    self = (struct kndRepo*)obj;
//
//    /* obj from separate msg */
//    if (!strncmp(name, "_attach", strlen("_attach"))) {
//        err = kndRepo_import_obj(self, self->task->obj, &chunk_size);
//        if (err) return err;
//        return knd_OK;
//    }
//
//    return knd_FAIL;
//}


static int kndRepo_restore(struct kndRepo *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    const char *inbox_filename = "inbox/import.data";
    int err;

    if (DEBUG_REPO_LEVEL_2)
        knd_log("  .. restoring repo \"%s\".. PATH: %s [%lu]",
                self->id, self->path, (unsigned long)self->path_size);

    memcpy(buf, self->path, self->path_size);
    memcpy(buf + self->path_size, inbox_filename, strlen(inbox_filename));
    buf_size = self->path_size + strlen(inbox_filename);
    buf[buf_size] = '\0';

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log(".. importing recs from \"%s\"..", buf);

    // fixme: actual restore
    return knd_OK;

    self->out->reset(self->out);
    err = self->out->write_file_content(self->out,
                               (const char*)buf);
    if (err) {
        knd_log("   -- failed to open the inbox \"%s\" :(", buf);
        if (err == knd_IO_FAIL) {
            knd_log("  .. skipping import of \"%s\"..", self->id);
            return knd_OK;
        }

        return err;
    }
    self->out->buf[self->out->buf_size] = '\0';

    //err = kndRepo_import_inbox_data(self, self->out->buf);
    //if (err) return err;

    return knd_OK;
}

static gsl_err_t
kndRepo_parse_class(void *obj,
                    const char *rec,
                    size_t *total_size)
{
    struct kndRepo *self, *repo;
    gsl_err_t err;

    self = (struct kndRepo*)obj;

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("   .. parsing the CLASS rec: \"%s\" REPO: %s\n\n", rec, self->name);

    if (!self->curr_repo) {
        knd_log("-- no repo selected :(");
        return *total_size = 0, make_gsl_err(gsl_FAIL);
    }

    repo = self->curr_repo;
    repo->out = self->task->out;

    struct gslTaskSpec specs[] = {
        { .name = "obj",
          .name_size = strlen("obj")
        }
    };

    if (DEBUG_REPO_LEVEL_1)
        knd_log("   .. parsing the CLASS rec: \"%s\" CURR REPO: %s",
                rec, repo->name);

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("-- failed to parse the CLASS rec: %d", err);
        return err;
    }

    /* call default action */

    return make_gsl_err(gsl_OK);
}


static gsl_err_t
kndRepo_parse_task(void *self,
                   const char *rec,
                   size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        { .name = "add",
          .name_size = strlen("add"),
          .run = kndRepo_run_add_repo,
          .obj = self
        },
        { .name = "n",
          .name_size = strlen("n"),
          .run = kndRepo_run_get_repo,
          .obj = self
        },
        { .name = "class",
          .name_size = strlen("class"),
          .parse = kndRepo_parse_class,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

extern int kndRepo_init(struct kndRepo *self)
{
    struct glbOutput *out;
    struct kndClass *c;
    int err;

    self->task =     self->user->task;
    self->out =      self->task->out;
    self->file_out = self->task->file_out;
    self->log =      self->task->log;
    out = self->out;

    memcpy(self->schema_path,
           self->user->shard->schema_path,
           self->user->shard->schema_path_size);
    self->schema_path_size = self->user->shard->schema_path_size;

    memcpy(self->path, self->user->path, self->user->path_size);
    self->path_size = self->user->path_size;

    out->reset(out);
    err = out->write(out, self->path, self->path_size);
    if (err) return err;
    err = out->write(out, "/frozen.gsp", strlen("/frozen.gsp"));
    if (err) return err;

    if (out->buf_size >= sizeof(self->frozen_output_file_name))
        return knd_LIMIT;

    memcpy(self->frozen_output_file_name, out->buf, out->buf_size);
    self->frozen_output_file_name_size = out->buf_size;
    self->frozen_output_file_name[out->buf_size] = '\0';
    
    c = self->root_class;

    /* try opening the frozen DB */
    err = c->open(c);
    if (err) {
        if (err != knd_NO_MATCH) return err;

        /* read class definitions */
        knd_log("-- no frozen DB found, reading original schemas..");
        
        c->batch_mode = true;
        err = c->load(c, NULL, "index", strlen("index"));
        if (err) {
            knd_log("-- couldn't read any schemas :(");
            return err;
        }

        err = c->coordinate(c);
        if (err) return err;
        c->batch_mode = false;
    }

    return knd_OK;
}

extern int
kndRepo_new(struct kndRepo **repo,
            struct kndMemPool *mempool)
{
    struct kndRepo *self;
    struct kndStateControl *state_ctrl;
    struct kndClass *c;
    struct kndClassEntry *class_entry;
    struct kndProc *proc;
    struct kndRel *rel;
    int err;

    self = malloc(sizeof(struct kndRepo));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndRepo));
    memset(self->id, '0', KND_ID_SIZE);

    //err = glbOutput_new(&self->out, KND_IDX_BUF_SIZE);
    //if (err != knd_OK) goto error;

    //err = glbOutput_new(&self->log, KND_MED_BUF_SIZE);
    //if (err != knd_OK) goto error;


    err = kndStateControl_new(&state_ctrl);
    if (err) return err;
    
    state_ctrl->max_updates = mempool->max_updates;
    state_ctrl->updates =     mempool->update_idx;
    self->state_ctrl = state_ctrl;
   
    err = mempool->new_class(mempool, &c);                                        RET_ERR();
    err = mempool->new_class_entry(mempool, &class_entry);                        RET_ERR();
    class_entry->name[0] = '/';
    class_entry->name_size = 1;
    class_entry->repo = self;
    class_entry->class = c;
    c->entry = class_entry;
    /* obj manager */
    err = mempool->new_obj(mempool, &c->curr_obj);                 RET_ERR();
    c->curr_obj->base = c;
    self->root_class = c;

    /* specific allocations for the root class */
    err = mempool->new_set(mempool, &c->class_idx);                               RET_ERR();
    c->class_idx->type = KND_SET_CLASS;

    err = ooDict_new(&c->class_name_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    err = kndProc_new(&proc, mempool);
    if (err) goto error;
    proc->entry->repo = self;
    self->root_proc = proc;
    
    err = kndRel_new(&rel);
    if (err) goto error;
    rel->repo = self;
    self->root_rel = rel;

    /* read any existing updates to the frozen DB (failure recovery) */
    /*err = conc->restore(conc);
    if (err) return err;
    */

    self->mempool = mempool;

    self->del = kndRepo_del;
    self->str = kndRepo_str;
    self->init = kndRepo_init;
    self->parse_task = kndRepo_parse_task;
    self->open = kndRepo_open;
    self->restore = kndRepo_restore;

    *repo = self;

    return knd_OK;
 error:
    // TODO: release resources
    return err;
}
