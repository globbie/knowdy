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

int knd_class_declar_new(struct kndMemPool *mempool, struct kndClassDeclaration **result)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndClassDeclaration));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndClassDeclaration));
    *result = page;
    return knd_OK;
}

int knd_proc_declar_new(struct kndMemPool *mempool, struct kndProcDeclaration **result)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndProcDeclaration));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0, sizeof(struct kndProcDeclaration));
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
    assert(mempool->tiny_page_size >= sizeof(struct kndSyNode));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
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
        if (self->seq_size) {
            knd_log("%*stext: \"%.*s\" (lang:%.*s)", depth * KND_OFFSET_SIZE, "",
                    self->seq_size, self->seq, self->locale_size, self->locale);
            return;
        }
        if (self->num_pars) {
            knd_log("%*stext (lang:%.*s) [par",
                    depth * KND_OFFSET_SIZE, "",
                    self->locale_size, self->locale);
            for (par = self->pars; par; par = par->next) {
                knd_log("%*s#%zu:", (depth + 1) * KND_OFFSET_SIZE, "", par->numid);

                for (sent = par->sents; sent; sent = sent->next) {
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
