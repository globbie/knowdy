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

void knd_dict_item_free(struct kndDictItem *item, struct kndMemPool *mempool)
{
    knd_mempool_free(mempool, KND_MEMPAGE_TINY, (void*)item);
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

void knd_dict_entry_free(struct kndDictEntry *entry, struct kndMemPool *mempool)
{
    knd_mempool_free(mempool, KND_MEMPAGE_TINY, (void*)entry);
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

int knd_dict_get(struct kndDict *dict, const char *key, size_t key_size, void **result,
                 struct kndTask *task)
{
    size_t h = knd_dict_hash(key, key_size) % dict->size;
    struct kndDictEntry *entry = dict->hash_array[h];
    struct kndDictItem *item;

    if (!entry) {
        switch (dict->storage_type) {
        case KND_DICT_PERSIST:
            return knd_dict_fetch_item(dict, h, key, key_size, result, task);
        default:
            break;
        }
        return knd_NO_MATCH;
    }

    FOREACH(item, entry->items) {
        if (item->key_size != key_size) continue;
        if (!memcmp(item->key, key, key_size)) {
            *result = item->data;
            return knd_OK;
        }
    }
    return knd_NO_MATCH;
}

static int add_item(struct kndDict *dict, struct kndDictEntry *entry,
                    const char *key, size_t key_size, void *data, struct kndTask *unused_var(task))
{
    struct kndDictItem *item;

    if (knd_dict_item_new(&item, dict->mempool) != knd_OK) return knd_NOMEM;
    item->data = data;
    item->key = key;
    item->key_size = key_size;
    item->next = entry->items;
    entry->items = item;
    entry->num_items++;

    dict->num_items++;
    return knd_OK;
}

int knd_dict_set(struct kndDict *dict, const char *key, size_t key_size, void *data, struct kndTask *task)
{
    size_t h = knd_dict_hash(key, key_size) % dict->size;
    struct kndDictEntry *entry = dict->hash_array[h];
    struct kndDictItem *item;
    int err;

    if (!entry) {
        err = knd_dict_entry_new(&entry, task->mempool);
        if (err) return err;
        dict->hash_array[h] = entry;
        return add_item(dict, entry, key, key_size, data, task);
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
        return add_item(dict, entry, key, key_size, data, task);
    }

    return knd_CONFLICT;
}

int knd_dict_remove(struct kndDict *dict, const char *key, size_t key_size)
{
    size_t h = knd_dict_hash(key, key_size) % dict->size;
    struct kndDictEntry *entry = dict->hash_array[h];
    struct kndDictItem *item;
    struct kndDictItem *prev = NULL;

    assert (dict->num_items > 0);

    if (!entry) return knd_NO_MATCH;
    item = entry->items;

    while (item) {
        if (item->key_size != key_size) goto next_item;
        if (memcmp(item->key, key, key_size)) goto next_item;

        if (prev) {
            prev->next = item->next;
        }

        knd_dict_item_free(item, dict->mempool);
        dict->num_items--;
        entry->num_items--;

        if (!entry->num_items) {
            knd_dict_entry_free(entry, dict->mempool);
            dict->hash_array[h] = NULL;
        }
        return knd_OK;
    next_item:
        prev = item;
        item = item->next;
    }
    return knd_NO_MATCH;
}

int knd_dict_map(struct kndDict *dict, map_cb_t cb, void *ctx, struct kndTask *task)
{
    struct kndDictEntry *entry;
    struct kndDictItem *item;
    int err;

    for (size_t i = 0; i < dict->size; i++) {
        entry = dict->hash_array[i];
        if (!entry) continue;
        FOREACH (item, entry->items) {
            err = cb(item->data, ctx, task);
            if (err) return err;
        }
    }
    return knd_OK;
}

void knd_dict_del(struct kndDict *dict)
{
    struct kndDictEntry *entry;
    struct kndDictItem *item, *curr_item;

    assert (dict->size == (sizeof(dict->hash_array) / sizeof(struct kndDictEntry*)));

    for (size_t i = 0; i < dict->size; i++) {
        entry = dict->hash_array[i];
        if (!entry) continue;

        item = entry->items;
        while (item) {
            curr_item = item;
            item = item->next;
            knd_dict_item_free(curr_item, dict->mempool);
        }
        knd_dict_entry_free(entry, dict->mempool);
    }
    free(dict->hash_array);
    free(dict);
}

void knd_dict_reset(struct kndDict *dict)
{
    memset(dict->hash_array, 0, sizeof(struct kndDictEntry*) * dict->size);
}

int knd_dict_new(struct kndDict **result, size_t init_size, struct kndMemPool *mempool)
{
    struct kndDict *dict;

    dict = calloc(1, sizeof(struct kndDict));
    if (!dict) return knd_NOMEM;
    dict->hash_array = calloc(init_size, sizeof(struct kndDictEntry*));
    if (!dict->hash_array) return knd_NOMEM;
    dict->size = init_size;
    dict->mempool = mempool;

    *result = dict;
    return knd_OK;
}
