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
#include "knd_parser.h"

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


static int
parse_read_inbox_addr(void *obj,
                      const char *rec,
                      size_t *total_size)
{
    struct kndDataReader *self = (struct kndDataReader*)obj;

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
parse_config_GSL(struct kndDataReader *self,
                 const char *rec,
                 size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = KND_NAME_SIZE;
    size_t chunk_size = 0;
    
    const char *gsl_format_tag = "{gsl";
    size_t gsl_format_tag_size = strlen(gsl_format_tag);

    const char *header_tag = "{knd::Knowdy Reader Service Configuration";
    size_t header_tag_size = strlen(header_tag);
    const char *c;

    self->name_size = KND_NAME_SIZE;
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
        { .name = "read_inbox",
          .name_size = strlen("read_inbox"),
          .parse = parse_read_inbox_addr,
          .obj = self
        },
        { .is_default = true,
          .name = "set_service_id",
          .name_size = strlen("set_service_id"),
          .buf = self->name,
          .buf_size = &self->name_size
        }
    };
    int err = knd_FAIL;

    if (!strncmp(rec, gsl_format_tag, gsl_format_tag_size)) {
        rec += gsl_format_tag_size;
        err = knd_get_schema_name(rec,
                                  buf, &buf_size, &chunk_size);
        if (!err) {
            rec += chunk_size;
            
            if (DEBUG_READER_LEVEL_TMP)
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
kndDataReader_new(struct kndDataReader **rec,
                  const char *config)
{
    struct kndDataReader *self;
    struct kndDataClass *dc;
    size_t chunk_size = 0;
    int err;

    self = malloc(sizeof(struct kndDataReader));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndDataReader));

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
    
    err = ooDict_new(&self->admin->user_idx, KND_SMALL_DICT_SIZE);
    if (err) goto error;

    /* read config */
    err = self->out->read_file(self->out, config, strlen(config));
    if (err) {
        knd_log("  -- config file read error :(");
        goto error;
    }
    
    err = parse_config_GSL(self, self->out->file, &chunk_size);
    if (err) {
        knd_log("  -- config parsing error :(");
        goto error;
    }

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
