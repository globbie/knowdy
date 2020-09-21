#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_text.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_shared_set.h"
#include "knd_shared_dict.h"
#include "knd_user.h"
#include "knd_mempool.h"
#include "knd_output.h"
#include "knd_utils.h"

#define DEBUG_TEXT_GSP_LEVEL_1 0
#define DEBUG_TEXT_GSP_LEVEL_2 0
#define DEBUG_TEXT_GSP_LEVEL_TMP 1

int knd_charseq_marshall(void *elem, size_t *output_size, struct kndTask *task)
{
    struct kndCharSeq *seq = elem;
    struct kndOutput *out = task->out;
    size_t orig_size = out->buf_size;

    OUT(seq->val, seq->val_size);

    if (DEBUG_TEXT_GSP_LEVEL_2)
        knd_log("** %zu => %.*s (size:%zu)",  seq->numid,
                seq->val_size, seq->val, out->buf_size - orig_size);

    *output_size = out->buf_size - orig_size;
    return knd_OK;
}

int knd_charseq_unmarshall(const char *elem_id, size_t elem_id_size,
                           const char *val, size_t val_size, void **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    struct kndCharSeq *seq;
    struct kndSharedDict *str_dict = task->repo->str_dict;
    int err;
    if (DEBUG_TEXT_GSP_LEVEL_2)
        knd_log("charseq \"%.*s\" => \"%.*s\"", elem_id_size, elem_id, val_size, val);

    err = knd_charseq_new(mempool, &seq);
    KND_TASK_ERR("charseq alloc failed");
    seq->val = val;
    seq->val_size = val_size;

    err = knd_shared_set_add(task->repo->str_idx, elem_id, elem_id_size, (void*)seq);
    KND_TASK_ERR("failed to register charseq \"%.*s\"", val_size, val);

    err = knd_shared_dict_set(str_dict, val, val_size, (void*)seq, mempool, NULL, &seq->item, false);
    KND_TASK_ERR("failed to register charseq \"%.*s\" in str dict", val_size, val);
    
    *result = seq;
    return knd_OK;
}
