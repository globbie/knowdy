#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glb-lib/output.h>

#include "knd_mempool.h"
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

static void reset(struct kndStateControl *self)
{
    self->num_updates = 0;
    self->out->reset(self->out);
}

/*static int export_update_GSP(struct kndUpdate *update,
                             struct glbOutput *out)
{
    char buf[KND_NAME_SIZE] = {0};
    size_t buf_size = 0;
    struct tm tm_info;
    int err;

    err = out->writec(out, '{');                                                  RET_ERR();
    time(&update->timestamp);
    localtime_r(&update->timestamp, &tm_info);
    buf_size = strftime(buf, KND_NAME_SIZE,
                        "{ts %Y-%m-%d %H:%M:%S}", &tm_info);
    err = out->write(out, buf, buf_size);                                         RET_ERR();

    err = out->writec(out, '}');                                                  RET_ERR();
    err = out->writec(out, '\n');                                                 RET_ERR();
    return knd_OK;
}
 */

 static int knd_sync_update(struct kndStateControl *self,
                           struct kndUpdate *unused_var(update))
{
    struct glbOutput *out = self->task->out;
    struct glbOutput *file_out = self->task->file_out;
    struct stat st;
    size_t planned_journal_size = 0;
    int err;

    /* linearize an update */
    file_out->reset(file_out);
    //err = export_update_GSP(update, file_out);    RET_ERR();

    out->reset(out);
    err = out->write(out, self->repo->path, self->repo->path_size);          RET_ERR();
    err = out->writef(out, "/journal%zu.log", self->repo->num_journals);     RET_ERR();
    out->buf[out->buf_size] = '\0';

    if (stat(out->buf, &st)) {
        knd_log(".. initializing the journal: %.*s", out->buf_size, out->buf);
        err = knd_write_file((const char*)out->buf,
                             "[!update\n", strlen("[!update\n"));
        if (err) return err;
    } else {
        planned_journal_size = st.st_size + file_out->buf_size;
        if (planned_journal_size >= self->repo->max_journal_size) {

            knd_log("!NB: journal size limit reached!");

            self->repo->num_journals++;

            out->reset(out);
            err = out->write(out, self->repo->path, self->repo->path_size);
            if (err) return err;
            err = out->writef(out, "/journal%zu.log", self->repo->num_journals);
            if (err) return err;
            out->buf[out->buf_size] = '\0';

            knd_log(".. switch journal: %.*s", out->buf_size, out->buf);
            err = knd_write_file((const char*)out->buf,
                                 "[!update\n", strlen("[!update\n"));
            if (err) return err;
        }
    }

    // TODO: call a goroutine
    err = knd_append_file((const char*)out->buf,
                          file_out->buf, file_out->buf_size);
    if (err) {
        knd_log("-- update write failure");
        return err;
    }
    return knd_OK;
}

static int build_update_report(struct kndStateControl *self,
                               struct kndUpdate *update)
{
    struct kndTask *task = self->task;
    struct glbOutput *out = task->out;
    int err;

    out->reset(out);
    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->writef(out, "\"update\":%zu", update->numid);                      RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

static int knd_confirm(struct kndStateControl *self,
                       struct kndUpdate *update)
{
    struct kndRepo *repo = self->repo;
    struct kndTask *task = self->task;
    struct glbOutput *out = task->out;
    struct glbOutput *file_out = task->file_out;
    int err;

    if (DEBUG_STATE_LEVEL_TMP)
        knd_log("State Controller: .. "
                " confirming update %zu..  Repo:%.*s",
                self->num_updates + 1, repo->name_size, repo->name);

    /*if (task->type == KND_LIQUID_STATE) {
        self->updates[self->num_updates] = update;
        self->num_updates++;
        if (DEBUG_STATE_LEVEL_TMP)
            knd_log("++  \"%zu\" liquid update confirmed!   global STATE: %zu",
                    update->numid, self->num_updates);
        return knd_OK;
    }
    */

    // TODO: check conflicts with previous updates

    // TODO: check the journal file size limit

    /*if (self->num_updates >= self->max_updates) {
        knd_log("-- max update limit exceeded, time to freeze?");
        return knd_FAIL;
    }
    */

    update->timestamp = time(NULL);
    update->numid = self->num_updates + 1;
    //knd_num_to_str(update->numid, update->id, &update->id_size, KND_RADIX_BASE);

    /* TODO: send request to sync the update */
    err = knd_sync_update(self, update);
    if (err) {
        // TODO: release resources
        return err;
    }

    update->next = repo->updates;
    repo->updates = update;
    repo->num_updates++;

    if (DEBUG_STATE_LEVEL_TMP)
        knd_log("++ State Controller: "
                "\"%zu\" update confirmed! total updates: %zu",
                update->numid, self->num_updates);

    /* sync latest state id */
    file_out->reset(file_out);
    err = file_out->writec(file_out, '{');                                        RET_ERR();
    err = file_out->write(file_out, "state ", strlen("state "));                  RET_ERR();
    //err = file_out->write(file_out, update->id, update->id_size);                 RET_ERR();

    err = file_out->writec(file_out, '{');                                        RET_ERR();
    err = file_out->write(file_out, "log ", strlen("log "));                      RET_ERR();
    err = file_out->writef(file_out, "%zu", self->repo->num_journals);            RET_ERR();
    err = file_out->writec(file_out, '}');                                        RET_ERR();

    err = file_out->writec(file_out, '}');                                        RET_ERR();

    out->reset(out);
    err = out->write(out, self->repo->path, self->repo->path_size);               RET_ERR();
    err = out->write(out, "/new_state.gsl", strlen("/new_state.gsl"));            RET_ERR();
    err = knd_write_file(out->buf,
                         file_out->buf, file_out->buf_size);                      RET_ERR();

    /* main state filename */
    file_out->reset(file_out);
    err = file_out->write(file_out, self->repo->path, self->repo->path_size);     RET_ERR();
    err = file_out->write(file_out, "/state.gsl", strlen("/state.gsl"));          RET_ERR();

    if (DEBUG_STATE_LEVEL_2)
        knd_log(".. activate new state: \"%s\" -> \"%s\"..",
                out->buf, file_out->buf);

    err = rename(out->buf, file_out->buf);
    if (err) return err;

    /* build report */
    err = build_update_report(self, update);     RET_ERR();
    // TODO: add format switch
    
    // TODO: trigger pushes to subscription channels

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
    self->reset  = reset;
    self->confirm  = knd_confirm;

    *state = self;

    return knd_OK;
}

extern int knd_state_new(struct kndMemPool *mempool,
                         struct kndState **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndState), &page);                      RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_state_ref_new(struct kndMemPool *mempool,
                             struct kndStateRef **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndStateRef), &page);                      RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_state_val_new(struct kndMemPool *mempool,
                             struct kndStateVal **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndStateVal), &page);                      RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_update_new(struct kndMemPool *mempool,
                          struct kndUpdate **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                            sizeof(struct kndUpdate), &page);                     RET_ERR();
    *result = page;
    return knd_OK;
}
