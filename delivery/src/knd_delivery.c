#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>

#include <knd_config.h>
#include <knd_output.h>
#include <knd_msg.h>
#include <knd_task.h>

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

    if (DEBUG_DELIV_LEVEL_TMP)
        knd_log(".. set tid to \"%.*s\"", tid_size, tid);

    memcpy(self->tid, tid, tid_size);
    self->tid[tid_size] = '\0';
    self->tid_size = tid_size;
   
    return knd_OK;
}


static int
run_set_sid(void *obj,
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

    if (DEBUG_DELIV_LEVEL_TMP)
        knd_log(".. set sid to \"%.*s\"", sid_size, sid);

    memcpy(self->sid, sid, sid_size);
    self->sid[sid_size] = '\0';
    self->sid_size = sid_size;
   
    return knd_OK;
}

static int 
parse_result(void *self,
             const char *rec,
             size_t *total_size)
{
    struct kndTaskSpec specs[] = {
        { .name = "tid",
          .name_size = strlen("tid"),
          .run = run_set_tid,
          .obj = self
        }
    };
    int err;
    
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}


static int 
parse_retrieve(void *self,
               const char *rec,
               size_t *total_size)
{
    struct kndTaskSpec specs[] = {
        { .name = "format",
          .name_size = strlen("format"),
          .run = run_set_tid,
          .obj = self
        }
    };
    int err;
    
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}


static int 
run_task(struct kndDelivery *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = KND_NAME_SIZE;
    size_t chunk_size = 0;
    
    const char *gsl_format_tag = "{gsl";
    size_t gsl_format_tag_size = strlen(gsl_format_tag);

    const char *header_tag = "{knd::Task";
    size_t header_tag_size = strlen(header_tag);
    const char *c;
    
    struct kndTaskSpec specs[] = {
        { .name = "tid",
          .name_size = strlen("tid"),
          .run = run_set_tid,
          .obj = self
        },
        { .name = "sid",
          .name_size = strlen("sid"),
          .run = run_set_sid,
          .obj = self
        },
        /*{ .name = "result",
          .name_size = strlen("result"),
          .parse = parse_result,
          .obj = self
          },*/
        { .name = "retrieve",
          .name_size = strlen("retrieve"),
          .parse = parse_retrieve,
          .obj = self
        }
    };
    int err;

    const char *rec = self->task;
    size_t total_size;
    
    if (!strncmp(rec, gsl_format_tag, gsl_format_tag_size)) {
        rec += gsl_format_tag_size;
    
        err = knd_get_schema_name(rec,
                                  buf, &buf_size, &chunk_size);
        if (!err) {
            rec += chunk_size;
            if (DEBUG_DELIV_LEVEL_TMP)
                knd_log("== got schema: \"%s\"", buf);
        }
    }
    
    if (strncmp(rec, header_tag, header_tag_size)) {
        knd_log("-- wrong GSL class header");
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

    const char *reply = "OK";
    size_t reply_size = strlen(reply);

    context = zmq_init(1);

    service = zmq_socket(context, ZMQ_REP);
    assert(service);
    assert((zmq_bind(service, self->addr) == knd_OK));

    knd_log("\n\n    ++ %s is up and running: %s\n",
            self->name, self->addr);

    while (1) {
        knd_log("\n    ++ DELIVERY service is waiting for new tasks...\n");

        self->task = knd_zmq_recv(service, &self->task_size);
        self->obj = knd_zmq_recv(service, &self->obj_size);

	knd_log("    ++ DELIVERY service has got a task:   \"%s\"",
                self->task);
        
        err = run_task(self);
        knd_log("== err: %d", err);
        
        knd_zmq_sendmore(service, header, header_size);
	knd_zmq_send(service, reply, reply_size);

        
        fflush(stdout);
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

    if (DEBUG_DELIV_LEVEL_TMP)
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

    if (DEBUG_DELIV_LEVEL_TMP)
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
            if (DEBUG_DELIV_LEVEL_TMP)
                knd_log("== got schema: \"%s\"", buf);
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


    /* TODO: tid allocation ring */

    err = ooDict_new(&self->auth_idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&self->sid_idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&self->uid_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&self->repo_idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

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
    
    /* output buffer */
    err = kndOutput_new(&self->out, KND_LARGE_BUF_SIZE);
    if (err) return err;

    kndDelivery_init(self); 

    err = kndMonitor_new(&self->monitor);
    if (err) {
        fprintf(stderr, "Couldn\'t load kndMonitor... ");
        return -1;
    }
    self->monitor->out = self->out;

    err = self->out->read_file(self->out, config, strlen(config));
    if (err) return err;
    
    err = parse_config_GSL(self, self->out->file, &chunk_size);
    if (err) goto error;

    if (self->path_size) {
        err = knd_mkpath(self->path, 0777, false);
        if (err) return err;
    }
    
    *deliv = self;

    return knd_OK;

 error:

    del(self);

    return err;
}
