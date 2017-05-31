#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include <libxml/parser.h>

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
kndDataWriter_read_XML_config(struct kndDataWriter *self,
                              const char *config)
{
    xmlDocPtr doc = NULL;
    xmlNodePtr root, cur_node, sub_node;

    xmlChar *val = NULL;
    //long num_value;
    int err;
    
    doc = xmlParseFile((const char*)config);
    if (!doc) {
	knd_log("-- DataWriter: no config file found."
                " Fresh start!\n");
	err = -1;
	goto error;
    }

    root = xmlDocGetRootElement(doc);
    if (!root) {
	knd_log("-- DataWriter: config is empty?\n");
	err = -2;
	goto error;
    }

    if (xmlStrcmp(root->name, (const xmlChar *) "db")) {
	fprintf(stderr,"Document of the wrong type: the root node " 
		" must be \"db\"");
	err = -3;
	goto error;
    }

    err = knd_copy_xmlattr(root, "name", 
			   &self->name, &self->name_size);
    if (err) return err;

    if (self->name_size > KND_ID_SIZE) {
        knd_log("  -- DB name must not exceed %lu characters :(",
                (unsigned long)KND_ID_SIZE);
        return knd_FAIL;
    }
    
    err = knd_copy_xmlattr(root, "path", 
			   &self->path, &self->path_size);
    if (err) return err;

    knd_log("  == DB PATH OK: %s\n", self->path);
    
    self->admin->sid_size = KND_TID_SIZE + 1;
    err = knd_get_xmlattr(root, "sid",
                          self->admin->sid, &self->admin->sid_size);
    if (err) {
        knd_log("-- administrative SID is not set :(");
        return err;
    }
    
    memcpy(self->admin->id, self->name, self->name_size);

    /* users path */
    self->admin->dbpath = self->path;
    self->admin->dbpath_size = self->path_size;

    memcpy(self->admin->path, self->path, self->path_size);
    memcpy(self->admin->path + self->path_size, "/users", strlen("/users"));
    self->admin->path_size = self->path_size + strlen("/users");
    self->admin->path[self->admin->path_size] = '\0';

    for (cur_node = root->children; 
         cur_node; 
         cur_node = cur_node->next) {
        if (cur_node->type != XML_ELEMENT_NODE) continue;
        
	if ((!xmlStrcmp(cur_node->name, (const xmlChar *)"devices"))) {
            for (sub_node = cur_node->children; 
                 sub_node; 
                 sub_node = sub_node->next) {
                if (sub_node->type != XML_ELEMENT_NODE) continue;

                if ((xmlStrcmp(sub_node->name, (const xmlChar *)"device")))
                    continue;

                val = xmlGetProp(sub_node,  (const xmlChar *)"name");
                if (!val) continue;

                if (strcmp((const char*)val, "write_inbox")) continue;

                err = knd_copy_xmlattr(sub_node, "frontend",
                                       &self->inbox_frontend_addr, 
                                       &self->inbox_frontend_addr_size);
                if (err) return err;
                
                err = knd_copy_xmlattr(sub_node, "backend",
                                       &self->inbox_backend_addr, 
                                       &self->inbox_backend_addr_size);
                if (err) return err;
            }
        }

	if ((!xmlStrcmp(cur_node->name, (const xmlChar *)"services"))) {

            for (sub_node = cur_node->children; 
                 sub_node; 
                 sub_node = sub_node->next) {
                if (sub_node->type != XML_ELEMENT_NODE) continue;

                if ((xmlStrcmp(sub_node->name, (const xmlChar *)"service")))
                    continue;

                val = xmlGetProp(sub_node,  (const xmlChar *)"name");
                if (!val) continue;

                if (!strcmp((const char*)val, "delivery")) {
                    err = knd_copy_xmlattr(sub_node, "addr", 
                                           &self->delivery_addr, 
                                           &self->delivery_addr_size);
                    if (err) return err;
                }
                xmlFree(val);
            }
        }
        
    }
    
    if (!self->inbox_frontend_addr ||
        !self->inbox_backend_addr  ||
        !self->delivery_addr)
        return oo_FAIL;

     err = knd_OK;

     
error:
     
     if (doc)
         xmlFreeDoc(doc);
     
     knd_log("   == config read: %d\n", err);
     
     return err;
}


