#pragma once

#include <knd_err.h>
#include <knd_config.h>

struct kndMemPool;
struct kndUser;
struct kndSharedDict;

typedef enum knd_agent_role_type {
    KND_READER,
    KND_WRITER,
    KND_ARBITER
} knd_agent_role_type;

static const char* const knd_agent_role_names[] = {
    [KND_READER] = "READER",
    [KND_WRITER] = "WRITER",
    [KND_ARBITER] = "ARBITER"
};

struct kndMemConfig {
    size_t num_pages;
    size_t num_small_x4_pages;
    size_t num_small_x2_pages;
    size_t num_small_pages;
    size_t num_tiny_pages;
};

struct kndShard
{
    knd_agent_role_type role;

    char name[KND_NAME_SIZE + 1];
    size_t name_size;

    char path[KND_PATH_SIZE + 1];
    size_t path_size;

    char schema_path[KND_PATH_SIZE + 1];
    size_t schema_path_size;

    char user_class_name[KND_NAME_SIZE];
    size_t user_class_name_size;
    char user_repo_name[KND_NAME_SIZE];
    size_t user_repo_name_size;
    char user_schema_path[KND_PATH_SIZE];
    size_t user_schema_path_size;
    struct kndUser *user;

    struct kndMemConfig mem_config;
    struct kndMemConfig ctx_mem_config;

    struct kndTask *task;
    struct kndMemPool *mempool;

    /* system repo */
    struct kndRepo *repo;
    struct kndSet *repo_idx;
    struct kndSharedDict *repo_name_idx;
};

int knd_shard_new(struct kndShard **self, const char *config, size_t config_size);
void knd_shard_del(struct kndShard *self);

int knd_shard_run_task(struct kndShard *self, const char *input, size_t input_size,
                       char *output, size_t *output_size);

int knd_shard_report_task(struct kndShard *self,
                          const char *task_id, size_t task_id_size);
int knd_shard_cancel_task(struct kndShard *self,
                          const char *task_id, size_t task_id_size);

// knd_shard.config.c
int knd_shard_parse_config(struct kndShard *self,
                           const char *rec, size_t *total_size,
                           struct kndMemPool *mempool);
