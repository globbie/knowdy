#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <pthread.h>

#include "knd_collection.h"

#include <knd_config.h>
#include <knd_msg.h>
#include <knd_utils.h>


void *
kndColl_requester(void *arg)
{
    void *context;
    void *frontend;
    void *backend;
    struct kndColl *coll;
    int ret;
    
    coll = (struct kndColl*)arg;
    context = coll->context;

    /* requesting service */
    frontend = zmq_socket(context, ZMQ_PULL);
    assert(frontend);

    backend = zmq_socket(context, ZMQ_PUSH);
    assert(backend);

    ret = zmq_bind(frontend, coll->request_proxy_frontend);
    if (ret != knd_OK)
        knd_log("bind %s zmqerr: %s\n",
                coll->request_proxy_frontend, zmq_strerror(errno));
    
    assert((ret == knd_OK));
    
    
    ret = zmq_bind(backend, coll->request_proxy_backend);
    if (ret != knd_OK)
        knd_log("bind %s zmqerr: %s\n",
                coll->request_proxy_backend, zmq_strerror(errno));
    
    assert(ret == knd_OK);
 
    knd_log("    ++ Collection Requester proxy is ready: %s\n",
            coll->request_proxy_frontend);


    zmq_proxy(frontend, backend, NULL);
    
    /* we never get here */
    zmq_close(frontend);
    zmq_close(backend);
    zmq_term(context);
    
    return NULL;
}


void *kndColl_publisher(void *arg)
{
    void *context;
    void *frontend;
    void *backend;
    struct kndColl *coll;
    int ret;

    coll = (struct kndColl*)arg;
    context = coll->context;

    frontend = zmq_socket(context, ZMQ_SUB);
    assert(frontend);

    backend = zmq_socket(context, ZMQ_PUB);
    assert(backend);

    ret = zmq_bind(frontend, coll->publish_proxy_frontend);
    if (ret != knd_OK)
        knd_log("bind %s zmqerr: %s\n",
                coll->publish_proxy_frontend, zmq_strerror(errno));
    
    assert((ret == knd_OK));
    zmq_setsockopt(frontend, ZMQ_SUBSCRIBE, "", 0);
    
    
    ret = zmq_bind(backend, coll->publish_proxy_backend);
    if (ret != knd_OK)
        knd_log("bind %s zmqerr: %s\n",
                coll->publish_proxy_backend, zmq_strerror(errno));
    
    assert(ret == knd_OK);
    
    knd_log("    ++ Collection Publisher proxy is ready....\n");

    zmq_proxy(frontend, backend, NULL);
    
    /* we never get here */
    zmq_close(frontend);
    zmq_close(backend);
    zmq_term(context);

    return NULL;
}

void *kndColl_selector(void *arg)
{
    void *context;
    void *frontend;
    void *backend;
    struct kndColl *coll;
    int ret;

    coll = (struct kndColl*)arg;
    context = coll->context;

    frontend = zmq_socket(context, ZMQ_PULL);
    assert(frontend);

    backend = zmq_socket(context, ZMQ_PUSH);
    assert(backend);

    ret = zmq_bind(frontend, coll->select_proxy_frontend);
    if (ret != knd_OK)
        knd_log("bind %s zmqerr: %s\n",
                coll->select_proxy_frontend, zmq_strerror(errno));
    assert((ret == knd_OK));
    
    
    ret = zmq_bind(backend, coll->select_proxy_backend);
    if (ret != knd_OK)
        knd_log("bind %s zmqerr: %s\n",
                coll->select_proxy_backend, zmq_strerror(errno));
    assert(ret == knd_OK);
 
    knd_log("    ++ Collection Selector proxy is ready....\n");

    zmq_proxy(frontend, backend, NULL);

    /* we never get here */
    zmq_close(frontend);
    zmq_close(backend);
    zmq_term(context);
    return NULL;
}

static int
kndColl_str(struct kndColl *self)
{
    knd_log("<struct kndColl at %p>\n", self);

    return knd_OK;
}

static int
kndColl_del(struct kndColl *self)
{
    free(self);
    return knd_OK;
}

 
static int 
kndColl_find_route(struct kndColl *self,
		   const char *topics,
		   const char **dest_coll_addr)
{
    const char *dest = NULL;

    knd_log("    !! Root Collection: finding route "
            " to the appropriate Collection...\n"
	   "     TOPIC-based routing:\n %s\n\n", topics);

    /* TODO: proper selection */
    knd_log("   ++ Collection chosen: %s\n",
            self->name);

    *dest_coll_addr = dest;
    return knd_OK;
}



/**
 *  kndCollection network service startup
 */
static int
kndColl_start(struct kndColl *self)
{
    void *frontend;
    void *backend;

    pthread_t requester;
    pthread_t publisher;
    pthread_t selector;

    int ret;

    /* search and request service */
    ret = pthread_create(&requester, 
			 NULL,
			 kndColl_requester, (void*)self);

    /* add publisher */
    ret = pthread_create(&publisher, 
			 NULL, 
			 kndColl_publisher, (void*)self);

    /* add selector service */
    ret = pthread_create(&selector,
			 NULL,
			 kndColl_selector,
			 (void*)self);
 
    /* recording service */
    frontend = zmq_socket(self->context, ZMQ_PULL);
    assert(frontend);

    backend = zmq_socket(self->context, ZMQ_PUSH);
    assert(backend);
 
    ret = zmq_bind(frontend, self->record_proxy_frontend);
    assert(ret == knd_OK);
    
    ret = zmq_bind(backend, self->record_proxy_backend);
    assert(ret == knd_OK);
    
    knd_log("    ++ Collection Recorder proxy is up and running..\n\n");

    zmq_proxy(frontend, backend, NULL);
    
    return knd_OK;
}


static int
kndColl_init(struct kndColl *self)
{
    self->str = kndColl_str;
    self->del = kndColl_del;
    self->start = kndColl_start;
    self->find_route = kndColl_find_route;

    return knd_OK;
}




static int
kndColl_check_settings(struct kndColl *self)
{

    /* TODO: check proxies */
    if (!self->request_proxy_frontend) {
        knd_log("  Alert: no request proxy frontend specified!\n");
        return knd_FAIL;
    }

    if (!self->request_proxy_backend) {
        knd_log("  Alert: no request proxy backend specified!\n");
        return knd_FAIL;
    }

    return knd_OK;
}


extern int 
kndColl_new(struct kndColl **rec,
		const char *config)
{
    struct kndColl *self;
    int err;
    
    self = malloc(sizeof(struct kndColl));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndColl));

    kndColl_init(self);

    //err = kndColl_read_config(self, config);
    //if (err) goto error;

    err = kndColl_check_settings(self);
    if (err) goto error;
    
    *rec = self;
    return knd_OK;

 error:
    kndColl_del(self);
    return err;
}
