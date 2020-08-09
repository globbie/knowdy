#pragma once

#include "knd_config.h"
#include "knd_mempool.h"

typedef enum knd_dict_item_phase { KND_DICT_VALID,
                                   KND_DICT_PENDING,
                                   KND_DICT_REMOVED } knd_dict_item_phase;

struct kndDictItem
{
    knd_dict_item_phase phase;
    const char *key;
    size_t key_size;
    void *data;
    struct kndDictItem *next;
};

struct kndDict
{
    struct kndDictItem **hash_array;
    size_t size;

    struct kndMemPool *mempool;
    size_t num_keys;
    size_t num_items;
};

void* knd_dict_get(struct kndDict *self, const char *key, size_t key_size);

int knd_dict_set(struct kndDict *self, const char *key, size_t key_size, void *data);

int knd_dict_remove(struct kndDict *self, const char *key, size_t key_size);

int knd_dict_new(struct kndDict **self,
                 struct kndMemPool *mempool,
                 size_t init_size);

void knd_dict_del(struct kndDict *self);
void knd_dict_reset(struct kndDict *self);
