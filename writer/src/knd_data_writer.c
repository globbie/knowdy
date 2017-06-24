#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include <knd_config.h>
#include <knd_dict.h>
#include <knd_utils.h>
#include <knd_msg.h>
#include <knd_output.h>
#include <knd_task.h>
#include <knd_attr.h>
#include <knd_user.h>

#include "knd_data_writer.h"

#define DEBUG_WRITER_LEVEL_1 0
#define DEBUG_WRITER_LEVEL_2 0
#define DEBUG_WRITER_LEVEL_3 0
#define DEBUG_WRITER_LEVEL_TMP 1

static void
kndDataWriter_del(struct kndDataWriter *self)
{
    /*if (self->path) free(self->path);*/

    /* TODO: storage */

    free(self);
}


static int  
kndDataWriter_start(struct kndDataWriter *self)
{
    void *context;
    void *outbox;
    char *task = NULL;
    size_t task_size = 0;
    char *obj = NULL;
    size_t obj_size = 0;
    
    int err;

    /* restore in-memory data after failure or restart */
    self->admin->role = KND_USER_ROLE_WRITER;
    err = self->admin->restore(self->admin);
    if (err) return err;
    
    context = zmq_init(1);

    knd_log("WRITER listener: %s\n",
            self->inbox_backend_addr);
    
    /* get messages from outbox */
    outbox = zmq_socket(context, ZMQ_PULL);
    assert(outbox);
    
    err = zmq_connect(outbox, self->inbox_backend_addr);
    assert(err == knd_OK);
    
    self->delivery = zmq_socket(context, ZMQ_REQ);
    assert(self->delivery);

    if (DEBUG_WRITER_LEVEL_TMP)
        knd_log(".. establish delivery connection: %s..", 
                self->delivery_addr);

    err = zmq_connect(self->delivery, self->delivery_addr);
    assert(err == knd_OK);

    self->task->delivery = self->delivery;

    while (1) {
        self->task->reset(self->task);

        if (DEBUG_WRITER_LEVEL_TMP)
            knd_log("\n++ DATAWRITER AGENT #%s is ready to receive new tasks!", 
                    self->name);

	task = knd_zmq_recv(outbox, &task_size);
	obj = knd_zmq_recv(outbox, &obj_size);

	knd_log("++ DATAWRITER AGENT #%s got task: %s [%lu]", 
                self->name, task, (unsigned long)task_size);

        err = self->task->run(self->task,
                              task, task_size,
                              obj, obj_size);
        if (err) {
            knd_log("-- task running failure: %d", err);
            goto final;
        }

    final:

        err = self->task->report(self->task);
        if (err) {
            knd_log("-- task report failed: %d", err);
        }
        
        if (task) free(task);
        if (obj) free(obj);
    }

    /* we should never get here */
    return knd_OK;
}


static int
parse_write_inbox_addr(void *obj,
                       const char *rec,
                       size_t *total_size)
{
    struct kndDataWriter *self = (struct kndDataWriter*)obj;

    self->inbox_frontend_addr_size = KND_NAME_SIZE;
    self->inbox_backend_addr_size = KND_NAME_SIZE;

    struct kndTaskSpec specs[] = {
        { .name = "frontend",
          .name_size = strlen("frontend"),
          .buf = self->inbox_frontend_addr,
          .buf_size = &self->inbox_frontend_addr_size
        },
        { .name = "backend",
          .name_size = strlen("backend"),
          .buf = self->inbox_backend_addr,
          .buf_size = &self->inbox_backend_addr_size
        }
    };
    int err;
    
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}


