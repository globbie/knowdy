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
    knd_dict_item_unmarshall_cb_t cb;
    void *cb_ctx;
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

    err = add_dict_item(ctx->entry, ctx->cb, ctx->cb_ctx, rec, total_size, task);
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

static int unmarshall_dict_entry(const char *elem_id, size_t elem_id_size,
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

    if (DEBUG_DICT_READ_LEVEL_TMP) {
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

int knd_dict_read_entry(struct kndDict *dict, const char *name, size_t name_size,
                        void **result, struct kndTask *task)
{
    int err;

    //*result = 
    return knd_OK;
}

static int dict_read_leaf(struct kndDict *dict, struct kndStorageLeaf *leaf,
                          knd_dict_item_unmarshall_cb_t cb, void *cb_ctx, struct kndTask *task)
{
    struct kndSet *idx = dict->idx;
    int err;

    struct LocalContext ctx = {
         .dict = dict,
         .cb = cb,
         .cb_ctx = cb_ctx,
         .task = task
    };

    err = knd_set_read_leaf(idx, leaf, unmarshall_dict_entry, &ctx, task);
    KND_TASK_ERR("failed to read a dict set leaf");

    return knd_OK;
}

int knd_dict_read(struct kndDict *dict, knd_dict_item_unmarshall_cb_t cb, void *cb_ctx, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    assert (mempool != NULL);
    struct kndSet *idx = dict->idx;
    struct kndStorageLeaf *leaf;
    int err;

    dict->storage_type = KND_DICT_PERSIST;
    dict->item_unmarshall_cb = cb;

    if (!idx) {
        err = knd_set_new(&idx, KND_SET_UNIQUE_VALUES, task->mempool);
        KND_TASK_ERR("failed to alloc a set");
        dict->idx = idx;
    }

    FOREACH (leaf, idx->leaves) {
        err = dict_read_leaf(dict, leaf, cb, cb_ctx, task);
        KND_TASK_ERR("failed to read a dict leaf");
    }
    return knd_OK;
}
