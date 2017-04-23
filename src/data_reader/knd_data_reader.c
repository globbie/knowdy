#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <syslog.h>

#include <pthread.h>
#include <unistd.h>
#include <assert.h>

#include <libxml/parser.h>

#include "../knd_config.h"
#include "../core/oodict.h"

#include "../core/knd_policy.h"
#include "../core/knd_user.h"
#include "../core/knd_output.h"
#include "knd_data_reader.h"
#include "../core/knd_dataclass.h"
#include "../core/knd_object.h"

#include "../core/knd_msg.h"
#include "../core/knd_utils.h"

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

/*
static int
kndDataReader_get_history(struct kndDataReader *self,
                          const char *classname,
                          struct kndObject *obj,
                          size_t state)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    char *header = NULL;
    size_t header_size;

    char *msg = NULL;
    size_t msg_size = 0;
    int err;

    knd_log("\n ... check history of \"%s\" %s..\n",
            classname, obj->id);

    buf_size = sprintf(buf, "<spec action=\"get_history\" "
                       "  class=\"%s\" obj_id=\"%s\" state=\"%lu\"/>\n",
                       classname, obj->id, (unsigned long)state);
    
    err = knd_zmq_sendmore(self->delivery, (const char*)buf, buf_size);
    err = knd_zmq_sendmore(self->delivery, "None", 4);
    err = knd_zmq_sendmore(self->delivery, "None", 4);
    err = knd_zmq_send(self->delivery, "None", 4);

    header = knd_zmq_recv(self->delivery, &header_size);
    msg = knd_zmq_recv(self->delivery, &msg_size);
    if (!msg_size) {
        err = knd_FAIL;
        goto final;
    }

    if (msg[0] == '{') {
        err = knd_OK;
        goto final;
    }
    
    //err = self->update->read_history(self->update,
    //                                 classname, obj->id,
    //                                 (const char*)msg, msg_size);
 final:

    if (header)
        free(header);

    if (msg)
        free(msg);
    
    return err;
}
*/

static int
kndDataReader_get_user(struct kndDataReader *self, const char *spec)
{
    char buf[KND_TEMP_BUF_SIZE];
    struct kndUser *user;
    //struct kndPolicy *policy;
    int err;

    if (DEBUG_READER_LEVEL_3)
        knd_log("\n   .. checking current privileges: %s\n", spec);

    /* HACK : space added to diff from "guid" */
    /*err = knd_get_attr(spec, " uid",
                       buf, &buf_size);
    if (err) {
        knd_log("-- no UID provided :(\n");
        return knd_FAIL;
        }*/
    
    /*if (!strcmp(buf, "000")) {
        user = self->admin;
    }
    else { */
    
    err = self->admin->get_user(self->admin, (const char*)buf, &user);
    if (err) {
        knd_log(" -- user not approved :(\n");
        return knd_FAIL;
    }

    /* set default LANG */
    memcpy(self->lang_code, user->lang_code, user->lang_code_size);
    self->lang_code_size = user->lang_code_size;

    self->curr_user = user;
                    
    return err;
}



static int
kndDataReader_get_repo(struct kndDataReader *self,
                       const char *name, size_t name_size,
                       struct kndRepo **result)
{
    char buf[KND_TEMP_BUF_SIZE];
    struct kndRepo *repo = NULL;
    int err;

    repo = self->repo_idx->get(self->repo_idx, name);
    if (repo) {
        *result = repo;
        return knd_OK;
    }

    /* check repo's name */
    err = knd_is_valid_id(name, name_size);
    if (err) return err;

    err = kndRepo_new(&repo);
    if (err) return knd_NOMEM;

    sprintf(buf, "%s/repos", self->path);

    err = knd_make_id_path(repo->path, buf, name, NULL);
    if (err) return err;

    err = knd_mkpath(repo->path, 0755, false);
    if (err) return err;

    repo->user = self->admin;
    strncpy(repo->name, name, name_size);
    repo->name_size = name_size;

    if (!strcmp(repo->name, self->default_repo_name)) {
        if (self->default_repo_title_size) {
            strncpy(repo->title, self->default_repo_title, self->default_repo_title_size);
            repo->title_size = self->default_repo_title_size;
        }
    }

    
    knd_log("  ==  REPO DIR: \"%s\" TITLE: %s\n",
            repo->path, repo->title);

    
    err = self->repo_idx->set(self->repo_idx, name, (void*)repo);
    if (err) return err;

