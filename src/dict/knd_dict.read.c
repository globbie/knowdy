#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "knd_dict.h"
#include "knd_memblock.h"
#include "knd_storage.h"
#include "knd_mempool.h"
#include "knd_task.h"
#include "knd_ignore.h"
#include "knd_utils.h"

#define DEBUG_DICT_READ_LEVEL_0 0
#define DEBUG_DICT_READ_LEVEL_1 0
#define DEBUG_DICT_READ_LEVEL_2 0
#define DEBUG_DICT_READ_LEVEL_3 0
#define DEBUG_DICT_READ_LEVEL_4 0
#define DEBUG_DICT_READ_LEVEL_TMP 1

struct LocalContext {
    struct kndDict *dict;
    struct kndDictEntry *entry;
    knd_dict_item_unmarshall_cb_t unmarshall_cb;
    knd_dict_item_fetch_cb_t fetch_cb;
    const char *key;
    size_t key_size;
    void *cb_ctx;
    void *result;
    struct kndTask *task;
};

static int add_dict_item(struct kndDictEntry *entry,
                         knd_dict_item_unmarshall_cb_t cb, void *cb_ctx,
                         const char *rec, size_t *total_size,
                         struct kndTask *task)
{
    struct kndDictItem *item;
    void *result;
    const char *seq;
    size_t seq_size;
    int err;

    err = cb(rec, 0, cb_ctx, total_size, &seq, &seq_size, &result, task);
    KND_TASK_ERR("failed to apply dict item cb func");

    err = knd_dict_item_new(&item, task->mempool);
    KND_TASK_ERR("failed to alloc a dict item");

    item->data = result;
    item->key = seq;
    item->key_size = seq_size;

    item->next = entry->items;
    entry->items = item;
    entry->num_items++;

    return knd_OK;
}

static gsl_err_t read_dict_item(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    int err;

    err = add_dict_item(ctx->entry, ctx->unmarshall_cb, ctx->cb_ctx, rec, total_size, task);
    if (err) {
        KND_TASK_LOG("failed to read a dict item");
        return *total_size = 0, make_gsl_err_external(err);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_dict_items(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;

    struct gslTaskSpec read_attr_stm_spec = {
        .is_list_item = true,
        .parse = read_dict_item,
        .obj = ctx
    };

    return gsl_parse_array(&read_attr_stm_spec, rec, total_size);
}

int knd_dict_unmarshall_entry(const char *elem_id, size_t elem_id_size,
                              const char *rec, size_t unused_var(rec_size),
                              void *ctx_obj, size_t *result_size, void **unused_var(result),
                              struct kndTask *task)
{
    struct LocalContext *ctx = ctx_obj;
    struct kndDict *dict = ctx->dict;
    size_t item_pos;
    struct kndDictEntry *entry;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_DICT_READ_LEVEL_2) {
        knd_log(">> dict entry {id %.*s}", elem_id_size, elem_id);
    }

    knd_calc_num_id(elem_id, elem_id_size, &item_pos);

    if (item_pos >= dict->size) return knd_LIMIT;

    err = knd_dict_entry_new(&entry, task->mempool);
    if (err) return err;
    dict->hash_array[item_pos] = entry;

    ctx->entry = entry;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = knd_ignore_value,
          .obj = ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "i",
          .name_size = strlen("i"),
          .parse = read_dict_items,
          .obj = ctx
        }
    };

    parser_err = gsl_parse_task(rec, result_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}

#if 0
static int fetch_dict_item(struct LocalContext *ctx,
                           const char *rec, size_t *total_size,
                           struct kndTask *task)
{
    //struct kndDictEntry *entry = ctx->entry;
    const char *key;
    size_t key_size;
    knd_dict_item_fetch_cb_t cb = ctx->fetch_cb;
    const char *seq;
    size_t seq_size;
    int err;

    err = cb(rec, 0, ctx->cb_ctx, total_size, &seq, &seq_size, &ctx->result, task);
    KND_TASK_ERR("failed to apply dict item cb func");

    if (DEBUG_DICT_READ_LEVEL_3) {
        knd_log(">> {rec-parsed %zu} dict entry {key %.*s}", *total_size, seq_size, seq);
    }

    return knd_OK;
}
#endif

