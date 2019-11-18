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

struct kndStatement
{
    size_t numid;
    struct kndClass *class;
    struct kndProc *proc;

    struct kndStatement *next;
};

struct kndSyNodeSpec
{
    size_t numid;
    struct kndClass *class;
    struct kndSyNode *synode;

    struct kndSyNodeSpec *next;
};

struct kndSyNode
{
    size_t numid;
    struct kndClass *class;

    struct kndSyNodeSpec *specs;
    size_t num_specs;

    size_t pos;
    size_t len;

    struct kndSyNode *next;
};

struct kndClause
{
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

    /* translated renderings of master content: manual or automatic */
    struct kndText *trs;
    size_t num_trs;

    struct kndState * _Atomic states;
    size_t num_states;

    struct kndText *next;
};

void knd_text_str(struct kndText *self, size_t depth);
gsl_err_t knd_text_import(struct kndText *self,
                          const char *rec,
                          size_t *total_size,
                          struct kndTask *task);

int knd_text_export(struct kndText *self,
                    knd_format format,
                    struct kndTask *task);
int knd_text_new(struct kndMemPool *mempool,
                 struct kndText   **self);
