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

static int dict_item_new(struct kndSharedDict *self, struct kndSharedDictItem **result)
{
    struct kndMemPool *mempool = self->mempool;
    void *page;
    int err;

    assert(KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndSharedDictItem));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndSharedDictItem));
    *result = page;
    return knd_OK;
}

static size_t knd_shared_dict_hash(const char *key, size_t key_size)
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

int knd_shared_dict_get(struct kndSharedDict *dict, const char *key, size_t key_size, void **result)
{
    assert(key != NULL);
    assert(key_size != 0);
    size_t h = knd_shared_dict_hash(key, key_size) % dict->size;
    struct kndSharedDictItem *item, *items =\
        atomic_load_explicit(&dict->hash_array[h], memory_order_relaxed);

    FOREACH (item, items) {
        if (item->key_size != key_size) continue;
        if (!memcmp(item->key, key, key_size)) {
            if (item->phase == KND_SHARED_DICT_REMOVED)
                return knd_NO_MATCH;
            *result = item->data;
            return knd_OK;
        }
    }
    return knd_NO_MATCH;
}

int knd_shared_dict_set(struct kndSharedDict *dict, const char *key, size_t key_size, void *data, void **result)
{
    struct kndSharedDictItem *head;
    struct kndSharedDictItem *new_item;
    size_t h = knd_shared_dict_hash(key, key_size) % dict->size;
    struct kndSharedDictItem *orig_head =\
        atomic_load_explicit(&dict->hash_array[h], memory_order_acquire);
    struct kndSharedDictItem *item = orig_head;

    while (item) {
        if (item->key_size != key_size) goto next_item;
        if (!memcmp(item->key, key, key_size)) {
            break;
        }
    next_item:
        item = item->next;
    }

    if (item) {
        switch (item->phase) {
        case KND_SHARED_DICT_VALID:
            //knd_log("-- valid entry already present in kndSharedDict: %.*s",
            //        item->key_size, item->key);
            *result = item->data;
            return knd_CONFLICT;
        default:
            break;
        }
    }

    /* alloc a new item */
    if (dict_item_new(dict, &new_item) != knd_OK) return knd_NOMEM;
    memset(new_item, 0, sizeof(struct kndSharedDictItem));
    new_item->phase = KND_SHARED_DICT_VALID;
    new_item->data = data;
    new_item->key = key;
    new_item->key_size = key_size;

    do {
        head = atomic_load_explicit(&dict->hash_array[h], memory_order_acquire);
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
            // TODO free mempool new_item
            *result = item->data;
            return knd_CONFLICT;
        }
    } while (!atomic_compare_exchange_weak(&dict->hash_array[h], &head, new_item));
    
    atomic_fetch_add_explicit(&dict->num_items, 1, memory_order_relaxed);
    return knd_OK;
}

int knd_shared_dict_remove(struct kndSharedDict *dict, const char *key, size_t key_size)
{
    size_t h = knd_shared_dict_hash(key, key_size) % dict->size;
    struct kndSharedDictItem *head = atomic_load_explicit(&dict->hash_array[h], memory_order_relaxed);
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

int knd_shared_dict_map(struct kndSharedDict *idx, map_cb_t cb, void *ctx, struct kndTask *task)
{
    struct kndSharedDictItem *item = NULL;
    int err;

    for (size_t i = 0; i < idx->size; i++) {
        item = atomic_load_explicit(&idx->hash_array[i], memory_order_acquire);
        for (; item; item = item->next) {
            err = cb(item->data, ctx, task);
            if (err) return err;
        }
    }
    return knd_OK;
}

void knd_shared_dict_del(struct kndSharedDict *dict)
{
    free(dict->hash_array);
}

int knd_shared_dict_new(struct kndSharedDict **result, size_t init_size,
                        knd_dict_cardinal_t cardinal_t, knd_dict_store_t store_t,
                        struct kndMemPool *mempool)
{
    void *page;
    struct kndSharedDict *dict;
    int err;

    assert(KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndSharedDict));

    switch (mempool->type) {
    case KND_ALLOC_SHARED:
        break;
    default:
        knd_log("shared mempool required for a shared dict");
        return knd_NOMEM;
    }

    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndSharedDict));
    dict = page;

    dict->hash_array = calloc(init_size, sizeof(struct kndSharedDictItem*));
    if (!dict->hash_array) return knd_NOMEM;

    dict->size = init_size;
    dict->mempool = mempool;
    dict->cardinal_t = cardinal_t;
    dict->store_t = store_t;

    *result = dict;

    return knd_OK;
}
