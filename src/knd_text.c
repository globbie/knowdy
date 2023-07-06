#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_text.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_class.h"
#include "knd_proc.h"
#include "knd_shard.h"
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

int knd_charseq_new(struct kndMemPool *mempool, struct kndCharSeq **result)
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

int knd_charseq_decode(struct kndRepo *repo, const char *val, size_t val_size, struct kndCharSeq **result,
                      struct kndTask *task)
{
    struct kndCharSeq *seq;
    int err;

    if (DEBUG_TEXT_LEVEL_2)
        knd_log(".. \"%.*s\" repo to decode \"%.*s\" charseq",
                repo->name_size, repo->name, val_size, val);

    assert(val_size <= KND_ID_SIZE);

    err = knd_shared_set_get(repo->str_idx, val, val_size, (void**)&seq);
    KND_TASK_ERR("failed to decode \"%.*s\" charseq ", val_size, val);
    *result = seq;
    return knd_OK;
}

int knd_charseq_fetch(struct kndRepo *repo, const char *val, size_t val_size, struct kndCharSeq **result,
                      struct kndTask *task)
{
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size;
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndCharSeq *seq;
    int err;
    assert(val != NULL);
    assert(val_size != 0);

    if (DEBUG_TEXT_LEVEL_2)
        knd_log(".. \"%.*s\" repo fetching \"%.*s\" charseq", repo->name_size, repo->name, val_size, val);

    seq = knd_shared_dict_get(repo->str_dict, val, val_size);
    if (seq) {
        if (DEBUG_TEXT_LEVEL_3)
            knd_log(">> \"%.*s\" charseq already registered", val_size, val);
        *result = seq;
        return knd_OK;
    }
    err = knd_charseq_new(mempool, &seq);
    KND_TASK_ERR("failed to alloc a charseq");
    seq->val = val;
    seq->val_size = val_size;
    seq->numid = atomic_fetch_add_explicit(&repo->num_strs, 1, memory_order_relaxed);
    
    err = knd_shared_dict_set(repo->str_dict, val, val_size,
                              (void*)seq, mempool, NULL, &seq->item, false);
    KND_TASK_ERR("failed to register a charseq");

    knd_uid_create(seq->numid, idbuf, &idbuf_size);
    err = knd_shared_set_add(repo->str_idx, idbuf, idbuf_size, (void*)seq);
    KND_TASK_ERR("failed to register a charseq by numid");

    if (DEBUG_TEXT_LEVEL_3)
        knd_log(">> \"%.*s\" (id:%.*s) charseq registered", val_size, val, idbuf_size, idbuf);

    *result = seq;
    return knd_OK;
}

int knd_text_search_report_new(struct kndMemPool *mempool, struct kndTextSearchReport **result)
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

int knd_text_loc_new(struct kndMemPool *mempool, struct kndTextLoc **result)
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

int knd_class_declar_new(struct kndMemPool *mempool, struct kndClassDeclar **result)
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

int knd_proposition_new(struct kndMemPool *mempool, struct kndProposition **result)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndProposition));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndProposition));
    *result = page;
    return knd_OK;
}

int knd_synode_spec_new(struct kndMemPool *mempool, struct kndSyNodeSpec **result)
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

int knd_synode_new(struct kndMemPool *mempool, struct kndSyNode **result)
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

int knd_clause_new(struct kndMemPool *mempool, struct kndClause **result)
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

int knd_sentence_new(struct kndMemPool *mempool, struct kndSentence **result)
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

int knd_statement_new(struct kndMemPool *mempool, struct kndStatement **result)
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

int knd_par_new(struct kndMemPool *mempool, struct kndPar **result)
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

int knd_text_new(struct kndMemPool *mempool, struct kndText **result)
{
    void *page;
    int err;
    assert(mempool->small_page_size >= sizeof(struct kndText));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL, &page);
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
                    knd_log("%*s#%zu: \"%.*s\"",
                            (depth + 2) * KND_OFFSET_SIZE, "",
                            sent->numid, sent->seq_size, sent->seq);
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

int knd_text_export(struct kndText *self, knd_format format, struct kndTask *task, size_t depth)
{
    int err;
    switch (format) {
    case KND_FORMAT_JSON:
        err = knd_text_export_JSON(self, task, depth);
        KND_TASK_ERR("failed to export text JSON");
        break;
    default:
        err = knd_text_export_GSL(self, task, depth);
        KND_TASK_ERR("failed to export text GSL");
        break;
    }
    return knd_OK;
}
