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

#define DEBUG_TEXT_LEVEL_0 0
#define DEBUG_TEXT_LEVEL_1 0
#define DEBUG_TEXT_LEVEL_2 0
#define DEBUG_TEXT_LEVEL_3 0
#define DEBUG_TEXT_LEVEL_TMP 1

struct LocalContext {
    struct kndTask       *task;
    struct kndText       *text;
    struct kndPar        *par;
    struct kndSentence   *sent;
    struct kndClause     *clause;
    struct kndSyNode     *synode;
    struct kndSyNodeSpec *synode_spec;
    struct kndStatement  *stm;
};

int knd_charseq_new(struct kndCharSeq **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndCharSeq));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndCharSeq));
    *result = page;
    return knd_OK;
}

void knd_charseq_free(struct kndCharSeq *seq, struct kndMemPool *mempool)
{
    knd_mempool_free(mempool, KND_MEMPAGE_TINY, (void*)seq);
}

int knd_text_search_report_new(struct kndTextSearchReport **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndTextSearchReport));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndTextSearchReport));
    *result = page;
    return knd_OK;
}

int knd_text_loc_new(struct kndTextLoc **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndTextLoc));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndTextLoc));
    *result = page;
    return knd_OK;
}

int knd_text_repr_new(struct kndTextRepr **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndTextRepr));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndTextRepr));
    *result = page;
    return knd_OK;
}

int knd_class_declar_new(struct kndClassDeclar **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndClassDeclar));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndClassDeclar));
    *result = page;
    return knd_OK;
}

int knd_proposition_new(struct kndProposition **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndProposition));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndProposition));
    *result = page;
    return knd_OK;
}

int knd_synode_spec_new(struct kndSyNodeSpec **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndSyNodeSpec));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndSyNodeSpec));
    *result = page;
    return knd_OK;
}

int knd_synode_new(struct kndSyNode **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndSyNode));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndSyNode));
    *result = page;
    return knd_OK;
}

int knd_clause_new(struct kndClause **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndClause));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndClause));
    *result = page;
    return knd_OK;
}

int knd_sentence_new(struct kndSentence **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndSentence));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndSentence));
    *result = page;
    return knd_OK;
}

int knd_statement_new(struct kndStatement **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndStatement));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndStatement));
    *result = page;
    return knd_OK;
}

int knd_par_new(struct kndPar **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndPar));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndPar));
    *result = page;
    return knd_OK;
}

int knd_text_new(struct kndText **result, struct kndMemPool *mempool)
{
    void *page;
    int err;
    assert(mempool->small_x2_page_size >= sizeof(struct kndText));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X2, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndText));
    *result = page;
    return knd_OK;
}

void knd_sentence_str(struct kndSentence *self, size_t depth)
{
    if (self->stm) {
        knd_log("%*s#%zu:", depth * KND_OFFSET_SIZE, "", self->numid);
    }
}

void knd_text_str(struct kndText *self, size_t depth)
{
    struct kndState *state;
    struct kndStateVal *val;
    struct kndPar *par;
    struct kndSentence *sent;

    state = atomic_load_explicit(&self->states, memory_order_relaxed);
    if (!state) {
        if (self->seq) {
            knd_log("%*stext: \"%.*s\" (lang:%.*s)", depth * KND_OFFSET_SIZE, "",
                    self->seq->val_size, self->seq->val, self->locale_size, self->locale);
            return;
        }

        if (self->num_pars) {
            knd_log("%*stext (lang:%.*s) [par",
                    depth * KND_OFFSET_SIZE, "",
                    self->locale_size, self->locale);
            FOREACH (par, self->pars) {
                knd_log("%*s#%zu:", (depth + 1) * KND_OFFSET_SIZE, "", par->numid);

                FOREACH (sent, par->sents) {
                    if (sent->seq) {
                        knd_log("%*s#%zu: \"%.*s\"",
                                (depth + 2) * KND_OFFSET_SIZE, "",
                                sent->numid, sent->seq->val_size, sent->seq->val);
                    }
                    knd_sentence_str(sent, depth + 2);
                }
            }
            knd_log("%*s]", depth * KND_OFFSET_SIZE, "");
        }
        return;
    }
    val = state->val;
    knd_log("%*stext: \"%.*s\" (lang:%.*s)", depth * KND_OFFSET_SIZE, "",
            val->val_size, val->val, self->locale_size, self->locale);
}

