#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include <time.h>

#include "knd_config.h"
#include "knd_mempool.h"
#include "knd_state.h"
#include "knd_dict.h"
#include "knd_utils.h"
#include "knd_msg.h"
#include "knd_output.h"
#include "knd_task.h"
#include "knd_attr.h"
#include "knd_user.h"
#include "knd_parser.h"
#include "knd_concept.h"
#include "knd_object.h"
#include "knd_rel.h"
#include "knd_proc.h"

#include "knd_learner.h"

#define DEBUG_LEARNER_LEVEL_1 0
#define DEBUG_LEARNER_LEVEL_2 0
#define DEBUG_LEARNER_LEVEL_3 0
#define DEBUG_LEARNER_LEVEL_TMP 1

static void
kndLearner_del(struct kndLearner *self)
{
    /*if (self->path) free(self->path);*/

    /* TODO: storage */
    free(self);
}


static int
kndLearner_start(struct kndLearner *self)
{
    void *context;
    void *outbox;
    char *task = NULL;
    size_t task_size = 0;
    size_t chunk_size = 0;
    char *obj = NULL;
    size_t obj_size = 0;

    time_t  t0, t1;
    clock_t c0, c1;

    int err;

    // restore in-memory data after failure or restart
    self->admin->role = KND_USER_ROLE_LEARNER;

    //err = self->admin->restore(self->admin);
    //if (err) return err;

    context = zmq_init(1);
    if (!context) {
        knd_log("zmq_init() failed, error: '%s'", strerror(errno));
        return knd_FAIL;
    }

    knd_log("LEARNER listener: %s\n", self->inbox_backend_addr);

    // get messages from outbox
    outbox = zmq_socket(context, ZMQ_PULL);
    if (!outbox) {
        knd_log("zmq_socket(outbox) failed, error: '%s'", strerror(errno));
        return knd_FAIL;
    }

    err = zmq_connect(outbox, self->inbox_backend_addr);
    if (err == -1) {
        knd_log("zmq_connect(outbox) failed, error: '%s'", strerror(errno));
        return knd_FAIL;
    }

    self->delivery = zmq_socket(context, ZMQ_REQ);
    if (!self->delivery) {
        knd_log("zmq_socket(delivery) failed, error: '%s'", strerror(errno));
        return knd_FAIL;
    }

    if (DEBUG_LEARNER_LEVEL_TMP)
        knd_log(".. establish delivery connection: %s..", self->delivery_addr);

    err = zmq_connect(self->delivery, self->delivery_addr);
    if (err == -1) {
        knd_log("zmq_connect(delivery) failed, error: '%s'", strerror(errno));
        return knd_FAIL;
    }

    self->task->delivery = self->delivery;

    // publisher
    self->publisher = zmq_socket(context, ZMQ_PUB);
    if (!self->publisher) {
        knd_log("zmq_socket(publisher) failed, error: '%s'", strerror(errno));
        return knd_FAIL;
    }

    err = zmq_connect(self->publisher, self->publish_proxy_frontend_addr);
    if (err == -1) {
        knd_log("zmq_connect(publisher) failed, error: '%s'", strerror(errno));
        return knd_FAIL;
    }
    self->task->publisher = self->publisher;

    while (1) {
        self->task->reset(self->task);

        task = knd_zmq_recv(outbox, &task_size);
        obj = knd_zmq_recv(outbox, &obj_size);

        t0 = time(NULL);
        c0 = clock();

        if (DEBUG_LEARNER_LEVEL_TMP) {
            chunk_size = task_size > KND_MAX_DEBUG_CHUNK_SIZE ? KND_MAX_DEBUG_CHUNK_SIZE : task_size;
            knd_log("\n++ Learner got a new task: \"%.*s\".. [size: %lu]", chunk_size, task, (unsigned long)task_size);
        }
        
        err = self->task->run(self->task,
                              task, task_size,
                              obj, obj_size);
        if (DEBUG_LEARNER_LEVEL_TMP) {
            if (!strcmp(obj, "TPS TEST")) {
                t1 = time(NULL);
                c1 = clock();

                printf ("\telapsed wall clock time: %ld\n", (long)  (t1 - t0));
                printf ("\telapsed CPU time:        %f\n",  (float) (c1 - c0)/CLOCKS_PER_SEC);
                knd_log("\tTOTAL objs imported: %lu", (unsigned long)self->admin->root_class->num_objs);
            }
            if (!strcmp(obj, "SYNC TEST")) {
                t1 = time(NULL);
                c1 = clock();
                printf ("\telapsed wall clock time: %ld\n", (long)  (t1 - t0));
                printf ("\telapsed CPU time:        %f\n",  (float) (c1 - c0)/CLOCKS_PER_SEC);
            }
        }

        if (err) {
            self->task->error = err;
            knd_log("-- task running failure: %d", err);
            goto final;
        }

    final:

        if (!self->task->tid_size) {
            self->task->tid[0] = '0';
            self->task->tid_size = 1;
        }
            
        err = self->task->report(self->task);
        if (err) {
            knd_log("-- task report failed: %d", err);
        }

        /*if (task) free(task);
          if (obj) free(obj);*/
    }

    /* we should never get here */
    return knd_OK;
}

