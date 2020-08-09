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

#include "knd_config.h"
#include "knd_state.h"

struct kndTask;
struct kndClass;
struct kndProc;
struct kndSyNode;
struct kndStatement;

struct kndDiscourseContext
{
    struct kndStatement *stms;

};

struct kndClassDeclaration
{
    struct kndClass *class;

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
    const char *name;
    size_t name_size;
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

    size_t pos;
    size_t len;

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

    const char *seq;
    size_t seq_size;
    struct kndSyNode *synodes;
    struct kndStatement *stms;

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
gsl_err_t knd_text_import(struct kndText *self, const char *rec, size_t *total_size, struct kndTask *task);

int knd_text_export(struct kndText *self, knd_format format, struct kndTask *task);
int knd_par_export_GSL(struct kndPar *par, struct kndTask *task);

int knd_text_new(struct kndMemPool *mempool, struct kndText **result);
int knd_par_new(struct kndMemPool *mempool, struct kndPar **result);
int knd_class_declar_new(struct kndMemPool *mempool, struct kndClassDeclaration **result);
int knd_sentence_new(struct kndMemPool *mempool, struct kndSentence **result);
int knd_clause_new(struct kndMemPool *mempool, struct kndClause **result);
int knd_statement_new(struct kndMemPool *mempool, struct kndStatement **result);
