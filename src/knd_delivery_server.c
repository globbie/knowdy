#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

#include <libxml/parser.h>

#include <zmq.h>
#include "zhelpers.h"

#include "knd_config.h"
#include "knd_utils.h"
#include "knd_delivery.h"

void *kndColl_delivery_service(void *arg)
{
    void *context;
    void *frontend;
    void *backend;
    int ret;

    context = zmq_init(1);

    frontend = zmq_socket(context, ZMQ_ROUTER);
    assert(frontend);
    backend = zmq_socket(context, ZMQ_DEALER);
    assert(backend);

    /* tcp://127.0.0.1:6902 */
    assert(zmq_bind(frontend, "ipc:///var/knd/deliv") == 0);

    /* tcp://127.0.0.1:6903 */
    assert(zmq_bind(backend, "ipc:///var/knd/deliv_backend") == 0);

    knd_log("    ++ KND Delivery Service is up and running!...\n");

    zmq_proxy(frontend, backend, NULL);

    /* we never get here */
    zmq_close(frontend);
    zmq_close(backend);
    zmq_term(context);
    return NULL;
}


int 
main(int const argc, 
     const char ** const argv) 
{
    struct kndDelivery *delivery;
    pthread_t delivery_service;

    const char *config = NULL;
    const char *pid_filename = NULL;
    
    int err;

    if (argc - 1 != 1) {
        fprintf(stderr, "You must specify 1 argument:  "
                " the name of the configuration file. "
                "You specified %d arguments.\n",  argc - 1);
        exit(1);
    }

    config = argv[1];
   
    xmlInitParser();

    err = kndDelivery_new(&delivery, config);
    if (err) {
        fprintf(stderr, "Couldn\'t load kndDelivery... ");
        return -1;
    }

    if (delivery->is_daemon)
        knd_daemonize(delivery->pid_filename);

    delivery->name = "KND DELIVERY SERVICE";

    /* add delivery service */
    /* ret = pthread_create(&delivery_service,
			 NULL,
			 kndColl_delivery_service,
			 (void*)delivery); */

    delivery->start(delivery);

    return 0;
}


