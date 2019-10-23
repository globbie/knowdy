/**
 *   Copyright (c) 2011-present by Dmitri Dmitriev
 *   All rights reserved.
 *
 *   --------
 *   knd_dict.c
 *   Knowdy dict implementation
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_dict.h"
#include "knd_state.h"
#include "knd_config.h"
#include "knd_utils.h"

static int dict_item_new(struct kndMemPool *mempool,
                         struct kndDictItem **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                                sizeof(struct kndDictItem), &page);
        if (err) return err;
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_TINY,
                                     sizeof(struct kndDictItem), &page);
        if (err) return err;
    }
    *result = page;
    return knd_OK;
}

static size_t 
knd_dict_hash(const char *key, size_t key_size)
{
    const char *p = key;
    size_t h = 0;

    if (!key_size) return 0;

    while (key_size) {
        h = (h << 1) ^ *p++;
        key_size--;
    }
    return h;
}

void* knd_dict_get(struct kndDict *self,
                   const char *key,
                   size_t key_size)
{
    size_t h = knd_dict_hash(key, key_size) % self->size;
    struct kndDictItem *item = self->hash_array[h];
    while (item) {
        if (item->key_size != key_size) goto next_item;
        if (!memcmp(item->key, key, key_size)) {
            if (item->phase == KND_DICT_REMOVED)
                return NULL;
            return item->data;
        }
    next_item:
        item = item->next;
    }
    return NULL;
}

int knd_dict_set(struct kndDict *self,
                 const char *key,
                 size_t key_size,
                 void *data)
{
    struct kndDictItem *new_item;
    size_t h = knd_dict_hash(key, key_size) % self->size;
    struct kndDictItem *orig_head = self->hash_array[h];
    struct kndDictItem *item = orig_head;

    while (item) {
        if (item->key_size != key_size) goto next_item;
        if (!memcmp(item->key, key, key_size)) {
            break;
        }
    next_item:
        item = item->next;
    }
    if (item) {
        if (item->phase == KND_DICT_VALID)
            return knd_CONFLICT;
    }

    /* add new item */
    if (dict_item_new(self->mempool, &new_item) != knd_OK) return knd_NOMEM;
    new_item->phase = KND_DICT_VALID;
    new_item->data = data;
    new_item->key = key;
    new_item->key_size = key_size;
    new_item->next = orig_head;
    self->num_items++;
    return knd_OK;
}

int knd_dict_remove(struct kndDict *self,
                    const char *key,
                    size_t key_size)
{
    size_t h = knd_dict_hash(key, key_size) % self->size;
    struct kndDictItem *head = self->hash_array[h];
    struct kndDictItem *item = head;

    while (item) {
        if (item->key_size != key_size) goto next_item;
        if (!memcmp(item->key, key, key_size)) {
            break;
        }
    next_item:
        item = item->next;
    }
    if (!item) return knd_FAIL;

    item->phase = KND_DICT_REMOVED;
    return knd_OK;
}

void knd_dict_del(struct kndDict *self)
{
    //struct kndDictItem *item;
    // TODO
    free(self->hash_array);
    free(self);
}

void knd_dict_reset(struct kndDict *self)
{
    memset(self->hash_array, 0, sizeof(struct kndDictItem*) * self->size);
}

int knd_dict_new(struct kndDict **dict,
                 struct kndMemPool *mempool,
                 size_t init_size)
{
    struct kndDict *self = malloc(sizeof(struct kndDict));
    if (!self) return knd_NOMEM;
    self->mempool = mempool;

    self->hash_array = calloc(init_size, sizeof(struct kndDictItem*));
    if (!self->hash_array) return knd_NOMEM;
    self->size = init_size;
    self->num_items = 0;

    *dict = self;

    return knd_OK;
}
