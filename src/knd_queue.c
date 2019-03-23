#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "knd_queue.h"
#include "knd_config.h"
#include "knd_utils.h"

int knd_queue_push(struct kndQueue *self,
                   void *elem)
{
    size_t head_pos, tail_pos, next_tail_pos;

    do {
        tail_pos = atomic_load_explicit(&self->tail_pos,
                                        memory_order_acquire);
        head_pos = atomic_load_explicit(&self->head_pos,
                                        memory_order_acquire);
        next_tail_pos = tail_pos + 1;

        if (next_tail_pos == head_pos) {
            knd_log("-- queue is full right now:  tail_pos:%zu head_pos:%zu ",
                    tail_pos, head_pos);
            return knd_LIMIT;
        }

        //knd_log(".. try pushing: tail pos:%zu  head pos:%zu",
        //        tail_pos, head_pos);

        if (next_tail_pos == self->capacity) {
            next_tail_pos = 0;
            if (head_pos == 0) {
                return knd_LIMIT;
            }
        }
    }
    while (!atomic_compare_exchange_weak(&self->tail_pos, &tail_pos, next_tail_pos));

    if (self->elems[next_tail_pos]) {
        knd_log("-- anomaly: next tail pos %zu is not empty?", next_tail_pos);
        return knd_FAIL;
    }
    self->elems[next_tail_pos] = elem;

    return knd_OK;
}

int knd_queue_pop(struct kndQueue *self,
                  void **result)
{
    void *elem;
    size_t head_pos, tail_pos, next_head_pos;

    do {
        tail_pos = atomic_load_explicit(&self->tail_pos,
                                        memory_order_acquire);
        head_pos = atomic_load_explicit(&self->head_pos,
                                        memory_order_acquire);
        if (tail_pos == head_pos) {
            //knd_log("-- queue is empty at head pos %zu", head_pos);
            return knd_NO_MATCH;
        }
        next_head_pos = head_pos + 1;

        if (next_head_pos == self->capacity) {
            next_head_pos = 0;
        }
        //knd_log("== try to pop: tail pos:%zu  next head pos:%zu",
        //        tail_pos, next_head_pos);
    } while (!atomic_compare_exchange_weak(&self->head_pos, &head_pos, next_head_pos));

    if (!self->elems[next_head_pos]) {
        knd_log("-- pos %zu is empty?", head_pos);
        return knd_FAIL;
    }

    elem = self->elems[next_head_pos];
    self->elems[next_head_pos] = NULL;
    *result = elem;

    return knd_OK;
}

int knd_queue_new(struct kndQueue **queue,
                  size_t capacity)
{
    struct kndQueue *self;
    self = malloc(sizeof(struct kndQueue));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndQueue));

    if (capacity) {
        self->elems = calloc(capacity, sizeof(void*));
        if (!self->elems) return knd_NOMEM;
        self->capacity = capacity;
    }

    *queue = self;
    return knd_OK;
}
