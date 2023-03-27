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
 *   <http://www.knowdy.net>
 *
 *   Initial author and maintainer:
 *         Dmitri Dmitriev aka M0nsteR <dmitri@globbie.net>
 *
 *   ----------
 *   knd_logic.h
 *   Knowdy Logic Clause
 */
#pragma once

#include "knd_task.h"
#include "knd_config.h"

typedef enum knd_logic_t { KND_LOGIC_AND, 
                           KND_LOGIC_OR,
                           KND_LOGIC_NOT
} knd_logic_t;

struct kndSituation
{
    char id[KND_ID_SIZE];
    size_t id_size;
    struct kndClassEntry *env;
    struct kndSituation  *next;
};

struct kndLogicClause
{
    knd_logic_t logic;
    struct kndSituation *sit;
};

int knd_situation_new(struct kndMemPool *mempool, struct kndSituation **self);
int knd_logic_clause_new(struct kndMemPool *mempool, struct kndLogicClause **result);

// knd_logic.import.c
int knd_logic_clause_parse(struct kndLogicClause *clause, const char *rec, size_t *total_size, struct kndTask *task);
