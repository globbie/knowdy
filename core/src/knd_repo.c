#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

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

static int kndRepo_restore(struct kndRepo *self,
                           const char *filename)
{
    struct glbOutput *out = self->task->file_out;
    int err;

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("  .. restoring repo \"%.*s\" in \"%s\"",
                self->name_size, self->name,
                filename);

    out->reset(out);
    err = out->write_file_content(out, filename);
    if (err) {
        knd_log("-- failed to open journal: \"%s\"", filename);
        return err;
    }

    knd_log(".. restore the journal file: %.*s", out->buf_size, out->buf);

    //err = kndRepo_import_inbox_data(self, self->out->buf);
    //if (err) return err;

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
    state_ctrl->max_updates = mempool->max_updates;
    state_ctrl->updates =     mempool->update_idx;
    state_ctrl->repo = self;
    self->state_ctrl = state_ctrl;
   
    err = kndClass_new(&c, mempool);
    if (err) goto error;
    c->entry->repo = self;
    self->root_class = c;

    err = kndProc_new(&proc, mempool);
    if (err) goto error;
    proc->entry->repo = self;
    self->root_proc = proc;
    
    err = kndRel_new(&rel, mempool);
    if (err) goto error;
    rel->entry->repo = self;
    self->root_rel = rel;

    self->mempool = mempool;

    self->del = kndRepo_del;
    self->str = kndRepo_str;
    self->init = kndRepo_init;

    *repo = self;

    return knd_OK;
 error:
    // TODO: release resources
    return err;
}
