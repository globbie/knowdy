#pragma once

#include <stdatomic.h>
#include "knd_config.h"

struct kndDictItem
{
    const char *key;
    size_t key_size;
    void *data;
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