static int fetch_entry(const char *elem_id, size_t elem_id_size,
                       const char *rec, size_t rec_size,
                       void *ctx_obj, size_t *result_size, void **result,
                       struct kndTask *task)
{
    struct LocalContext *ctx = ctx_obj;
    struct kndDict *dict = ctx->dict;
    size_t item_pos;
    const char *c = rec;
    size_t chunk_size;
    size_t curr_size = 0;
    int err;

    if (DEBUG_DICT_READ_LEVEL_2) {
        knd_log(">> dict entry to fetch {key %.*s {id %.*s}} from {rec %.*s {size %zu}}",
                ctx->key_size, ctx->key, elem_id_size, elem_id, rec_size, rec, rec_size);
    }

    knd_calc_num_id(elem_id, elem_id_size, &item_pos);
    if (item_pos >= dict->size) return knd_LIMIT;

    do {
        err = ctx->fetch_cb(c, 0, ctx->key, ctx->key_size, ctx->cb_ctx, &chunk_size, result, task);
        switch (err) {
        case knd_OK:
            *result_size = chunk_size;
            return knd_OK;
        case knd_NO_MATCH:
            break;
        default:
            KND_TASK_ERR("failed to apply dict item fetch_cb func");
        }

        curr_size += (chunk_size + 1);
        if (curr_size >= rec_size) break;

        c += (chunk_size + 1);
    }
    while (1);
    
    return knd_NO_MATCH;
}

int knd_dict_fetch(struct kndDict *dict,
                   const char *key, size_t key_size,
                   knd_dict_item_fetch_cb_t item_fetch_cb,
                   void *item_fetch_cb_ctx,
                   void **result, struct kndTask *task)
{
    size_t hash_val = knd_dict_hash(key, key_size) % dict->size;
    struct kndSet *idx = dict->idx;
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size;
    int err;

    struct LocalContext ctx = {
         .dict = dict,
         .fetch_cb = item_fetch_cb,
         .cb_ctx = item_fetch_cb_ctx,
         .key = key,
         .key_size = key_size,
         .task = task
    };

    knd_uid_create(hash_val, idbuf, &idbuf_size);

    if (DEBUG_DICT_READ_LEVEL_2) {
        knd_log(".. {dict {size %zu}} fetching {item %.*s {hash-val %.*s}} "
                " from GSP snapshot ",
                dict->size, key_size, key, idbuf_size, idbuf);
    }

    err = knd_set_fetch(idx, idbuf, idbuf_size, fetch_entry, &ctx, result, task);
    switch (err) {
    case knd_OK:
        break;
    case knd_NO_MATCH:
        break;
    default:
        KND_TASK_ERR("failed to fetch an item from dict");
    }
    return err;
}

static int dict_read_leaf(struct kndDict *dict, struct kndStorageLeaf *leaf,
                          knd_dict_item_unmarshall_cb_t cb, void *cb_ctx, struct kndTask *task)
{
    struct kndSet *idx = dict->idx;
    int err;

    struct LocalContext ctx = {
         .dict = dict,
         .unmarshall_cb = cb,
         .cb_ctx = cb_ctx,
         .task = task
    };

    err = knd_set_read_leaf(idx, leaf, NULL, knd_dict_unmarshall_entry, &ctx, task);
    KND_TASK_ERR("failed to read a dict set leaf");

    return knd_OK;
}

int knd_dict_read(struct kndDict *dict, knd_dict_item_unmarshall_cb_t cb, void *cb_ctx,
                  struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    assert (mempool != NULL);
    struct kndSet *idx = dict->idx;
    struct kndStorageLeaf *leaf;
    int err;

    dict->storage_type = KND_DICT_PERSIST;
    dict->item_unmarshall_cb = cb;

    if (!idx) {
        err = knd_set_new(&idx, KND_SET_STORE_PERSIST, task->mempool);
        KND_TASK_ERR("failed to alloc a set");
        dict->idx = idx;
    }

    for (size_t i = 0; i < idx->store->num_leaves; i++) {
        leaf = idx->store->leaves[i];
        err = dict_read_leaf(dict, leaf, cb, cb_ctx, task);
        KND_TASK_ERR("failed to read a dict leaf");
    }
    return knd_OK;
}
