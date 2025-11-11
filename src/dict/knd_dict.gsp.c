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
#include "knd_utils.h"

#define DEBUG_DICT_GSP_LEVEL_0 0
#define DEBUG_DICT_GSP_LEVEL_1 0
#define DEBUG_DICT_GSP_LEVEL_2 0
#define DEBUG_DICT_GSP_LEVEL_3 0
#define DEBUG_DICT_GSP_LEVEL_4 0
#define DEBUG_DICT_GSP_LEVEL_TMP 1

struct LocalContext {
    knd_set_elem_marshall_cb_t cb;
    void *cb_ctx;
    size_t count;
};

#if 0
static int export_dict_meta(struct kndDict *dict,
                            const char *path, size_t path_size,struct kndTask *task)
{
    knd_log(".. export dict meta to {path %.*s}", path_size, path);

    return knd_OK;
}
#endif

static int leaf_write_buf(const char *buf, size_t buf_size,
                          struct kndStorageLeaf *leaf, struct kndTask *task)
{
    int err;
    switch (task->mode) {
    case KND_TASK_TRACE_MODE:
        //knd_log(".. write {chunk %.*s} to {filepath %.*s}", buf_size, buf,
        //        leaf->filepath_size, leaf->filepath);
        break;
    default:
        err = knd_append_file((const char*)leaf->filepath, buf, buf_size);
        KND_TASK_ERR("buf to leaf write failure");
        break;
    }
    return knd_OK;
}

static int marshall_entry(void *elem, void *ctx, struct kndStorageLeaf *leaf,
                          size_t *output_size, struct kndTask *task)
{
    struct kndDictEntry *entry = elem;
    struct LocalContext *local_ctx = ctx;
    struct kndDictItem *item;
    size_t item_output_size;
    size_t total_output_size = 0;
    int err;

    assert (local_ctx->cb != NULL);
    err = leaf_write_buf("[i", strlen("[i"), leaf, task);
    KND_TASK_ERR("leaf write failure");
    total_output_size += strlen("[i");

    FOREACH (item, entry->items) {
        item_output_size = 0;
        err = leaf_write_buf("{", 1, leaf, task);
        KND_TASK_ERR("leaf write failure");
        total_output_size++;

        err = local_ctx->cb(item->data, local_ctx->cb_ctx, leaf, &item_output_size, task);
        KND_TASK_ERR("failed to marshall a dict item");
        total_output_size += item_output_size;

        err = leaf_write_buf("}", 1, leaf, task);
        KND_TASK_ERR("leaf write failure");
        total_output_size++;
    }
    err = leaf_write_buf("]", 1, leaf, task);
    KND_TASK_ERR("leaf write failure");
    total_output_size++;

    *output_size = total_output_size;
    return knd_OK;
}

int knd_dict_marshall(struct kndDict *dict, struct kndDictRange *unused_var(range),
                      const char *path, size_t path_size,
                      knd_set_elem_marshall_cb_t cb, void *cb_ctx, struct kndTask *task)
{
    struct kndSet *idx = dict->idx;
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size;
    struct kndDictEntry *entry = NULL;
    //size_t output_size = 0;
    int err;

    struct LocalContext ctx = {
         .cb = cb,
         .cb_ctx = cb_ctx
    };

    if (!idx) {
        err = knd_set_new(&idx, KND_SET_UNIQUE_VALUES, task->mempool);
        KND_TASK_ERR("failed to alloc a set");
        dict->idx = idx;
    }

    for (size_t i = 0; i < dict->size; i++) {
        entry = dict->hash_array[i];
        if (!entry) continue;

        knd_uid_create(i, idbuf, &idbuf_size);

        err = knd_set_add(idx, idbuf, idbuf_size, (void*)entry, task);
        KND_TASK_ERR("failed to add a dict entry to a set idx");
    }

    err = knd_set_marshall(idx, NULL, path, path_size, marshall_entry, &ctx, task);
    KND_TASK_ERR("failed to marshall a set of dict entries");

    //err = export_dict_meta(dict, path, path_size, task);
    //KND_TASK_ERR("failed to export dict meta data");

    return knd_OK;
}
