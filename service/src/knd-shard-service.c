#include <pthread.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <gsl-parser.h>
#include <glb-lib/output.h>

#include "knd-shard-service.h"
#include <knd_shard.h>
#include <knd_utils.h>

static int send_reply(struct kmqEndPoint *endpoint,
                      const char *result, size_t result_size)
{
    struct kmqTask *reply;
    int err;

    knd_log("== Knode got reply: %.*s",  result_size, result);

    err = kmqTask_new(&reply);
    if (err != 0) {
        knd_log("-- task report failed, allocation failed");
        return -1;
    }

    err = reply->copy_data(reply,
                           result, result_size);
    if (err != 0) {
        knd_log("-- task report failed, reply data copy failed");
        goto free_reply;
    }

    err = endpoint->schedule_task(endpoint, reply);
    if (err != 0) {
        knd_log("-- task report failed, schedule reply failed");
        goto free_reply;
    }

 free_reply:
    reply->del(reply);
    return knd_OK;
}

static int
task_callback(struct kmqEndPoint *endpoint, struct kmqTask *task, void *cb_arg)
{
    struct kndLearnerService *self = cb_arg;
    const char *data;
    size_t size;
    char *result = malloc(KND_IDX_BUF_SIZE);
    if (!result) return -1;

    size_t result_size = KND_IDX_BUF_SIZE;
    int err;

    err = task->get_data(task, 0, &data, &size);
    if (err != knd_OK) {
        knd_log("-- task read failed");
        goto error;
    }

    err = knd_shard_run_task(self->shard, data, size,
                             result, &result_size);
    if (err != knd_OK) {
        knd_log("-- task run failed");
        goto error;
    }

    err = send_reply(endpoint, result, result_size);
    if (err != knd_OK) {
        knd_log("-- task reply failed");
        goto error;
    }

 error:
    free(result);
    return err;
}

static int
start__(struct kndLearnerService *self)
{
    int err;

    err = knd_shard_serve(self->shard);
    if (err) return err;

    knd_log("\n.. Knowdy shard service is up and running, num workers:%zu\n",
            self->shard->num_tasks);

    self->knode->dispatch(self->knode);

    knd_log("-- Knowdy shard service has been stopped.\n");
    return knd_FAIL;
}

static void
delete__(struct kndLearnerService *self)
{
    if (self->entry_point) self->entry_point->del(self->entry_point);
    if (self->knode) self->knode->del(self->knode);
    free(self);
}

int kndLearnerService_new(struct kndLearnerService **service,
                          const struct kndLearnerOptions *opts)
{
    struct kndLearnerService *self;
    struct addrinfo *address;

    char *config = NULL;
    size_t config_size = 0;
    int fd = -1;

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

    err = addrinfo_new(&address, "localhost:10001", strlen("localhost:10001"));
    if (err != 0) goto error;

    err = self->entry_point->set_address(self->entry_point, address);
    if (err != 0) goto error;

    self->knode->add_endpoint(self->knode, self->entry_point);

    { // read config
        struct stat stat;

        fd = open(opts->config_file, O_RDONLY);
        if (fd == -1) goto error;
        fstat(fd, &stat);

        config_size = (size_t) stat.st_size;

        config = malloc(config_size);
        if (!config) goto error;

        ssize_t bytes_read = read(fd, config, config_size);
        if (bytes_read <= 0) goto error;

        if (fd != -1) close(fd);
    }

    err = knd_shard_new(&self->shard, config, config_size);
    if (err != 0) goto error;

    if (config) free(config);

    self->start = start__;
    self->del = delete__;

    *service = self;

    return knd_OK;
error:
    if (fd != -1) close(fd);
    if (config) free(config);
    delete__(self);
    return knd_FAIL;
}

