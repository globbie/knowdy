/**
 *   Copyright (c) 2011-present by Dmitri Dmitriev
 *   All rights reserved.
 *
 *   --------
 *   knd_shared_dict.c
 *   Knowdy lock-free shared dict implementation
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "knd_shared_dict.h"
#include "knd_state.h"
#include "knd_mempool.h"
#include "knd_config.h"
#include "knd_utils.h"

static int dict_item_new(struct kndMemPool *mempool, struct kndSharedDictItem **result)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndSharedDictItem));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndSharedDictItem));
    *result = page;
    return knd_OK;
}

static size_t 
knd_shared_dict_hash(const char *key, size_t key_size)
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

void* knd_shared_dict_get(struct kndSharedDict *self, const char *key, size_t key_size)
{
    assert(key != NULL);
    assert(key_size != 0);
    size_t h = knd_shared_dict_hash(key, key_size) % self->size;
    struct kndSharedDictItem *item = atomic_load_explicit(&self->hash_array[h], memory_order_relaxed);
    while (item) {
        if (item->key_size != key_size) goto next_item;
        if (!memcmp(item->key, key, key_size)) {
            if (item->phase == KND_SHARED_DICT_REMOVED)
                return NULL;
            return item->data;
        }
    next_item:
        item = item->next;
    }
    return NULL;
}

int knd_shared_dict_set(struct kndSharedDict *self, const char *key, size_t key_size,
                        void *data, struct kndMemPool *mempool, struct kndCommit *commit,
                        struct kndSharedDictItem **result, bool allow_overwrite)
{
    struct kndSharedDictItem *head;
    struct kndSharedDictItem *new_item;
    size_t h = knd_shared_dict_hash(key, key_size) % self->size;

    struct kndSharedDictItem *orig_head = atomic_load_explicit(&self->hash_array[h], memory_order_acquire);
    struct kndSharedDictItem *item = orig_head;
    struct kndState *state;
    int err;

    while (item) {
        if (item->key_size != key_size) goto next_item;
        if (!memcmp(item->key, key, key_size)) {
            break;
        }
    next_item:
        item = item->next;
    }
    if (item && !allow_overwrite) {
        switch (item->phase) {
        case KND_SHARED_DICT_VALID:
            //knd_log("-- valid entry already present in kndSharedDict: %.*s",
            //        item->key_size, item->key);
            return knd_CONFLICT;
        default:
            break;
        }
    }

    /* add new item */
    if (dict_item_new(mempool, &new_item) != knd_OK) return knd_NOMEM;
    memset(new_item, 0, sizeof(struct kndSharedDictItem));

    new_item->phase = KND_SHARED_DICT_VALID;
    if (commit) {
        new_item->phase = KND_SHARED_DICT_PENDING;
        err = knd_state_new(mempool, &state);
        if (err) return err;
        state->commit = commit;
        state->data = data;
        new_item->states = state;
        *result = new_item;
    }
    new_item->data = data;
    new_item->key = key;
    new_item->key_size = key_size;

    do {
        head = atomic_load_explicit(&self->hash_array[h], memory_order_acquire);
        new_item->next = head;
        item = head;

        /* no new conflicts in place? */
        while (item) {
            if (item == orig_head) {
                item = NULL;
                break;
            }
            if (item->key_size != key_size) goto next_check;
            if (!memcmp(item->key, key, key_size)) {
                break;
            }
        next_check:
            item = item->next;
        }
        if (item) {
            // free mempool item
            return knd_CONFLICT;
        }
    } while (!atomic_compare_exchange_weak(&self->hash_array[h], &head, new_item));
    
    atomic_fetch_add_explicit(&self->num_items, 1, memory_order_relaxed);
    return knd_OK;
}

int knd_shared_dict_remove(struct kndSharedDict *self, const char *key, size_t key_size)
{
    size_t h = knd_shared_dict_hash(key, key_size) % self->size;
    struct kndSharedDictItem *head = atomic_load_explicit(&self->hash_array[h], memory_order_relaxed);
    struct kndSharedDictItem *item = head;

    FOREACH (item, head) {
        if (item->key_size != key_size) continue;
        if (!memcmp(item->key, key, key_size))
            break;
    }
    if (!item) return knd_FAIL;
    item->phase = KND_SHARED_DICT_REMOVED;
    return knd_OK;
}

void knd_shared_dict_del(struct kndSharedDict *self)
{
    //struct kndSharedDictItem *item;
    // TODO
    free(self->hash_array);
    free(self);
}

int knd_shared_dict_new(struct kndSharedDict **dict, size_t init_size)
{
    struct kndSharedDict *self = malloc(sizeof(struct kndSharedDict));
    if (!self) return knd_NOMEM;

    self->hash_array = calloc(init_size, sizeof(struct kndSharedDictItem*));
    if (!self->hash_array) return knd_NOMEM;
    self->size = init_size;
    self->num_items = 0;

    *dict = self;

    return knd_OK;
}
