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
#include "knd_spec.h"
#include "knd_msg.h"

#include "knd_data_reader.h"

#define DEBUG_READER_LEVEL_1 0
#define DEBUG_READER_LEVEL_2 0
#define DEBUG_READER_LEVEL_3 0
#define DEBUG_READER_LEVEL_TMP 1

static int
kndDataReader_del(struct kndDataReader *self)
{
    if (self->path) free(self->path);
    /* TODO: storage */
    
    free(self);

    return knd_OK;
}

static int
kndDataReader_read_config(struct kndDataReader *self,
                         const char *config)
{
    struct kndDataClass *c = NULL;

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

    err = knd_copy_xmlattr(root, "path", 
			   &self->path, &self->path_size);
    if (err) return err;

    err = knd_copy_xmlattr(root, "webpath", 
			   &self->webpath, &self->webpath_size);
    if (err) return err;




    
    
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
                
                
                if (!xmlStrcmp(val, (const xmlChar*)"monitor")) {
                    err = knd_copy_xmlattr(sub_node, "addr", 
                                           &self->monitor_addr, 
                                           &self->monitor_addr_size);
                    if (err) return err;
                }
                
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
        !self->monitor_addr ||
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
kndDataReader_run_tasks(struct kndDataReader *self)
{
    struct kndSpecInstruction *instruct;
    int err;

    for (size_t i = 0; i < self->spec->num_instructions; i++) {
        instruct = &self->spec->instructions[i];
        
        switch (instruct->type) {
        case KND_AGENT_REPO:
            knd_log(".. REPO task %d in progress", i);
            self->admin->repo->instruct = instruct;
            err = self->admin->repo->run(self->admin->repo);
            if (err) return err;

            break;
        case KND_AGENT_USER:

            knd_log(".. USER task %d in progress", i);
            self->admin->instruct = instruct;
            err = self->admin->run(self->admin);
            if (err) return err;

            break;
        case KND_AGENT_DEFAULT:
            break;
        default:
            break;
        }

    }

    return knd_OK;
}


static int
kndDataReader_check_privileges(struct kndDataReader *self)
{
    struct kndUser *user;
    int err;

    if (DEBUG_READER_LEVEL_TMP)
        knd_log(".. checking current privileges");

    if (!self->spec->sid_size) {
        knd_log("-- no SID token provided :(");
        return knd_FAIL;
    }

    /* TODO: internal auth */
    if (strcmp(self->spec->sid, "AUTH_SERVER_SID")) {
        return knd_FAIL;
    }

    if (DEBUG_READER_LEVEL_TMP)
        knd_log("++ valid SID: \"%s\"!", self->spec->sid);
    
    if (!self->spec->uid_size) {
        knd_log("-- no UID provided: admin as default");
        self->curr_user = self->admin;
        return knd_OK;
    }

    if (DEBUG_READER_LEVEL_TMP)
        knd_log("== UID: \"%s\"", self->spec->uid);
    
    err = self->admin->get_user(self->admin, (const char*)self->spec->uid, &user);
    if (err) {
        knd_log(" -- UID \"%s\" not approved :(", self->spec->uid);
        return knd_FAIL;
    }

    if (DEBUG_READER_LEVEL_TMP)
        knd_log("++ valid UID: \"%s\"!",
                user->id);

    self->curr_user = user;
    
    /*  TODO: is UID granted access to Repo? */
    
    /* set DB path */
    /*buf_size = sprintf(buf, "%s/repos", self->path);
    
    err = knd_make_id_path(repo->path, buf, repo->id, NULL);
    if (err) goto final;
    
    self->curr_repo = repo;
    */
    
    /*buf_size = KND_SMALL_BUF_SIZE;
    err = knd_get_attr(spec, "policy",
                       buf, &buf_size);
    if (err) {
        knd_log("-- Policy %s is not granted to repo \"%s\" :(\n",
                buf, repo->id);
        return knd_FAIL;
    }

    
    err = repo->set_policy(repo, (const char*)buf, buf_size);
    if (err) {
        knd_log("-- Policy %s is not granted to repo \"%s\" :(\n",
                buf, repo->id);
        return err;
    }
    
    if (DEBUG_READER_LEVEL_2)
        knd_log("Policy %s is granted to repo \"%s\".\n", buf, repo->id);
    */
    
    return knd_OK;
}



static int
kndDataReader_start(struct kndDataReader *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    void *context;
    void *outbox;
    char *header = NULL;
    char *confirm = NULL;
    char *spec;
    size_t spec_size;
    char *obj;
    size_t obj_size;
    size_t chunk_size;
    int err;

    context = zmq_init(1);

    /* get messages from outbox */
    outbox = zmq_socket(context, ZMQ_PULL);
    if (!outbox) return knd_FAIL;
    err = zmq_connect(outbox, self->inbox_backend_addr);

    /* inform monitor */
    self->monitor = zmq_socket(context, ZMQ_PUSH);
    if (!self->monitor) return knd_FAIL;
    assert((zmq_connect(self->monitor,  self->monitor_addr) == knd_OK));

    /* updates */
    self->update = zmq_socket(context, ZMQ_PUSH);
    if (!self->update) return knd_FAIL;
    assert((zmq_connect(self->update,  self->update_addr) == knd_OK));

    /* delivery service */
    self->delivery = zmq_socket(context, ZMQ_REQ);
    if (!self->delivery) return knd_FAIL;
    assert((zmq_connect(self->delivery,  self->delivery_addr) == knd_OK));

    while (1) {
        self->spec->reset(self->spec);
        self->out->reset(self->out);
        self->spec_out->reset(self->spec_out);
        self->obj_out->reset(self->obj_out);

        buf[0] = '\0';
        
	knd_log("    ++ Reader #%s is ready to receive new tasks!\n",
                self->name);

	spec  = knd_zmq_recv(outbox, &spec_size);
	obj   = knd_zmq_recv(outbox, &obj_size);

        knd_log("\n    ++ Reader #%s got SPEC: %s OBJ: %s\n", 
                self->name, spec, obj);

        err = self->spec->parse(self->spec, spec, &chunk_size);
        if (err) {
            knd_log("  -- SPEC parse failed: %d\n", err);
            goto final;
        }

        /* check uid and privileges */
        err = kndDataReader_check_privileges(self);
        if (err) {
            knd_log("  -- privileges checking failure: %d\n", err);
            goto final;
        }

        knd_log("    SPEC after parsing: \"%s\" %lu\n", 
                spec, (unsigned long)spec_size);

        self->spec->input = spec;
        self->spec->input_size = spec_size;
        self->spec->obj = obj;
        self->spec->obj_size = obj_size;

        err = kndDataReader_run_tasks(self);
        if (err) {
            knd_log("  -- task running failure: %d", err);
            goto final;
        }


        


    final:

	if (header) {
	    free(header);
	    header = NULL;
	}
	if (confirm) {
	    free(confirm);
	    confirm = NULL;
	}

	/*fflush(stdout);*/
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
    
    /* result output buffer */
    err = kndOutput_new(&self->out, KND_IDX_BUF_SIZE);
    if (err) return err;

    /* spec output buffer */
    err = kndOutput_new(&self->spec_out, KND_TEMP_BUF_SIZE);
    if (err) return err;

    /* obj output buffer */
    err = kndOutput_new(&self->obj_out, KND_LARGE_BUF_SIZE);
    if (err) return err;

    /* task specification */
    err = kndSpec_new(&self->spec);
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
    
    /*err = kndDataClass_new(&self->dc);
    if (err) return err;
    self->dc->out = self->out;
    self->dc->name[0] = '/';
    self->dc->name_size = 1;

    self->dc->path = self->path;
    self->dc->path_size = self->path_size;
    */

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
    struct kndData *data;
    const char *empty_msg = "None";
    size_t empty_msg_size = strlen(empty_msg);

    int err;

    reader = (struct kndDataReader*)arg;

    context = zmq_init(1);

    err = kndData_new(&data);
    if (err) pthread_exit(NULL);

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
        
	data->reset(data);

	printf("\n    ++ READER SUBSCRIBER is waiting for new tasks...\n");

        data->spec = knd_zmq_recv(subscriber, &data->spec_size);

	printf("\n    ++ READER SUBSCRIBER has got spec:\n"
          "       %s\n",  data->spec);

	data->body = knd_zmq_recv(subscriber, &data->body_size);

	printf("\n    ++ READER SUBSCRIBER is updating its inbox..\n");


        
	knd_zmq_sendmore(inbox, data->spec, data->spec_size);
	knd_zmq_sendmore(inbox, data->body, data->body_size);
	knd_zmq_send(inbox, empty_msg, empty_msg_size);

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
