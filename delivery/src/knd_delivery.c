#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>

#include "knd_config.h"
#include "knd_output.h"
#include "knd_utils.h"
#include "knd_msg.h"
#include "knd_task.h"
#include "knd_parser.h"

#include "knd_delivery.h"

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


static int
run_set_tid(void *obj,
            struct kndTaskArg *args, size_t num_args)
{
    struct kndDelivery *self;
    struct kndTaskArg *arg;
    const char *tid = NULL;
    size_t tid_size = 0;
    
    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "tid", strlen("tid"))) {
            tid = arg->val;
            tid_size = arg->val_size;
        }
    }

    if (!tid_size) return knd_FAIL;

    self = (struct kndDelivery*)obj;

    if (tid_size >= KND_TID_SIZE) return knd_LIMIT;

    memcpy(self->tid, tid, tid_size);
    self->tid[tid_size] = '\0';
    self->tid_size = tid_size;
   
    return knd_OK;
}



static int run_set_sid(void *obj,
                       struct kndTaskArg *args, size_t num_args)
{
    struct kndDelivery *self;
    struct kndTaskArg *arg;
    const char *sid = NULL;
    size_t sid_size = 0;
    
    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "sid", strlen("sid"))) {
            sid = arg->val;
            sid_size = arg->val_size;
        }
    }

    if (!sid_size) return knd_FAIL;

    self = (struct kndDelivery*)obj;

    if (DEBUG_DELIV_LEVEL_1)
        knd_log(".. set sid to \"%.*s\"", sid_size, sid);

    memcpy(self->sid, sid, sid_size);
    self->sid[sid_size] = '\0';
    self->sid_size = sid_size;
   
    return knd_OK;
}

static int run_set_result(void *obj,
                          struct kndTaskArg *args, size_t num_args)
{
    struct kndDelivery *self;
    struct kndTaskArg *arg;
    struct kndTID *tid;
    struct kndResult *res = NULL;
    const char *name = NULL;
    size_t name_size = 0;
    int err;
    
    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "save", strlen("save"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }

    if (!name_size) return knd_FAIL;

    self = (struct kndDelivery*)obj;

    if (DEBUG_DELIV_LEVEL_1)
        knd_log(".. set result of \"%.*s\"", self->obj_size, self->obj);

    if (self->num_tids >= self->max_tids)
        self->num_tids = 0;

    tid = &self->tids[self->num_tids];
    if (*(tid->tid) != '\0') {
        res = self->idx->get(self->idx, tid->tid);
        if (!res) return knd_NO_MATCH;

        /* free result memory */
        if (res->obj) {
            free(res->obj);
            res->obj_size = 0;
        }
        
        /* remove this key from the idx */
        self->idx->remove(self->idx, tid->tid);
    } else {
        /* alloc result */
        res = malloc(sizeof(struct kndResult));
        if (!res) return knd_NOMEM;
        memset(res, 0, sizeof(struct kndResult));
    }
    
    memcpy(tid->tid, self->tid, self->tid_size); 
    tid->tid[self->tid_size] = '\0';
    
    res->obj = self->obj;
    res->obj_size = self->obj_size;
    
    /* assign key to idx */

    err = self->idx->set(self->idx, tid->tid, (void*)res);
    if (err) return err;
    
    if (DEBUG_DELIV_LEVEL_TMP)
        knd_log("== result %s => %s [%lu]",
                tid->tid, res->obj, (unsigned long)res->obj_size);

    self->num_tids++;
    return knd_OK;
}

static int  run_retrieve(void *obj,
                         struct kndTaskArg *args, size_t num_args)
{
    struct kndDelivery *self;
    struct kndTaskArg *arg;
    struct kndResult *res = NULL;
    const char *name = NULL;
    size_t name_size = 0;
    
    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "retrieve", strlen("retrieve"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }

    if (!name_size) return knd_FAIL;

    self = (struct kndDelivery*)obj;
    
    if (DEBUG_DELIV_LEVEL_1)
        knd_log(".. retrieve obj,  tid \"%.*s\"", self->tid_size, self->tid);

    res = self->idx->get(self->idx, self->tid);
    if (!res) return knd_NO_MATCH;

    self->reply_obj = res->obj;
    self->reply_obj_size = res->obj_size;

    return knd_OK;
}



static int parse_auth(void *obj,
                      const char *rec,
                      size_t *total_size)
{
    struct kndDelivery *self = (struct kndDelivery*)obj;
    struct kndTaskSpec specs[] = {
        { .name = "sid",
          .name_size = strlen("sid"),
          .run = run_set_sid,
          .obj = self
        }
    };
    int err;
    
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) {
        knd_log("-- auth parse error: %d", err);
        return err;
    }
    
    return knd_OK;
}


