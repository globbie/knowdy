#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

#include <zmq.h>
#include "zhelpers.h"

#include "knd_config.h"
#include "knd_collection.h"
#include "knd_utils.h"

#define NUM_RECORDERS 1
#define NUM_REQUESTERS 1

struct agent_args {
    int agent_id;
    struct kndColl *collection;
    void *zmq_context;
};

void *kndColl_recorder_agent(void *arg)
{
    struct agent_args *args;
    struct kndColl *coll;

    void *inbox;
    void *publisher;

    const char *dest_coll_addr = NULL;

    struct kndData *data;

    int ret;

    args = (struct agent_args*)arg;
    coll = args->collection;

    ret = kndData_new(&data);
    if (ret != knd_OK) pthread_exit(NULL);

    inbox = zmq_socket(args->zmq_context, ZMQ_PULL);
    assert(inbox);

    ret = zmq_connect(inbox, coll->record_proxy_backend);
    assert(ret == knd_OK);

    publisher = zmq_socket(args->zmq_context, ZMQ_PUB);
    assert(publisher);

    ret = zmq_connect(publisher, coll->publish_proxy_frontend);
    assert(ret == knd_OK);

    while (1) {
        data->reset(data);

	knd_log("    !! Collection Recorder Agent #%d is ready!\n", 
	       args->agent_id);

	/* waiting for spec */
        data->spec = s_recv(inbox, &data->spec_size);

	knd_log("    !! Collection Recorder Agent #%d: got spec \"%s\"\n", 
	       args->agent_id, data->spec);
	data->obj = s_recv(inbox, &data->obj_size);


        /* TODO: semantic routing */
	/*ret = coll->find_route(coll, buf, &dest_coll_addr); */

	/* stay in this collection */
	if (!dest_coll_addr) {
            /* TODO check overflow */
            /* send task to all storages */
	    s_sendmore(publisher, data->spec, data->spec_size);
	    s_send(publisher, data->obj, data->obj_size);
	}

        fflush(stdout);
    }

    zmq_close(inbox);


    return NULL;
}


void *kndColl_requester_agent(void *arg)
{
    struct agent_args *args;
    struct kndColl *coll;

    void *inbox;
    void *selector;

    const char *dest_coll_addr = NULL;

    struct kndData *data;

    int ret;

    args = (struct agent_args*)arg;
    coll = args->collection;

    ret = kndData_new(&data);
    assert(ret == knd_OK);

    inbox = zmq_socket(args->zmq_context, ZMQ_PULL);
    assert(inbox);
    
    ret = zmq_connect(inbox, coll->request_proxy_backend);
    if (ret != knd_OK)
        knd_log("ERR: %s\n", zmq_strerror(errno));
    assert(ret == knd_OK);
    
    selector = zmq_socket(args->zmq_context, ZMQ_PUSH);
    assert(selector);
    
    ret = zmq_connect(selector, coll->select_proxy_frontend);
    if (ret != knd_OK)
        knd_log("ERR: %s\n", zmq_strerror(errno));
    assert(ret == knd_OK);

    while (1) {
	data->reset(data);

	knd_log("    !! Collection Requester Agent #%d listens to %s\n", 
                args->agent_id, coll->request_proxy_backend);

	/* waiting for spec */
        data->spec = s_recv(inbox, &data->spec_size);
	data->obj = s_recv(inbox, &data->obj_size);

	knd_log("    !! Collection Requester Agent #%d: got spec \"%s\"\n", 
	       args->agent_id, data->spec);

	ret = coll->find_route(coll, data->topics, &dest_coll_addr);

	/* stay in this collection */
	if (!dest_coll_addr) {
            /* send task to one of the storages */
	    s_sendmore(selector, data->spec, data->spec_size);
	    s_send(selector, data->obj, data->obj_size);
        }

        fflush(stdout);
    }

    zmq_close(inbox);


    return NULL;
}


int 
main(int           const argc, 
     const char ** const argv) 
{
    void *context;
    struct kndColl *coll;

    pthread_t recorder;
    pthread_t requester;

    struct agent_args rec_args[NUM_RECORDERS];
    struct agent_args req_args[NUM_REQUESTERS];

    const char *config = NULL;

    int i, ret;

    if (argc - 1 != 1) {
        fprintf(stderr, "You must specify 1 argument:  "
                " the name of the configuration file. "
                "You specified %d arguments.\n",  argc - 1);
        exit(1);
    }

    config = argv[1];
    ret = kndColl_new(&coll, config);
    if (ret) {
        fprintf(stderr, "Couldn\'t load kndColl... ");
        return -1;
    }

    context = zmq_init(1);

    coll->context = context;

    /* pool of collection recorders */
    for (i = 0; i < NUM_RECORDERS; i++) {
        rec_args[i].agent_id = i; 
        rec_args[i].collection = coll; 
        rec_args[i].zmq_context = context;
 
        ret = pthread_create(&recorder,
                             NULL,
                             kndColl_recorder_agent, 
                             (void*)&rec_args[i]);
    }

    /* pool of collection requesters */
    for (i = 0; i < NUM_REQUESTERS; i++) {
        req_args[i].agent_id = i; 
        req_args[i].collection = coll; 
        req_args[i].zmq_context = context;
 
        ret = pthread_create(&requester, 
                             NULL, 
                             kndColl_requester_agent, 
                             (void*)&req_args[i]);
    }

    coll->start(coll);
 
    /* we never get here */
    zmq_term(context);

    return 0;
}


