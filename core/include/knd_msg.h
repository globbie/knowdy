#ifndef KND_MSG_H
#define KND_MSG_H

#include <zmq.h>


/* ZeroMQ  version checking, and patch up missing constants to match 2.1 */
#ifndef ZMQ_ROUTER
#   define ZMQ_ROUTER ZMQ_XREP
#endif
#ifndef ZMQ_DEALER
#   define ZMQ_DEALER ZMQ_XREQ
#endif

#ifndef ZMQ_DONTWAIT
#   define ZMQ_DONTWAIT   ZMQ_NOBLOCK
#endif

#ifndef ZMQ_RCVHWM
#   define ZMQ_RCVHWM     ZMQ_HWM
#endif

#ifndef ZMQ_SNDHWM
#   define ZMQ_SNDHWM     ZMQ_HWM
#endif
#if ZMQ_VERSION_MAJOR == 2
#   define more_t int64_t
#   define zmq_ctx_destroy(context) zmq_term(context)
#   define zmq_msg_send(msg, sock, opt) zmq_send(sock, msg, opt)
#   define zmq_msg_recv(msg, sock, opt) zmq_recv(sock, msg, opt)
#   define ZMQ_POLL_MSEC    1000        //  zmq_poll is usec
#elif ZMQ_VERSION_MAJOR == 3
#   define more_t int
#   define ZMQ_POLL_MSEC    1           //  zmq_poll is msec
#endif


extern char *
knd_zmq_recv(void *socket, size_t *msg_size);

extern int
knd_zmq_send(void *socket, const char *string, size_t string_size);

extern int
knd_zmq_sendmore(void *socket, const char *string, size_t string_size);

extern int knd_recv_task(void *outbox, char *task, size_t *task_size);

#endif
