#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>

#include <knd_config.h>
#include <knd_utils.h>
#include <knd_msg.h>

#include "knd_storage.h"

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
    zmq_bind(frontend, "ipc:///var/lib/knowdy/storage_sub"); // fixme
    zmq_setsockopt(frontend, ZMQ_SUBSCRIBE, "", 0);

    backend = zmq_socket(context, ZMQ_PUB);
    if (!backend) pthread_exit(NULL);
    zmq_bind(backend, "ipc:///var/lib/knowdy/storage_pub"); // fixme

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
    if (ret) return knd_FAIL;

    /* create queue device */
    frontend = zmq_socket(self->context, ZMQ_PULL);
    assert(frontend);

    backend = zmq_socket(self->context, ZMQ_PUSH);
    assert(backend);

    ret = zmq_bind(frontend, "ipc:///var/lib/knowdy/storage_pull"); // fixme

    ret = zmq_bind(backend, "ipc:///var/lib/knowdy/storage_push"); // fixme

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


int kndStorage_new(struct kndStorage **rec,
		   const char *config)
{
    struct kndStorage *self;
    int err;
    
    self = malloc(sizeof(struct kndStorage));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndStorage));

    //err = kndStorage_read_config(self, config);
    //if (ret != knd_OK) goto error;

    self->str = kndStorage_str;
    self->del = kndStorage_del;
    self->start = kndStorage_start;

    *rec = self;

    return knd_OK;

 error:

    kndStorage_del(self);
    return err;
}