    *result = repo;
    
    return knd_OK;
}


static int  
kndDataReader_reply(struct kndDataReader *self,
                    struct kndData *data,
                    int status)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    char *header = NULL;
    size_t header_size;
    char *confirm = NULL;
    size_t confirm_size;

    int err;

    data->tid_size = KND_TID_SIZE + 1;

    /*err = knd_get_attr(data->spec, "tid",
                       data->tid, &data->tid_size);
    if (err) {
        knd_log("-- no TID provided :(\n");
        return knd_FAIL;
    }
    */
    
    if (status) {                   
        err = self->spec_out->write(self->spec_out, "<spec action=\"report\"", strlen("<spec action=\"report\""));
        if (err) goto final;

        buf_size = sprintf(buf, " state=\"%d\"",
                           status);

        err = self->spec_out->write(self->spec_out, buf, buf_size);
        if (err) goto final;
    }
    else {
        err = self->spec_out->write(self->spec_out, "<spec action=\"save\"", strlen("<spec action=\"save\""));
        if (err) goto final;
    }

    buf_size = sprintf(buf, " uid=\"%s\" tid=\"%s\" sid=\"AUTH_SERVER_SID\" ",
                       self->curr_user->id, data->tid);

    err = self->spec_out->write(self->spec_out, buf, buf_size);
    if (err) goto final;

    if (data->classname_size) {
        err = self->spec_out->write(self->spec_out, " class=\"", strlen(" class=\""));
        if (err) goto final;

        err = self->spec_out->write(self->spec_out, data->classname, data->classname_size);
        if (err) goto final;

        err = self->spec_out->write(self->spec_out, "\"", 1);
        if (err) goto final;
    }

    switch (data->format) {
    case KND_FORMAT_XML:
        err = self->spec_out->write(self->spec_out, " format=\"XML\"", strlen(" format=\"XML\""));
        if (err) goto final;
        break;
    case KND_FORMAT_HTML:
        err = self->spec_out->write(self->spec_out, " format=\"HTML\"", strlen(" format=\"HTML\""));
        if (err) goto final;
        break;
    case KND_FORMAT_JS:
        err = self->spec_out->write(self->spec_out, " format=\"JS\"", strlen(" format=\"JS\""));
        if (err) goto final;
        break;
    default:
        break;
    }

    buf_size = sprintf(buf, " filename=\"%s\"",
                       data->filename);

    err = self->spec_out->write(self->spec_out, buf, buf_size);
    if (err) goto final;

    buf_size = sprintf(buf, " filesize=\"%lu\"",
                       (unsigned long)data->filesize);
    err = self->spec_out->write(self->spec_out, buf, buf_size);
    if (err) goto final;

    buf_size = sprintf(buf, " mime=\"%s\"",
                       data->mimetype);

    err = self->spec_out->write(self->spec_out, buf, buf_size);
    if (err) goto final;

    /*err = kndDataReader_read_obj_file(self, obj, &objfile, &objfile_size);
    if (err)
    goto final; */

    err = self->spec_out->write(self->spec_out, "/>", strlen("/>"));
    if (err) goto final;

    if (!self->out->buf_size) {
        err = self->out->write(self->out, "None", strlen("None"));
        if (err) goto final;
    }
    
    err = knd_zmq_sendmore(self->delivery, (const char*)self->spec_out->buf, self->spec_out->buf_size);

    if (data->query_size && strcmp(data->query, "None")) {
        err = knd_zmq_sendmore(self->delivery, (const char*)data->query, data->query_size);
    }
    else if (data->name_size) {
        err = knd_zmq_sendmore(self->delivery, (const char*)data->name, data->name_size);
    }
    else {
        err = knd_zmq_sendmore(self->delivery, "None", strlen("None"));
    }

    
    err = knd_zmq_sendmore(self->delivery, self->out->buf, self->out->buf_size);

    if (self->obj_out->buf_size) {
        err = knd_zmq_send(self->delivery, self->obj_out->buf, self->obj_out->buf_size);
    }
    else {
        err = knd_zmq_send(self->delivery, "None", strlen("None"));
    }


    /* get reply from delivery */
    header = knd_zmq_recv(self->delivery, &header_size);
    confirm = knd_zmq_recv(self->delivery, &confirm_size);

    if (DEBUG_READER_LEVEL_TMP)
        knd_log("  .. Delivery Service reply: %s\n", confirm);

 final:

    if (header)
        free(header);

    if (confirm)
        free(confirm);
    
    return err;
}

