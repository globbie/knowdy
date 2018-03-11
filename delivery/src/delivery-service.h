#pragma once


#include <kmq.h>

struct kndDeliveryOptions
{
    char *config_file;
};

struct kndDeliveryService
{
    struct kmqKnode *knode;
    struct kmqEndPoint *entry_point;

    const struct kndDeliveryOptions *opts;

    /*********************  public interface  *********************************/
    int (*start)(struct kndDeliveryService *self);
    int (*del)(struct kndDeliveryService *self);
};

int kndDeliveryService_new(struct kndDeliveryService **service, const struct kndDeliveryOptions *opts);

