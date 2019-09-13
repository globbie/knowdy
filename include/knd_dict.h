#pragma once

#include <stdatomic.h>
#include "knd_config.h"

typedef enum knd_dict_item_phase { KND_DICT_VALID,
                                   KND_DICT_REMOVED } knd_dict_item_phase;

struct kndDictItem
{
    const char *key;
    size_t key_size;
    void *data;
    knd_dict_item_phase phase;
    struct kndDictItem *next;
};

struct kndDict
{
    struct kndDictItem* _Atomic *hash_array;
    size_t size;

    atomic_size_t num_items;
};

void* knd_dict_get(struct kndDict *self,
                   const char *key,
                   size_t key_size);
int knd_dict_set(struct kndDict *self,
                 const char *key,
                 size_t key_size,
                 void *data);
int knd_dict_remove(struct kndDict *self,
                    const char *key,
                    size_t key_size);

int knd_dict_new(struct kndDict **self, size_t init_size);
void knd_dict_del(struct kndDict *self);
