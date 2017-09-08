#include <stdio.h>
#include <stdlib.h>

#include "knd_utils.h"
#include "knd_auth.h"

/*
void *kndColl_auth_service(void *arg)
{
    void *context;
    void *frontend;
    void *backend;

    context = zmq_init(1);

    frontend = zmq_socket(context, ZMQ_ROUTER);
    assert(frontend);
    backend = zmq_socket(context, ZMQ_DEALER);
    assert(backend);

    // tcp://127.0.0.1:6902
    assert(zmq_bind(frontend, "ipc:///var/knd/deliv") == 0);

    // tcp://127.0.0.1:6903
    assert(zmq_bind(backend, "ipc:///var/knd/deliv_backend") == 0);

    knd_log("    ++ KND Auth Service is up and running!...\n");

    zmq_proxy(frontend, backend, NULL);

    // we never get here
    zmq_close(frontend);
    zmq_close(backend);
    zmq_term(context);
    return NULL;
}
*/


int 
main(int const argc, 
     const char ** const argv) 
{
    struct kndAuth *auth;
    //pthread_t auth_service;

    const char *config = NULL;
    //const char *pid_filename = NULL;
    
    int err;

    if (argc - 1 != 1) {
        fprintf(stderr, "You must specify 1 argument:  "
                " the name of the configuration file. "
                "You specified %d arguments.\n",  argc - 1);
        exit(1);
    }

    config = argv[1];

    err = kndAuth_new(&auth, config);
    if (err) {
        fprintf(stderr, "Couldn\'t load kndAuth... ");
        return -1;
    }

    /* add auth service */
    /* ret = pthread_create(&auth_service,
			 NULL,
			 kndColl_auth_service,
			 (void*)auth); */
    err = auth->update(auth);
    if (err) return err;

    err = auth->update(auth);
    if (err) return err;

    auth->start(auth);

    return 0;
}


