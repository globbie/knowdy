#pragma once

#include <pthread.h>

#include <knd_err.h>
#include <knd_config.h>
#include <knd_task.h>
#include <knd_storage.h>

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

    struct kndTask **tasks;
    size_t num_tasks;

    struct kndQueue *task_context_queue;

    struct kndSet *ctx_idx;
    size_t task_count;

    struct kndStorage *storage;
    //struct kndNetwork *network;

    struct kndUser *user;

    /* system repo */
    struct kndRepo *repo;
    /* all other repos */
    struct kndSet *repos;

    struct kndMemPool *mempool;
};

int knd_shard_new(struct kndShard **self, const char *config, size_t config_size);
void knd_shard_del(struct kndShard *self);

int knd_shard_serve(struct kndShard *self);
int knd_shard_stop(struct kndShard *self);

int knd_shard_push_task(struct kndShard *self,
                        const char *input, size_t input_size,
                        const char **task_id, size_t *task_id_size,
                        task_cb_func cb, void *obj);
int knd_shard_run_task(struct kndShard *self,
                       const char *input, size_t input_size,
                       char *output, size_t *output_size);

int knd_shard_report_task(struct kndShard *self,
                          const char *task_id, size_t task_id_size);
int knd_shard_cancel_task(struct kndShard *self,
                          const char *task_id, size_t task_id_size);

// knd_shard.config.c
int knd_shard_parse_config(struct kndShard *self,
                           const char *rec, size_t *total_size,
                           struct kndMemPool *mempool);
