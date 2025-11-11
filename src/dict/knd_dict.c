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

#include "knd_set.h"
#include "knd_dict.h"
#include "knd_state.h"
#include "knd_config.h"
#include "knd_utils.h"

int knd_dict_item_new(struct kndDictItem **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndDictItem));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndDictItem));
    *result = page;
    return knd_OK;
}

int knd_dict_entry_new(struct kndDictEntry **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndDictEntry));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndDictEntry));
    *result = page;
    return knd_OK;
}

static size_t knd_dict_hash(const char *key, size_t key_size)
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

void* knd_dict_get(struct kndDict *self, const char *key, size_t key_size)
{
    size_t h = knd_dict_hash(key, key_size) % self->size;
    struct kndDictEntry *entry = self->hash_array[h];
    struct kndDictItem *item;

    if (!entry) return NULL;

    // TODO  entry demarshalling needed?

    item = entry->items;

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

static int add_item(struct kndDict *self, struct kndDictEntry *entry,
                    const char *key, size_t key_size, void *data)
{
    struct kndDictItem *item;

    if (knd_dict_item_new(&item, self->mempool) != knd_OK) return knd_NOMEM;
    item->phase = KND_DICT_VALID;
    item->data = data;
    item->key = key;
    item->key_size = key_size;
    item->next = entry->items;
    entry->items = item;
    entry->num_items++;

    return knd_OK;
}

int knd_dict_set(struct kndDict *self, const char *key, size_t key_size, void *data)
{
    size_t h = knd_dict_hash(key, key_size) % self->size;
    struct kndDictEntry *entry = self->hash_array[h];
    struct kndDictItem *item;
    int err;

    if (!entry) {
        err = knd_dict_entry_new(&entry, self->mempool);
        if (err) return err;
        self->hash_array[h] = entry;
        return add_item(self, entry, key, key_size, data);
    }

    item = entry->items;
    while (item) {
        if (item->key_size != key_size) goto next_item;
        if (!memcmp(item->key, key, key_size)) {
            break;
        }
    next_item:
        item = item->next;
    }

    if (!item) {
        return add_item(self, entry, key, key_size, data);
    }

    if (item->phase == KND_DICT_VALID)
        return knd_CONFLICT;

    return knd_CONFLICT;
}

int knd_dict_remove(struct kndDict *self, const char *key, size_t key_size)
{
    size_t h = knd_dict_hash(key, key_size) % self->size;
    struct kndDictEntry *head = self->hash_array[h];
    struct kndDictItem *item = head->items;

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
    self->num_keys--;
    return knd_OK;
}

int knd_dict_map(struct kndDict *idx, map_cb_t cb, void *ctx)
{
    struct kndDictEntry *entry;
    struct kndDictItem *item;
    int err;

    for (size_t i = 0; i < idx->size; i++) {
        entry = idx->hash_array[i];
        item = entry->items;
        for (; item; item = item->next) {
            err = cb(item->data, ctx);
            if (err) return err;
        }
    }
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
    memset(self->hash_array, 0, sizeof(struct kndDictEntry*) * self->size);
}

int knd_dict_new(struct kndDict **result, size_t init_size, struct kndMemPool *mempool)
{
    struct kndDict *self;

    self = calloc(1, sizeof(struct kndDict));
    if (!self) return knd_NOMEM;
    self->hash_array = calloc(init_size, sizeof(struct kndDictEntry*));
    if (!self->hash_array) return knd_NOMEM;
    self->size = init_size;
    self->mempool = mempool;
    *result = self;
    return knd_OK;
}