static int parse_publisher_service_addr(void *obj,
                                        const char *rec,
                                        size_t *total_size)
{
    struct kndLearner *self = (struct kndLearner*)obj;
    struct kndTaskSpec specs[] = {
        { .name = "frontend",
          .name_size = strlen("frontend"),
          .buf = self->publish_proxy_frontend_addr,
          .buf_size = &self->publish_proxy_frontend_addr_size,
          .max_buf_size = KND_NAME_SIZE
        },
        { .name = "backend",
          .name_size = strlen("backend"),
          .buf = self->publish_proxy_backend_addr,
          .buf_size = &self->publish_proxy_backend_addr_size,
          .max_buf_size = KND_NAME_SIZE
        }
    };
    int err;

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}

static int
parse_write_inbox_addr(void *obj,
                       const char *rec,
                       size_t *total_size)
{
    struct kndLearner *self = (struct kndLearner*)obj;


    struct kndTaskSpec specs[] = {
        { .name = "frontend",
          .name_size = strlen("frontend"),
          .buf = self->inbox_frontend_addr,
          .buf_size = &self->inbox_frontend_addr_size,
          .max_buf_size = KND_NAME_SIZE       
        },
        { .name = "backend",
          .name_size = strlen("backend"),
          .buf = self->inbox_backend_addr,
          .buf_size = &self->inbox_backend_addr_size,
          .max_buf_size = KND_NAME_SIZE       
        }
    };
    int err;
    
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}

static int parse_memory_settings(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct kndMemPool *self = obj;
    struct kndTaskSpec specs[] = {
        { .name = "max_users",
          .name_size = strlen("max_users"),
          .parse = knd_parse_size_t,
          .obj = &self->max_users
        },
        { .name = "max_classes",
          .name_size = strlen("max_classes"),
          .parse = knd_parse_size_t,
          .obj = &self->max_classes
        },
        { .name = "max_objs",
          .name_size = strlen("max_objs"),
          .parse = knd_parse_size_t,
          .obj = &self->max_objs
        },
        { .name = "max_elems",
          .name_size = strlen("max_elems"),
          .parse = knd_parse_size_t,
          .obj = &self->max_elems
        },
        { .name = "max_rels",
          .name_size = strlen("max_rels"),
          .parse = knd_parse_size_t,
          .obj = &self->max_rels
        },
        { .name = "max_rel_instances",
          .name_size = strlen("max_rel_instances"),
          .parse = knd_parse_size_t,
          .obj = &self->max_rel_insts
        },
        { .name = "max_procs",
          .name_size = strlen("max_procs"),
          .parse = knd_parse_size_t,
          .obj = &self->max_procs
        },
        { .name = "max_proc_instances",
          .name_size = strlen("max_proc_instances"),
          .parse = knd_parse_size_t,
          .obj = &self->max_proc_insts
        }
    };
    int err;

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}