int knd_text_export(struct kndText *self, knd_format format,
                    struct kndRepo *repo, struct kndTask *task, size_t depth)
{
    int err;
    switch (format) {
    case KND_FORMAT_JSON:
        err = knd_text_export_JSON(self, repo, task, depth);
        KND_TASK_ERR("failed to export text JSON");
        break;
    default:
        err = knd_text_export_GSL(self, repo, task, depth);
        KND_TASK_ERR("failed to export text GSL");
        break;
    }
    return knd_OK;
}

static int charseq_bulk_register(const char *str, size_t str_size,
                                 struct kndCharSeq **result, struct kndTask *task)
{
    struct kndDict *str_dict = task->idxs.str_dict;
    struct kndSet *str_idx = task->idxs.str_idx;
    struct kndCharSeq *seq;
    int err;

    assert (str_dict != NULL);
    assert (str_idx != NULL);

    if (DEBUG_TEXT_LEVEL_TMP) {
        knd_log(".. initial bulk register {seq %.*s}", str_size, str);
    }

    err = knd_dict_get(str_dict, str, str_size, (void**)&seq, task);
    switch (err) {
    case knd_OK:
        *result = seq;
        return knd_OK;
    case knd_NO_MATCH:
        break;
    default:
        KND_TASK_ERR("failed to get an str dict entry {err %d}", err);  
    }

    err = knd_charseq_new(&seq, task->mempool);
    KND_TASK_ERR("failed to alloc a charseq");
    seq->val = str;
    seq->val_size = str_size;
    seq->numid = task->idxs.str_idx->num_elems + 1;
    knd_uid_create(seq->numid, seq->id, &seq->id_size);

    err = knd_set_add(str_idx, seq->id, seq->id_size, (void*)seq, task);
    KND_TASK_ERR("failed to register a charseq by numid {err %d}", err);
 
    err = knd_dict_set(str_dict, str, str_size, (void*)seq, task);
    KND_TASK_ERR("failed to register a charseq {err %d}", err);

    if (DEBUG_TEXT_LEVEL_3) {
        knd_log(">> {seq %.*s {id %.*s}} registered", str_size, str, seq->id_size, seq->id);
    }
    *result = seq;
    return knd_OK;
}

int knd_charseq_register(struct kndRepo *repo, const char *str, size_t str_size,
                         struct kndCharSeq **result, struct kndTask *task)
{
    struct kndDict *str_dict;
    struct kndSet *str_idx;
    struct kndCharSeq *seq;
    int err;

    switch (task->type) {
    case KND_TASK_BULK_LOAD:
        return charseq_bulk_register(str, str_size, result, task);
        break;
    default:
        break;
    }

    if (DEBUG_TEXT_LEVEL_TMP) {
        knd_log(".. register {seq %.*s} {task-type %d}", str_size, str, task->type);
    }

#if 0
    /* try task local cache */
    str_dict = task->cache.str_dict;
    assert (str_dict != NULL);

    err = knd_dict_get(str_dict, val, val_size, (void**)&seq, task);
    switch (err) {
    case knd_OK:
        *result = seq;
        return knd_OK;
    case knd_NO_MATCH:
        break;
    default:
        KND_TASK_ERR("failed to get an str dict entry {err %d}", err);  
    }

    /* try global cache */
    str_dict = repo->snapshot->cache.str_dict;
    err = knd_dict_get(str_dict, val, val_size, (void**)&seq, task);
    switch (err) {
    case knd_OK:
        *result = seq;
        return knd_OK;
    case knd_NO_MATCH:
        break;
    default:
        KND_TASK_ERR("failed to get an str dict entry {err %d}", err);  
    }

    err = knd_charseq_new(&seq, task->mempool);
    KND_TASK_ERR("failed to alloc a charseq");
    seq->val = val;
    seq->val_size = val_size;
    seq->numid = task->idxs.str_idx->num_elems + 1;
    knd_uid_create(seq->numid, seq->id, &seq->id_size);

    str_idx = task->cache.str_idx;
    err = knd_set_add(str_idx, seq->id, seq->id_size, (void*)seq, task);
    KND_TASK_ERR("failed to register a charseq by numid {err %d}", err);
 
    str_dict = task->cache.str_dict;
    err = knd_dict_set(str_dict, val, val_size, (void*)seq, task);
    KND_TASK_ERR("failed to register a charseq {err %d}", err);

    if (DEBUG_TEXT_LEVEL_3) {
        knd_log(">> {seq %.*s {id %.*s}} registered", val_size, val, seq->id_size, seq->id);
    }
    *result = seq;
#endif
    return knd_FAIL;
}

