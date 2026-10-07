#pragma once

#include <stdatomic.h>
#include "knd_config.h"
#include "knd_set.h"
#include "knd_shared_set.h"

struct kndCommit;
struct kndState;
struct kndMemPool;

typedef enum knd_shared_dict_item_phase { KND_SHARED_DICT_VALID,
                                          KND_SHARED_DICT_PENDING,
                                          KND_SHARED_DICT_REMOVED } knd_shared_dict_item_phase;

typedef enum knd_dict_cardinal_t { KND_DICT_UNIQUE_VALUES,
                                   KND_DICT_MULTIPLE_VALUES } knd_dict_cardinal_t;

typedef enum knd_dict_store_t { KND_DICT_STORE_DEFAULT,
                                KND_DICT_STORE_MEMONLY,
                                KND_DICT_STORE_PERSIST } knd_dict_store_t;

struct kndSharedDictItem
{
    knd_shared_dict_item_phase phase;
    const char *key;
    size_t key_size;
    void *data;
    struct kndState* _Atomic states;
    struct kndSharedDictItem *next;
};

struct kndSharedDict
{
    knd_dict_cardinal_t cardinal_t;
    knd_dict_store_t store_t;

    struct kndSharedDictItem* _Atomic *hash_array;
    size_t size;
    atomic_size_t num_items;

    struct kndMemPool *mempool;

    /* for marshalled elems */
    struct kndSet *idx;
};

int knd_shared_dict_new(struct kndSharedDict **result, size_t init_size,
                        knd_dict_cardinal_t cardinal_t, knd_dict_store_t store_t,
                        struct kndMemPool *mempool);
void knd_shared_dict_del(struct kndSharedDict *self);

int knd_shared_dict_get(struct kndSharedDict *dict, const char *key, size_t key_size, void **result);
int knd_shared_dict_set(struct kndSharedDict *dict, const char *key, size_t key_size, void *data, void **result);
int knd_shared_dict_remove(struct kndSharedDict *self, const char *key, size_t key_size);
int knd_shared_dict_map(struct kndSharedDict *self, map_cb_t cb, void *obj, struct kndTask *task);

int knd_shared_dict_marshall(struct kndSharedDict *self, const char *path, size_t path_size,
                             const char *pref, size_t pref_size,
                             knd_set_elem_marshall_cb_t cb, struct kndSharedDict *result_dict,
                             struct kndTask *task);
