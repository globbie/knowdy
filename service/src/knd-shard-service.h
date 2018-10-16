#pragma once

#include <pthread.h>
#include <glb-lib/output.h>

#include <kmq.h>
#include <knd_shard.h>

struct kndLearnerService;

struct kndLearnerOptions
{
    char *config_file;
    struct addrinfo *address;
};

struct kndLearnerService
{
    struct kmqKnode *knode;
    struct kmqEndPoint *entry_point;

    struct kndShard *shard;

    char name[KND_NAME_SIZE];
    size_t name_size;

//    char path[KND_NAME_SIZE];
//    size_t path_size;
//
//    char schema_path[KND_NAME_SIZE];
//    size_t schema_path_size;
//
//    char delivery_addr[KND_NAME_SIZE];
//    size_t delivery_addr_size;

//    size_t max_users;

    const struct kndLearnerOptions *opts;

    /*********************  public interface  *********************************/
    int (*start)(struct kndLearnerService *self);
    void (*del)(struct kndLearnerService *self);
};

int kndLearnerService_new(struct kndLearnerService **service, const struct kndLearnerOptions *opts);

