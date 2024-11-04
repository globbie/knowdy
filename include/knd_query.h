/**
 *   Copyright (c) 2011-2018 by Dmitri Dmitriev
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
 *   knd_query.h
 *   Knowdy Query
 */

#pragma once

#include "knd_class.h"
#include "knd_utils.h"
#include "knd_config.h"

struct kndSet;
struct kndMemPool;

typedef enum knd_query_type { KND_QUERY_DEFAULT,
                              KND_QUERY_GET,
                              KND_QUERY_SELECT,
                              KND_QUERY_CREATE,
                              KND_QUERY_UPDATE } knd_query_type;

typedef enum knd_query_obj_type { KND_QUERY_OBJ_DEFAULT,
                                  KND_QUERY_OBJ_REPO,
                                  KND_QUERY_OBJ_CLASS,
                                  KND_QUERY_OBJ_CLASS_INST,
                                  KND_QUERY_OBJ_REL,
                                  KND_QUERY_OBJ_REL_INST,
                                  KND_QUERY_OBJ_PROC,
                                  KND_QUERY_OBJ_PROC_INST } knd_query_obj_type;

struct kndBatchLimits
{
    size_t from;
    size_t size;
    size_t max_items;   
};

struct kndStateRange
{
    size_t eq;
    size_t gt;
    size_t lt;
    size_t gte;
    size_t lte;
};

struct kndQueryView
{
    struct kndBatchLimits *batch;

    bool show_removed_objs;
};

struct kndQuery 
{
    knd_query_type type;
    knd_query_obj_type obj_type;

    struct kndStateRange state;

    struct kndQueryView *view;

    struct kndRepo    *repo;
    struct kndClass   *cls;

    struct kndClassBasePred *base_preds;
    struct kndClassBasePred *base_preds_tail;
    size_t num_base_preds;

    struct kndSet *set;

    bool is_negated;
    knd_logic_t logic;

    struct kndQuery *children;
    size_t num_children;

    struct kndSet *result_set;

    struct kndQuery *next;
};


static inline void knd_query_append_base_pred(struct kndQuery *q, struct kndClassBasePred *base_pred)
{
    if (!q->base_preds) {
        q->base_preds_tail = base_pred;
        q->base_preds = base_pred;
    } else {
        q->base_preds_tail->next = base_pred;
        q->base_preds_tail = base_pred;
    }
    q->num_base_preds++;
}

int knd_query_export_GSL(struct kndQuery *self, struct kndTask *task);
extern int knd_query_new(struct kndQuery **self, struct kndMemPool *mempool);