/*static int  
kndDataWriter_reply(struct kndDataWriter *self,
                    struct kndData *data)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    char *header = NULL;
    size_t header_size;
    char *confirm = NULL;
    size_t confirm_size;

    int err;

    if (!self->spec->tid_size) {
        knd_log("-- no TID provided for reply :(\n");
        return knd_FAIL;
    }

    
    buf_size = sprintf(buf, "<spec action=\"save\" uid=\"%s\" "
                       " tid=\"%s\" sid=\"AUTH_SERVER_SID\" ",
                       self->curr_user->id, self->spec->tid);

    err = self->spec_out->write(self->spec_out, buf, buf_size);
    if (err) goto final;

    
    if (data->filepath_size) {
        buf_size = sprintf(buf,
                           " filepath=\"%s\" filesize=\"%lu\" mime=\"%s\"",
                           data->filepath,
                           (unsigned long)data->filesize,
                           data->mimetype);

        err = self->spec_out->write(self->spec_out, buf, buf_size);
        if (err) goto final;
    }
    
    err = self->spec_out->write(self->spec_out, "/>", strlen("/>"));
    if (err) goto final;

    if (!self->out->buf_size) {
        err = self->out->write(self->out, "None", strlen("None"));
        if (err) goto final;
    }
    
    err = knd_zmq_sendmore(self->delivery, (const char*)self->spec_out->buf, self->spec_out->buf_size);
    err = knd_zmq_sendmore(self->delivery, (const char*)data->name, data->name_size);
    err = knd_zmq_sendmore(self->delivery, self->out->buf, self->out->buf_size);
    err = knd_zmq_send(self->delivery, "None", strlen("None"));

    header = knd_zmq_recv(self->delivery, &header_size);
    confirm = knd_zmq_recv(self->delivery, &confirm_size);

    if (DEBUG_WRITER_LEVEL_2)
        knd_log("  MSG SPEC: %s\n\n  == Delivery Service reply: %s\n",
                self->spec_out->buf, confirm);

 final:

    if (header)
        free(header);

    if (confirm)
        free(confirm);
    
    return err;
}

*/


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
    
    while (1) {
        self->task->reset(self->task);

        if (DEBUG_WRITER_LEVEL_TMP)
            knd_log("    ++ DATAWRITER AGENT #%s is ready to receive new tasks!", 
                    self->name);

	task = knd_zmq_recv(outbox, &task_size);
	obj = knd_zmq_recv(outbox, &obj_size);
        
	knd_log("    ++ DATAWRITER AGENT #%s got task: %s [%lu]\n", 
                self->name, task, (unsigned long)task_size);

        err = self->task->run(self->task, task, task_size, obj, obj_size);
        if (err) {
            knd_log("  -- task running failure: %d", err);
            goto final;
        }

    final:

        /*err = kndDataWriter_reply(self);
         */
        
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

    /* output buffer for linearized indices */
    err = kndOutput_new(&self->out, KND_IDX_BUF_SIZE);
    if (err) return err;

    /* buffer for specs */
    err = kndOutput_new(&self->spec_out, KND_TEMP_BUF_SIZE);
    if (err) return err;

    /* obj output buffer */
    err = kndOutput_new(&self->obj_out, KND_LARGE_BUF_SIZE);
    if (err) return err;

    /* task specification */
    err = kndTask_new(&self->task);
    if (err) return err;
    
    err = kndOutput_new(&self->task->out, KND_LARGE_BUF_SIZE);
    if (err) return err;
    
    /* special user */
    err = kndUser_new(&self->admin);
    if (err) return err;
    self->task->admin = self->admin;
    
    /* admin indices */
    err = ooDict_new(&self->admin->user_idx, KND_SMALL_DICT_SIZE);
    if (err) goto error;
    
    err = ooDict_new(&self->admin->repo_idx, KND_SMALL_DICT_SIZE);
    if (err) goto error;

    /* read config */
    err = kndDataWriter_read_XML_config(self, config);
    if (err) {
        knd_log("config parse: %d", err);
        return err;
    }
    
    err = kndDataClass_new(&dc);
    if (err) return err;
    dc->out = self->out;
    dc->name[0] = '/';
    dc->name_size = 1;

    dc->dbpath = self->path;
    dc->dbpath_size = self->path_size;

    err = ooDict_new(&dc->class_idx, KND_SMALL_DICT_SIZE);
    if (err) goto error;
    
    /* read class definitions */
    err = dc->read_onto(dc, "classes/index.gsl");
    if (err) goto error;
    
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

    int err;

    writer = (struct kndDataWriter*)arg;

    context = zmq_init(1);

    err = kndData_new(&data);
    if (err) pthread_exit(NULL);

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
	data->reset(data);

        data->spec = knd_zmq_recv(subscriber, &data->spec_size);
	data->obj = knd_zmq_recv(subscriber, &data->obj_size);

	knd_zmq_sendmore(inbox, data->spec, data->spec_size);
	knd_zmq_send(inbox, data->obj, data->obj_size);

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

    xmlInitParser();

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

