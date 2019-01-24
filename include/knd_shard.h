#pragma once

#include <pthread.h>

#include <knd_err.h>
#include <knd_config.h>
#include <knd_task.h>
#include <knd_storage.h>

struct kndWorker {
    pthread_t thread;
    size_t id;
    struct kndShard *shard;
    struct kndTask *task;
    size_t num_success;
    size_t num_failed;
};

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

    struct kndTask    **tasks;
    struct kndWorker  **workers;
    size_t num_workers;

    struct kndQueue *task_context_queue;
    // TODO: ctx idx
    struct kndTaskContext *contexts;

    struct kndStorage *storage;

    struct kndUser *user;

    /* system repo */
    struct kndRepo *repo;

    struct kndMemPool *mempool;

    struct glbOutput *out;
    struct glbOutput *log;

    const char *report;
    size_t report_size;

    /**********  interface methods  **********/
    void (*del)(struct kndShard *self);
    void (*str)(struct kndShard *self);
};

int kndShard_new(struct kndShard **self, const char *config, size_t config_size);
void kndShard_del(struct kndShard *self);

int kndShard_run_task(struct kndShard *self,
                      const char *input, size_t input_size,
                      const char **output, size_t *output_size, int *out_task_type,
                      size_t task_id);

int knd_shard_serve(struct kndShard *self);
int knd_shard_run_task(struct kndShard *self,
                       const char *input, size_t input_size,
                       const char **task_id, size_t *task_id_size);
int knd_shard_report_task(struct kndShard *self,
                          const char *task_id, size_t task_id_size);
int knd_shard_cancel_task(struct kndShard *self,
                          const char *task_id, size_t task_id_size);

// knd_shard.config.c
extern int kndShard_parse_config(struct kndShard *self,
                                 const char *rec, size_t *total_size,
                                 struct kndMemPool *mempool);
