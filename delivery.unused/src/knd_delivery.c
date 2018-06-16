#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>

#include "knd_config.h"
#include "knd_output.h"
#include "knd_utils.h"
#include "knd_msg.h"
#include "knd_task.h"
#include "knd_delivery.h"

#include <gsl-parser.h>

#define DEBUG_DELIV_LEVEL_0 0
#define DEBUG_DELIV_LEVEL_1 0
#define DEBUG_DELIV_LEVEL_2 0
#define DEBUG_DELIV_LEVEL_3 0
#define DEBUG_DELIV_LEVEL_TMP 1

static void
str(struct kndDelivery *self)
{
    knd_log("<struct kndDelivery at %p>", self);
}

static int
del(struct kndDelivery *self)
{

    free(self);
    return knd_OK;
}


static gsl_err_t run_set_tid(void *obj, const char *tid, size_t tid_size)
{
    struct kndDelivery *self;

    if (!tid_size) return make_gsl_err(gsl_FORMAT);

    self = (struct kndDelivery*)obj;

    if (tid_size >= KND_TID_SIZE) return make_gsl_err(gsl_LIMIT);

    memcpy(self->tid, tid, tid_size);
    self->tid_size = tid_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_set_sid(void *obj, const char *sid, size_t sid_size)
{
    struct kndDelivery *self;

    if (!sid_size) return make_gsl_err(gsl_FORMAT);

    self = (struct kndDelivery*)obj;
    memcpy(self->sid, sid, sid_size);
    self->sid[sid_size] = '\0';
    self->sid_size = sid_size;
    if (DEBUG_DELIV_LEVEL_2)
        knd_log("== sid set to \"%.*s\"", sid_size, sid);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_set_error(void *obj, const char *name, size_t name_size)
{
    struct kndDelivery *self;
    struct kndTID *tid;
    struct kndResult *res = NULL;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);

    self = (struct kndDelivery*)obj;

    if (DEBUG_DELIV_LEVEL_2)
        knd_log(".. set the error: \"%.*s\"", name_size, name);

    /* reset TID count */
    if (self->num_tids >= self->max_tids)
        self->num_tids = 0;

    tid = &self->tids[self->num_tids];
    if (*(tid->tid) != '\0') {
        res = self->idx->get(self->idx, tid->tid, tid->size);
        if (!res) {
            knd_log("-- no rec found for TID \"%.*s\"", tid->size, tid->tid);
            return make_gsl_err(gsl_NO_MATCH);
        }
        /* free result memory */
        if (res->obj) {
            free(res->obj);
            res->obj_size = 0;
        }
        /* remove this key from the idx */
        if (DEBUG_DELIV_LEVEL_TMP)
            knd_log(".. remove TID \"%.*s\" from idx", tid->size, tid->tid);

        self->idx->remove(self->idx, tid->tid, tid->size);
    } else {
        /* alloc result */
        res = malloc(sizeof(struct kndResult));
        if (!res) return make_gsl_err_external(knd_NOMEM);
        memset(res, 0, sizeof(struct kndResult));
    }

    memcpy(tid->tid, self->tid, self->tid_size);
    tid->size = self->tid_size;

    memcpy(res->sid, self->sid, self->sid_size);
    res->sid_size = self->sid_size;

    res->sid_required = true;
    res->obj = self->obj;
    res->obj_size = self->obj_size;

    /* assign key to idx */
    err = self->idx->set(self->idx, tid->tid, tid->size, (void*)res);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_DELIV_LEVEL_2)
        knd_log("== saved error for TID: \"%.*s\" => %s [%lu]",
                tid->size, tid->tid, res->obj, (unsigned long)res->obj_size);

    self->num_tids++;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_set_result(void *obj, const char *name, size_t name_size)
{
    struct kndDelivery *self = obj;
    struct kndTID *tid;
    struct kndResult *res = NULL;
    size_t result_size;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);


    if (self->num_tids >= self->max_tids)
        self->num_tids = 0;

    tid = &self->tids[self->num_tids];
    if (*(tid->tid) != '\0') {
        res = self->idx->get(self->idx, tid->tid, tid->size);
        if (res) {
            /* free result memory */
            if (res->obj) {
                free(res->obj);
                res->obj_size = 0;
            }
            /* remove this key from the idx */
            self->idx->remove(self->idx, tid->tid, tid->size);
        }
    } else {
        /* alloc result */
        res = malloc(sizeof(struct kndResult));
        if (!res) return make_gsl_err_external(knd_NOMEM);
        memset(res, 0, sizeof(struct kndResult));
    }

    memcpy(tid->tid, self->tid, self->tid_size);
    tid->size = self->tid_size;

    memcpy(res->sid, self->sid, self->sid_size);
    res->sid_size = self->sid_size;
    res->sid_required = true;

    res->obj = self->obj;
    res->obj_size = self->obj_size;

    /* assign key to idx */
    err = self->idx->set(self->idx, tid->tid, tid->size, (void*)res);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_DELIV_LEVEL_TMP) {
        result_size = res->obj_size;
        if (result_size > KND_MAX_DEBUG_CONTEXT_SIZE)
            result_size = KND_MAX_DEBUG_CONTEXT_SIZE;

        knd_log("++ cache updated for \"%.*s\" => %.*s [%zu]",
                tid->size, tid->tid, result_size, res->obj, res->obj_size);
    }

    self->num_tids++;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_retrieve(void *obj, const char *tid, size_t tid_size)
{
    struct kndDelivery *self;
    struct kndResult *res = NULL;
    size_t result_size;
    int err;

    if (!tid_size) return make_gsl_err(gsl_FORMAT);

    self = (struct kndDelivery*)obj;

    if (DEBUG_DELIV_LEVEL_2)
        knd_log(".. retrieving obj, tid \"%.*s\"", tid_size, tid);

    res = self->idx->get(self->idx, tid, tid_size);
    if (!res)  {
        if (DEBUG_DELIV_LEVEL_TMP)
            knd_log("-- no result found for tid \"%.*s\"!", tid_size, tid);
        return make_gsl_err(gsl_NO_MATCH);
    }

    if (res->sid_required) {
        if (DEBUG_DELIV_LEVEL_2)
            knd_log("NB: SID is required to retrieve tid \"%.*s\"  SID: \"%.*s\" [%lu] REQUIRED: \"%.*s\"",
                    tid_size, tid, self->sid_size, self->sid, (unsigned long)self->sid_size,
                    res->sid_size, res->sid);

        if (memcmp(self->sid, res->sid, res->sid_size)) {
            err = self->out->write(self->out,
                                   "{\"err\":\"authentication failure: ",
                                   strlen("{\"err\":\"authentication failure: "));
            if (err) return make_gsl_err_external(err);

            err = self->out->write(self->out, self->sid, self->sid_size);
            if (err) return make_gsl_err_external(err);

            err = self->out->write(self->out, " SID rejected\"}", strlen(" SID rejected\"}"));
            if (err) return make_gsl_err_external(err);

            knd_log("AUTH ERROR: \"%.*s\" [%lu]\n",
                    self->out->buf_size,
                    self->out->buf,
                    (unsigned long)self->out->buf_size);

            self->reply_obj = self->out->buf;
            self->reply_obj_size = self->out->buf_size;
            return make_gsl_err(gsl_OK);
        }
    }

    if (DEBUG_DELIV_LEVEL_TMP) {
        result_size = res->obj_size;
        if (result_size > KND_MAX_DEBUG_CONTEXT_SIZE)
            result_size = KND_MAX_DEBUG_CONTEXT_SIZE;
        knd_log("== cached result \"%.*s\" => %.*s [%zu]",
                tid_size, tid, result_size, res->obj, res->obj_size);
    }

    self->reply_obj = res->obj;
    self->reply_obj_size = res->obj_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_auth(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndDelivery *self = (struct kndDelivery*)obj;
    struct gslTaskSpec specs[] = {
        { .name = "sid",
          .name_size = strlen("sid"),
          .run = run_set_sid,
          .obj = self
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) {
        knd_log("-- auth parse error: %d", err.code);
        return err;
    }

    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_retrieve(void *obj,
                                const char *rec,
                                size_t *total_size)
{
    struct kndDelivery *self = (struct kndDelivery*)obj;
    struct gslTaskSpec specs[] = {
        { .name = "tid",
          .name_size = strlen("tid"),
          .run = run_retrieve,
          .obj = self
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) {
        knd_log("-- retrieve func parse error: %d", err.code);
        return err;
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_user(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndDelivery *self = (struct kndDelivery*)obj;
    struct gslTaskSpec specs[] = {
        { .name = "auth",
          .name_size = strlen("auth"),
          .parse = parse_auth,
          .obj = self
        },
        { .name = "err",
          .name_size = strlen("err"),
          .run = run_set_error,
          .obj = self
        },
        { .name = "save",
          .name_size = strlen("save"),
          .run = run_set_result,
          .obj = self
        },
        { .name = "retrieve",
          .name_size = strlen("retrieve"),
          .parse = parse_retrieve,
          .obj = self
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) {
        knd_log("-- delivery task parse error: %d", err.code);
        return err;
    }

    return make_gsl_err(gsl_OK);
}

static int run_task(struct kndDelivery *self)
{
    const char *header_tag = "{task";
    size_t header_tag_size = strlen(header_tag);
    const char *c;

    struct gslTaskSpec specs[] = {
        { .name = "tid",
          .name_size = strlen("tid"),
          .run = run_set_tid,
          .obj = self
        },
        { .name = "user",
          .name_size = strlen("user"),
          .parse = parse_user,
          .obj = self
        }
    };
    gsl_err_t parser_err;

    const char *rec = self->task;
    size_t total_size;

    if (strncmp(rec, header_tag, header_tag_size)) {
        knd_log("-- wrong GSL header");
        return knd_FAIL;
    }

    c = rec + header_tag_size;

    parser_err = gsl_parse_task(c, &total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        //knd_log("-- task parse error: %d", parser_err.code);
        return gsl_err_to_knd_err_codes(parser_err);
    }

    return knd_OK;
}

/**
 *  kndDelivery network service startup
 */
static int
kndDelivery_start(struct kndDelivery *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    void *context;
    void *service;
    time_t timestamp;
    struct tm tm_info;

    const char *header = "DELIVERY";
    size_t header_size = strlen(header);

    const char *reply = "{\"error\":\"delivery error\"}";
    size_t reply_size = strlen(reply);
    int err;

    context = zmq_init(1);

    service = zmq_socket(context, ZMQ_REP);
    assert(service);
    assert((zmq_bind(service, self->addr) == knd_OK));

    knd_log("++ %s is up and running: %s",
            self->name, self->addr);
    self->task = NULL;
    self->task_size = 0;
    self->obj = NULL;
    self->obj_size = 0;

    while (1) {
        if (!self->task) {
            self->task = knd_zmq_recv(service, &self->task_size);
            if (!self->task || !self->task_size) continue;
        }

        if (memcmp(self->task, "{task", strlen("{task"))) {
            self->task = NULL;
            self->task_size = 0;
            continue;
        }

        self->obj = knd_zmq_recv(service, &self->obj_size);
        if (!self->obj || !self->obj_size) {
            self->task = NULL;
            self->task_size = 0;
            self->obj = NULL;
            self->obj_size = 0;
            continue;
        }

	time(&timestamp);
	localtime_r(&timestamp, &tm_info);
	buf_size = strftime(buf, KND_NAME_SIZE,
                            "%Y-%m-%d %H:%M:%S", &tm_info);

        knd_log("%s: \"%.*s\"",
                buf, self->task_size, self->task);

        self->out->reset(self->out);
        self->reply_obj = NULL;
        self->reply_obj_size = 0;
        self->tid[0] = '\0';
        self->sid[0] = '\0';

        err = run_task(self);

        if (self->reply_obj_size) {
            reply = self->reply_obj;
            reply_size = self->reply_obj_size;
        }

        knd_zmq_sendmore(service, header, header_size);
        knd_zmq_send(service, reply, reply_size);

        if (self->task) {
            free(self->task);
            self->task = NULL;
            self->task_size = 0;
        }
        /* TODO: free obj if it was not set to index */
        if (self->obj) {
            self->obj = NULL;
            self->obj_size = 0;
        }

	reply = "{\"error\":\"delivery error\"}";
	reply_size = strlen(reply);
    }

    /* we never get here */
    zmq_close(service);
    zmq_term(context);

    return knd_OK;
}



static gsl_err_t
run_set_db_path(void *obj, const char *path, size_t path_size)
{
    struct kndDelivery *self;

    if (!path_size) return make_gsl_err(gsl_FORMAT);

    self = (struct kndDelivery*)obj;

    if (DEBUG_DELIV_LEVEL_1)
        knd_log(".. set DB path to \"%.*s\"", path_size, path);

    memcpy(self->path, path, path_size);
    self->path[path_size] = '\0';
    self->path_size = path_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t
run_set_service_addr(void *obj, const char *addr, size_t addr_size)
{
    struct kndDelivery *self;

    if (!addr_size) return make_gsl_err(gsl_FORMAT);

    self = (struct kndDelivery*)obj;

    if (DEBUG_DELIV_LEVEL_1)
        knd_log(".. set service addr to \"%.*s\"", addr_size, addr);

    memcpy(self->addr, addr, addr_size);
    self->addr[addr_size] = '\0';
    self->addr_size = addr_size;


    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_check_schema(void *obj, const char *val, size_t val_size)
{
    const char *schema_name = "Delivery Service Configuration";
    size_t schema_name_size = strlen(schema_name);

    if (val_size != schema_name_size)  return make_gsl_err(gsl_FAIL);
    if (memcmp(schema_name, val, val_size)) return make_gsl_err(gsl_FAIL);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_config(void *obj,
			      const char *rec,
			      size_t *total_size)
{
    struct kndDelivery *self = obj;
    struct gslTaskSpec specs[] = {
	{ .is_implied = true,
          .run = run_check_schema,
          .obj = self
        },
	{ .name = "service",
          .name_size = strlen("service"),
          .run = run_set_service_addr,
          .obj = self
        },
        { .name = "path",
          .name_size = strlen("path"),
          .run = run_set_db_path,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int parse_schema(struct kndDelivery *self,
			const char *rec,
			size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        { .name = "schema",
          .name_size = strlen("schema"),
          .parse = parse_config,
          .obj = self
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}


static int
kndDelivery_init(struct kndDelivery *self)
{

    self->str = str;
    self->del = del;

    self->start = kndDelivery_start;

    return knd_OK;
}

extern int
kndDelivery_new(struct kndDelivery **deliv,
                const char          *config)
{
    struct kndDelivery *self;
    struct kndAuthRec *rec;
    size_t chunk_size = 0;
    int err;

    self = malloc(sizeof(struct kndDelivery));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndDelivery));



    err = ooDict_new(&self->auth_idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&self->sid_idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&self->uid_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&self->idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    err = kndOutput_new(&self->out, KND_IDX_BUF_SIZE);
    if (err) return err;

    /* special user */
    err = kndUser_new(&self->admin);
    if (err) return err;
    self->admin->out = self->out;

    /* create DEFAULT user record */
    rec = malloc(sizeof(struct kndAuthRec));
    if (!rec) {
        err = knd_NOMEM;
        goto error;
    }

    memset(rec, 0, sizeof(struct kndAuthRec));
    memcpy(rec->uid, "000", KND_ID_SIZE);

    err = ooDict_new(&rec->cache, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    self->default_rec = rec;

    err = ooDict_new(&rec->cache, KND_LARGE_DICT_SIZE);
    if (err) goto error;
    self->spec_rec = rec;

    kndDelivery_init(self);

    /*err = kndMonitor_new(&self->monitor);
    if (err) {
        fprintf(stderr, "Couldn\'t load kndMonitor... ");
        return -1;
    }
    self->monitor->out = self->out;
    */

    err = self->out->read_file(self->out, config, strlen(config));
    if (err) return err;

    knd_log("Delivery CONFIG:\"%s\"", self->out->file);
    err = parse_schema(self, self->out->file, &chunk_size);
    if (err) goto error;

    if (self->path_size) {
        err = knd_mkpath(self->path, self->path_size, 0777, false);               RET_ERR();
    }

    if (!self->max_tids)
        self->max_tids = KND_MAX_TIDS;

    self->tids = malloc(sizeof(struct kndTID) * self->max_tids);
    if (!self->tids) return knd_NOMEM;
    memset(self->tids, 0, sizeof(struct kndTID) * self->max_tids);

    *deliv = self;

    return knd_OK;

 error:

    del(self);

    return err;
}