#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "knd_state.h"
#include "knd_user.h"
#include "knd_output.h"
#include "knd_utils.h"
#include "knd_parser.h"
#include "knd_msg.h"

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
    knd_log("curr state: %.*s", KND_STATE_SIZE, self->state);
}

static void reset(struct kndStateControl *self)
{
    self->num_updates = 0;
    self->out->reset(self->out);
}

static int confirm(struct kndStateControl *self,
                   struct kndUpdate *update)
{
    struct kndOutput *out;
    int err;

    if (DEBUG_STATE_LEVEL_2)
        knd_log(".. confirming update: %zu", update->id);

    /* TODO: update conflicts */

    if (self->num_updates + 1 >= self->max_updates) {
        knd_log("-- max update limit exceeded, time to freeze?");
        return knd_FAIL;
    }

    update->timestamp = time(NULL);
    update->id = self->num_updates + 1;

    /* TODO: send request to sync the update */

    self->updates[self->num_updates] = update;
    self->num_updates++;

    if (DEBUG_STATE_LEVEL_TMP)
        knd_log("++  \"%zu\" update confirmed!   global STATE: %zu",
                update->id, self->num_updates);

    return knd_OK;
}

static int make_selection(struct kndStateControl *self)
{
    struct kndUpdate *update;
    size_t start_pos;
    size_t end_pos;

    self->num_selected = 0;

    start_pos = self->task->batch_gt;
    end_pos = self->task->batch_lt;

    if (self->task->batch_eq) {
        start_pos = self->task->batch_eq;
        end_pos = self->task->batch_eq + 1;
    }

    if (!end_pos)
        end_pos = self->num_updates;
    else
        end_pos = end_pos - 1;

    if (end_pos > self->num_updates) {
        knd_log("-- requested range of updates exceeded :(");
        return knd_RANGE;
    }

    if (start_pos > end_pos) return knd_RANGE;

    for (size_t i = start_pos; i < end_pos; i++) {
        update = self->updates[i];
        self->selected[self->num_selected] = update;
        self->num_selected++;
    }

    return knd_OK;
}

extern int kndStateControl_new(struct kndStateControl **state)
{
    struct kndStateControl *self;
    int err;
    
    self = malloc(sizeof(struct kndStateControl));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndStateControl));

    memset(self->state, '0', KND_STATE_SIZE);

    err = kndOutput_new(&self->log, KND_TEMP_BUF_SIZE);
    if (err) return err;

    err = kndOutput_new(&self->spec_out, KND_MED_BUF_SIZE);
    if (err) return err;

    err = kndOutput_new(&self->update, KND_LARGE_BUF_SIZE);
    if (err) return err;

    self->del    = del;
    self->str    = str;
    self->reset  = reset;
    self->confirm  = confirm;
    self->select  = make_selection;

    *state = self;

    return knd_OK;
}
