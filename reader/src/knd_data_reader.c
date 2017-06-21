#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <assert.h>

#include "knd_user.h"
#include "knd_output.h"
#include "knd_dataclass.h"
#include "knd_task.h"
#include "knd_utils.h"
#include "knd_msg.h"

#include "knd_data_reader.h"

#define DEBUG_READER_LEVEL_1 0
#define DEBUG_READER_LEVEL_2 0
#define DEBUG_READER_LEVEL_3 0
#define DEBUG_READER_LEVEL_TMP 1

static int
kndDataReader_del(struct kndDataReader *self)
{
    /* TODO: storage */
    
    free(self);

    return knd_OK;
}

static int
kndDataReader_start(struct kndDataReader *self)
{
    void *context;
    void *outbox;
    char *task;
    size_t task_size;
    char *obj;
    size_t obj_size;
    int err;

    err = self->admin->restore(self->admin);
    if (err) return err;

    context = zmq_init(1);

    /* get messages from outbox */
    outbox = zmq_socket(context, ZMQ_PULL);
    if (!outbox) return knd_FAIL;

    err = zmq_connect(outbox, self->inbox_backend_addr);
    
    /* liquid updates */
    self->update_service = zmq_socket(context, ZMQ_PUSH);
    if (!self->update_service) return knd_FAIL;
    assert((zmq_connect(self->update_service,  self->update_addr) == knd_OK));
    self->admin->update_service = self->update_service;
    
    /* delivery service */
    self->delivery = zmq_socket(context, ZMQ_REQ);
    if (!self->delivery) return knd_FAIL;
    assert((zmq_connect(self->delivery,  self->delivery_addr) == knd_OK));

    self->task->delivery = self->delivery;
    
    while (1) {
        self->task->reset(self->task);
	knd_log("    ++ Reader #%s is ready to receive new tasks!\n",
                self->name);

	task  = knd_zmq_recv(outbox, &task_size);
	obj   = knd_zmq_recv(outbox, &obj_size);

        knd_log("\n    ++ Reader #%s got task: %s OBJ: %s\n", 
                self->name, task, obj);

        err = self->task->run(self->task, task, task_size, obj, obj_size);
        if (err) {
            self->task->error = err;
            knd_log("-- task run failed: %d", err);
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

    zmq_close(outbox);

    return knd_OK;
}


extern int
kndDataReader_new(struct kndDataReader **rec,
                  const char *config)
{
    struct kndDataReader *self;
    struct kndDataClass *dc;
    int err;

    self = malloc(sizeof(struct kndDataReader));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndDataReader));

    err = kndOutput_new(&self->out, KND_IDX_BUF_SIZE);
    if (err) return err;

    err = kndTask_new(&self->task);
    if (err) return err;

    err = kndOutput_new(&self->task->out, KND_IDX_BUF_SIZE);
    if (err) return err;

    err = kndUser_new(&self->admin);
    if (err) return err;
    self->task->admin = self->admin;
    self->admin->out = self->out;
    
    err = ooDict_new(&self->admin->user_idx, KND_SMALL_DICT_SIZE);
    if (err) goto error;
    
    /*err = kndDataReader_read_config(self, config);
    if (err) {
        knd_log("  -- config read error :(\n");
        goto error;
        }*/

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

    self->del = kndDataReader_del;
    self->start = kndDataReader_start;

    *rec = self;
    return knd_OK;

 error:

    knd_log("  -- Data Reader failure :(\n");
    
    kndDataReader_del(self);
    return err;
}




/** SERVICES */

