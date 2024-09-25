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

/*err = knd_charseq_decode(val, val_size, &ctx->text->seq, task);
    if (err) {
        KND_TASK_LOG("failed to decode a gloss charseq %.*s", val_size, val);
        return make_gsl_err_external(err);
    }
    if (DEBUG_TEXT_READ_LEVEL_TMP) {
        knd_log(">> {locale %.*s {gloss %.*s}}", ctx->text->locale_size, ctx->text->locale,
            ctx->text->seq->val_size, ctx->text->seq->val);
    }
    */


int knd_charseq_decode(const char *id, size_t id_size,
                       struct kndCharSeq **result, struct kndTask *task)
{
    struct kndSharedSet *str_idx = task->idxs->str_idx;
    struct kndCharSeq *seq;
    struct kndStorageLeaf *leaf;
    int err;
    assert(id_size <= KND_ID_SIZE);

    if (DEBUG_TEXT_DECODE_LEVEL_2) {
        knd_log(".. decoding {seq {id %.*s}}", id_size, id);
    }

    err = knd_shared_set_get(str_idx, id, id_size, (void**)&seq);
    if (!err) {
        *result = seq;
        return knd_OK;
    }

    err = knd_shared_set_find_leaf(str_idx, id, id_size, &leaf, task);
    KND_TASK_ERR("no storage leaf found for unmarshalling {seq %.*s}", id_size, id);

    err = knd_storage_leaf_read_elem(leaf, id, id_size, knd_string_unmarshall,
                                     NULL, (void**)&seq, task);
    KND_TASK_ERR("failed to unmarshall {seq %.*s}", id_size, id);

    *result = seq;
    return knd_OK;
}

int knd_charseq_fetch(struct kndRepo *repo, const char *val, size_t val_size,
                      struct kndCharSeq **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndCharSeq *seq;
    int err;
    assert(val != NULL);
    assert(val_size != 0);

    if (DEBUG_TEXT_DECODE_LEVEL_2) {
        knd_log(".. {repo %.*s} fetching {seq %.*s}",
                repo->name_size, repo->name, val_size, val);
    }
    seq = knd_shared_dict_get(task->idxs->str_dict, val, val_size);
    if (seq) {
        if (DEBUG_TEXT_DECODE_LEVEL_3) {
            knd_log(">> {seq %.*s} already registered", val_size, val);
        }
        *result = seq;
        return knd_OK;
    }

    err = knd_charseq_new(&seq, mempool);
    KND_TASK_ERR("failed to alloc a charseq");
    seq->val = val;
    seq->val_size = val_size;
    seq->numid = atomic_fetch_add_explicit(&task->idxs->num_strs, 1, memory_order_relaxed);
    knd_uid_create(seq->numid, seq->id, &seq->id_size);
 
    err = knd_shared_dict_set(task->idxs->str_dict, val, val_size, (void*)seq);
    KND_TASK_ERR("failed to register a charseq");

    err = knd_shared_set_add(task->idxs->str_idx, seq->id, seq->id_size, (void*)seq);
    KND_TASK_ERR("failed to register a charseq by numid");

    if (DEBUG_TEXT_DECODE_LEVEL_3) {
        knd_log(">> {seq %.*s {id %.*s}} registered", val_size, val, seq->id_size, seq->id);
    }
    *result = seq;
    return knd_OK;
}
