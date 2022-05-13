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
 *   knd_queue.h
 *   Knowdy Lock-Free Queue
 */

#pragma once

#include <stdatomic.h>
#include "knd_config.h"

struct kndQueueDir
{
    size_t elem_block_sizes[KND_RADIX_BASE];
    struct kndQueueDir * _Atomic subdirs[KND_RADIX_BASE];

    size_t num_elems;
    size_t payload_block_size;
    size_t payload_footer_size;

    size_t num_subdirs;
    size_t subdir_block_size;

    size_t cell_max_val;
    size_t total_elems;

    size_t global_offset;
    size_t total_size;

    /* options */
    bool elems_linear_scan;
    bool subdirs_expanded;
};

struct kndQueueElemIdx
{
    void * _Atomic elems[KND_RADIX_BASE];
    struct kndQueueElemIdx * _Atomic idxs[KND_RADIX_BASE];
};

struct kndQueue
{
    char path[KND_PATH_SIZE];
    size_t path_size;

    struct kndQueueElemIdx * _Atomic idx;
    struct kndQueueElemIdx * idxs[KND_MAX_TASKS];

    atomic_size_t num_elems;
    atomic_size_t num_valid_elems;

    struct kndQueueDir *dir;

    struct kndMemPool *mempool;
    bool allow_overwrite;
};
