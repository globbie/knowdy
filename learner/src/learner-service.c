#include "learner-service.h"

#include <knd_err.h>

static int
start__(struct kndLearnerService *self)
{
    return knd_FAIL;
}

static int
delete__(struct kndLearnerService *self)
{
    if (self->entry_point) self->entry_point->del(self->entry_point);
    if (self->knode) self->knode->del(self->knode);
    free(self);

    return knd_OK;
}

int
kndLearnerService_new(struct kndLearnerService **service, const char *config_file)
{
    struct kndLearnerService *self;
    int error_code;

    self = calloc(1, sizeof(*self));
    if (!self) return knd_FAIL;

    error_code = kmqKnode_new(&self->knode);
    if (error_code != 0) goto error;

    self->start = start__;
    self->del = delete__;

    *service = self;

    return knd_OK;
error:
    delete__(self);
    return knd_FAIL;
}