static int parse_user(void *obj,
                      const char *rec,
                      size_t *total_size)
{
    struct kndDelivery *self = (struct kndDelivery*)obj;
    struct kndTaskSpec specs[] = {
        { .name = "auth",
          .name_size = strlen("auth"),
          .parse = parse_auth,
          .obj = self
        },
        { .name = "save",
          .name_size = strlen("save"),
          .run = run_set_result,
          .obj = self
        },
        { .name = "retrieve",
          .name_size = strlen("retrieve"),
          .run = run_retrieve,
          .obj = self
        }
    };
    int err;
    
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) {
        knd_log("-- task parse error: %d", err);
        return err;
    }
    
    return knd_OK;
}


static int run_task(struct kndDelivery *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = KND_NAME_SIZE;
    size_t chunk_size = 0;

    const char *header_tag = "{task";
    size_t header_tag_size = strlen(header_tag);
    const char *c;
    
    struct kndTaskSpec specs[] = {
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
    int err;

    const char *rec = self->task;
    size_t total_size;
    
    if (strncmp(rec, header_tag, header_tag_size)) {
        knd_log("-- wrong GSL header");
        return knd_FAIL;
    }

    c = rec + header_tag_size;
    
    err = knd_parse_task(c, &total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) {
        knd_log("-- task parse error: %d", err);
        return err;
    }
    
    return knd_OK;
}

/**
 *  kndDelivery network service startup
 */
static int 
kndDelivery_start(struct kndDelivery *self)
{
    void *context;
    void *service;
    int err;

    const char *header = "DELIVERY";
    size_t header_size = strlen(header);

    const char *reply = "{\"error\":\"delivery error\"}";
    size_t reply_size = strlen(reply);

    context = zmq_init(1);

    service = zmq_socket(context, ZMQ_REP);
    assert(service);
    assert((zmq_bind(service, self->addr) == knd_OK));

    knd_log("++ %s is up and running: %s",
            self->name, self->addr);

    while (1) {
        knd_log("++ DELIVERY service is waiting for new tasks...");

        self->reply_obj = NULL;
        self->reply_obj_size = 0;
        
        self->task = knd_zmq_recv(service, &self->task_size);
        self->obj = knd_zmq_recv(service, &self->obj_size);

	knd_log("++ DELIVERY service has got a task:   \"%s\"",
                self->task);
        
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
        /*if (self->obj) {
            self->obj = NULL;
            self->obj_size = 0;
        }*/
        
    }

    /* we never get here */
    zmq_close(service);
    zmq_term(context);

    return knd_OK;
}



static int
run_set_db_path(void *obj,
                struct kndTaskArg *args, size_t num_args)
{
    struct kndDelivery *self;
    struct kndTaskArg *arg;
    const char *path = NULL;
    size_t path_size = 0;
    
    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "path", strlen("path"))) {
            path = arg->val;
            path_size = arg->val_size;
        }
    }

    if (!path_size) return knd_FAIL;

    self = (struct kndDelivery*)obj;

    if (DEBUG_DELIV_LEVEL_1)
        knd_log(".. set DB path to \"%.*s\"", path_size, path);

    memcpy(self->path, path, path_size);
    self->path[path_size] = '\0';
    self->path_size = path_size;
   
    return knd_OK;
}

static int
run_set_service_addr(void *obj,
                     struct kndTaskArg *args, size_t num_args)
{
    struct kndDelivery *self;
    struct kndTaskArg *arg;
    const char *addr = NULL;
    size_t addr_size = 0;
    
    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "service", strlen("service"))) {
            addr = arg->val;
            addr_size = arg->val_size;
        }
    }

    if (!addr_size) return knd_FAIL;

    self = (struct kndDelivery*)obj;

    if (DEBUG_DELIV_LEVEL_1)
        knd_log(".. set service addr to \"%.*s\"", addr_size, addr);

    memcpy(self->addr, addr, addr_size);
    self->addr[addr_size] = '\0';
    self->addr_size = addr_size;

    
    return knd_OK;
}

static int
parse_config_GSL(struct kndDelivery *self,
                 const char *rec,
                 size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = KND_NAME_SIZE;
    size_t chunk_size = 0;
    
    const char *gsl_format_tag = "{gsl";
    size_t gsl_format_tag_size = strlen(gsl_format_tag);

    const char *header_tag = "{knd::Delivery Service Configuration";
    size_t header_tag_size = strlen(header_tag);
    const char *c;
    
    struct kndTaskSpec specs[] = {
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
    int err;

    if (!strncmp(rec, gsl_format_tag, gsl_format_tag_size)) {
        rec += gsl_format_tag_size;

        err = knd_get_schema_name(rec,
                                  buf, &buf_size, &chunk_size);
        if (!err) {
            rec += chunk_size;
        }
    }
    
    if (strncmp(rec, header_tag, header_tag_size)) {
        knd_log("-- wrong GSL class header");
        return err;
    }
    c = rec + header_tag_size;
    
    err = knd_parse_task(c, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) {
        knd_log("-- config parse error: %d", err);
        return err;
    }
    
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
    
    err = parse_config_GSL(self, self->out->file, &chunk_size);
    if (err) goto error;

    if (self->path_size) {
        err = knd_mkpath(self->path, 0777, false);
        if (err) return err;
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
