#pragma once

#include <pthread.h>
#include <glb-lib/output.h>

#include <knd_mempool.h>
#include <knd_task.h>
#include <knd_user.h>

#include <kmq.h>

struct kndLearnerService;

struct kndLearnerOptions
{
    char *config_file;
    struct addrinfo *address;
};

struct kndLearnerOwner
{
    size_t id;
    pthread_t thread;
    struct kndLearnerService *service;

    struct kndMemPool *mempool;
    struct kndUser *user;
    struct kndTask *task;
    struct glbOutput *task_storage;
    struct glbOutput *out;
    struct glbOutput *log;
};

struct kndLearnerService
{
    struct kmqKnode *knode;
    struct kmqEndPoint *entry_point;

    struct kndLearnerOwner *owners;
    size_t num_owners;

    struct glbOutput *task_storage;
    struct glbOutput *out;
    struct glbOutput *log;

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

    size_t max_users;

    const struct kndLearnerOptions *opts;

    /*********************  public interface  *********************************/
    int (*start)(struct kndLearnerService *self);
    int (*del)(struct kndLearnerService *self);
};

int kndLearnerService_new(struct kndLearnerService **service, const struct kndLearnerOptions *opts);

