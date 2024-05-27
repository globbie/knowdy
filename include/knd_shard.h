#pragma once

#include <knd_err.h>
#include <knd_config.h>

struct kndMemPool;
struct kndUser;
struct kndSharedDict;

typedef enum knd_agent_role_type {
    KND_AGENT_READER,
    KND_AGENT_WRITER,
    KND_AGENT_ARBITER,
    KND_AGENT_AUX
} knd_agent_role_type;

static const char* const knd_agent_role_names[] = {
    [KND_AGENT_READER] = "READER",
    [KND_AGENT_WRITER] = "WRITER",
    [KND_AGENT_ARBITER] = "ARBITER",
    [KND_AGENT_AUX] = "AUX"
};

struct kndMemConfig {
    size_t num_large_x4_pages;
    size_t num_large_x2_pages;
    size_t num_large_pages;
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

    char data_path[KND_PATH_SIZE + 1];
    size_t data_path_size;

    char user_class_name[KND_NAME_SIZE];
    size_t user_class_name_size;
    char user_repo_name[KND_NAME_SIZE];
    size_t user_repo_name_size;
    char user_schema_path[KND_PATH_SIZE];
    size_t user_schema_path_size;
    struct kndUser *user;

    struct kndMemConfig mem_config;
    struct kndMemConfig mem_user_config;
    struct kndMemConfig mem_ctx_config;

    /* aux task */
    struct kndTask *task;
    struct kndOutput *out;
    struct kndOutput *log;

    struct kndMemPool *mempool_read;
    struct kndMemPool *mempool_read_temp;

    struct kndMemPool *mempool_write;
    struct kndMemPool *mempool_write_temp;

    const char *msg;
    size_t msg_size;

    /* system repo */
    struct kndRepo *repo;

    /* subrepos */
    struct kndSet *repo_idx;
    struct kndSharedDict *repo_name_idx;
};

int knd_shard_new(struct kndShard **shard, const char *config, size_t config_size);
void knd_shard_del(struct kndShard *shard);

int knd_shard_run_task(struct kndShard *self, const char *input, size_t input_size,
                       char *output, size_t *output_size);
int knd_shard_report_task(struct kndShard *self,
                          const char *task_id, size_t task_id_size);
int knd_shard_cancel_task(struct kndShard *self,
                          const char *task_id, size_t task_id_size);
