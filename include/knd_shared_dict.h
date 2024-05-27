#pragma once

#include <stdatomic.h>
#include "knd_config.h"

struct kndCommit;
struct kndState;
struct kndMemPool;

typedef enum knd_shared_dict_item_phase { KND_SHARED_DICT_VALID,
                                          KND_SHARED_DICT_PENDING,
                                          KND_SHARED_DICT_REMOVED } knd_shared_dict_item_phase;
typedef int (*map_cb_func)(void *obj, const char *elem_id, size_t elem_id_size, size_t count, void *elem);

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
};

int knd_shared_dict_new(struct kndSharedDict **self, size_t init_size);
void knd_shared_dict_del(struct kndSharedDict *self);

void* knd_shared_dict_get(struct kndSharedDict *self, const char *key, size_t key_size);
int knd_shared_dict_set(struct kndSharedDict *self, const char *key, size_t key_size,
                        void *data, struct kndMemPool *mempool,
                        struct kndCommit *commit,
                        struct kndSharedDictItem **result, bool allow_overwrite);
int knd_shared_dict_remove(struct kndSharedDict *self, const char *key, size_t key_size);
int knd_shared_dict_map(struct kndSharedDict *self, map_cb_func cb, void *obj);

