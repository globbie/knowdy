/**
 *   Copyright (c) 2011-2017 by Dmitri Dmitriev
 *   All rights reserved.
 *
 *   --------
 *   knd_dict.h
 *   Knowdy Dictionary
 */

#pragma once

#include <stdatomic.h>

typedef size_t (*knd_hash_func)(const char *key, size_t key_size);
struct kndDictItem;

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

    knd_hash_func hash_func;

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

int knd_dict_set_hash(struct kndDict *self,
                      knd_hash_func new_hash);

/* listing the keys and values */
/*int (*rewind)(struct kndDict *self);
int (*next_item)(struct kndDict *self,
                 const char **key,
                 void **data); */

int knd_dict_new(struct kndDict **self, size_t init_size);
