#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_text.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_class.h"
#include "knd_proc.h"
#include "knd_user.h"
#include "knd_shared_set.h"
#include "knd_utils.h"
#include "knd_mempool.h"
#include "knd_output.h"

#define DEBUG_TEXT_DECODE_LEVEL_0 0
#define DEBUG_TEXT_DECODE_LEVEL_1 0
#define DEBUG_TEXT_DECODE_LEVEL_2 0
#define DEBUG_TEXT_DECODE_LEVEL_3 0
#define DEBUG_TEXT_DECODE_LEVEL_TMP 1

int knd_charseq_decode(struct kndRepo *repo, const char *id, size_t id_size,
                       struct kndCharSeq **result, struct kndTask *task)
{
    struct kndSet *str_idx;
    struct kndCharSeq *seq;
    int err;

    assert(id_size <= KND_ID_SIZE);

    if (DEBUG_TEXT_DECODE_LEVEL_2) {
        knd_log(".. decoding {seq {id %.*s}}", id_size, id);
    }

    str_idx = task->idxs.str_idx;
    err = knd_set_get(str_idx, id, id_size, (void**)&seq, task);
    switch (err) {
    case knd_OK:
        *result = seq;
        return knd_OK;
    case knd_NO_MATCH:
        break;
    default:
        KND_TASK_ERR("failed to get a charseq");
    }

    str_idx = repo->snapshot->cache.str_idx;
    err = knd_set_get(str_idx, id, id_size, (void**)&seq, task);
    switch (err) {
    case knd_OK:
        // TODO register charseq in task idx

        *result = seq;
        return knd_OK;
    case knd_NO_MATCH:
        break;
    default:
        KND_TASK_ERR("failed to get a charseq");
    }
    return knd_NO_MATCH;
}
