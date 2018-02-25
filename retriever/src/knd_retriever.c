#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <assert.h>

#include "knd_user.h"
#include "knd_output.h"
#include "knd_concept.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_object.h"
#include "knd_state.h"
#include "knd_task.h"
#include "knd_utils.h"
#include "knd_msg.h"
#include "knd_mempool.h"

#include <gsl-parser.h>

#include "knd_retriever.h"

#define DEBUG_RETRIEVER_LEVEL_1 0
#define DEBUG_RETRIEVER_LEVEL_2 0
#define DEBUG_RETRIEVER_LEVEL_3 0
#define DEBUG_RETRIEVER_LEVEL_TMP 1

static void
kndRetriever_del(struct kndRetriever *self)
{
    /* TODO: free aggrs  */
    free(self);
}

static int send_http_reply(struct kndRetriever *self)
{
    char *b;
    size_t buf_size;
    int err;

    b = self->task->delivery_addr;
    b += self->task->delivery_addr_size;

    memcpy(b, "/", 1);
    b++;

    memcpy(b, self->task->tid,  self->task->tid_size);
    b += self->task->tid_size;
    *b = '\0';

    knd_log(".. try HTTP callback: \"%s\"", self->task->delivery_addr);

    err = knd_http_post(self->task->delivery_addr, self->task->out->buf, self->task->out->buf_size);
    if (err) return err;

    return knd_OK;
}

static int
kndRetriever_start(struct kndRetriever *self)
{
    void *context;
    void *outbox;

    size_t chunk_size = 0;
    char *task;
    size_t task_size;
    const char *obj = NULL;
    size_t obj_size = 0;
    int err;

    task = malloc(KND_MED_BUF_SIZE + 1);
    if (!task) return knd_NOMEM;

    context = zmq_init(1);
    outbox = zmq_socket(context, ZMQ_PULL);
    if (!outbox) {
        free(task);
        return knd_FAIL;
    }

    assert((zmq_connect(outbox, self->inbox_backend_addr) == knd_OK));

    /* delivery service */
    self->delivery = zmq_socket(context, ZMQ_REQ);
    if (!self->delivery) {
        free(task);
        return knd_FAIL;
    }

    assert((zmq_connect(self->delivery,  self->delivery_addr) == knd_OK));
    self->task->delivery = self->delivery;

    knd_log("++ %s Retriever is up and running!", self->name);

    task_size = 0;
    obj_size = 0;

    while (1) {
        task_size = KND_MED_BUF_SIZE;
        err = knd_recv_task(outbox, task, &task_size);
        if (err) {
            knd_log("-- failed to recv task :(");
            continue;
        }

        if (DEBUG_RETRIEVER_LEVEL_TMP) {
            chunk_size = (task_size > KND_MAX_DEBUG_CHUNK_SIZE) ?\
                KND_MAX_DEBUG_CHUNK_SIZE : task_size;
            knd_log("\n++ Retriever got a new task: \"%.*s\".. [size: %zu]",
                    chunk_size, task, task_size);
        }

        self->task->reset(self->task);
        err = self->task->run(self->task, task, task_size, obj, obj_size);
        if (err) {
            self->task->error = err;
            knd_log("-- task run failed: %d", err);
        } else {
            /* no need to inform delivery about every liquid update success */
            if (self->task->type == KND_UPDATE_STATE)
                continue;
        }

	if (self->task->delivery_type == KND_DELIVERY_HTTP) {
	    err = send_http_reply(self);
            if (err) {
		knd_log("-- http callback failed: %d", err);
	    }
	    continue;
	}

        err = self->task->report(self->task);
        if (err) {
            knd_log("-- task report failed: %d", err);
        }
    }

    zmq_close(outbox);
    zmq_close(self->delivery);

    return knd_OK;
}


