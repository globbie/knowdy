#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <assert.h>

#include <libxml/parser.h>

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
kndDataReader_read_config(struct kndDataReader *self,
                         const char *config)
{
    struct kndDataClass *c = NULL;

    const char *default_db_path = "/usr/lib/knowdy/";
    const char *default_schema_path = "/etc/knowdy/schemas/";

    xmlDocPtr doc;
    xmlNodePtr root, cur_node, sub_node;
    xmlChar *val = NULL;

    int err;

    doc = xmlParseFile(config);
    if (!doc) {
	fprintf(stderr, "\n    -- DataReader: no config file found."
                        " Fresh start!\n");
	err = -1;
	goto error;
    }

    root = xmlDocGetRootElement(doc);
    if (!root) {
	fprintf(stderr,"empty document\n");
	err = -2;
	goto error;
    }

    if (xmlStrcmp(root->name, (const xmlChar *) "db")) {
	fprintf(stderr, "Document of the wrong type: the root node " 
		" must be \"db\"");
	err = -3;
	goto error;
    }

    err = knd_copy_xmlattr(root, "name", 
			   &self->name, &self->name_size);
    if (err) return err;

    
    memcpy(self->path, default_db_path, strlen(default_db_path));
    self->path_size = strlen(default_db_path);

    self->path_size = KND_TEMP_BUF_SIZE;
    err = knd_get_xmlattr(root, "path", 
                          self->path, &self->path_size);
    if (err) {
        knd_log("-- custom DB path not set, using default:  %s", self->path);
    }
    else {
        knd_log("== custom DB path set to \"%s\"", self->path);
    }
    
    /* default schema path */
    memcpy(self->schema_path, default_schema_path, strlen(default_schema_path));
    self->schema_path_size = strlen(default_schema_path);

    self->schema_path_size = KND_TEMP_BUF_SIZE;
    err = knd_get_xmlattr(root, "schema",
                          self->schema_path, &self->schema_path_size);
    if (err) {
        knd_log("-- custom schemas path not set, using default:  '%s'", self->schema_path);
    } else {
        knd_log("== custom schemas path set to '%s'", self->schema_path);
    }

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

    
    
    val = xmlGetProp(root,  (const xmlChar *)"default_repo");
    if (val) {
        self->default_repo_name_size = strlen((const char*)val);
        if (self->default_repo_name_size >= KND_NAME_SIZE) return knd_LIMIT;
        
        strcpy(self->default_repo_name, (const char*)val);
        
        xmlFree(val);
        val = NULL;
    }

    val = xmlGetProp(root,  (const xmlChar *)"default_repo_title");
    if (val) {
        self->default_repo_title_size = strlen((const char*)val);
        if (self->default_repo_title_size >= KND_TEMP_BUF_SIZE) return knd_LIMIT;
        
        strcpy(self->default_repo_title, (const char*)val);
        xmlFree(val);
        val = NULL;
    }

    for (cur_node = root->children; 
         cur_node; 
         cur_node = cur_node->next) {
        if (cur_node->type != XML_ELEMENT_NODE) continue;


        /*if ((!xmlStrcmp(cur_node->name, (const xmlChar *)"pids"))) {
            for (sub_node = cur_node->children; 
                 sub_node; 
                 sub_node = sub_node->next) {
                if (sub_node->type != XML_ELEMENT_NODE) continue;

                if ((xmlStrcmp(sub_node->name, (const xmlChar *)"pid")))
                    continue;

                val = xmlGetProp(sub_node,  (const xmlChar *)"name");
                if (!val) continue;

                if (strcmp((const char*)val, "read_pid")) continue;
                err = knd_copy_xmlattr(sub_node, "path", 
                                       &self->pid_filename, 
                                       &curr_size);
                if (err) return err;
            }
            } */


        if ((!xmlStrcmp(cur_node->name, (const xmlChar *)"devices"))) {

            for (sub_node = cur_node->children; 
                 sub_node; 
                 sub_node = sub_node->next) {
                if (sub_node->type != XML_ELEMENT_NODE) continue;

                if ((xmlStrcmp(sub_node->name, (const xmlChar *)"device")))
                    continue;

                val = xmlGetProp(sub_node,  (const xmlChar *)"name");
                if (!val) continue;

                if (!strcmp((const char*)val, "write_inbox")) {
                    err = knd_copy_xmlattr(sub_node, "frontend", 
                                           &self->update_addr, 
                                           &self->update_addr_size);
                    if (err) return err;
                    continue;
                }

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
                
                
                if (!xmlStrcmp(val, (const xmlChar*)"delivery")) {
                    err = knd_copy_xmlattr(sub_node, "addr", 
                                           &self->delivery_addr,
                                           &self->delivery_addr_size);
                    if (err) return err;
                }

                xmlFree(val);
                val = NULL;
            }
        }


    }

    if (!self->inbox_frontend_addr ||
        !self->inbox_backend_addr  ||
        !self->delivery_addr)
        return oo_FAIL;

    err = knd_OK;
    
    
error:

    if (val)
        xmlFree(val);

    if (c)
        c->del(c);
    
    xmlFreeDoc(doc);

    return err;
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

    while (1) {
        self->task->reset(self->task);
        
	knd_log("    ++ Reader #%s is ready to receive new tasks!\n",
                self->name);

	task  = knd_zmq_recv(outbox, &task_size);
	obj   = knd_zmq_recv(outbox, &obj_size);

        knd_log("\n    ++ Reader #%s got TASK: %s OBJ: %s\n", 
                self->name, task, obj);

        err = self->task->run(self->task, task, task_size, obj, obj_size);
        if (err) {
            knd_log("  -- TASK parse failed: %d\n", err);
            goto final;
        }

    final:
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
    
    /* task taskification */
    err = kndTask_new(&self->task);
    if (err) return err;

    /* special user */
    err = kndUser_new(&self->admin);
    if (err) return err;
    err = ooDict_new(&self->admin->user_idx, KND_SMALL_DICT_SIZE);
    if (err) goto error;

    /* read config */
    err = kndDataReader_read_config(self, config);
    if (err) {
        knd_log("  -- XML config read error :(\n");
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

    xmlInitParser();

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