static int
parse_config_GSL(struct kndDataWriter *self,
                 const char *rec,
                 size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = KND_NAME_SIZE;
    size_t chunk_size = 0;
    
    const char *gsl_format_tag = "{gsl";
    size_t gsl_format_tag_size = strlen(gsl_format_tag);

    const char *header_tag = "{knd::Knowdy Writer Service Configuration";
    size_t header_tag_size = strlen(header_tag);
    const char *c;

    self->path_size = KND_NAME_SIZE;
    self->schema_path_size = KND_NAME_SIZE;
    self->delivery_addr_size = KND_NAME_SIZE;
    self->admin->sid_size = KND_NAME_SIZE;
    
    struct kndTaskSpec specs[] = {
         { .name = "path",
           .name_size = strlen("path"),
           .buf = self->path,
           .buf_size = &self->path_size
         },
         { .name = "schemas",
           .name_size = strlen("schemas"),
           .buf = self->schema_path,
           .buf_size = &self->schema_path_size
         },
         { .name = "sid",
           .name_size = strlen("sid"),
           .buf = self->admin->sid,
           .buf_size = &self->admin->sid_size
         },
         { .name = "delivery",
           .name_size = strlen("delivery"),
           .buf = self->delivery_addr,
           .buf_size = &self->delivery_addr_size
         },
        { .name = "write_inbox",
          .name_size = strlen("write_inbox"),
          .parse = parse_write_inbox_addr,
          .obj = self
        }
    };
    int err = knd_FAIL;

    if (!strncmp(rec, gsl_format_tag, gsl_format_tag_size)) {
        rec += gsl_format_tag_size;
        err = knd_get_schema_name(rec,
                                  buf, &buf_size, &chunk_size);
        if (!err) {
            rec += chunk_size;
            if (DEBUG_WRITER_LEVEL_TMP)
                knd_log("== got schema: \"%s\"", buf);
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

    if (!*self->path) {
        knd_log("-- DB path not set :(");
        return knd_FAIL;
    }
    
    if (!*self->schema_path) {
        knd_log("-- schema path not set :(");
        return knd_FAIL;
    }

    if (!*self->inbox_frontend_addr) {
        knd_log("-- inbox frontend addr not set :(");
        return knd_FAIL;
    }
    if (!*self->inbox_backend_addr) {
        knd_log("-- inbox backend addr not set :(");
        return knd_FAIL;
    }

    if (!*self->admin->sid) {
        knd_log("-- administrative SID is not set :(");
        return knd_FAIL;
    }
    
    memcpy(self->admin->id, "000", strlen("000"));

    /* users path */
    self->admin->dbpath = self->path;
    self->admin->dbpath_size = self->path_size;

    memcpy(self->admin->path, self->path, self->path_size);
    memcpy(self->admin->path + self->path_size, "/users", strlen("/users"));
    self->admin->path_size = self->path_size + strlen("/users");
    self->admin->path[self->admin->path_size] = '\0';

    
    return knd_OK;
}

extern int
kndDataWriter_new(struct kndDataWriter **rec,
                  const char *config)
{
    struct kndDataWriter *self;
    struct kndDataClass *dc;
    size_t chunk_size;
    int err;

    self = malloc(sizeof(struct kndDataWriter));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndDataWriter));

    err = kndOutput_new(&self->out, KND_IDX_BUF_SIZE);
    if (err) return err;

    /* task specification */
    err = kndTask_new(&self->task);
    if (err) return err;
    
    err = kndOutput_new(&self->task->out, KND_IDX_BUF_SIZE);
    if (err) return err;
    
    /* special user */
    err = kndUser_new(&self->admin);
    if (err) return err;
    self->task->admin = self->admin;
    self->admin->out = self->out;
    
    /* admin indices */
    err = ooDict_new(&self->admin->user_idx, KND_SMALL_DICT_SIZE);
    if (err) goto error;
        
    /* read config */
    err = self->out->read_file(self->out, config, strlen(config));
    if (err) return err;
    
    err = parse_config_GSL(self, self->out->file, &chunk_size);
    if (err) goto error;
    
    err = kndDataClass_new(&dc);
    if (err) return err;
    dc->out = self->out;
    dc->name[0] = '/';
    dc->name_size = 1;

    dc->dbpath = self->schema_path;
    dc->dbpath_size = self->schema_path_size;

    err = ooDict_new(&dc->class_idx, KND_SMALL_DICT_SIZE);
    if (err) goto error;
    
    /* read class definitions */
    err = dc->read_onto(dc, "index.gsl");
    if (err) {
 	knd_log("-- couldn't read any schema definitions :("); 
        goto error;
    }
    
    err = dc->coordinate(dc);
    if (err) goto error;
    
    self->admin->root_dc = dc;

    self->del = kndDataWriter_del;
    self->start = kndDataWriter_start;

    *rec = self;

    return knd_OK;

 error:
    kndDataWriter_del(self);
    return err;
}


/** SERVICES */

