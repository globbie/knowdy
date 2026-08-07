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
    assert(KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndCharSeq));
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
    assert(KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndTextSearchReport));
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
    assert( KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndTextLoc));
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
    assert(KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndTextRepr));
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
    assert(KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndClassDeclar));
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
    assert(KND_SMALL_MEMPAGE_SIZE >= sizeof(struct kndProposition));
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
    assert(KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndSyNodeSpec));
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
    assert(KND_SMALL_MEMPAGE_SIZE >= sizeof(struct kndSyNode));
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
    assert(KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndClause));
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
    assert(KND_SMALL_MEMPAGE_SIZE >= sizeof(struct kndSentence));
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
    assert(KND_SMALL_MEMPAGE_SIZE >= sizeof(struct kndStatement));
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
    assert(KND_TINY_MEMPAGE_SIZE >= sizeof(struct kndPar));
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
    assert(KND_SMALL_X2_MEMPAGE_SIZE >= sizeof(struct kndText));
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
    struct kndPar *par;
    struct kndSentence *sent;

    state = atomic_load_explicit(&self->states, memory_order_relaxed);
    if (!state) {

        if (self->num_pars) {
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
}

int knd_text_export(struct kndText *self, knd_format format,
                    struct kndTask *task, size_t depth)
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

static int charseq_register(const char *str, size_t str_size,
                            struct kndCharSeq **result, struct kndTask *task)
{
    struct kndDict *str_dict = task->idxs.str_dict;
    struct kndSet *str_idx = task->idxs.str_idx;
    struct kndCharSeq *seq;
    int err;

    assert (str_dict != NULL);
    assert (str_idx != NULL);

    if (DEBUG_TEXT_LEVEL_TMP) {
        knd_log(".. register {seq %.*s}", str_size, str);
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
    seq->numid = str_idx->num_elems + 1;
    knd_uid_create(seq->numid, seq->id, &seq->id_size);

    err = knd_set_add(str_idx, seq->id, seq->id_size, (void*)seq, task);
    KND_TASK_ERR("failed to register a charseq by numid {err %d}", err);
 
    err = knd_dict_set(str_dict, str, str_size, (void*)seq, task);
    KND_TASK_ERR("failed to register a charseq {err %d}", err);

    if (DEBUG_TEXT_LEVEL_TMP) {
        knd_log(">> {seq %.*s {id %.*s}} registered", str_size, str, seq->id_size, seq->id);
    }
    *result = seq;
    return knd_OK;
}

int knd_charseq_register(struct kndRepoSnapshot *snapshot, const char *str, size_t str_size,
                         struct kndCharSeq **result, struct kndTask *task)
{
    struct kndDict *str_dict;
    struct kndSet *str_idx;
    struct kndCharSeq *seq;
    int err;

    switch (task->type) {
    case KND_TASK_BULK_LOAD:
        return charseq_register(str, str_size, result, task);
        break;
    default:
        break;
    }

    if (DEBUG_TEXT_LEVEL_2) {
        knd_log(".. register {seq %.*s} {task-type %d}", str_size, str, task->type);
    }

    /* try task local memcache */
    str_dict = task->cache.str_dict;
    assert (str_dict != NULL);

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

    /* try global read-only idx */
    str_dict = snapshot->cache.str_dict;
    err = knd_dict_fetch(str_dict, str, str_size, knd_charseq_fetch, snapshot, (void**)&seq, task);
    switch (err) {
    case knd_OK:
        /* save in task local cache */
        str_idx = task->cache.str_idx;
        err = knd_set_add(str_idx, seq->id, seq->id_size, (void*)seq, task);
        KND_TASK_ERR("failed to register a charseq by numid {err %d}", err);
 
        str_dict = task->cache.str_dict;
        err = knd_dict_set(str_dict, str, str_size, (void*)seq, task);
        KND_TASK_ERR("failed to register a charseq {err %d}", err);

        if (DEBUG_TEXT_LEVEL_3) {
            knd_log(">> {seq %.*s {id %.*s}} saved in task local memcache",
                    str_size, str, seq->id_size, seq->id);
        }
        *result = seq;
        return knd_OK;
    case knd_NO_MATCH:
        break;
    default:
        KND_TASK_ERR("failed to get an str dict entry {err %d}", err);  
    }

    /* new charseq in import commit */
    return charseq_register(str, str_size, result, task);
}

int knd_text_match_locale(const char *locale_id, size_t locale_id_size,
                          struct kndLocale **result, struct kndLocaleConfig *conf)
{
    struct kndLocale *locale;
    for (size_t i = 0; i < conf->num_supported; i++) {
        locale = conf->supported[i];
        if (locale->id_size != locale_id_size) continue;
        if (memcmp(locale->id, locale_id, locale_id_size)) continue; 
        *result = locale;
        return knd_OK;
    }
    return knd_NO_MATCH;
}
