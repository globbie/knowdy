#pragma once

#include <kmq.h>

struct kndLearnerService
{
    struct kmqKnode *knode;
    struct kmqEndPoint *entry_point;

    int (*start)(struct kndLearnerService *self);
    int (*del)(struct kndLearnerService *self);
};

struct kndLearnerService_new(struct kndLearnerService **service);
