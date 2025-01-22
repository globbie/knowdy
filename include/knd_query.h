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
#include "knd_attr_stm.h"
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

    struct kndAttrStm *attr_stms;
    struct kndAttrStm *attr_stms_tail;
    size_t num_attr_stms;

    // TODO rels

    knd_logic_t logic;

    struct kndQuery *children;
    size_t num_children;

    struct kndSet *match;

    struct kndQuery *next;
};

extern gsl_err_t knd_parse_query(void *obj, const char *rec, size_t *total_size);

extern int knd_query_export_GSL(struct kndQuery *self, struct kndTask *task);
extern int knd_query_new(struct kndQuery **self, struct kndMemPool *mempool);
extern int knd_query_plan(struct kndQuery *query, struct kndTask *task);
extern int knd_query_run(struct kndQuery *query, struct kndTask *task);

static inline void knd_query_append_attr_stm(struct kndQuery *q, struct kndAttrStm *stm)
{
    if (!q->attr_stms_tail) {
        q->attr_stms_tail = stm;
        q->attr_stms = stm;
    }
    else {
        q->attr_stms_tail->next = stm;
        q->attr_stms_tail = stm;
    }
    q->num_attr_stms++;
}

