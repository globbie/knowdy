/**
 *   Copyright (c) 2011-present by Dmitri Dmitriev
 *   All rights reserved.
 *
 *   This file is part of the Knowdy Graph DB, 
 *   and as such it is subject to the license stated
 *   in the LICENSE file which you have received 
 *   as part of this distribution.
 *
 *   Project homepage:
 *   <http://www.globbie.net>
 *
 *   Initial author and maintainer:
 *         Dmitri Dmitriev aka M0nsteR <dmitri@globbie.net>
 *
 *   ----------
 *   knd_text.h
 *   Knowdy Text Element
 */
#pragma once

#include <stdatomic.h>

#include "knd_config.h"
#include "knd_state.h"
#include "knd_set.h"
#include "knd_storage.h"
#include "knd_shared_idx.h"

struct kndTask;
struct kndSyNode;
struct kndStatement;
struct kndRepo;
struct kndAttrStm;

typedef enum knd_charseq_enc_t {
    KND_CHARSEQ_UTF8,
    KND_ASCII
} knd_charseq_enc_t;

typedef enum knd_charseq_chunk_t {
    KND_CHARSEQ_FULL,
    KND_CHARSEQ_MULTIPART
} knd_charseq_chunk_t;

typedef enum knd_proposition_t {
    KND_ATTR_STATE_PROPOSITION,
    KND_RELATION_PROPOSITION,
    KND_PROCESS_PROPOSITION
} knd_proposition_t;

struct kndCharSeq
{
    knd_charseq_enc_t enc;
    knd_charseq_chunk_t chunk_t;

    /* repo wide str idx id */
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    /* the string in MemBlock */
    const char *val;
    size_t val_size;

    struct kndCharSeq *next;
    struct kndCharSeq *prev;
};

struct kndDiscourseContext
{
    struct kndStatement *stms;
};

struct kndTextLoc
{
    knd_state_t type;

    struct kndClassInst *src;
    struct kndAttr *attr;
    size_t par_id;
    size_t sent_id;
    void *obj;

    struct kndTextLoc *children;
    struct kndTextLoc *next;
};

struct kndTextSearchReport
{
    struct kndClassEntry *entry;
    struct kndAttr *attr;
    struct kndStatement *stm;

    struct kndClassIdx *idx;

    struct kndTextLoc *locs;
    size_t num_locs;
    size_t total_locs;

    struct kndTextSearchReport *next;
};

struct kndClassDeclar
{
    struct kndClassEntry *entry;

    struct kndClassInstEntry *insts;
    struct kndClassInstEntry *inst_tail;
    size_t num_insts;

    struct kndClassDeclar *next;
};

struct kndPropositionSpec
{
    struct kndClass *rel_type;

    struct kndProposition *prop;

    struct kndPropositionSpec *next;
};

struct kndProposition
{
    knd_proposition_t type;
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    struct kndProcInst *inst;

    struct kndProposition *parent;

    struct kndPropositionSpec *specs;
    size_t num_specs;

    struct kndProposition *next;
};

struct kndStatement
{
    const char *schema_name;
    size_t schema_name_size;
    size_t numid;

    struct kndClass *stm_type;

    struct kndClassInstEntry *author;
    struct kndDiscourseContext *discourse;

    struct kndClassDeclar *declars;
    size_t num_declars;

    struct kndProposition *propositions;
    size_t num_propositions;

    struct kndStatement *next;
};

struct kndSyNodeSpec
{
    const char *name;
    size_t name_size;
    struct kndClass *class;

    struct kndSyNode *synode;

    struct kndSyNodeSpec *next;
};

struct kndSyNodeAttr
{
    const char *name;
    size_t name_size;
    struct kndClass *val;

    struct kndSyNodeAttr *next;
};

struct kndSyNode
{
    const char *name;
    size_t name_size;
    struct kndClass *role;

    struct kndSyNodeAttr *attrs;

    struct kndSyNode *topic;
    struct kndSyNodeSpec *spec;
    struct kndSyNode *peers;

    size_t linear_pos;
    size_t linear_len;
    bool is_terminal;
    struct kndSyNode *next;
};

struct kndTextRepr
{
    const char *lang;
    size_t lang_size;
    struct kndClass *cs;

    struct kndCharSeq *seq;

    struct kndSyNode *synode;

    struct kndTextRepr *trs;
    size_t num_trs;

    struct kndTextRepr *next;
};

// finite verb clause vs semantic proposition
struct kndClause
{
    const char *name;
    size_t name_size;
    size_t numid;

    struct kndClass *type;

    struct kndSyNode *subj;
    struct kndSyNode *pred;

    struct kndClause *next;
};

struct kndSentence
{
    size_t numid;

    const char *lang;
    size_t lang_size;

    struct kndCharSeq *seq;

    struct kndSyNode *synode;

    struct kndClause    *clause;
    struct kndStatement *stm;

    struct kndSentence *prev;
    struct kndSentence *next;
};

struct kndPar
{
    size_t numid;
    struct kndSentence *sents;
    struct kndSentence *last_sent;
    size_t num_sents;

    struct kndPar *next;
};

struct kndText
{
    char id[KND_ID_SIZE];
    size_t id_size;

    char locale_id[KND_ID_SIZE];
    size_t locale_id_size;
    const char *locale;
    size_t locale_size;

