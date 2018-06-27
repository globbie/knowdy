#pragma once

#include <knd_err.h>
#include <knd_config.h>

struct kndShard
{
    char id[KND_ID_SIZE];
    size_t id_size;

    char name[KND_NAME_SIZE];
    size_t name_size;

    char path[KND_NAME_SIZE];
    size_t path_size;

    char schema_path[KND_NAME_SIZE];
    size_t schema_path_size;

    struct glbOutput *task_storage;
    struct glbOutput *out;
    struct glbOutput *log;

    struct kndTask *task;
    struct kndUser *admin;

    const char *report;
    size_t report_size;

    struct kndMemPool *mempool;

    /**********  interface methods  **********/
    void (*del)(struct kndShard *self);
    void (*str)(struct kndShard *self);
    int (*run_task)(struct kndShard *self,
                    const char     *rec,
                    size_t   rec_size);
};

extern void kndShard_init(struct kndShard *self);
extern int kndShard_new(struct kndShard **self, const char *config_filename);
