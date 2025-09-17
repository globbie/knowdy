#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "knd_shared_dict.h"
#include "knd_set.h"
#include "knd_memblock.h"
#include "knd_storage.h"
#include "knd_mempool.h"
#include "knd_task.h"
#include "knd_utils.h"

#define DEBUG_SHARED_DICT_GSP_LEVEL_0 0
#define DEBUG_SHARED_DICT_GSP_LEVEL_1 0
#define DEBUG_SHARED_DICT_GSP_LEVEL_2 0
#define DEBUG_SHARED_DICT_GSP_LEVEL_3 0
#define DEBUG_SHARED_DICT_GSP_LEVEL_4 0
#define DEBUG_SHARED_DICT_GSP_LEVEL_TMP 1

static int build_leaf_filename(struct kndStorageLeaf *leaf,
                               const char *path, size_t path_size,
                               const char *pref, size_t pref_size, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    int err;

    out->reset(out);
    OUT(path, path_size);
    OUT("/", 1);
    OUT(pref, pref_size);
    OUT(KND_GSP_FILE_TMP_EXT_NAME, strlen(KND_GSP_FILE_TMP_EXT_NAME));

    if (out->buf_size >= KND_PATH_SIZE) {
        err = knd_LIMIT;
        KND_TASK_ERR("GSP path too long");
    }

    memcpy(leaf->filepath, out->buf, out->buf_size);
    leaf->filepath[out->buf_size] = '\0';
    leaf->filepath_size = out->buf_size;
    return knd_OK;
}

int knd_shared_dict_marshall(struct kndSharedDict *dict, const char *path, size_t path_size,
                             const char *pref, size_t pref_size,
                             knd_set_elem_marshall_cb_t cb, struct kndSharedDict *result_dict,
                             struct kndTask *task)
{
    struct kndSet *idx;
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size;
    struct kndSharedDictItem *item = NULL;
    struct kndStorageLeaf *leaf;
    size_t output_size = 0;
    int err;

    err = knd_set_new(&idx, KND_SET_MULTIPLE_VALUES, task->mempool);
    KND_TASK_ERR("failed to alloc a shared set");

    // TODO: save dict size and hash func

    for (size_t i = 0; i < dict->size; i++) {
        item = atomic_load_explicit(&dict->hash_array[i], memory_order_acquire);
        if (!item) continue;

        knd_uid_create(i, idbuf, &idbuf_size);
        
        for (; item; item = item->next) {
            err = knd_set_add(idx, idbuf, idbuf_size, (void*)item->data);
            KND_TASK_ERR("failed to add a set elem");
        }
    }

    err = knd_storage_leaf_new(&leaf, 1);
    KND_TASK_ERR("failed to alloc a storage leaf");

    err = build_leaf_filename(leaf, path, path_size, pref, pref_size, task);
    KND_TASK_ERR("failed to build a leaf filename");

    err = knd_set_leaf_marshall(idx, leaf, NULL, cb, NULL, &output_size, task);
    KND_TASK_ERR("failed to marshall a {set %.*s}", pref_size, pref);

    result_dict->idx = idx;
    return knd_OK;
}
