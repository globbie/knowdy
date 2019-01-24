#pragma once

#include <knd_err.h>
#include <knd_config.h>
#include <knd_task.h>

struct kndShard
{
    char name[KND_NAME_SIZE];
    size_t name_size;

    char path[KND_PATH_SIZE];
    size_t path_size;

    char schema_path[KND_PATH_SIZE];
    size_t schema_path_size;

    char user_class_name[KND_NAME_SIZE];
    size_t user_class_name_size;
    char user_repo_name[KND_NAME_SIZE];
    size_t user_repo_name_size;
    char user_schema_path[KND_PATH_SIZE];
    size_t user_schema_path_size;

    struct glbOutput *out;
    struct glbOutput *log;

    struct kndTask **workers;
    size_t num_workers;
    struct kndQueue *task_queue;
    struct kndTaskContext *contexts;

    struct kndUser *user;

    /* system repo */
    struct kndRepo *repo;
    /* shared repos */
    //struct kndRepo *repos;

    // TODO: remove
    struct kndMemPool *mempool;

    const char *report;
    size_t report_size;

    /**********  interface methods  **********/
    void (*del)(struct kndShard *self);
    void (*str)(struct kndShard *self);
};

extern int kndShard_new(struct kndShard **self, const char *config, size_t config_size);
extern void kndShard_del(struct kndShard *self);
extern int kndShard_run_task(struct kndShard *self, const char *input, size_t input_size,
                             const char **output, size_t *output_size, int *out_task_type,
                             size_t task_id);

// knd_shard.config.c
extern int kndShard_parse_config(struct kndShard *self, const char *rec, size_t *total_size,
                                 struct kndMemPool *mempool);
