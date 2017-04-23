#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../knd_config.h"
#include "knd_msg.h"

/**
 *  Receive 0MQ string from socket and convert into C string
 *  Caller must free returned string. Returns NULL if the context
 *  is being terminated.
 */
extern char *
knd_zmq_recv(void *socket, size_t *msg_size) 
{
    zmq_msg_t message;
    int size;
    char *string = NULL;
    
    zmq_msg_init(&message);

    size = zmq_msg_recv(&message, socket, 0);
    if (size == -1) goto final;
    
    size = zmq_msg_size(&message);
    if (size < 1) goto final;
    
    string = malloc(size + 1);
    if (!string) goto final;
    
    memcpy(string, zmq_msg_data (&message), size);
    string[size] = 0;

    *msg_size = size;

 final:
    zmq_msg_close(&message);
    return string;
}


/**
 *  Convert C string to 0MQ string and send to socket
 */
extern int
knd_zmq_send(void *socket, const char *string, size_t string_size) 
{
    int rc;
    zmq_msg_t message;
    zmq_msg_init_size (&message, string_size);
    memcpy (zmq_msg_data(&message), string, string_size);
    rc = zmq_msg_send(&message, socket, 0);
    zmq_msg_close (&message);
    return (rc);
}

/**
 *  Sends string as 0MQ string, as multipart non-terminal
 */
extern int
knd_zmq_sendmore (void *socket, const char *string, size_t string_size)
{
    int rc;
    zmq_msg_t message;
    zmq_msg_init_size (&message, string_size);
    memcpy (zmq_msg_data (&message), string, string_size);
    rc = zmq_msg_send(&message, socket, ZMQ_SNDMORE);
    zmq_msg_close (&message);
    return (rc);
}
