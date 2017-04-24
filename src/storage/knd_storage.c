#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include <pthread.h>
#include <unistd.h>
#include <assert.h>

#include <libxml/parser.h>

#include "../../core/include/knd_config.h"
#include "knd_storage.h"
#include "../core/knd_utils.h"
#include "../core/knd_msg.h"

static int
kndStorage_str(struct kndStorage *self)
{
    printf("<struct kndStorage at %p>\n", (void *)self);

    return knd_OK;
}


static int
kndStorage_del(struct kndStorage *self)
{
    if (self->name)
        free(self->name);

    if (self->path)
        free(self->path);
    
    free(self);

    return knd_OK;
}


void *kndStorage_publisher(void *arg __attribute__((unused)))
{
    void *context;
    void *frontend;
    void *backend;

    context = zmq_init(1);

    frontend = zmq_socket(context, ZMQ_SUB);
    if (!frontend) pthread_exit(NULL);
    zmq_bind(frontend, "ipc:///var/knd/storage_sub");
    zmq_setsockopt(frontend, ZMQ_SUBSCRIBE, "", 0);

    backend = zmq_socket(context, ZMQ_PUB);
    if (!backend) pthread_exit(NULL);
    zmq_bind(backend, "ipc:///var/knd/storage_pub");

    printf("    ++ Storage Publisher device is ready....\n");

    zmq_device(ZMQ_FORWARDER, frontend, backend);

    /* we never get here */
    zmq_close(frontend);
    zmq_close(backend);
    zmq_term(context);

    return NULL;
}

/**
 *  kndStorage network service startup
 */
static int 
kndStorage_start(struct kndStorage *self)
{
    void *frontend;
    void *backend;
    pthread_t publisher;
    int ret;

    /* add publisher */
    ret = pthread_create(&publisher, 
			 NULL, 
			 kndStorage_publisher, (void*)self);

    /* create queue device */
    frontend = zmq_socket(self->context, ZMQ_PULL);
    assert(frontend);

    backend = zmq_socket(self->context, ZMQ_PUSH);
    assert(backend);

    ret = zmq_bind(frontend, "ipc:///var/knd/storage_pull");

    ret = zmq_bind(backend, "ipc:///var/knd/storage_push");

    printf("\n\n    ++ %s Storage Selector device is ready...\n\n",
	   self->name);

    zmq_device(ZMQ_QUEUE, frontend, backend);

    /* we never get here */
    zmq_close(frontend);
    zmq_close(backend);
    zmq_term(self->context);

    return knd_OK;
}

/*
static int
kndStorage_init(struct kndStorage *self)
{
    self->str = kndStorage_str;
    self->del = kndStorage_del;
    self->start = kndStorage_start;

    return knd_OK;
}
*/

static int
kndStorage_read_config(struct kndStorage *self,
                       const char *config)
{
    char buf[KND_TEMP_BUF_SIZE];
    xmlDocPtr doc;
    xmlNodePtr root;
    //xmlNodePtr cur_node;
    int err;

    buf[0] = '\0';

    doc = xmlParseFile((const char*)config);
    if (!doc) {
	fprintf(stderr, "\n    -- No prior config file found."
                        " Fresh start!\n");
	err = -1;
	goto final;
    }

    root = xmlDocGetRootElement(doc);
    if (!root) {
	fprintf(stderr,"empty document\n");
	err = -2;
	goto final;
    }

    if (xmlStrcmp(root->name, (const xmlChar *) "storage")) {
	fprintf(stderr,"Document of the wrong type: the root node " 
		" must be \"storage\"");
	err = -3;
	goto final;
    }

    err = knd_copy_xmlattr(root, "name", 
			   &self->name, &self->name_size);
    if (err) goto final;

    err = knd_copy_xmlattr(root, "path", 
			   &self->path, &self->path_size);
    if (err) goto final;


final:
    xmlFreeDoc(doc);

    return err;
}

int kndStorage_new(struct kndStorage **rec,
		   const char *config)
{
    struct kndStorage *self;
    int ret = knd_OK;
    
    self = malloc(sizeof(struct kndStorage));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndStorage));

    ret = kndStorage_read_config(self, config);
    if (ret != knd_OK) goto error;

    self->str = kndStorage_str;
    self->del = kndStorage_del;
    self->start = kndStorage_start;

    *rec = self;

    return knd_OK;

 error:

    kndStorage_del(self);

    return ret;
}