static int
kndDataReader_start(struct kndDataReader *self)
{
    char buf[KND_TEMP_BUF_SIZE];

    struct kndData *data;

    void *context;
    void *outbox;

    char *header = NULL;

    char *confirm = NULL;


    int err;

    err = kndData_new(&data);
    if (err) return err;

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

        data->reset(data);
        self->out->reset(self->out);
        self->spec_out->reset(self->spec_out);
        self->obj_out->reset(self->obj_out);

        buf[0] = '\0';
        
	knd_log("\n    ++ Reader #%s is ready to receive new tasks!\n",
                self->name);

	data->spec  = knd_zmq_recv(outbox, &data->spec_size);
	data->obj   = knd_zmq_recv(outbox, &data->obj_size);
	data->query = knd_zmq_recv(outbox, &data->query_size);

	knd_log("\n    ++ Reader #%s got spec: %s QUERY: %s\n", 
                self->name, data->spec, data->query);

        /* TODO: regroup incoming messages  */
        if (!data->spec_size) {
            continue;
        }
        
        /* check uid and privileges */
        err = kndDataReader_get_user(self, data->spec);
        if (err) {
            knd_log("  -- get user by uid  failure: %d\n", err);
            goto final;
        }

	if (strstr(data->spec, "action=\"idle\"")) {
            printf("    ?? Any idle time jobs needed?\n");

            /* TODO: check the number of uninterrupted idle calls and act:
             * do time consuming tasks like sort, compress, dump indices.. etc.
             */
            goto final;
        }

        if (strstr(data->spec, "action=\"update\"")) {
            printf("Reader UPDATE: %s\n", data->spec);

            goto final;
        }

        if (strstr(data->spec, "action=\"select\"")) {
            err = self->curr_user->select(self->curr_user, data);

            err = kndDataReader_reply(self, data, err);
            goto final;
        }

	if (strstr(data->spec, "action=\"get\"")) {
            err = self->curr_user->get_obj(self->curr_user, data);

            err = kndDataReader_reply(self, data, err);
            goto final;
        }

        if (strstr(data->spec, "action=\"flatten\"")) {
            err = self->curr_user->flatten(self->curr_user, data);

            err = kndDataReader_reply(self, data, err);
            goto final;
        }

        if (strstr(data->spec, "action=\"match\"")) {
            err = self->curr_user->match(self->curr_user, data);

            err = kndDataReader_reply(self, data, err);
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
    data->del(data);

    return knd_OK;
}


extern int
kndDataReader_new(struct kndDataReader **rec,
                  const char *config)
{
    struct kndDataReader *self;
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

    /* special user */
    err = kndUser_new(&self->admin);
    if (err) return err;
    err = ooDict_new(&self->admin->user_idx, KND_SMALL_DICT_SIZE);
    if (err) goto error;

    self->admin->reader = self;
    
    /* read config */
    err = kndDataReader_read_config(self, config);
    if (err) {
        knd_log("  -- XML config read error :(\n");
        goto error;
    }
    
    err = kndDataClass_new(&self->dc);
    if (err) return err;
    self->dc->out = self->out;
    self->dc->name[0] = '/';
    self->dc->name_size = 1;

    self->dc->path = self->path;
    self->dc->path_size = self->path_size;

    err = ooDict_new(&self->dc->class_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&self->dc->attr_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&self->repo_idx, KND_SMALL_DICT_SIZE);
    if (err) goto error;

    /* read class definitions */
    err = self->dc->read_onto(self->dc, "classes.gsl");
    if (err) goto error;

    err = self->dc->coordinate(self->dc);
    if (err) goto error;

    self->del = kndDataReader_del;
    self->start = kndDataReader_start;
    self->get_repo = kndDataReader_get_repo;

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
