#pragma once

#include <kmq.h>

struct kndLearnerService
{
    struct kmqKnode *knode;
    struct kmqEndPoint *entry_point;

    int (*start)(struct kndLearnerService *self);
    int (*del)(struct kndLearnerService *self);
};

int kndLearnerService_new(struct kndLearnerService **service, const char *config_file);

