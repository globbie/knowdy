#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_task.h"
#include "knd_user.h"
#include "knd_output.h"
#include "knd_utils.h"
#include "knd_parser.h"
#include "knd_msg.h"
#include "knd_http_codes.h"

#define DEBUG_TASK_LEVEL_0 0
#define DEBUG_TASK_LEVEL_1 0
#define DEBUG_TASK_LEVEL_2 0
#define DEBUG_TASK_LEVEL_3 0
#define DEBUG_TASK_LEVEL_TMP 1

static void del(struct kndTask *self)
{
    free(self);
}

static void str(struct kndTask *self __attribute__((unused)), size_t depth __attribute__((unused)))
{
    
}

static void reset(struct kndTask *self)
{
    self->sid_size = 0;
    self->uid_size = 0;
    self->tid_size = 0;
    self->locale = self->admin->default_locale;
    self->locale_size = self->admin->default_locale_size;
    self->curr_locale_size = 0;

    memset(self->state, '0', KND_STATE_SIZE);
    self->is_state_changed = false;

    self->type = KND_GET_STATE;
    /*self->admin->locale = self->admin->default_locale;
      self->admin->locale_size = self->admin->default_locale_size; */

    self->batch_max = KND_RESULT_BATCH_SIZE;
    self->batch_size = 0;
    self->batch_from = 0;
    self->start_from = 0;
    self->match_count = 0;

    self->error = 0;
    self->http_code = HTTP_OK;
    self->log->reset(self->log);
    self->out->reset(self->out);
    self->update->reset(self->update);
    self->spec_out->reset(self->spec_out);
}

static int parse_update(void *obj,
                        const char *rec,
                        size_t *total_size)
{
    struct kndTask *self = (struct kndTask*)obj;
    int err;

    self->type = KND_UPDATE_STATE;
    self->tid[0] = '0';
    self->tid_size = 0;
    self->admin->task = self;
    self->admin->out = self->out;
    self->admin->log = self->log;
            
    err = self->admin->parse_task(self->admin, rec, total_size);
    if (err) {
        knd_log("-- update parse failed :(");
        return knd_FAIL;
    }

    return knd_OK;
}


static int parse_iter_batch(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndTask *self = obj;
    struct kndTaskSpec specs[] = {
        { .name = "size",
          .name_size = strlen("size"),
          .parse = knd_parse_size_t,
          .obj = &self->batch_max
        },
        { .name = "from",
          .name_size = strlen("from"),
          .parse = knd_parse_size_t,
          .obj = &self->batch_from
        }
    };
    int err;

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    if (self->batch_max > KND_RESULT_MAX_BATCH_SIZE) {
        knd_log("-- batch size exceeded: %zu (max limit: %d) :(",
                self->batch_max, KND_RESULT_MAX_BATCH_SIZE);
        return knd_LIMIT;
    }

    self->start_from = self->batch_max * self->batch_from;

    return knd_OK;
}


static int parse_iter(void *obj,
                      const char *rec,
                      size_t *total_size)
{
    struct kndTaskSpec specs[] = {
        { .name = "batch",
          .name_size = strlen("batch"),
          .parse = parse_iter_batch,
          .obj = obj
        }
    };
    int err;

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}

static int parse_user(void *obj,
                      const char *rec,
                      size_t *total_size)
{
    struct kndTask *self = (struct kndTask*)obj;
    int err;

    self->admin->task = self;
    self->admin->out = self->out;
    self->admin->log = self->log;

    if (self->curr_locale_size) {
        self->locale = self->curr_locale;
        self->locale_size = self->curr_locale_size;
    }

    err = self->admin->parse_task(self->admin, rec, total_size);
    if (err) {
        knd_log("-- User area parse failed");
        return knd_FAIL;
    }

    return knd_OK;
}

