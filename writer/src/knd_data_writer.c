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

extern int
kndDataWriter_new(struct kndDataWriter **rec,
                  const char *config)
{
    struct kndDataWriter *self;
    struct kndDataClass *dc;
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
    /*err = kndDataWriter_read_XML_config(self, config);
    if (err) return err;
    */
    
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

