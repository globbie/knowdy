#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include <pthread.h>

#include <libxml/parser.h>

#include "../knd_config.h"
#include "knd_collection.h"
#include "../core/knd_msg.h"
#include "../core/knd_utils.h"


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

    knd_log("   ++ Collection chosen: %s\n", self->name);

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
kndColl_read_proxies(struct kndColl *self,
                    xmlNodePtr input_node)
{
    xmlNodePtr cur_node;
    char *proxy_name, *frontend, *backend;
    size_t strsize;
    int ret;

    proxy_name = NULL;
    frontend = NULL;
    backend = NULL;

    for (cur_node = input_node->children; 
         cur_node; 
         cur_node = cur_node->next) {
        if (cur_node->type != XML_ELEMENT_NODE) continue;
                
        if ((xmlStrcmp(cur_node->name, (const xmlChar *)"proxy")))
            continue;
                
        ret = knd_copy_xmlattr(cur_node, "name", 
                               &proxy_name, 
                               &strsize);
        if (ret != knd_OK) {
            knd_log("  Alert: proxy name not specified!\n");
            return ret;
        }

        ret = knd_copy_xmlattr(cur_node, "frontend", 
                               &frontend, 
                               &strsize);
        if (ret != knd_OK) {
            knd_log("  Alert: no request proxy frontend specified!\n");
            return ret;
        }
        
        ret = knd_copy_xmlattr(cur_node, "backend",
                               &backend, 
                               &strsize);
        if (ret != knd_OK) {
            knd_log("  Alert: no request proxy backend specified!\n");
            return ret;
        }

        
        if (!strcmp(proxy_name, "request")) {
            self->request_proxy_frontend = frontend;
            self->request_proxy_backend = backend;
            goto proxy_end;
        }

        if (!strcmp(proxy_name, "record")) {
            self->record_proxy_frontend = frontend;
            self->record_proxy_backend = backend;
            goto proxy_end;
        }

        if (!strcmp(proxy_name, "publish")) {
            self->publish_proxy_frontend = frontend;
            self->publish_proxy_backend = backend;
            goto proxy_end;
        }

        if (!strcmp(proxy_name, "select")) {
            self->select_proxy_frontend = frontend;
            self->select_proxy_backend = backend;
            goto proxy_end;
        }
        
        
    proxy_end:
        free(proxy_name);
        proxy_name = NULL;
        frontend = NULL;
        backend = NULL;
    }

    return knd_OK;
}

static int
kndColl_read_config(struct kndColl *self,
                    const char *config)
{
    char buf[KND_TEMP_BUF_SIZE];
    xmlDocPtr doc;
    xmlNodePtr root, cur_node;
    int err;

    buf[0] = '\0';

    doc = xmlParseFile(config);
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

    if (xmlStrcmp(root->name, (const xmlChar *) "icoll")) {
	fprintf(stderr,"Document of the wrong type: the root node " 
		" must be \"icoll\"");
	err = -3;
	goto final;
    }

    err = knd_copy_xmlattr(root, "name", 
			   &self->name, &self->name_size);
    if (err) goto final;


    for (cur_node = root->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type != XML_ELEMENT_NODE) continue;

        if ((!xmlStrcmp(cur_node->name, (const xmlChar *)"proxies"))) {
            kndColl_read_proxies(self, cur_node);
        }

    }


    
final:
    xmlFreeDoc(doc);

    return err;
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
    int ret;
    
    self = malloc(sizeof(struct kndColl));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndColl));

    kndColl_init(self);

    ret = kndColl_read_config(self, config);
    if (ret != knd_OK) goto error;

    ret = kndColl_check_settings(self);
    if (ret != knd_OK) goto error;
    
    *rec = self;
    return knd_OK;

 error:
    kndColl_del(self);
    return ret;
}
