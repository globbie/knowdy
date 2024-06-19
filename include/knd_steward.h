#pragma once

#include <knd_err.h>
#include <knd_config.h>
#include <knd_memblock.h>
#include <knd_mempool.h>

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

struct kndResourceReport {
    size_t mem_usage;
    size_t disk_usage;
    bool mem_threshold_alert;
};

struct kndSteward
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

    struct kndMemConfig mem_main_config;
    struct kndMemConfig mem_cache_config;
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

int  knd_steward_new(struct kndSteward **steward, const char *config, size_t config_size);
int  knd_steward_cleanup(struct kndSteward *steward);
void knd_steward_monitor(struct kndSteward *steward, struct kndResourceReport *report);
void knd_steward_del(struct kndSteward *steward);

int knd_steward_run_task(struct kndSteward *self, const char *input, size_t input_size,
                       char *output, size_t *output_size);
int knd_steward_report_task(struct kndSteward *self,
                          const char *task_id, size_t task_id_size);
int knd_steward_cancel_task(struct kndSteward *self,
                          const char *task_id, size_t task_id_size);
int knd_steward_snapshot(struct kndSteward *steward);

