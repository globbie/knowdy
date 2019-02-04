/**
 *   Copyright (c) 2011-present by Dmitri Dmitriev
 *   All rights reserved.
 *
 *   This file is part of the Knowdy Graph DB, 
 *   and as such it is subject to the license stated
 *   in the LICENSE file which you have received 
 *   as part of this distribution.
 *
 *   Project homepage:
 *   <http://www.knowdy.net>
 *
 *   Initial author and maintainer:
 *         Dmitri Dmitriev aka M0nsteR <dmitri@globbie.net>
 *
 *   ----------
 *   knd_storage.h
 *   Knowdy Storage
 */

#pragma once

#include <stdatomic.h>

#include "knd_config.h"
#include "knd_task.h"
#include "knd_queue.h"

struct kndStorage
{
    char path[KND_PATH_SIZE];
    size_t path_size;

    char commit_filename[KND_PATH_SIZE];
    size_t commit_filename_size;

    struct kndQueue *input_queue;
    struct kndQueue *output_queue;

    struct kndSet *ctx_idx;

    struct kndTask  **tasks;
    size_t num_tasks;
};

int knd_storage_new(struct kndStorage **self, size_t queue_capacity);
int knd_storage_reset(struct kndStorage *self);
int knd_storage_serve(struct kndStorage *self);
int knd_storage_stop(struct kndStorage *self);

int knd_storage_report(struct kndStorage *self,
                       const char task_id, size_t task_id_size);
int knd_storage_cancel(struct kndStorage *self,
                       const char task_id, size_t task_id_size);