static int
parse_config_GSL(struct kndLearner *self,
                 const char *rec,
                 size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = KND_NAME_SIZE;
    size_t chunk_size = 0;
    
    const char *gsl_format_tag = "{gsl";
    size_t gsl_format_tag_size = strlen(gsl_format_tag);

    const char *header_tag = "{knd::Knowdy Learner Service Configuration";
    size_t header_tag_size = strlen(header_tag);
    const char *c;
    
    struct kndTaskSpec specs[] = {
         { .name = "path",
           .name_size = strlen("path"),
           .buf = self->path,
           .buf_size = &self->path_size,
           .max_buf_size = KND_NAME_SIZE
         },
         { .name = "schemas",
           .name_size = strlen("schemas"),
           .buf = self->schema_path,
           .buf_size = &self->schema_path_size,
           .max_buf_size = KND_NAME_SIZE
         },
         { .name = "sid",
           .name_size = strlen("sid"),
           .buf = self->admin->sid,
           .buf_size = &self->admin->sid_size,
           .max_buf_size = KND_NAME_SIZE
         },
         { .name = "memory",
           .name_size = strlen("memory"),
           .parse = parse_memory_settings,
           .obj = self->mempool,
         },
         { .name = "delivery",
           .name_size = strlen("delivery"),
           .buf = self->delivery_addr,
           .buf_size = &self->delivery_addr_size,
           .max_buf_size = KND_NAME_SIZE
         },
        { .name = "write_inbox",
          .name_size = strlen("write_inbox"),
          .parse = parse_write_inbox_addr,
          .obj = self
        },
        { .name = "publish",
          .name_size = strlen("publish"),
          .parse = parse_publisher_service_addr,
          .obj = self
        },
        { .name = "agent",
          .name_size = strlen("agent"),
          .buf = self->name,
          .buf_size = &self->name_size,
          .max_buf_size = KND_NAME_SIZE
        }

    };
    int err = knd_FAIL;

    if (!strncmp(rec, gsl_format_tag, gsl_format_tag_size)) {
        rec += gsl_format_tag_size;
        err = knd_get_schema_name(rec,
                                  buf, &buf_size, &chunk_size);
        if (!err) {
            rec += chunk_size;
            if (DEBUG_LEARNER_LEVEL_TMP)
                knd_log("== got schema: \"%.*s\"", buf_size, buf);
        }
    }
    
    if (strncmp(rec, header_tag, header_tag_size)) {
        knd_log("-- wrong GSL class header");
        return knd_FAIL;
    }
    c = rec + header_tag_size;
    
    err = knd_parse_task(c, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) {
        knd_log("-- config parse error: %d", err);
        return err;
    }

    if (!self->path_size) {
        knd_log("-- DB path not set :(");
        return knd_FAIL;
    }
    
    if (!self->schema_path_size) {
        knd_log("-- schema path not set :(");
        return knd_FAIL;
    }

    if (!self->inbox_frontend_addr_size) {
        knd_log("-- inbox frontend addr not set :(");
        return knd_FAIL;
    }
    if (!self->inbox_backend_addr_size) {
        knd_log("-- inbox backend addr not set :(");
        return knd_FAIL;
    }

    if (!self->admin->sid_size) {
        knd_log("-- administrative SID is not set :(");
        return knd_FAIL;
    }
    memcpy(self->admin->id, "000", strlen("000"));

    /* users path */
    self->path[self->path_size] = '\0';
    self->admin->dbpath = self->path;
    self->admin->dbpath_size = self->path_size;

    memcpy(self->admin->path, self->path, self->path_size);
    memcpy(self->admin->path + self->path_size, "/users", strlen("/users"));
    self->admin->path_size = self->path_size + strlen("/users");
    self->admin->path[self->admin->path_size] = '\0';
    
    return knd_OK;
}

