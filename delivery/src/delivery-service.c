#include "delivery-service.h"

#include <knd_err.h>
#include <knd_utils.h>

static int
task_callback(struct kmqEndPoint *endpoint __attribute__((unused)), struct kmqTask *task,
        void *cb_arg)
{
    struct kndDeliveryService *self = cb_arg;
    const char *data;
    size_t size;
    int err;

    (void) self;
    err = task->get_data(task, 0, &data, &size);
    if (err != knd_OK) { knd_log("-- task read failed"); return -1; }

    printf(">>>\n%.*s\n<<<\n", (int) size, data);

    return 0;
}

static int
start__(struct kndDeliveryService *self)
{
    knd_log("delivery has been started\n");
    self->knode->dispatch(self->knode);
    knd_log("delivery has been stopped\n");
    return knd_FAIL;
}

static int
delete__(struct kndDeliveryService *self)
{
    if (self->entry_point) self->entry_point->del(self->entry_point);
    if (self->knode) self->knode->del(self->knode);
    free(self);

    return knd_OK;
}

int
kndDeliveryService_new(struct kndDeliveryService **service, const struct kndDeliveryOptions *opts)
{
    struct kndDeliveryService *self;
    int err;

    self = calloc(1, sizeof(*self));
    if (!self) return knd_FAIL;
    self->opts = opts;

    err = kmqKnode_new(&self->knode);
    if (err != 0) goto error;

    err = kmqEndPoint_new(&self->entry_point);
    self->entry_point->options.type = KMQ_PULL;
    self->entry_point->options.role = KMQ_TARGET;
    self->entry_point->options.callback = task_callback;
    self->entry_point->options.cb_arg = self;
    self->knode->add_endpoint(self->knode, self->entry_point);

    self->start = start__;
    self->del = delete__;

    *service = self;
    return knd_OK;
error:
    delete__(self);
    return knd_FAIL;
}

