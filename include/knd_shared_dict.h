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
    struct kndSharedDictItem* _Atomic *hash_array;
    size_t size;
    atomic_size_t num_items;

    struct kndMemPool *mempool;

    /* for marshalled elems */
    struct kndSet *idx;

    bool allow_key_overwrite;
};

int knd_shared_dict_new(struct kndSharedDict **dict, size_t init_size,
                        struct kndMemPool *mempool, bool allow_key_overwrite);
void knd_shared_dict_del(struct kndSharedDict *self);

void* knd_shared_dict_get(struct kndSharedDict *self, const char *key, size_t key_size);
int knd_shared_dict_set(struct kndSharedDict *self, const char *key, size_t key_size, void *data);
int knd_shared_dict_remove(struct kndSharedDict *self, const char *key, size_t key_size);
int knd_shared_dict_map(struct kndSharedDict *self, map_cb_t cb, void *obj);

int knd_shared_dict_marshall(struct kndSharedDict *self, const char *path, size_t path_size,
                             const char *pref, size_t pref_size,
                             knd_set_elem_marshall_cb_t cb, struct kndSharedDict *result_dict,
                             struct kndTask *task);