void *kndDataWriter_inbox(void *arg)
{
    void *context;
    void *frontend;
    void *backend;
    struct kndDataWriter *writer;
    int err;

    context = zmq_init(1);
    writer = (struct kndDataWriter*)arg;

    frontend = zmq_socket(context, ZMQ_PULL);
    assert(frontend);
    
    backend = zmq_socket(context, ZMQ_PUSH);
    assert(backend);

    knd_log("%s <-> %s\n", writer->inbox_frontend_addr, writer->inbox_backend_addr);

    err = zmq_bind(frontend, writer->inbox_frontend_addr);
    assert(err == knd_OK);

    err = zmq_bind(backend, writer->inbox_backend_addr);
    assert(err == knd_OK);

    knd_log("    ++ DataWriter \"%s\" Queue device is ready...\n\n", 
            writer->name);

    zmq_device(ZMQ_QUEUE, frontend, backend);

    /* we never get here */
    zmq_close(frontend);
    zmq_close(backend);
    zmq_term(context);
    return NULL;
}

void *kndDataWriter_selector(void *arg)
{
    void *context;
    void *frontend;
    void *backend;
    struct kndDataWriter *writer;
    int err;

    context = zmq_init(1);
    writer = (struct kndDataWriter*)arg;

    frontend = zmq_socket(context, ZMQ_PULL);
    assert(frontend);
    
    backend = zmq_socket(context, ZMQ_PUSH);
    assert(backend);
    
    err = zmq_connect(frontend, "ipc:///var/lib/knowdy/storage_push");
    assert(err == knd_OK);

    err = zmq_connect(backend, writer->inbox_frontend_addr);
    assert(err == knd_OK);
    
    knd_log("    ++ DataWriter %s Selector device is ready: %s...\n\n",
            writer->name, writer->inbox_frontend_addr);

    zmq_device(ZMQ_QUEUE, frontend, backend);

    /* we never get here */
    zmq_close(frontend);
    zmq_close(backend);
    zmq_term(context);

    return NULL;
}

void *kndDataWriter_subscriber(void *arg)
{
    void *context;
    void *subscriber;
    //void *agents;
    void *inbox;

    struct kndDataWriter *writer;
    struct kndData *data;
    //const char *empty_msg = "None";
    //size_t empty_msg_size = strlen(empty_msg);
    char *spec;
    size_t spec_size;
    char *obj;
    size_t obj_size;
    
    int err;

    writer = (struct kndDataWriter*)arg;

    context = zmq_init(1);


    subscriber = zmq_socket(context, ZMQ_SUB);
    assert(subscriber);

    err = zmq_connect(subscriber, "ipc:///var/lib/knowdy/storage_pub");
    assert(err == knd_OK);
    
    zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, "", 0);

    inbox = zmq_socket(context, ZMQ_PUSH);
    assert(inbox);

    err = zmq_connect(inbox, writer->inbox_frontend_addr);
    assert(err == knd_OK);

    while (1) {

        spec = knd_zmq_recv(subscriber, &spec_size);
	obj = knd_zmq_recv(subscriber, &obj_size);

	knd_zmq_sendmore(inbox, spec, spec_size);
	knd_zmq_send(inbox, obj, obj_size);

	printf("    ++ all messages sent!\n");
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

int 
main(int const argc, 
     const char ** const argv) 
{
    struct kndDataWriter *writer;

    const char *config = NULL;

    pthread_t subscriber;
    pthread_t selector;
    pthread_t inbox;
    int err;

    if (argc - 1 != 1) {
        fprintf(stderr, "You must specify 1 argument:  "
                " the name of the configuration file. "
                "You specified %d arguments.\n",  argc - 1);
        exit(1);
    }

    config = argv[1];

    err = kndDataWriter_new(&writer, config);
    if (err) {
        fprintf(stderr, "Couldn\'t load the DataWriter... ");
        return -1;
    }


    /* add device */
    err = pthread_create(&inbox,
                         NULL,
			 kndDataWriter_inbox, 
                         (void*)writer);
    
    
    /* add subscriber */
    err = pthread_create(&subscriber, 
			 NULL,
			 kndDataWriter_subscriber, 
                         (void*)writer);

    /* add selector */
    err = pthread_create(&selector, 
			 NULL,
			 kndDataWriter_selector, 
                         (void*)writer);

    writer->start(writer);

    return 0;
}