static int parse_task(void *obj,
                      const char *rec,
                      size_t *total_size)
{
    struct kndTask *self = (struct kndTask*)obj;
    int err;
   
    struct kndTaskSpec specs[] = {
        { .name = "schema",
          .name_size = strlen("schema"),
          .buf = self->schema_name,
          .buf_size = &self->schema_name_size,
          .max_buf_size = KND_NAME_SIZE
        },
        { .name = "tid",
          .name_size = strlen("tid"),
          .buf = self->tid,
          .buf_size = &self->tid_size,
          .max_buf_size = KND_NAME_SIZE
        },
        { .name = "locale",
          .name_size = strlen("locale"),
          .buf = self->curr_locale,
          .buf_size = &self->curr_locale_size,
          .max_buf_size = KND_NAME_SIZE
        },
        { .name = "user",
          .name_size = strlen("user"),
          .parse = parse_user,
          .obj = self
        },
        { .name = "update",
          .name_size = strlen("update"),
          .parse = parse_update,
          .obj = self
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    /* check mandatory fields */
    if (!self->tid_size) {
        switch (self->type) {
        case KND_UPDATE_STATE:
            return knd_OK;
        default:
            knd_log("-- no TID found");
            return knd_FAIL;
        }
    }

    return knd_OK;
}

static int parse_GSL(struct kndTask *self,
                     const char *rec,
                     size_t rec_size,
                     const char *obj,
                     size_t obj_size)
{
    if (DEBUG_TASK_LEVEL_2)
        knd_log(".. parsing task: \"%s\"..", rec);

    struct kndTaskSpec specs[] = {
        { .name = "task",
          .name_size = strlen("task"),
          .parse = parse_task,
          .obj = self
        }
    };
    size_t total_size = 0;
    int err, e;

    self->spec = rec;
    self->spec_size = rec_size;
    self->obj = obj;
    self->obj_size = obj_size;
   
    err = knd_parse_task(rec, &total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) {
        knd_log("-- task parse failure: \"%.*s\" :(", self->log->buf_size, self->log->buf);
        if (!self->log->buf_size) {
            e = self->log->write(self->log, "internal server error",
                                 strlen("internal server error"));
            if (e) return e;
        }
        return err;
    }

    return knd_OK;
}


static int report(struct kndTask *self)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size;
    const char *gsl_header = "{task";
    const char *msg = "None";
    size_t msg_size = strlen(msg);
    struct kndOutput *out = self->spec_out;
    char *header;
    size_t header_size;
    char *obj;
    size_t obj_size;
    int err;

    if (DEBUG_TASK_LEVEL_2)
        knd_log("..TASK reporting..");
        
    out->reset(out);
    err = out->write(out, gsl_header, strlen(gsl_header));
    if (err) return err;

    /*err = out->write(out, "{agent ", strlen("{agent "));
    if (err) return err;
    err = out->write(out, self->agent_name, self->agent_name_size);
    if (err) return err;
    err = out->write(out, "}", 1);
    if (err) return err;*/

    err = out->write(out, "{tid ", strlen("{tid "));
    if (err) return err;

    if (self->tid_size) {
        err = out->write(out, self->tid, self->tid_size);
        if (err) return err;
    } else {
        err = out->write(out, "0", 1);
        if (err) return err;
    }

    err = out->write(out, "}", 1);
    if (err) return err;

    err = out->write(out, "{user{auth", strlen("{user{auth"));
    if (err) return err;

    err = out->write(out, "{sid ", strlen("{sid "));
    if (err) return err;
    err = out->write(out, self->admin->sid, self->admin->sid_size);
    if (err) return err;
    err = out->write(out, "}}", strlen("}}"));
    if (err) return err;

    if (self->error) {
        err = out->write(out, "{err ", strlen("{err "));
        if (err) return err;
        err = out->write(out, self->log->buf, self->log->buf_size);
        if (err) return err;
        err = out->write(out, "}", 1);
        if (err) return err;

        /* TODO: build JSON reply in loco */
        self->out->reset(self->out);
        err = self->out->write(self->out, "{\"err\":\"", strlen("{\"err\":\""));
        if (err) return err;
        err = self->out->write(self->out, self->log->buf, self->log->buf_size);
        if (err) return err;
        err = self->out->write(self->out, "\"", strlen("\""));
        if (err) return err;

        if (self->http_code != HTTP_OK) {
            err = self->out->write(self->out,
                                   ",\"http_code\":", strlen(",\"http_code\":"));
            if (err) return err;
            buf_size = sprintf(buf, "%d", self->http_code);
            err = self->out->write(self->out, buf, buf_size);
            if (err) return err;
        }

        err = self->out->write(self->out, "}", strlen("}"));
        if (err) return err;

    } else {
        err = out->write(out, "{save _obj}", strlen("{save _obj}"));
        if (err) return err;
    }
    
    if (DEBUG_TASK_LEVEL_TMP) {
        obj_size = self->out->buf_size;
        if (obj_size > KND_MAX_DEBUG_CONTEXT_SIZE) {
            obj_size = KND_MAX_DEBUG_CONTEXT_SIZE;
            knd_log("== TASK report: SPEC: \"%.*s\"\n\n== BODY: \"%.*s...\" [total size: %zu]\n",
                    out->buf_size, out->buf, obj_size, self->out->buf, self->out->buf_size);
        }
        else {
            knd_log("== TASK report: SPEC: \"%.*s\"\n\n== BODY: \"%.*s\" [size: %zu]\n",
                    out->buf_size, out->buf, obj_size, self->out->buf, self->out->buf_size);
        }
    }

    err = knd_zmq_sendmore(self->delivery, (const char*)out->buf, out->buf_size);
    /* obj body */
    if (self->out->buf_size) {
        msg = self->out->buf;
        msg_size = self->out->buf_size;
    }

    /* send delta */
    if (self->type == KND_DELTA_STATE) {
        if (self->update->buf_size) {
            msg = self->update->buf;
            msg_size = self->update->buf_size;
        }
    }

    err = knd_zmq_send(self->delivery, msg, msg_size);

    /* get confirmation reply from  the manager */
    header = knd_zmq_recv(self->delivery, &header_size);
    obj = knd_zmq_recv(self->delivery, &obj_size);
    
    if (DEBUG_TASK_LEVEL_TMP)
        knd_log("== Delivery reply header: \"%s\" obj: \"%s\"",
                header, obj);

    if (header)
        free(header);
    if (obj)
        free(obj);

    if (!self->is_state_changed) return knd_OK;

    /* inform all retrievers about the state change */
    if (DEBUG_TASK_LEVEL_2)
        knd_log("\n\n** UPDATE retrievers: \"%.*s\" [%lu]",
                self->update->buf_size, self->update->buf,
                (unsigned long)self->update->buf_size);

    err = knd_zmq_sendmore(self->publisher, self->update->buf, self->update->buf_size);
    err = knd_zmq_send(self->publisher, "None", strlen("None"));
    
    return knd_OK;
}

extern int kndTask_new(struct kndTask **task)
{
    struct kndTask *self;
    int err;
    
    self = malloc(sizeof(struct kndTask));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndTask));

    err = kndOutput_new(&self->log, KND_TEMP_BUF_SIZE);
    if (err) return err;

    err = kndOutput_new(&self->spec_out, KND_MED_BUF_SIZE);
    if (err) return err;

    err = kndOutput_new(&self->update, KND_LARGE_BUF_SIZE);
    if (err) return err;

    self->del    = del;
    self->str    = str;
    self->reset  = reset;
    self->run    = parse_GSL;
    self->report = report;
    self->parse_iter = parse_iter;

    *task = self;

    return knd_OK;
}
