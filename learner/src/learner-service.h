#pragma once

#include <knd_mempool.h>
#include <knd_output.h>
#include <knd_task.h>
#include <knd_user.h>

#include <kmq.h>

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




    /*********************  public interface  *********************************/
    int (*start)(struct kndLearnerService *self);
    int (*del)(struct kndLearnerService *self);
};

int kndLearnerService_new(struct kndLearnerService **service, const char *config_file);

