#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glb-lib/output.h>

#include "knd_state.h"
#include "knd_user.h"
#include "knd_repo.h"
#include "knd_utils.h"

#define DEBUG_STATE_LEVEL_0 0
#define DEBUG_STATE_LEVEL_1 0
#define DEBUG_STATE_LEVEL_2 0
#define DEBUG_STATE_LEVEL_3 0
#define DEBUG_STATE_LEVEL_TMP 1

static void del(struct kndStateControl *self)
{
    self->log->del(self->log);
    self->spec_out->del(self->spec_out);
    self->update->del(self->update);
    free(self);
}

static void str(struct kndStateControl *self)
{
    //knd_log("curr state: %.*s", KND_STATE_SIZE, self->state);
}

static void reset(struct kndStateControl *self)
{
    self->num_updates = 0;
    self->out->reset(self->out);
}

static int knd_export_update_GSP(struct kndUpdate *update,
                                 struct glbOutput *out)
{
    int err;
    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->writef(out, "\"update\":%zu", update->numid);                      RET_ERR();

    err = out->writec(out, '}');                                                  RET_ERR();
    err = out->writec(out, '\n');                                                 RET_ERR();
    return knd_OK;
}

static int knd_sync_update(struct kndStateControl *self,
                           struct kndUpdate *update)
{
    struct glbOutput *out = self->repo->task->out;
    struct glbOutput *file_out = self->repo->task->file_out;
    int err;
    
    /* linearize an update */
    file_out->reset(file_out);
    err = knd_export_update_GSP(update, file_out);
    if (err) return err;

    out->reset(out);
    err = out->write(out, self->repo->path, self->repo->path_size);
    if (err) return err;
    err = out->write(out, "/journal.log", strlen("/journal.log"));
    if (err) return err;
    out->buf[out->buf_size] = '\0';

    // TODO: call a goroutine
    err = knd_append_file((const char*)out->buf,
                          file_out->buf, file_out->buf_size);
    if (err) {
        knd_log("-- update write failure");
        return err;
    }
    return knd_OK;
}

static int knd_confirm(struct kndStateControl *self,
                       struct kndUpdate *update)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct kndRepo *repo = self->repo;
    struct kndTask *task = self->repo->task;
    struct glbOutput *out = self->out;
    struct glbOutput *file_out = task->file_out;
    int err;

    if (DEBUG_STATE_LEVEL_TMP)
        knd_log("State Controller: .. "
                " confirming update %zu..  Repo:%.*s",
                update->numid, repo->name_size, repo->name);

    if (task->type == KND_LIQUID_STATE) {
        self->updates[self->num_updates] = update;
        self->num_updates++;
        if (DEBUG_STATE_LEVEL_TMP)
            knd_log("++  \"%zu\" liquid update confirmed!   global STATE: %zu",
                    update->numid, self->num_updates);
        return knd_OK;
    }

    /* TODO: check conflicts with previous updates */

    if (self->num_updates >= self->max_updates) {
        knd_log("-- max update limit exceeded, time to freeze?");
        return knd_FAIL;
    }

    update->timestamp = time(NULL);
    update->numid = self->num_updates + 1;

    /* TODO: send request to sync the update */
    err = knd_sync_update(self, update);
    if (err) {
        // TODO: release resources
        return err;
    }
    self->updates[self->num_updates] = update;
    self->num_updates++;

    if (DEBUG_STATE_LEVEL_TMP)
        knd_log("++ State Controller: "
                "\"%zu\" update confirmed! total updates: %zu",
                update->numid, self->num_updates);

    // TODO: add format switch
    out = task->out; 
    out->reset(out);
    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->writef(out, "\"update\":%zu", update->numid);                      RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    /* sync latest state id */
    knd_num_to_str(update->numid, buf, &buf_size, KND_RADIX_BASE);

    file_out->reset(file_out);
    err = file_out->writec(file_out, '{');                                        RET_ERR();
    err = file_out->write(file_out, "state ", strlen("state "));                  RET_ERR();
    err = file_out->write(file_out, buf, buf_size);                               RET_ERR();
    err = file_out->writec(file_out, '}');                                        RET_ERR();

    out->reset(out);
    err = out->write(out, self->repo->path, self->repo->path_size);               RET_ERR();
    err = out->write(out, "/state.gsl", strlen("/state.gsl"));                    RET_ERR();

    err = knd_write_file(out->buf,
                         file_out->buf, file_out->buf_size);                      RET_ERR();

    /*if (DEBUG_STATE_LEVEL_2)
        knd_log(".. renaming \"%s\" -> \"%s\"..",
                new_state_path, state_path);
    err = rename(new_state_path, state_path);
    if (err) return err;
    */

    return knd_OK;
}

extern int kndStateControl_new(struct kndStateControl **state)
{
    struct kndStateControl *self;
    int err;
    
    self = malloc(sizeof(struct kndStateControl));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndStateControl));

    err = glbOutput_new(&self->log, KND_TEMP_BUF_SIZE);
    if (err) return err;

    err = glbOutput_new(&self->spec_out, KND_MED_BUF_SIZE);
    if (err) return err;

    err = glbOutput_new(&self->update, KND_LARGE_BUF_SIZE);
    if (err) return err;

    self->del    = del;
    self->str    = str;
    self->reset  = reset;
    self->confirm  = knd_confirm;

    *state = self;

    return knd_OK;
}
