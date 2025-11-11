#pragma once

#include "knd_config.h"
#include "knd_mempool.h"
#include "knd_storage.h"
#include "knd_set.h" // for map_cb_t

typedef enum knd_dict_entry_phase { KND_DICT_ENTRY_DEFAULT,
                                   KND_DICT_ENTRY_CACHED } knd_dict_entry_phase;

typedef enum knd_dict_item_phase { KND_DICT_VALID,
                                   KND_DICT_PENDING,
                                   KND_DICT_REMOVED } knd_dict_item_phase;

struct kndDictRange
{
    size_t from_pos;
    size_t to_pos;
};

struct kndDictItem
{
    knd_dict_item_phase phase;
    const char *key;
    size_t key_size;
    void *data;
    struct kndDictItem *next;
};

struct kndDictEntry
{
    knd_dict_entry_phase phase;
    struct kndDictItem *items;
    size_t num_items;
    struct kndStorageLeaf *leaf;

    size_t total_items;
};

struct kndDict
{
    struct kndDictEntry **hash_array;
    size_t size;

    struct kndMemPool *mempool;
    struct kndSet *idx;
    size_t num_keys;
    size_t num_items;
};

int knd_dict_new(struct kndDict **self, size_t init_size, struct kndMemPool *mempool);
int knd_dict_entry_new(struct kndDictEntry **result, struct kndMemPool *mempool);
int knd_dict_item_new(struct kndDictItem **result, struct kndMemPool *mempool);

void knd_dict_del(struct kndDict *self);
void knd_dict_reset(struct kndDict *self);

void* knd_dict_get(struct kndDict *self, const char *key, size_t key_size);
int knd_dict_set(struct kndDict *self, const char *key, size_t key_size, void *data);
int knd_dict_remove(struct kndDict *self, const char *key, size_t key_size);
int knd_dict_map(struct kndDict *self, map_cb_t cb, void *obj);

int knd_dict_marshall(struct kndDict *dict, struct kndDictRange *range,
                      const char *path, size_t path_size,
                      knd_set_elem_marshall_cb_t cb, void *cb_ctx, struct kndTask *task);

int knd_dict_append_leaf(struct kndDict *dict, struct kndStorageLeaf *leaf, struct kndTask *task);
int knd_dict_read(struct kndDict *dict, knd_set_elem_unmarshall_cb_t cb, void *cb_ctx, struct kndTask *task);

