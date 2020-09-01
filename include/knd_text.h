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
#include "knd_shared_idx.h"

struct kndTask;
struct kndClass;
struct kndProc;
struct kndSyNode;
struct kndStatement;
struct kndRepo;
struct kndAttrVar;

struct kndDiscourseContext
{
    struct kndStatement *stms;

};

struct kndTextIdx
{
    struct kndClassEntry *entry;
    struct kndAttr *attr;

    struct kndSharedIdx * _Atomic idx;

    struct kndTextLoc * _Atomic locs;
    atomic_size_t num_locs;

    struct kndProcArgRef *arg_roles;

    /* any other attrs? */
    struct kndTextIdx *children;
    struct kndTextIdx *next;
};

struct kndTextLoc
{
    knd_state_type type;

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

    struct kndSet *idx;
    struct kndTextLoc *locs;
    size_t num_locs;
    struct kndTextSearchReport *next;
};

struct kndClassDeclaration
{
    struct kndClassEntry *entry;

    struct kndClassInstEntry *insts;
    struct kndClassInstEntry *inst_tail;
    size_t num_insts;

    struct kndClassDeclaration *next;
};

struct kndProcDeclaration
{
    struct kndProc *proc;

    struct kndProcInstEntry *insts;
    struct kndProcInstEntry *inst_tail;
    size_t num_insts;

    struct kndProcDeclaration *next;
};

struct kndStatement
{
    const char *schema_name;
    size_t schema_name_size;
    size_t numid;

    struct kndClass *stm_type;

    struct kndDiscourseContext *discourse;

    struct kndClassDeclaration *class_declars;
    struct kndProcDeclaration  *proc_declars;

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

struct kndSyNode
{
    const char *name;
    size_t name_size;
    struct kndClass *class;

    struct kndSyNodeSpec *specs;
    size_t num_specs;

    size_t linear_pos;
    size_t linear_len;

    struct kndSyNode *next;
};

struct kndClause
{
    const char *name;
    size_t name_size;
    size_t numid;
    struct kndClass *class;

    struct kndSyNode *subj;
    struct kndSyNode *pred;

    struct kndClause *next;
};

struct kndSentence
{
    size_t numid;
    const char *lang;
    size_t lang_size;

    const char *seq;
    size_t seq_size;

    struct kndClause *clause;
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

    struct kndClassDeclaration *class_declars;
    struct kndProcDeclaration  *proc_declars;
    struct kndDict *name_idx;

    struct kndPar *next;
};

struct kndText
{
    const char *locale;
    size_t locale_size;

    struct kndAttrVar *attr_var;
    const char *seq;
    size_t seq_size;
    struct kndSyNode *synodes;
    struct kndStatement *stms;

    struct kndPar *pars;
    struct kndPar *last_par;
    size_t num_pars;
    size_t total_props;

    /* translated renderings of deep semantics: manual or automatic */
    struct kndText *trs;
    size_t num_trs;

    struct kndState * _Atomic states;
    size_t num_states;

    struct kndText *next;
};

void knd_text_str(struct kndText *self, size_t depth);
gsl_err_t knd_text_import(struct kndText *self, const char *rec, size_t *total_size, struct kndTask *task);
int knd_text_index(struct kndText *self, struct kndRepo *repo, struct kndTask *task);
gsl_err_t knd_text_search(struct kndRepo *repo, const char *rec, size_t *total_size, struct kndTask *task);
gsl_err_t knd_statement_import(struct kndStatement *stm, const char *rec, size_t *total_size, struct kndTask *task);

int knd_text_export(struct kndText *self, knd_format format, struct kndTask *task);
int knd_par_export_GSL(struct kndPar *par, struct kndTask *task);
int knd_text_export_query_report(struct kndTask *task);
int knd_text_export_query_report_GSL(struct kndTask *task);

int knd_text_new(struct kndMemPool *mempool, struct kndText **result);
int knd_synode_new(struct kndMemPool *mempool, struct kndSyNode **result);
int knd_synode_spec_new(struct kndMemPool *mempool, struct kndSyNodeSpec **result);

int knd_par_new(struct kndMemPool *mempool, struct kndPar **result);
int knd_class_declar_new(struct kndMemPool *mempool, struct kndClassDeclaration **result);
int knd_proc_declar_new(struct kndMemPool *mempool, struct kndProcDeclaration **result);
int knd_sentence_new(struct kndMemPool *mempool, struct kndSentence **result);
int knd_clause_new(struct kndMemPool *mempool, struct kndClause **result);
int knd_statement_new(struct kndMemPool *mempool, struct kndStatement **result);
int knd_text_idx_new(struct kndMemPool *mempool, struct kndTextIdx **result);
int knd_text_loc_new(struct kndMemPool *mempool, struct kndTextLoc **result);
int knd_text_search_report_new(struct kndMemPool *mempool, struct kndTextSearchReport **result);