    struct kndAttrStm *attr_stm;
    struct kndCharSeq *seq;

    char abbr_id[KND_ID_SIZE];
    size_t abbr_id_size;
    struct kndCharSeq *abbr;

    struct kndSyNode *synodes;
    struct kndStatement *stms;

    // TODO text structure
    struct kndPar *pars;
    struct kndPar *last_par;
    size_t num_pars;

    /* translated renderings of deep semantics: manual or automatic */
    struct kndText *trs;
    size_t num_trs;

    struct kndState * _Atomic states;
    size_t num_states;

    struct kndText *next;
};

void knd_text_str(struct kndText *self, size_t depth);
gsl_err_t knd_text_import(struct kndText *self, const char *rec, size_t *total_size,
                          struct kndRepoSnapshot *snapshot, struct kndTask *task);
int knd_text_resolve(struct kndAttrStm *attr_stm, struct kndRepoSnapshot *snapshot, struct kndTask *task);

gsl_err_t knd_text_read(struct kndText *self, const char *rec, size_t *total_size, struct kndTask *task);
int knd_text_index(struct kndText *self, struct kndRepoSnapshot *snapshot, struct kndTask *task);
gsl_err_t knd_text_search(struct kndRepoSnapshot *snapshot, const char *rec, size_t *total_size, struct kndTask *task);

gsl_err_t knd_statement_import(struct kndStatement *stm, const char *rec, size_t *total_size, struct kndTask *task);
gsl_err_t knd_statement_read(struct kndStatement *stm, const char *rec, size_t *total_size, struct kndTask *task);
int knd_statement_resolve(struct kndStatement *stm, struct kndTask *task);

int knd_text_export(struct kndText *self, knd_format format, struct kndTask *task, size_t depth);

int knd_text_export_query_report(struct kndTask *task);
int knd_text_export_query_report_GSL(struct kndTask *task);

int knd_text_export_GSL(struct kndText *glosses, struct kndTask *task, size_t depth);
int knd_text_glosses_export_GSL(struct kndText *text, bool use_locale, struct kndTask *task, size_t depth);
int knd_text_glosses_export_JSON(struct kndText *glosses, struct kndTask *task, size_t depth);

int knd_text_export_GSP(struct kndText *self, struct kndTask *task);
int knd_text_export_JSON(struct kndText *self, struct kndTask *task, size_t depth);
int knd_text_build_JSON(const char *rec, size_t rec_size, struct kndTask *task);

int knd_par_export_GSL(struct kndPar *par, struct kndTask *task);

int knd_charseq_new(struct kndCharSeq **result, struct kndMemPool *mempool);
void knd_charseq_free(struct kndCharSeq *seq, struct kndMemPool *mempool);

int knd_charseq_register(struct kndRepoSnapshot *snapshot, const char *val, size_t val_size, struct kndCharSeq **result, struct kndTask *task);

int knd_charseq_marshall(void *elem, void *ctx, struct kndStorageLeaf *leaf,
                         size_t *output_size, struct kndTask *task);
int knd_charseq_mapping_marshall(void *elem, void *unused_var(ctx), struct kndStorageLeaf *leaf,
                                 size_t *output_size, struct kndTask *task);

int knd_charseq_fetch(const char *rec, size_t unused_var(rec_size), const char *str, size_t str_size,
                      void *ctx, size_t *result_size, void **result, struct kndTask *task);
int knd_charseq_decode(struct kndSet *str_idx, const char *id, size_t id_size,
                       struct kndCharSeq **result, struct kndTask *task);

int knd_text_new(struct kndText **result, struct kndMemPool *mempool);
int knd_synode_new(struct kndSyNode **result, struct kndMemPool *mempool);
int knd_synode_spec_new(struct kndSyNodeSpec **result, struct kndMemPool *mempool);

int knd_par_new(struct kndPar **result, struct kndMemPool *mempool);
int knd_class_declar_new(struct kndClassDeclar **result, struct kndMemPool *mempool);
int knd_sentence_new(struct kndSentence **result,struct kndMemPool *mempool);
int knd_clause_new(struct kndClause **result, struct kndMemPool *mempool);
int knd_statement_new(struct kndStatement **result, struct kndMemPool *mempool);
int knd_proposition_new(struct kndProposition **result, struct kndMemPool *mempool);
int knd_text_repr_new(struct kndTextRepr **result, struct kndMemPool *mempool);

int knd_text_loc_new(struct kndTextLoc **result, struct kndMemPool *mempool);
int knd_text_search_report_new(struct kndTextSearchReport **result, struct kndMemPool *mempool);

gsl_err_t knd_parse_gloss_array(void *obj, const char *rec, size_t *total_size);
gsl_err_t knd_parse_summary_array(void *obj, const char *rec, size_t *total_size);
int knd_read_gloss_array(struct kndClass *cls, const char *rec, size_t *total_size, struct kndTask *task);

int knd_synode_export_JSON(struct kndSyNode *syn, struct kndTask *task);
int knd_synode_concise_export_JSON(struct kndSyNode *syn, struct kndTask *task);

int knd_charseq_unmarshall(const char *elem_id, size_t elem_id_size,
                           const char *rec, size_t rec_size,
                           void *ctx, size_t *parsed_size, void **result,
                           struct kndTask *task);
int knd_gloss_parse(struct kndText *t, const char *rec, size_t *total_size, struct kndTask *task);