void *kndDataReader_inbox(void *arg)
{
    void *context;
    void *frontend;
    void *backend;
    struct kndDataReader *reader;
    int err;

    context = zmq_init(1);
    reader = (struct kndDataReader*)arg;

    frontend = zmq_socket(context, ZMQ_PULL);
    assert(frontend);
    
    backend = zmq_socket(context, ZMQ_PUSH);
    assert(backend);

    /* knd_log("%s <-> %s\n", reader->inbox_frontend_addr, reader->inbox_backend_addr); */

    err = zmq_bind(frontend, reader->inbox_frontend_addr);
    assert(err == knd_OK);

    err = zmq_bind(backend, reader->inbox_backend_addr);
    assert(err == knd_OK);

    knd_log("    ++ DataReader \"%s\" Inbox device is ready...\n\n", 
            reader->name);

    zmq_device(ZMQ_QUEUE, frontend, backend);

    /* we never get here */
    zmq_close(frontend);
    zmq_close(backend);
    zmq_term(context);
    return NULL;
}

void *kndDataReader_selector(void *arg)
{
    void *context;
    void *frontend;
    void *backend;
    struct kndDataReader *reader;
    int err;

    context = zmq_init(1);
    reader = (struct kndDataReader*)arg;

    frontend = zmq_socket(context, ZMQ_PULL);
    assert(frontend);
    
    backend = zmq_socket(context, ZMQ_PUSH);
    assert(backend);
    
    err = zmq_connect(frontend, "tcp://127.0.0.1:6913");
    assert(err == knd_OK);

    err = zmq_connect(backend, reader->inbox_frontend_addr);
    assert(err == knd_OK);

    knd_log("    ++ DataReader %s Selector device is ready: %s...\n\n",
            reader->name,
            reader->inbox_frontend_addr);

    zmq_device(ZMQ_QUEUE, frontend, backend);

    /* we never get here */
    zmq_close(frontend);
    zmq_close(backend);
    zmq_term(context);

    return NULL;
}

void *kndDataReader_subscriber(void *arg)
{
    void *context;
    void *subscriber;
    void *inbox;

    struct kndDataReader *reader;

    char *obj;
    size_t obj_size;
    char *task;
    size_t task_size;
    
    int err;

    reader = (struct kndDataReader*)arg;

    context = zmq_init(1);

    subscriber = zmq_socket(context, ZMQ_SUB);
    assert(subscriber);

    err = zmq_connect(subscriber, "ipc:///tmp/writer_pub");
    assert(err == knd_OK);
    
    zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, "", 0);

    inbox = zmq_socket(context, ZMQ_PUSH);
    assert(inbox);

    err = zmq_connect(inbox, reader->inbox_frontend_addr);
    assert(err == knd_OK);
    
    while (1) {
	printf("    ++ READER SUBSCRIBER is waiting for new tasks...\n");

        task = knd_zmq_recv(subscriber, &task_size);

	printf("    ++ READER SUBSCRIBER has got task:\n"
          "       %s",  task);

	obj = knd_zmq_recv(subscriber, &obj_size);

	printf("    ++ READER SUBSCRIBER is updating its inbox..\n");
        
	knd_zmq_sendmore(inbox, task, task_size);
	knd_zmq_send(inbox, obj, obj_size);

	printf("    ++ all messages sent!");

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
    struct kndDataReader *reader;

    const char *config = NULL;

    pthread_t subscriber;
    pthread_t selector;
    pthread_t inbox;
    int err;

    if (argc - 1 != 1) {
        fprintf(stderr, "You must specify 1 argument:  "
                " the name of the configuration file."
                "You specified %d arguments.\n",  argc - 1);
        exit(1);
    }

    config = argv[1];

    err = kndDataReader_new(&reader, config);
    if (err) {
        fprintf(stderr, "Couldn\'t load the DataReader... ");
        return -1;
    }
    
    /* add device */
    err = pthread_create(&inbox, 
			 NULL,
			 kndDataReader_inbox, 
                         (void*)reader);
    
    /* add subscriber */
    err = pthread_create(&subscriber, 
			 NULL,
			 kndDataReader_subscriber, 
                         (void*)reader);
   
    
    /* add selector */
    err = pthread_create(&selector, 
			 NULL,
			 kndDataReader_selector, 
                         (void*)reader);

    reader->start(reader);
    
    
    return 0;
}
