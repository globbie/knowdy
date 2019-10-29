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
 *   knd_attr_inst.h
 *   Knowdy Attr Instance
 */

#pragma once

#include "knd_config.h"
#include "knd_text.h"
#include "knd_attr.h"

struct kndClassInst;
struct glbOutput;
struct kndRelType;

struct kndUser;
struct kndRepo;
struct kndClass;
struct kndTask;

struct kndAttrInst
{
    struct kndAttr *attr;

    struct kndClassInst *parent;
    struct kndClassInst *root;

    struct kndClassInst *inner;
    struct kndClassInst *inner_tail;

    struct kndClassInst *ref_inst;

    bool is_list;

    const char *val;
    size_t val_size;

    struct kndText *text;
    struct kndNum *num;

    struct kndState *states;
    size_t init_state;
    size_t num_states;

    struct kndAttrInst *next;
};

void knd_attr_inst_str(struct kndAttrInst *self, size_t depth);

gsl_err_t knd_attr_inst_parse_select(struct kndAttrInst *self,
                                     const char *rec,
                                     size_t *total_size);

int knd_attr_inst_export(struct kndAttrInst *self,
                         knd_format format,
                         struct kndTask *task);

int knd_attr_inst_new(struct kndMemPool *mempool,
                      struct kndAttrInst **result);
int knd_attr_inst_mem(struct kndMemPool *mempool,
                      struct kndAttrInst **result);

gsl_err_t knd_import_attr_inst(struct kndAttrInst *self,
                               const char *rec, size_t *total_size,
                               struct kndTask *task);
int knd_attr_inst_resolve(struct kndAttrInst *self,
                          struct kndTask *task);
