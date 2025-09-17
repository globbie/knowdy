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

struct kndStorageLeaf
{
    size_t numid;

    size_t min_size;
    size_t max_size;
    size_t curr_size;

    size_t num_elems;

    char range_from_addr[KND_PATH_SIZE + 1];
    size_t range_from_addr_size;

    char range_to_addr[KND_PATH_SIZE + 1];
    size_t range_to_addr_size;

    char name[KND_SHORT_NAME_SIZE + 1];
    size_t name_size;

    char filepath[KND_PATH_SIZE + 1];
    size_t filepath_size;

    char file_hash[KND_HASH_SIZE];
    size_t file_hash_size;

    struct kndStorageLeaf *next;
    struct kndStorageLeaf *tail;
};

int knd_storage_leaf_new(struct kndStorageLeaf **result, size_t numid);
