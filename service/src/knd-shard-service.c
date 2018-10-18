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

static int
task_callback(struct kmqEndPoint *endpoint, struct kmqTask *task, void *cb_arg)
{
    struct kndLearnerService *self = cb_arg;
    const char *data;
    size_t size;
    const char *result = NULL;
    size_t result_size = 0;
    int err;

    err = task->get_data(task, 0, &data, &size);
    if (err != knd_OK) {
        knd_log("-- task read failed");
        return -1;
    }

    err = kndShard_run_task(self->shard, data, size, &result, &result_size);
    if (err != knd_OK) {
        knd_log("-- task execution failed");
        return -1;
    }

    {
        struct kmqTask *reply;
        err = kmqTask_new(&reply);
        if (err != 0) {
            knd_log("-- task report failed, allocation failed");
            return -1;
        }
        err = reply->copy_data(reply,
                               self->shard->report, self->shard->report_size);
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
    }
    return 0;
}


static int
start__(struct kndLearnerService *self)
{
    knd_log("learner has been started..\n");
    self->knode->dispatch(self->knode);
    knd_log("learner has been stopped\n");
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

    err = kndShard_new(&self->shard, config, config_size);
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
