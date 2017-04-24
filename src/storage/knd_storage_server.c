#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>


#include "../../core/include/knd_config.h"
#include "../core/knd_utils.h"
#include "../core/knd_msg.h"
#include "knd_storage.h"

void *kndStorage_subscriber(void *arg)
{
    void *context;
    void *reception;
    //void *agents;
    void *inbox;
    void *publisher;

    struct kndStorage *storage;
    struct kndData *data;

    int ret;

    storage = (struct kndStorage*)arg;
    context = storage->context;

    ret = kndData_new(&data);
    if (ret != knd_OK) pthread_exit(NULL);

    reception = zmq_socket(context, ZMQ_SUB);
    if (!reception) pthread_exit(NULL);

    ret = zmq_connect(reception, "ipc:///var/knd/coll_pub_backend");
    assert(ret == knd_OK);
    zmq_setsockopt(reception, ZMQ_SUBSCRIBE, "", 0);

    inbox = zmq_socket(context, ZMQ_PUSH);
    assert(inbox);
    
    ret = zmq_connect(inbox, "ipc:///var/knd/storage_pull");
    assert(ret == knd_OK);
    
    publisher = zmq_socket(context, ZMQ_PUB);
    if (!publisher) pthread_exit(NULL);
    
    ret = zmq_connect(publisher, "ipc:///var/knd/storage_sub");

    while (1) {
        data->reset(data);
        data->spec = knd_zmq_recv(reception, &data->spec_size);
        data->obj = knd_zmq_recv(reception, &data->obj_size);

        /*printf("\n    ++ STORAGE Subscriber has got spec:\n"
          "       %s\n",  data->spec); */
        /*if (strstr(data->spec, "action=\"idle\"")) {
            printf("inform all partitions about IDLE..\n\n");

            knd_zmq_sendmore(publisher, data->spec, data->spec_size);
            knd_zmq_send(publisher, "None", 4);

            goto flush;
            }*/

        /*printf("\n    ++ STORAGE Subscriber is updating the queue..\n");*/

        knd_zmq_sendmore(inbox, data->spec, data->spec_size);
        knd_zmq_send(inbox, data->obj, data->obj_size);

    //flush:
        fflush(stdout);
    }

    /* we never get here */
    zmq_close(reception);
    zmq_term(context);

    return NULL;
}


/*void *kndStorage_selector(void *arg)
{
    void *context;
    void *coll;
    void *inbox;

    struct kndStorage *storage;
    struct kndData *data;

    int ret;

    storage = (struct kndStorage*)arg;
    context = storage->context;

    ret = kndData_new(&data);
    if (ret != knd_OK) pthread_exit(NULL);

    coll = zmq_socket(context, ZMQ_PULL);
    if (!coll) pthread_exit(NULL);
    zmq_connect(coll, "tcp://127.0.0.1:6913");

    inbox = zmq_socket(context, ZMQ_PUB);
    if (!inbox) pthread_exit(NULL);
    zmq_connect(inbox, "tcp://127.0.0.1:6940");

    while (1) {

	data->reset(data);

	printf("\n    ++ STORAGE Selector is waiting for new tasks...\n");
        data->spec = knd_zmq_recv(coll, &data->spec_size);

	printf("got spec: %s\n\n", data->spec);

	data->body = knd_zmq_recv(coll, &data->body_size);
	printf("got body: %s\n\n", data->body);

	printf("\n    ++ STORAGE Selector is publishing task to all Subscribers..\n");

	knd_zmq_sendmore(inbox, data->spec, data->spec_size);
	knd_zmq_send(inbox, data->body, data->body_size);

    final:

        fflush(stdout);
    }

    zmq_close(coll);
    zmq_close(inbox);
    zmq_term(context);

    return NULL;
}
*/


void *kndStorage_timer(void *arg)
{
    void *context;
    void *coll;
    void *inbox;

    struct kndStorage *storage;
    int ret;

    const char *spec = "<spec action=\"beat\"/>";
    size_t spec_size = strlen(spec);

    storage = (struct kndStorage*)arg;
    context = storage->context;

    inbox = zmq_socket(context, ZMQ_PUB);
    assert(inbox);

    ret = zmq_connect(inbox, "tcp://127.0.0.1:6910");
    assert(ret == knd_OK);
 
    while (1) {
        sleep(KND_IDLE_TIMEOUT);

        /*knd_log("\n    ++ STORAGE Timer is sending heartbeat signal to all Readers..\n");*/

        knd_zmq_sendmore(inbox, spec, spec_size);
        knd_zmq_sendmore(inbox, "None", 4);
        knd_zmq_sendmore(inbox, "None", 4);
        knd_zmq_sendmore(inbox, "None", 4);
        knd_zmq_send(inbox, "None", 4);


        fflush(stdout);
    }

    /* we never get here */
    zmq_close(coll);
    zmq_close(inbox);
    zmq_term(context);

    return NULL;
}




int 
main(int const argc, 
     const char ** const argv) 
{
    struct kndStorage *storage;
    const char *config = NULL;
    void *context;

    pthread_t reception;
    //pthread_t search_service;
    //pthread_t timer;

    int err;

    if (argc - 1 != 1) {
        fprintf(stderr, "You must specify 1 argument:  "
                " the name of the configuration file. "
                "You specified %d arguments.\n",  argc - 1);
        exit(1);
    }

    config = argv[1];

    xmlInitParser();

    err = kndStorage_new(&storage, config);
    if (err) {
        fprintf(stderr, "Couldn\'t create kndStorage... ");
        return -1;
    }


    context = zmq_init(1);
    storage->context = context;

    /* add a reception */
    err = pthread_create(&reception,
			 NULL,
			 kndStorage_subscriber,
			 (void*)storage);

    /* add search service */
    /*err = pthread_create(&search_service,
			 NULL,
			 kndStorage_selector,
			 (void*)storage); */

    /* add timer */
    /*err = pthread_create(&timer,
			 NULL,
			 kndStorage_timer,
			 (void*)storage);
    */
    
    storage->start(storage);

    return 0;
}


