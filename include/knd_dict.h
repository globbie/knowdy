#pragma once

#include "knd_config.h"
#include "knd_mempool.h"
#include "knd_storage.h"
#include "knd_set.h" // for map_cb_t

typedef enum knd_dict_storage_t { KND_DICT_DEFAULT,
                                  KND_DICT_MEMONLY,
                                  KND_DICT_PERSIST } knd_dict_storage_t;

typedef enum knd_dict_entry_phase { KND_DICT_ENTRY_DEFAULT,
                                    KND_DICT_ENTRY_CACHED } knd_dict_entry_phase;

typedef int (*knd_dict_item_marshall_cb_t)(void *item, void *ctx, struct kndStorageLeaf *leaf,
                                           size_t *output_size, struct kndTask *task);
typedef int (*knd_dict_item_unmarshall_cb_t)(const char *rec, size_t rec_size,
                                             void *ctx, size_t *result_size,
                                             const char **key, size_t *key_size, void **result, struct kndTask *task);

struct kndDictRange
{
    size_t from_pos;
    size_t to_pos;
};

struct kndDictItem
{
    const char *key;
    size_t key_size;
    void *data;

    struct kndDictItem *next;
};

struct kndDictEntry
{
    knd_dict_entry_phase phase;
    struct kndDictItem *items;
    /* in-memory */
    size_t num_items;
    /* persisted on-disk */
    size_t total_items;

    struct kndStorageLeaf *leaf;
};

struct kndDict
{
    knd_dict_storage_t storage_type;
    struct kndDictEntry **hash_array;
    size_t size;

    struct kndSet *idx;
    size_t num_keys;
    size_t num_items;
    struct kndMemPool *mempool;

    knd_dict_item_marshall_cb_t item_marshall_cb;
    knd_dict_item_unmarshall_cb_t item_unmarshall_cb;

};

int knd_dict_new(struct kndDict **self, size_t init_size, struct kndMemPool *mempool);
int knd_dict_entry_new(struct kndDictEntry **result, struct kndMemPool *mempool);
void knd_dict_entry_free(struct kndDictEntry *entry, struct kndMemPool *mempool);
int knd_dict_item_new(struct kndDictItem **result, struct kndMemPool *mempool);
void knd_dict_item_free(struct kndDictItem *item, struct kndMemPool *mempool);

void knd_dict_del(struct kndDict *self);
void knd_dict_reset(struct kndDict *self);

int knd_dict_get(struct kndDict *self, const char *key, size_t key_size, void **result, struct kndTask *task);
int knd_dict_set(struct kndDict *self, const char *key, size_t key_size, void *data, struct kndTask *task);

int knd_dict_remove(struct kndDict *self, const char *key, size_t key_size);
int knd_dict_map(struct kndDict *dict, map_cb_t cb, void *ctx, struct kndTask *task);

int knd_dict_marshall(struct kndDict *dict, struct kndDictRange *range,
                      const char *path, size_t path_size,
                      knd_dict_item_marshall_cb_t cb, void *cb_ctx,
                      struct kndStorage *store, struct kndTask *task);

int knd_dict_unmarshall_entry(const char *elem_id, size_t elem_id_size,
                              const char *rec, size_t unused_var(rec_size),
                              void *ctx_obj, size_t *result_size, void **unused_var(result),
                              struct kndTask *task);

int knd_dict_append_leaf(struct kndDict *dict, struct kndStorageLeaf *leaf, struct kndTask *task);

int knd_dict_fetch_entry(const char *elem_id, size_t elem_id_size,
                         const char *rec, size_t unused_var(rec_size),
                         void *ctx_obj, size_t *result_size, void **unused_var(result),
                         struct kndTask *task);
int knd_dict_fetch_item(struct kndDict *dict, size_t hash_val, const char *name, size_t name_size, void **result, struct kndTask *task);

