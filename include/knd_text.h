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
struct kndStatement;

struct kndText
{
    const char *locale;
    size_t locale_size;

    const char *seq;
    size_t seq_size;

    /* translated renderings of master content: manual or automatic */
    struct kndText *trs;
    size_t num_trs;

    // TODO: sem graph
    struct kndStatement *stm;

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