extern int
kndLearner_new(struct kndLearner **rec,
               const char *config)
{
    struct kndLearner *self;
    struct kndConcept *conc;
    struct kndOutput *out;
    size_t chunk_size;
    int err;

    self = calloc(1, sizeof(struct kndLearner));
    if (!self) return knd_NOMEM;

    err = kndOutput_new(&self->out, KND_IDX_BUF_SIZE);
    if (err) goto error;

    /* task specification */
    err = kndTask_new(&self->task);
    if (err) goto error;

    err = kndOutput_new(&self->task->out, KND_IDX_BUF_SIZE);
    if (err) goto error;

    err = kndOutput_new(&self->log, KND_MED_BUF_SIZE);
    if (err) goto error;

    /* special user */
    err = kndUser_new(&self->admin);
    if (err) goto error;
    self->task->admin = self->admin;
    self->admin->out = self->out;


    err = kndMemPool_new(&self->mempool);
    if (err) return err;
    
    /* read config */
    err = self->out->read_file(self->out, config, strlen(config));
    if (err) goto error;

    err = parse_config_GSL(self, self->out->file, &chunk_size);
    if (err) goto error;

    self->mempool->alloc(self->mempool);

    err = kndStateControl_new(&self->task->state_ctrl);
    if (err) return err;
    self->task->state_ctrl->max_updates = self->mempool->max_updates;
    self->task->state_ctrl->updates = self->mempool->update_idx;

    memcpy(self->task->agent_name, self->name, self->name_size);
    self->task->agent_name_size = self->name_size;
    self->task->agent_name[self->name_size] = '\0';

    out = self->out;
    out->reset(out);
    err = out->write(out, self->path, self->path_size);
    if (err) return err;
    err = out->write(out, "/frozen.gsp", strlen("/frozen.gsp"));
    if (err) return err;
    memcpy(self->admin->frozen_output_file_name, out->buf, out->buf_size);
    self->admin->frozen_output_file_name_size = out->buf_size;
    self->admin->frozen_output_file_name[out->buf_size] = '\0';

    err = kndConcept_new(&conc);
    if (err) goto error;

    conc->out = self->out;
    conc->log = self->log;
    conc->name[0] = '/';
    conc->name_size = 1;

    conc->dbpath = self->schema_path;
    conc->dbpath_size = self->schema_path_size;
    conc->frozen_output_file_name = self->admin->frozen_output_file_name;
    conc->frozen_output_file_name_size = self->admin->frozen_output_file_name_size;

    conc->dir = malloc(sizeof(struct kndConcDir));
    if (!conc->dir) return knd_NOMEM;
    memset(conc->dir, 0, sizeof(struct kndConcDir));
    memset(conc->dir->name, '0', KND_ID_SIZE);
    conc->dir->name_size = KND_ID_SIZE;
    conc->dir->conc = conc;

    conc->mempool = self->mempool;

    err = kndProc_new(&conc->proc);
    if (err) goto error;
    conc->proc->mempool = self->mempool;
    
    err = kndRel_new(&conc->rel);
    if (err) goto error;
    conc->rel->mempool = self->mempool;

    /* specific allocations of the root concs */
    err = ooDict_new(&conc->class_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&conc->proc->proc_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;
    conc->proc->class_idx = conc->class_idx;

    err = ooDict_new(&conc->rel->rel_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;
    conc->rel->class_idx = conc->class_idx;

    /* user idx */
    if (self->mempool->max_users) {
        knd_log("MAX USERS: %zu", self->mempool->max_users);
        self->max_users = self->mempool->max_users;
        self->admin->user_idx = calloc(self->max_users, 
                                       sizeof(struct kndObject*));
        if (!self->admin->user_idx) return knd_NOMEM;
        self->admin->max_users = self->max_users;
    }

    /* try opening the frozen DB */
    conc->user = self->admin;
    self->admin->root_class = conc;

    err = conc->open(conc);
    if (err) {
        if (err != knd_NO_MATCH) goto error;
        /* read class definitions */
        knd_log("-- no frozen DB found, reading schemas..");
        conc->dbpath = self->schema_path;
        conc->dbpath_size = self->schema_path_size;
        conc->batch_mode = true;
        err = conc->load(conc, "index", strlen("index"));
        if (err) {
            knd_log("-- couldn't read any schema definitions :(");
            goto error;
        }
        err = conc->coordinate(conc);
        if (err) goto error;
        conc->batch_mode = false;
    }

    conc->dbpath = self->path;
    conc->dbpath_size = self->path_size;

    /* read any existing updates to the frozen DB (failure recovery) */
    /*err = conc->restore(conc);
    if (err) return err;
    */
    /* test
    err = dc->build_diff(dc, "0001");
    if (err) return err;
    */

    self->admin->root_class = conc;

    self->del = kndLearner_del;
    self->start = kndLearner_start;

    *rec = self;

    return knd_OK;

 error:

    kndLearner_del(self);
    return err;
}


/** SERVICES */

void *kndLearner_inbox(void *arg)
{
    void *context;
    void *frontend;
    void *backend;
    struct kndLearner *learner;
    int err;

    learner = (struct kndLearner *) arg;

    context = zmq_init(1);
    if (!context) {
        knd_log("zmq_init() failed, error: '%s'", strerror(errno));
        return NULL; // todo: set error
    }

    frontend = zmq_socket(context, ZMQ_PULL);
    if (!frontend) {
        knd_log("zmq_socket(inbox frontend) failed, error: '%s'", strerror(errno));
        return NULL; // todo: set error
    }

    backend = zmq_socket(context, ZMQ_PUSH);
    if (!backend) {
        knd_log("zmq_socket(inbox backend) failed, error: '%s'", strerror(errno));
        return NULL; // todo: set error
    }

    knd_log("%s <-> %s\n", learner->inbox_frontend_addr, learner->inbox_backend_addr);

    err = zmq_bind(frontend, learner->inbox_frontend_addr);
    if (err == -1) {
        knd_log("zmq_bind(frontend) failed, error: '%s'", strerror(errno));
        return NULL; // todo: set error
    }

    err = zmq_bind(backend, learner->inbox_backend_addr);
    if (err == -1) {
        knd_log("zmq_bind(backend) failed, error: '%s'", strerror(errno));
        return NULL; // todo: set error
    }

    knd_log("    ++ Learner \"%s\" Queue device is ready...\n\n", learner->name);

    err = zmq_device(ZMQ_QUEUE, frontend, backend);
    if (err == -1) {
        knd_log("zmq_device() failed, error: '%s'", strerror(errno));
        return NULL;
    }

    /* we never get here */
    zmq_close(frontend);
    zmq_close(backend);
    zmq_term(context);

    return NULL;
}

void *kndLearner_selector(void *arg)
{
    void *context;
    void *frontend;
    void *backend;
    struct kndLearner *learner;
    int err;

    learner = (struct kndLearner *) arg;

    context = zmq_init(1);
    if (!context) {
        knd_log("zmq_init() failed, error: '%s'", strerror(errno));
        return NULL;
    }

    frontend = zmq_socket(context, ZMQ_PULL);
    if (!frontend) {
        knd_log("zmq_socket(frontend) failed, error '%s'", strerror(errno));
        return NULL;
    }

    backend = zmq_socket(context, ZMQ_PUSH);
    if (!backend) {
        knd_log("zmq_socket(backend) failed, error '%s'", strerror(errno));
        return NULL;
    }

    err = zmq_connect(frontend, "ipc:///var/lib/knowdy/storage_push"); // todo: fix hardcode
    if (err == -1) {
        knd_log("zmq_connect(frontend, '%s') failed, error: '%s'", "ipc:///var/lib/knowdy/storage_push",
                strerror(errno));
        return NULL;
    }

    err = zmq_connect(backend, learner->inbox_frontend_addr);
    if (err == -1) {
        knd_log("zmq_connect(backend, '%s') failed, error: '%s'", learner->inbox_frontend_addr, strerror(errno));
        return NULL;
    }

    knd_log("    ++ Learner %s Selector device is ready: %s...\n\n",
            learner->name, learner->inbox_frontend_addr);

    zmq_device(ZMQ_QUEUE, frontend, backend);

    /* we never get here */
    zmq_close(frontend);
    zmq_close(backend);
    zmq_term(context);

    return NULL;
}

void *kndLearner_subscriber(void *arg)
{
    void *context;
    void *subscriber;
    void *inbox;

    struct kndLearner *learner;
    char *spec;
    size_t spec_size;
    char *obj;
    size_t obj_size;

    int err;

    learner = (struct kndLearner *) arg;

    context = zmq_init(1);
    if (!context) {
        knd_log("zmq_init(subscriber) failed, error: '%s'", strerror(errno));
        return NULL;
    }

    subscriber = zmq_socket(context, ZMQ_SUB);
    if (!subscriber) {
        knd_log("zmq_socket(subscriber) failed, error: '%s'", strerror(errno));
        return NULL;
    }

    err = zmq_connect(subscriber, "ipc:///var/lib/knowdy/storage_pub"); // todo: fix hardcode
    if (err == -1) {
        knd_log("zmq_connect(subscriber) failed, error: '%s'", strerror(errno));
        return NULL;
    }

    zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, "", 0); // todo: error check

    inbox = zmq_socket(context, ZMQ_PUSH);
    if (!inbox) {
        knd_log("zmq_socket(inbox) failed, error: '%s'", strerror(errno));
        return NULL;
    }

    err = zmq_connect(inbox, learner->inbox_frontend_addr);
    if (err == -1) {
        knd_log("zmq_connect(inbox) failed, error: '%s'", strerror(errno));
        return NULL;
    }

    while (1) {
        spec = knd_zmq_recv(subscriber, &spec_size);
        obj = knd_zmq_recv(subscriber, &obj_size);

        knd_zmq_sendmore(inbox, spec, spec_size);
        knd_zmq_send(inbox, obj, obj_size);

        free(spec);
        free(obj);
    }

    /* we never get here */
    zmq_close(subscriber);
    zmq_term(context);

    return NULL;
}

/* send live updates to all readers
   aka retrievers */
void *kndLearner_publisher(void *arg)
{
    void *context;
    void *frontend;
    void *backend;
    struct kndLearner *learner;
    int ret;

    learner = (struct kndLearner *) arg;

    context = zmq_init(1);
    if (!context) {
        knd_log("zmq_init() failed, error: '%s'", zmq_strerror(errno));
        return NULL;
    }

    frontend = zmq_socket(context, ZMQ_SUB);
    if (!frontend) {
        knd_log("zmq_socket() failed, error: '%s'", zmq_strerror(errno));
        return NULL;
    }

    backend = zmq_socket(context, ZMQ_PUB);
    if (!frontend) {
        knd_log("zmq_socket() failed, error: '%s'", zmq_strerror(errno));
        return NULL;
    }

    ret = zmq_bind(frontend, learner->publish_proxy_frontend_addr);
    if (ret != knd_OK) {
        knd_log("bind %s zmqerr: %s\n", learner->publish_proxy_frontend_addr, zmq_strerror(errno));
        return NULL;
    }

    zmq_setsockopt(frontend, ZMQ_SUBSCRIBE, "", 0);

    ret = zmq_bind(backend, learner->publish_proxy_backend_addr);
    if (ret != knd_OK) {
        knd_log("bind %s zmqerr: %s\n", learner->publish_proxy_backend_addr, zmq_strerror(errno));
        return NULL;
    }

    knd_log("++ The Learner's publisher proxy is up and running!");

    zmq_proxy(frontend, backend, NULL);

    /* we never get here */
    zmq_close(frontend);
    zmq_close(backend);
    zmq_term(context);

    return NULL;
}


/**
 *  MAIN SERVICE
 *
 *  todo:
 *  - clean up all functions
 *  - write classes or functions for zmq-patterns
 *  - return and check errors from threads
 *  - write assert macros for error checking and logging
 */

int
main(const int argc,
     const char ** const argv)
{
    struct kndLearner *learner;
    const char *config = NULL;

    pthread_t subscriber;
    pthread_t publisher;
    pthread_t selector;
    pthread_t inbox;
    int err;

    if (argc - 1 != 1) {
        fprintf(stderr, "You must specify 1 argument:  "
                " the name of the configuration file. "
                "You specified %d arguments.\n",  argc - 1);
        return EXIT_FAILURE;
    }
    config = argv[1];

    err = kndLearner_new(&learner, config);
    if (err) {
        fprintf(stderr, "Couldn\'t load the Learner... ");
        return EXIT_FAILURE;
    }

    /* add device */
    err = pthread_create(&inbox, NULL, kndLearner_inbox, (void *) learner);

    /* add subscriber */
    err = pthread_create(&subscriber, NULL, kndLearner_subscriber, (void *) learner);

    /* add selector */
    err = pthread_create(&selector, NULL, kndLearner_selector, (void *) learner);

    /* add updates publisher */
    err = pthread_create(&publisher, NULL, kndLearner_publisher, (void *) learner);

    learner->start(learner);

    learner->del(learner);

    return EXIT_SUCCESS;
}

