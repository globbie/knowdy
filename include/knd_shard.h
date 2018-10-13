#pragma once

#include <knd_err.h>
#include <knd_config.h>

struct kndShard
{
//    char id[KND_ID_SIZE];
//    size_t id_size;

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

    struct glbOutput *task_storage;
    struct glbOutput *out;
    struct glbOutput *log;

    struct kndTask *task;
    struct kndUser *user;

    /* system repo */
    struct kndRepo *repo;
    /* shared repos */
    //struct kndRepo *repos;

    const char *report;
    size_t report_size;

    struct kndMemPool *mempool;

    /**********  interface methods  **********/
    void (*del)(struct kndShard *self);
    void (*str)(struct kndShard *self);
};

extern int kndShard_parse_schema(struct kndShard *self, const char *rec, size_t *total_size);

extern int kndShard_new(struct kndShard **self, const char *config, size_t config_size);
extern void kndShard_del(struct kndShard *self);
extern int kndShard_run_task(struct kndShard *self, const char *input, size_t input_size, const char **output, size_t *output_size);
