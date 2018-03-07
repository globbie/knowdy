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
#include "knd_concept.h"
#include "knd_object.h"
#include "knd_rel.h"
#include "knd_proc.h"

#include <gsl-parser.h>

#include "knd_learner.h"

#define DEBUG_LEARNER_LEVEL_1 0
#define DEBUG_LEARNER_LEVEL_2 0
#define DEBUG_LEARNER_LEVEL_3 0
#define DEBUG_LEARNER_LEVEL_TMP 1

static void
kndLearner_del(struct kndLearner *self)
{
    struct kndConcept *conc;

    conc = self->admin->root_class;
    conc->class_name_idx->del(conc->class_name_idx);
    conc->rel->rel_idx->del(conc->rel->rel_idx);
    conc->rel->del(conc->rel);

    conc->proc->proc_idx->del(conc->proc->proc_idx);
    conc->proc->del(conc->proc);
    conc->del(conc);

    self->task->state_ctrl->del(self->task->state_ctrl);
    self->task->out->del(self->task->out);
    self->task->del(self->task);

    free(self->admin->user_idx);
    self->admin->del(self->admin);

    self->out->del(self->out);
    self->log->del(self->log);
    self->mempool->del(self->mempool);

    free(self);
}


static int
kndLearner_start(struct kndLearner *self)
{
    void *context;
    void *outbox;

    char *task_buf = NULL;
    size_t task_buf_size = 0;

    char *task = NULL;
    size_t task_size = 0;
    size_t total_task_size = 0;

    size_t chunk_size = 0;

    const char *obj = "None";
    size_t obj_size = strlen("None");

    time_t  t0, t1;
    clock_t c0, c1;
    int err;

    task_buf_size = KND_LARGE_BUF_SIZE + 1;
    task_buf = malloc(task_buf_size);
    if (!task_buf) return knd_NOMEM;

    task = task_buf;

    // restore in-memory data after failure or restart
    self->admin->role = KND_USER_ROLE_LEARNER;

    //err = self->admin->restore(self->admin);
    //if (err) return err;

    context = zmq_init(1);
    if (!context) {
        knd_log("zmq_init() failed, error: '%s'", strerror(errno));
        return knd_FAIL;
    }

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
        task_size = KND_LARGE_BUF_SIZE;
        err = knd_recv_task(outbox, task, &task_size);
        if (err) {
            knd_log("-- failed to recv task :(");
            task_size = 0;
            continue;
        }

        t0 = time(NULL);
        c0 = clock();

        if (DEBUG_LEARNER_LEVEL_TMP) {
            chunk_size = task_size > KND_MAX_DEBUG_CHUNK_SIZE ? KND_MAX_DEBUG_CHUNK_SIZE : task_size;
            knd_log("\n++ Learner got a new task: \"%.*s\".. [size: %lu]",
                    chunk_size, task, (unsigned long)task_size);
        }

        self->task->reset(self->task);
        err = self->task->run(self->task,
                              task, task_size,
                              obj, obj_size);
        if (DEBUG_LEARNER_LEVEL_TMP) {
            if (!strcmp(obj, "TPS TEST")) {
                t1 = time(NULL);
                c1 = clock();

                printf ("\telapsed wall clock time: %ld\n", (long)  (t1 - t0));
                printf ("\telapsed CPU time:        %f\n",  (float) (c1 - c0)/CLOCKS_PER_SEC);
                knd_log("\tTOTAL objs imported: %zu", self->task->state_ctrl->total_objs);
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

        task += task_size;
        total_task_size += task_size;

    final:

        if (!self->task->tid_size) {
            self->task->tid[0] = '0';
            self->task->tid_size = 1;
        }

        err = self->task->report(self->task);
        if (err) {
            knd_log("-- task report failed: %d", err);
        }
    }

    /* we should never get here */
    return knd_OK;
}

static gsl_err_t parse_publisher_service_addr(void *obj,
                                              const char *rec,
                                              size_t *total_size)
{
    struct kndLearner *self = (struct kndLearner*)obj;
    struct gslTaskSpec specs[] = {
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

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t
parse_inbox_addr(void *obj,
                 const char *rec,
                 size_t *total_size)
{
    struct kndLearner *self = (struct kndLearner*)obj;


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
        { .name = "max_attrs",
          .name_size = strlen("max_attrs"),
          .parse = gsl_parse_size_t,
          .obj = &self->max_attrs
        },
        { .name = "max_conc_items",
          .name_size = strlen("max_conc_items"),
          .parse = gsl_parse_size_t,
          .obj = &self->max_conc_items
        },
        { .name = "max_states",
          .name_size = strlen("max_states"),
          .parse = gsl_parse_size_t,
          .obj = &self->max_states
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
    const char *schema_name = "Knowdy Learner Service";
    size_t schema_name_size = strlen(schema_name);

    if (val_size != schema_name_size)  return make_gsl_err(gsl_FAIL);
    if (memcmp(schema_name, val, val_size)) return make_gsl_err(gsl_FAIL);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_config(void *obj,
			      const char *rec,
			      size_t *total_size)
{
    struct kndLearner *self = obj;

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
        { .name = "inbox",
          .name_size = strlen("inbox"),
          .parse = parse_inbox_addr,
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
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- config parse error: %d", parser_err.code);
        return parser_err;
    }

    if (!self->path_size) {
        knd_log("-- DB path not set :(");
	return make_gsl_err(gsl_FAIL);
    }
    err = knd_mkpath(self->path, self->path_size, 0755, false);
    if (err) return make_gsl_err_external(err);

    if (!self->schema_path_size) {
        knd_log("-- schema path not set :(");
	return make_gsl_err(gsl_FAIL);
    }

    if (!self->inbox_frontend_addr_size) {
        knd_log("-- inbox frontend addr not set :(");
	return make_gsl_err(gsl_FAIL);
    }
    if (!self->inbox_backend_addr_size) {
        knd_log("-- inbox backend addr not set :(");
	return make_gsl_err(gsl_FAIL);
    }

    if (!self->admin->sid_size) {
        knd_log("-- administrative SID is not set :(");
        return make_gsl_err(gsl_FAIL);
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

    return make_gsl_err(gsl_OK);
}

static int parse_schema(struct kndLearner *self,
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
    self->task->mempool = self->mempool;

    /* read config */
    err = self->out->read_file(self->out, config, strlen(config));
    if (err) goto error;

    err = parse_schema(self, self->out->file, &chunk_size);
    if (err) goto error;

    err = self->mempool->alloc(self->mempool);                                    RET_ERR();

    err = kndStateControl_new(&self->task->state_ctrl);
    if (err) return err;
    self->task->state_ctrl->max_updates = self->mempool->max_updates;
    self->task->state_ctrl->updates = self->mempool->update_idx;
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

    err = self->mempool->new_class(self->mempool, &conc);                         RET_ERR();
    conc->out = self->out;
    conc->task = self->task;
    conc->log = self->task->log;
    conc->name[0] = '/';
    conc->name_size = 1;

    conc->dbpath = self->schema_path;
    conc->dbpath_size = self->schema_path_size;
    conc->frozen_output_file_name = self->admin->frozen_output_file_name;
    conc->frozen_output_file_name_size = self->admin->frozen_output_file_name_size;

    err = self->mempool->new_conc_dir(self->mempool, &conc->dir);                 RET_ERR();
    memset(conc->dir->name, '0', KND_ID_SIZE);
    conc->dir->name_size = KND_ID_SIZE;
    conc->dir->conc = conc;
    conc->mempool = self->mempool;
    conc->dir->mempool = self->mempool;

    err = kndProc_new(&conc->proc);
    if (err) goto error;
    conc->proc->mempool = self->mempool;

    err = kndRel_new(&conc->rel);
    if (err) goto error;
    conc->rel->mempool = self->mempool;
    conc->rel->frozen_output_file_name = self->admin->frozen_output_file_name;
    conc->rel->frozen_output_file_name_size = self->admin->frozen_output_file_name_size;

    /* specific allocations of the root concs */
    err = ooDict_new(&conc->class_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&conc->class_name_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&conc->proc->proc_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;
    conc->proc->class_name_idx = conc->class_name_idx;

    err = ooDict_new(&conc->rel->rel_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;
    conc->rel->class_name_idx = conc->class_name_idx;

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

    /* obj manager */
    err = self->mempool->new_obj(self->mempool, &conc->curr_obj);                 RET_ERR();
    conc->curr_obj->mempool = self->mempool;

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
    if (!backend) {
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

int main(const int argc,
         const char ** const argv)
{
    struct kndLearner *learner;
    const char *config = NULL;

    //pthread_t subscriber;
    pthread_t publisher;
    //pthread_t selector;
    pthread_t inbox;
    int err;

    if (argc - 1 != 1) {
        fprintf(stderr, "You must specify 1 argument:  "
                " the name of the configuration file. "
                "You specified %d arguments.\n",  argc - 1);
        return EXIT_FAILURE;
    }
    config = argv[1];

    err = kndLearner_new(&learner, config);   RET_ERR();

    err = pthread_create(&inbox, NULL, kndLearner_inbox, (void *) learner);

    /*err = pthread_create(&subscriber, NULL, kndLearner_subscriber, (void *) learner);
    err = pthread_create(&selector, NULL, kndLearner_selector, (void *) learner);
    */

    err = pthread_create(&publisher, NULL, kndLearner_publisher, (void *) learner);

    learner->start(learner);

    learner->del(learner);

    return EXIT_SUCCESS;
}

