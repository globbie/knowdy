#pragma once

#include <knd_err.h>

struct kndShard
{
    char id[KND_ID_SIZE];
    size_t id_size;
    char name[KND_NAME_SIZE];
    size_t name_size;

    struct glbOutput *task_storage;
    struct glbOutput *out;
    struct glbOutput *log;

    struct kndTask *task;
    struct kndUser *admin;

    const char *report;
    size_t report_size;

    struct kndMemPool *mempool;

    /**********  interface methods  **********/
    int (*del)(struct kndShard *self);
    int (*str)(struct kndShard *self);
    int (*run_task)(struct kndShard *self,
                    const char     *rec,
                    size_t   rec_size);
};

extern int kndShard_init(struct kndShard *self);
extern int kndShard_new(struct kndShard **self);
