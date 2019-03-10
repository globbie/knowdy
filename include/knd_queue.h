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
 *   Knowdy Queue
 */

#pragma once

#include <stdatomic.h>
#include "knd_config.h"

struct kndQueue
{
    void **elems;
    size_t capacity;
    atomic_size_t size;
    atomic_size_t head_pos;
    atomic_size_t tail_pos;
};

int knd_queue_new(struct kndQueue **self,
                  size_t capacity);
int knd_queue_reset(struct kndQueue *self);

int knd_queue_push(struct kndQueue *self,
                   void *elem);
int knd_queue_pop(struct kndQueue *self,
                  void **result);
