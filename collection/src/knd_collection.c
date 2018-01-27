#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <pthread.h>

#include "knd_collection.h"
#include "knd_config.h"
#include "knd_msg.h"
#include "knd_utils.h"
#include "knd_task.h"

#include <gsl-parser.h>

#define DEBUG_COLL_LEVEL_0 0
#define DEBUG_COLL_LEVEL_1 0
#define DEBUG_COLL_LEVEL_2 0
#define DEBUG_COLL_LEVEL_3 0
#define DEBUG_COLL_LEVEL_TMP 1


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
 
    knd_log("  ++ Collection Selector proxy is ready....\n");

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
    knd_log("== request proxy frontend: %s", self->request_proxy_frontend);
    if (!*self->request_proxy_frontend) {
        knd_log("  Alert: no request proxy frontend specified!\n");
        return knd_FAIL;
    }

    knd_log("== request proxy backend: %s", self->request_proxy_backend);
    if (!*self->request_proxy_backend) {
        knd_log("  Alert: no request proxy backend specified!\n");
        return knd_FAIL;
    }

    if (!*self->record_proxy_backend) {
        knd_log("  Alert: no record proxy frontend specified!\n");
        return knd_FAIL;
    }
    if (!*self->record_proxy_backend) {
        knd_log("  Alert: no record proxy backend specified!\n");
        return knd_FAIL;
    }
    
    if (!*self->select_proxy_backend) {
        knd_log("  Alert: no select proxy frontend specified!\n");
        return knd_FAIL;
    }
    if (!*self->select_proxy_backend) {
        knd_log("  Alert: no select proxy backend specified!\n");
        return knd_FAIL;
    }

    if (!*self->publish_proxy_backend) {
        knd_log("  Alert: no publish proxy frontend specified!\n");
        return knd_FAIL;
    }
    if (!*self->publish_proxy_backend) {
        knd_log("  Alert: no publish proxy backend specified!\n");
        return knd_FAIL;
    }

    
    return knd_OK;
}


static gsl_err_t
parse_request_service_addr(void *self,
                           const char *rec,
                           size_t *total_size)
{
    struct kndColl *coll = (struct kndColl *)self;
    
    struct gslTaskSpec specs[] = {
        { .name = "frontend",
          .name_size = strlen("frontend"),
          .buf = coll->request_proxy_frontend,
          .buf_size = &coll->request_proxy_frontend_size,
          .max_buf_size = sizeof coll->request_proxy_frontend
        },
        { .name = "backend",
          .name_size = strlen("backend"),
          .buf = coll->request_proxy_backend,
          .buf_size = &coll->request_proxy_backend_size,
          .max_buf_size = sizeof coll->request_proxy_backend
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}


static gsl_err_t
parse_record_service_addr(void *self,
                          const char *rec,
                          size_t *total_size)
{
    struct kndColl *coll = (struct kndColl *)self;
    struct gslTaskSpec specs[] = {
        { .name = "frontend",
          .name_size = strlen("frontend"),
          .buf = coll->record_proxy_frontend,
          .buf_size = &coll->record_proxy_frontend_size,
          .max_buf_size = sizeof coll->record_proxy_frontend
        },
        { .name = "backend",
          .name_size = strlen("backend"),
          .buf = coll->record_proxy_backend,
          .buf_size = &coll->record_proxy_backend_size,
          .max_buf_size = sizeof coll->record_proxy_backend
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t
parse_publish_service_addr(void *self,
                           const char *rec,
                           size_t *total_size)
{
    struct kndColl *coll = (struct kndColl *)self;
    struct gslTaskSpec specs[] = {
        { .name = "frontend",
          .name_size = strlen("frontend"),
          .buf = coll->publish_proxy_frontend,
          .buf_size = &coll->publish_proxy_frontend_size,
          .max_buf_size = sizeof coll->publish_proxy_frontend
        },
        { .name = "backend",
          .name_size = strlen("backend"),
          .buf = coll->publish_proxy_backend,
          .buf_size = &coll->publish_proxy_backend_size,
          .max_buf_size = sizeof coll->publish_proxy_backend
        }
    };


    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t
parse_select_service_addr(void *self,
                          const char *rec,
                          size_t *total_size)
{
    struct kndColl *coll = (struct kndColl *)self;

    struct gslTaskSpec specs[] = {
        { .name = "frontend",
          .name_size = strlen("frontend"),
          .buf = coll->select_proxy_frontend,
          .buf_size = &coll->select_proxy_frontend_size,
          .max_buf_size = sizeof coll->select_proxy_frontend
        },
        { .name = "backend",
          .name_size = strlen("backend"),
          .buf = coll->select_proxy_backend,
          .buf_size = &coll->select_proxy_backend_size,
          .max_buf_size = sizeof coll->select_proxy_backend
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int
parse_config_GSL(struct kndColl *self,
                 const char *rec,
                 size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = KND_NAME_SIZE;
    size_t chunk_size = 0;
    
    const char *gsl_format_tag = "{gsl";
    size_t gsl_format_tag_size = strlen(gsl_format_tag);

    const char *header_tag = "{knd::Collection Service Configuration";
    size_t header_tag_size = strlen(header_tag);
    const char *c;
    
    struct gslTaskSpec specs[] = {
        { .name = "request",
          .name_size = strlen("request"),
          .parse = parse_request_service_addr,
          .obj = self
        },
        { .name = "record",
          .name_size = strlen("record"),
          .parse = parse_record_service_addr,
          .obj = self
        },
        { .name = "publish",
          .name_size = strlen("publish"),
          .parse = parse_publish_service_addr,
          .obj = self
        },
        { .name = "select",
          .name_size = strlen("select"),
          .parse = parse_select_service_addr,
          .obj = self
        }
    };
    int err;
    gsl_err_t parser_err;

    if (!strncmp(rec, gsl_format_tag, gsl_format_tag_size)) {
        rec += gsl_format_tag_size;
        err = knd_get_schema_name(rec,
                                  buf, &buf_size, &chunk_size);
        if (!err) {
            rec += chunk_size;
            if (DEBUG_COLL_LEVEL_TMP)
                knd_log("== got schema: \"%.*s\"", buf_size, buf);
        }
    }
    
    if (strncmp(rec, header_tag, header_tag_size)) {
        knd_log("-- wrong GSL class header");
        return err;
    }
    c = rec + header_tag_size;
    
    parser_err = gsl_parse_task(c, total_size, specs, sizeof specs  / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- config parse error: %d", parser_err.code);
        return gsl_err_to_knd_err_codes(parser_err);
    }
    
    return knd_OK;
}


extern int 
kndColl_new(struct kndColl **rec,
		const char *config)
{
    struct kndColl *self;
    size_t total_chunk_parsed = 0;
    
    int err;
    
    self = malloc(sizeof(struct kndColl));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndColl));

    kndColl_init(self);

    err = kndOutput_new(&self->out, KND_TEMP_BUF_SIZE);
    if (err) goto error;

    err = self->out->read_file(self->out, config, strlen(config));
    if (err) goto error;
  
    err = parse_config_GSL(self, self->out->file, &total_chunk_parsed);
    if (err) goto error;

    err = kndColl_check_settings(self);
    if (err) goto error;
    
    *rec = self;
    return knd_OK;

 error:
    kndColl_del(self);
    return err;
}
