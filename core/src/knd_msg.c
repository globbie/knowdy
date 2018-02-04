#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include "knd_msg.h"
#include "knd_utils.h"

extern int knd_recv_task(void *outbox, char *task, size_t *task_size)
{
    zmq_msg_t message;
    int64_t msg_size = 0;
    size_t max_msg_size = *task_size;

    do {
	zmq_msg_init(&message);
	msg_size = zmq_msg_recv(&message, outbox, 0);
	if (msg_size == -1) {
	    knd_log("-- no msg received :(");
	    knd_log("ZMQ err: %s\n", zmq_strerror(errno));
	    zmq_msg_close(&message);
	    continue;
	}
	msg_size = zmq_msg_size(&message);
	if (msg_size < 1) {
	    knd_log("-- negative msg size :(");
	    knd_log("ZMQ err: %s\n", zmq_strerror(errno));
	    zmq_msg_close(&message);
	    continue;
	}
	if (!msg_size) {
	    knd_log("-- zero msg size :(");
	    knd_log("ZMQ err: %s\n", zmq_strerror(errno));
	    zmq_msg_close(&message);
	    continue;
	}
	if ((size_t)msg_size >= max_msg_size) {
	    knd_log("-- msg too large :(");
	    *task_size = 0;
	    zmq_msg_close(&message);
	    continue;
	}
	//knd_log("++ got MSG [size:%zu]", msg_size);

	memcpy(task, zmq_msg_data(&message), msg_size);
	task[msg_size] = '\0';
	*task_size = msg_size;
	zmq_msg_close(&message);
	break;
    } while (1);

    if (!(*task_size)) return knd_FAIL;

    return knd_OK;
}

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
    
    memcpy(string, zmq_msg_data(&message), size);
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
knd_zmq_sendmore(void *socket, const char *string, size_t string_size)
{
    int rc;
    zmq_msg_t message;
    zmq_msg_init_size (&message, string_size);
    memcpy (zmq_msg_data (&message), string, string_size);
    rc = zmq_msg_send(&message, socket, ZMQ_SNDMORE);
    zmq_msg_close (&message);
    return (rc);
}


/* HTTP-communication via CURL*/
extern int
knd_http_post(const char *url)
{
    CURL *curl;
    CURLcode res;
 
    curl_global_init(CURL_GLOBAL_ALL);
 
    /* get a curl handle */ 
    curl = curl_easy_init();
    if (!curl) {
	curl_global_cleanup();
	return knd_FAIL;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    /* POST data */ 
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "msg_type=curl");
 
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
	fprintf(stderr, "curl_easy_perform() failed: %s\n",
		curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return knd_OK;
}