static gsl_err_t
parse_inbox_addr(void *obj,
                 const char *rec,
                 size_t *total_size)
{
    struct kndRetriever *self = (struct kndRetriever*)obj;

    struct gslTaskSpec specs[] = {
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

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}


static gsl_err_t
run_set_service_id(void *obj,
                   const char *name,
                   size_t name_size)
{
    struct kndRetriever *self = (struct kndRetriever*)obj;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    memcpy(self->name, name, name_size);
    self->name_size = name_size;
    self->name[name_size] = '\0';

    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_memory_settings(void *obj,
                                       const char *rec,
                                       size_t *total_size)
{
    struct kndMemPool *self = obj;
    struct gslTaskSpec specs[] = {
        { .name = "max_users",
          .name_size = strlen("max_users"),
          .parse = gsl_parse_size_t,
          .obj = &self->max_users
        },
        { .name = "max_classes",
          .name_size = strlen("max_classes"),
          .parse = gsl_parse_size_t,
          .obj = &self->max_classes
        },
        { .name = "max_objs",
          .name_size = strlen("max_objs"),
          .parse = gsl_parse_size_t,
          .obj = &self->max_objs
        },
        { .name = "max_elems",
          .name_size = strlen("max_elems"),
          .parse = gsl_parse_size_t,
          .obj = &self->max_elems
        },
        { .name = "max_rels",
          .name_size = strlen("max_rels"),
          .parse = gsl_parse_size_t,
          .obj = &self->max_rels
        },
        { .name = "max_rel_args",
          .name_size = strlen("max_rels_args"),
          .parse = gsl_parse_size_t,
          .obj = &self->max_rel_args
        },
        { .name = "max_rel_refs",
          .name_size = strlen("max_rel_refs"),
          .parse = gsl_parse_size_t,
          .obj = &self->max_rel_refs
        },
        { .name = "max_rel_instances",
          .name_size = strlen("max_rel_instances"),
          .parse = gsl_parse_size_t,
          .obj = &self->max_rel_insts
        },
        { .name = "max_rel_arg_instances",
          .name_size = strlen("max_rel_arg_instances"),
          .parse = gsl_parse_size_t,
          .obj = &self->max_rel_arg_insts
        },
        { .name = "max_rel_arg_inst_refs",
          .name_size = strlen("max_rel_arg_inst_refs"),
          .parse = gsl_parse_size_t,
          .obj = &self->max_rel_arg_inst_refs
        },
        { .name = "max_procs",
          .name_size = strlen("max_procs"),
          .parse = gsl_parse_size_t,
          .obj = &self->max_procs
        },
        { .name = "max_proc_instances",
          .name_size = strlen("max_proc_instances"),
          .parse = gsl_parse_size_t,
          .obj = &self->max_proc_insts
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}


static gsl_err_t run_check_schema(void *obj, const char *val, size_t val_size)
{
    const char *schema_name = "Knowdy Retriever Service";
    size_t schema_name_size = strlen(schema_name);

    if (val_size != schema_name_size)  return make_gsl_err(gsl_FAIL);
    if (memcmp(schema_name, val, val_size)) return make_gsl_err(gsl_FAIL);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_config(void *obj,
			      const char *rec,
			      size_t *total_size)
{
    struct kndRetriever *self = obj;
    struct gslTaskSpec specs[] = {
         { .is_implied = true,
           .run = run_check_schema,
           .obj = self
         },
         { .name = "path",
           .name_size = strlen("path"),
           .buf = self->path,
           .buf_size = &self->path_size,
          .max_buf_size = KND_NAME_SIZE
         },
         { .name = "n",
           .name_size = strlen("n"),
           .buf = self->name,
           .buf_size = &self->name_size,
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
         { .name = "locale",
           .name_size = strlen("locale"),
           .buf = self->admin->default_locale,
           .buf_size = &self->admin->default_locale_size,
           .max_buf_size = KND_NAME_SIZE
         },
         { .name = "coll_request",
           .name_size = strlen("coll_request"),
           .buf = self->coll_request_addr,
           .buf_size = &self->coll_request_addr_size,
           .max_buf_size = KND_NAME_SIZE
         },
         { .name = "delivery",
           .name_size = strlen("delivery"),
           .buf = self->delivery_addr,
           .buf_size = &self->delivery_addr_size,
           .max_buf_size = KND_NAME_SIZE
         },
        { .name = "inbox",
          .name_size = strlen("inbox"),
          .parse = parse_inbox_addr,
          .obj = self
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- config parse error: %d", parser_err.code);
	return parser_err;
    }

    if (!*self->path) {
        knd_log("-- DB path not set :(");
	return make_gsl_err(gsl_FAIL);
    }

    if (!*self->schema_path) {
        knd_log("-- schema path not set :(");
	return make_gsl_err(gsl_FAIL);
    }

    if (!*self->inbox_frontend_addr) {
        knd_log("-- inbox frontend addr not set :(");
	return make_gsl_err(gsl_FAIL);
    }
    if (!*self->inbox_backend_addr) {
        knd_log("-- inbox backend addr not set :(");
	return make_gsl_err(gsl_FAIL);
    }

    if (!*self->admin->sid) {
        knd_log("-- administrative SID is not set :(");
	return make_gsl_err(gsl_FAIL);
    }

    memcpy(self->admin->id, "000", strlen("000"));

    /* users path */
    self->admin->dbpath = self->path;
    self->admin->dbpath_size = self->path_size;

    memcpy(self->admin->path, self->path, self->path_size);
    memcpy(self->admin->path + self->path_size, "/users", strlen("/users"));
    self->admin->path_size = self->path_size + strlen("/users");
    self->admin->path[self->admin->path_size] = '\0';

    return make_gsl_err(gsl_OK);
}


static int parse_schema(struct kndRetriever *self,
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


extern int
kndRetriever_new(struct kndRetriever **rec,
                  const char *config)
{
    struct kndRetriever *self;
    struct kndConcept *conc;
    struct kndOutput *out;
    size_t chunk_size = 0;
    int err;

    self = malloc(sizeof(struct kndRetriever));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndRetriever));

    err = kndOutput_new(&self->out, KND_IDX_BUF_SIZE);
    if (err) return err;

    err = kndTask_new(&self->task);
    if (err) goto error;

    err = kndOutput_new(&self->task->out, KND_IDX_BUF_SIZE);
    if (err) goto error;

    err = kndUser_new(&self->admin);
    if (err) goto error;
    self->task->admin = self->admin;
    self->admin->out = self->out;
    self->admin->log = self->task->log;

    /* read config */
    err = self->out->read_file(self->out, config, strlen(config));
    if (err) {
        knd_log("  -- config file read error :(");
        goto error;
    }

    err = kndMemPool_new(&self->mempool);
    if (err) return err;

    err = parse_schema(self, self->out->file, &chunk_size);
    if (err) {
        knd_log("  -- config parsing error :(");
        goto error;
    }

    self->mempool->alloc(self->mempool);

    err = kndStateControl_new(&self->task->state_ctrl);
    if (err) return err;
    self->task->state_ctrl->max_updates = self->mempool->max_updates;
    self->task->state_ctrl->updates = self->mempool->update_idx;
    self->task->state_ctrl->selected = self->mempool->update_selected_idx;
    self->task->state_ctrl->task = self->task;

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
    if (err) return err;
    conc->out = self->out;
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
    conc->dir->mempool = self->mempool;

    conc->task = self->task;
    conc->log = self->task->log;

    err = kndProc_new(&conc->proc);
    if (err) goto error;
    conc->proc->mempool = self->mempool;

    err = kndRel_new(&conc->rel);
    if (err) goto error;
    conc->rel->mempool = self->mempool;
    conc->rel->frozen_output_file_name = self->admin->frozen_output_file_name;
    conc->rel->frozen_output_file_name_size = self->admin->frozen_output_file_name_size;

    /* specific allocations of the root concs */
    err = ooDict_new(&conc->class_idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&conc->class_name_idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&conc->proc->proc_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;
    conc->proc->class_name_idx = conc->class_name_idx;

    err = ooDict_new(&conc->rel->rel_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;
    conc->rel->class_name_idx = conc->class_name_idx;

    /* user idx */
    /*if (self->mempool->max_users) {
        self->admin->user_idx = calloc(self->mempool->max_users,
                                       sizeof(struct kndObject*));
        if (!self->admin->user_idx) return knd_NOMEM;
        self->admin->max_users = self->mempool->max_users;
        } */

    conc->user = self->admin;
    self->admin->root_class = conc;

    self->del = kndRetriever_del;
    self->start = kndRetriever_start;

    /* open frozen DB */
    err = conc->open(conc);
    if (err && err != knd_NO_MATCH) goto error;

    *rec = self;
    return knd_OK;

 error:

    knd_log("-- Retriever construction failure: %d :(", err);

    //kndRetriever_del(self);
    return err;
}

/** SERVICES */
void *kndRetriever_inbox(void *arg)
{
    void *context;
    void *frontend;
    void *backend;
    struct kndRetriever *retriever;
    int err;

    context = zmq_init(1);
    retriever = (struct kndRetriever*)arg;

    frontend = zmq_socket(context, ZMQ_PULL);
    assert(frontend);

    backend = zmq_socket(context, ZMQ_PUSH);
    assert(backend);

    /* knd_log("%s <-> %s\n", retriever->inbox_frontend_addr, retriever->inbox_backend_addr); */

    err = zmq_bind(frontend, retriever->inbox_frontend_addr);
    assert(err == knd_OK);

    err = zmq_bind(backend, retriever->inbox_backend_addr);
    assert(err == knd_OK);

    knd_log("    ++ Retriever \"%s\" Inbox device is ready...\n\n",
            retriever->name);

    zmq_device(ZMQ_QUEUE, frontend, backend);

    /* we never get here */
    zmq_close(frontend);
    zmq_close(backend);
    zmq_term(context);
    return NULL;
}

void *kndRetriever_selector(void *arg)
{
    void *context;
    void *frontend;
    void *backend;
    struct kndRetriever *retriever;
    int err;

    context = zmq_init(1);
    retriever = (struct kndRetriever*)arg;

    frontend = zmq_socket(context, ZMQ_PULL);
    assert(frontend);

    backend = zmq_socket(context, ZMQ_PUSH);
    assert(backend);

    err = zmq_connect(frontend, retriever->coll_request_addr);
    assert(err == knd_OK);

    err = zmq_connect(backend, retriever->inbox_frontend_addr);
    assert(err == knd_OK);

    knd_log("    ++ Retriever %s Selector device is ready: %s...\n\n",
            retriever->name,
            retriever->inbox_frontend_addr);

    zmq_device(ZMQ_QUEUE, frontend, backend);

    /* we never get here */
    zmq_close(frontend);
    zmq_close(backend);
    zmq_term(context);

    return NULL;
}

void *kndRetriever_subscriber(void *arg)
{
    void *context;
    void *subscriber;
    void *inbox;
    struct kndRetriever *retriever;

    char *obj;
    size_t obj_size;
    char *task;
    size_t task_size;
    int err;

    retriever = (struct kndRetriever*)arg;

    context = zmq_init(1);

    subscriber = zmq_socket(context, ZMQ_SUB);
    assert(subscriber);
    err = zmq_connect(subscriber, "ipc:///var/lib/knowdy/learner_pub_backend");
    assert(err == knd_OK);
    zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, "", 0);

    inbox = zmq_socket(context, ZMQ_PUSH);
    assert(inbox);
    err = zmq_connect(inbox, retriever->inbox_frontend_addr);
    assert(err == knd_OK);

    while (1) {
        task = NULL;
        task_size = 0;
        obj = NULL;
        obj_size = 0;

        task = knd_zmq_recv(subscriber, &task_size);
        //obj = knd_zmq_recv(subscriber, &obj_size);

        /* sometimes bad messages arrive */
        if (!task || !task_size) continue;
        //if (!obj || !obj_size) continue;

        if (DEBUG_RETRIEVER_LEVEL_2) {
            printf("++ %s Retriever has got an update from Learner:"
                   "       %.*s [%lu]", retriever->name,
                   (unsigned int)task_size, task, (unsigned long)task_size);
        }

        err = knd_zmq_send(inbox, task, task_size);
        //err = knd_zmq_send(inbox, obj, obj_size);

        if (task)
            free(task);

        fflush(stdout);
    }

    /* we never get here */
    zmq_close(subscriber);
    zmq_term(context);

    return NULL;
}



/**
 *  MAIN SERVICE
 */

int main(int const argc,
         const char ** const argv)
{
    struct kndRetriever *retriever;
    const char *config = NULL;

    pthread_t subscriber;
    //pthread_t selector;
    pthread_t inbox;
    int err;

    if (argc - 1 != 1) {
        fprintf(stderr, "You must specify 1 argument:  "
                " the name of the configuration file."
                "You specified %d arguments.\n",  argc - 1);
        exit(1);
    }

    config = argv[1];

    err = kndRetriever_new(&retriever, config);
    if (err) {
        fprintf(stderr, "-- couldn\'t init the Retriever... ");
        exit(-1);
    }

    /* add device */
    err = pthread_create(&inbox,
                         NULL,
                         kndRetriever_inbox,
                         (void*)retriever);

    /* add subscriber */
    err = pthread_create(&subscriber,
                         NULL,
                         kndRetriever_subscriber,
                         (void*)retriever);

    /* add selector */
    /*err = pthread_create(&selector,
                         NULL,
                         kndRetriever_selector,
                         (void*)retriever); */

    retriever->start(retriever);


    return 0;
}
