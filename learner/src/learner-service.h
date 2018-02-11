#pragma once

#include <knd_mempool.h>
#include <knd_output.h>
#include <knd_task.h>
#include <knd_user.h>

#include <kmq.h>

struct kndLearnerOptions
{
    char *config_file;
    struct addrinfo *address;
};

struct kndLearnerService
{
    struct kmqKnode *knode;
    struct kmqEndPoint *entry_point;

    struct kndOutput *out;
    struct kndOutput *log;

    struct kndTask *task;
    struct kndUser *admin;

    struct kndMemPool *mempool;


    char name[KND_NAME_SIZE];
    size_t name_size;

    char path[KND_NAME_SIZE];
    size_t path_size;

    char schema_path[KND_NAME_SIZE];
    size_t schema_path_size;

    char delivery_addr[KND_NAME_SIZE];
    size_t delivery_addr_size;



    const struct kndLearnerOptions *opts;

    /*********************  public interface  *********************************/
    int (*start)(struct kndLearnerService *self);
    int (*del)(struct kndLearnerService *self);
};

int kndLearnerService_new(struct kndLearnerService **service, const struct kndLearnerOptions *opts);

