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
 *   knd_rel.h
 *   Knowdy Relation
 */

#pragma once

#include "knd_config.h"
#include "knd_mempool.h"
#include "knd_attr.h"
#include <gsl-parser.h>

struct kndRel
{
    struct kndAttr *attr;

    const char *ref_classname;
    size_t ref_classname_size;
    struct kndClassEntry *ref_class_entry;
    struct kndClass *ref_class;

    const char *ref_proc_name;
    size_t ref_proc_name_size;
    struct kndProc *proc;

    const char *subj_arg_name;
    size_t subj_arg_name_size;
    struct kndProcArg *subj_arg;

    /* implicit arg name in relation */
    const char *impl_arg_name;
    size_t impl_arg_name_size;
    struct kndProcArg *impl_arg;
};

struct kndRelPred
{
    struct kndRel *rel;
};

extern int knd_rel_new(struct kndMemPool *mempool, struct kndRel **result);
extern int knd_rel_pred_new(struct kndMemPool *mempool, struct kndRelPred **result);

gsl_err_t knd_rel_import(struct kndAttr *attr, struct kndTask *task, const char *rec, size_t *total_size);

extern int knd_rel_resolve(struct kndRel *rel, struct kndRepo *repo, struct kndTask *task);
extern int knd_rel_pred_resolve(struct kndAttrVar *var, struct kndRepo *repo, struct kndTask *task);
