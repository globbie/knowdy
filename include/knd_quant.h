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
 *   <http://www.globbie.net>
 *
 *   Initial author and maintainer:
 *         Dmitri Dmitriev aka M0nsteR <dmitri@globbie.net>
 *
 *   ----------
 *   knd_quant.h
 *   Knowdy Quant Element
 */

#pragma once

#include "knd_config.h"
#include "knd_state.h"

struct kndAttrStm;
struct kndAttrFacet;
struct kndTask;
struct kndClassEntry;
struct kndQuery;

typedef enum knd_quant_base_type { KND_QUANT_DIGIT,
                                 KND_QUANT_TEN,
                                 KND_QUANT_HUNDRED,
                                 KND_QUANT_THOUSAND,
                                 KND_QUANT_TEN_THOUSAND,
                                 KND_QUANT_HUNDRED_THOUSAND,
                                 KND_QUANT_MILLION,
                                 KND_QUANT_BILLION,
                                 KND_QUANT_TRILLION,
                                 KND_QUANT_QUADRILLION } knd_quant_base_type;

typedef enum knd_quant_oper_type { KND_QUANT_OPER_ADD,
                                 KND_QUANT_OPER_SUBTR,
                                 KND_QUANT_OPER_MULT,
                                 KND_QUANT_OPER_DIV,
                                 KND_QUANT_OPER_POW,
                                 KND_QUANT_OPER_SQRT } knd_quant_oper_type;

typedef enum knd_quant_attr_setting {
    KND_QUANT_ATTR_NONE,
    KND_QUANT_ATTR_CALC
} knd_quant_attr_setting;

typedef enum knd_quant_type { KND_QUANT_INT,
                              KND_QUANT_UINT,
                              KND_QUANT_RATIO,
                              KND_QUANT_URATIO,
                              KND_QUANT_REAL,
                              KND_QUANT_UREAL } knd_quant_type;

typedef enum knd_quant_pred_type { KND_QUANT_EQ,
                                   KND_QUANT_RANGE,
                                   KND_QUANT_APPROX } knd_quant_pred_type;

static const char* const knd_quant_attr_setting_names[] = {
    "none",
    "calc"
};

struct kndQuant
{
    knd_quant_type type;
    knd_quant_base_type base;

    knd_quant_oper_type oper;
    struct kndQuant *arg;
};

struct kndQuantUIntFacet
{
    char code;
    size_t pos;
    size_t numval;
};

struct kndQuantUInt
{
    char seq[KND_UINT_MAX_SEQ_SIZE];
    size_t seq_size;
    size_t numval;
};

struct kndQuantUIntRange
{
    struct kndQuantUInt *gt;
    bool gt_eq;

    struct kndQuantUInt *lt;
    bool lt_eq;
};

struct kndQuantUReal
{
    char seq[KND_UREAL_MAX_SEQ_SIZE];
    size_t seq_size;
    long double numval;
};

struct kndQuantAttr
{
    knd_quant_type type;
    const char *name;
    size_t name_size;

    bool is_calculated;

    struct kndQuantAttr *next;
};

struct kndQuantAttrStm
{
    knd_quant_pred_type type;
    struct kndQuantAttr *attr;

    struct kndQuantUInt *uint;
    struct kndQuantUIntRange *uint_range;

    struct kndQuantUReal *ureal;

    struct kndQuantAttrStm *next;
};

struct kndQuantState
{
    knd_state_phase phase;
    //char state[KND_STATE_SIZE];
    
    char val[KND_VAL_SIZE];
    size_t val_size;

    long quantval;

    //struct kndQuant *val;
    struct kndQuantState *next;
};

int knd_quant_new(struct kndQuant **self, struct kndMemPool *mempool);
int knd_quant_attr_new(struct kndQuantAttr **result, knd_quant_type type,
                       const char *name, size_t name_size, struct kndMemPool *mempool);
int knd_quant_attr_stm_new(struct kndQuantAttrStm **result, struct kndMemPool *mempool);

int knd_quant_uint_new(struct kndQuantUInt **result, struct kndMemPool *mempool);
int knd_quant_uint_range_new(struct kndQuantUIntRange **result, struct kndMemPool *mempool);

int knd_quant_uint_facet_new(struct kndQuantUIntFacet **result, struct kndMemPool *mempool);

int knd_quant_ureal_new(struct kndQuantUReal **result, struct kndMemPool *mempool);

int knd_quant_attr_setting_import(struct kndQuantAttr *self, const char *name, size_t name_size,
                                  const char *rec, size_t *total_size, struct kndTask *task);

int knd_quant_parse_uint(const char *val, size_t val_size,
                         struct kndQuantUInt **result, struct kndTask *task);
int knd_quant_parse_ureal(const char *val, size_t val_size,
                         struct kndQuantUReal **result, struct kndTask *task);

int knd_quant_uint_index(struct kndAttrFacet *facet, struct kndClassEntry *topic,
                         struct kndAttrStm *stm, struct kndTask *task);
int knd_quant_ureal_index(struct kndAttrFacet *facet, struct kndClassEntry *topic,
                          struct kndAttrStm *stm, struct kndTask *task);

int knd_quant_uint_parse_stm(struct kndQuantAttrStm *stm,
                             const char *rec, size_t *total_size, struct kndTask *task);
