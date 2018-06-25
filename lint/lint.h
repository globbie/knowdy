#pragma once

#include <glb-lib/output.h>

#include <knd_mempool.h>
#include <knd_task.h>
#include <knd_user.h>


struct kndLintOptions
{
    char *config_file;
};

struct kndLint
{
    struct glbOutput *task_storage;
    struct glbOutput *out;
    struct glbOutput *log;

    struct kndTask *task;
    struct kndUser *admin;

    struct kndMemPool *mempool;

    char name[KND_NAME_SIZE];
    size_t name_size;

    char path[KND_NAME_SIZE];
    size_t path_size;

    char schema_path[KND_NAME_SIZE];
    size_t schema_path_size;

    size_t max_users;
};

knd_err_codes kndLint_new(struct kndLint **lint, const struct kndLintOptions *opts);
knd_err_codes kndLint_delete(struct kndLint *self);
