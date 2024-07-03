#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "knd_shared_dict.h"
#include "knd_shared_set.h"
#include "knd_memblock.h"
#include "knd_mempool.h"
#include "knd_task.h"
#include "knd_utils.h"

#define DEBUG_SHARED_DICT_GSP_LEVEL_0 0
#define DEBUG_SHARED_DICT_GSP_LEVEL_1 0
#define DEBUG_SHARED_DICT_GSP_LEVEL_2 0
#define DEBUG_SHARED_DICT_GSP_LEVEL_3 0
#define DEBUG_SHARED_DICT_GSP_LEVEL_4 0
#define DEBUG_SHARED_DICT_GSP_LEVEL_TMP 1

int knd_shared_dict_marshall(struct kndSharedDict *self, const char *path, size_t path_size,
                             const char *pref, size_t pref_size,
                             elem_marshall_cb cb, struct kndSharedDict *result_dict,
                             struct kndTask *task)
{
    struct kndSharedSet *idx;
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size;
    struct kndSharedDictItem *item = NULL;
    int err;

    err = knd_shared_set_new(&idx, task->mempool);
    KND_TASK_ERR("failed to alloc a shared set");
    
    for (size_t i = 0; i < self->size; i++) {
        item = atomic_load_explicit(&self->hash_array[i], memory_order_acquire);
        if (!item) continue;

        knd_uid_create(i, idbuf, &idbuf_size);

        err = knd_shared_set_add(idx, idbuf, idbuf_size, (void*)item);
        KND_TASK_ERR("failed to add a set elem");
    }

    err = knd_shared_set_marshall(idx, path, path_size, pref, pref_size, cb, idx, task);
    KND_TASK_ERR("failed to marshall class name idx");

    result_dict->idx = idx;
    return knd_OK;
}
