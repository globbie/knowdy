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
 *   knd_attr.h
 *   Knowdy Concept Attr
 */

#pragma once

#include "knd_dict.h"
#include "knd_utils.h"
#include "knd_task.h"
#include "knd_logic.h"
#include "knd_config.h"
#include "knd_output.h"

#include <gsl-parser/gsl_err.h>

#include <stddef.h>

struct kndClass;
struct kndClassEntry;
struct kndText;
struct kndAttr;
struct kndAttrStm;
struct kndAttrFacet;
struct kndProc;
struct kndClassUpdate;
struct kndProcCallArg;
struct kndTask;

typedef enum knd_attr_type {
    KND_ATTR_NONE,
    KND_ATTR_ATOM,
    KND_ATTR_STR,
    KND_ATTR_BIN,
    KND_ATTR_CDATA,
    KND_ATTR_INNER,
    KND_ATTR_CLASS_REF,
    KND_ATTR_CLASS_INST_REF,
    KND_ATTR_TEXT,
    KND_ATTR_INT,
    KND_ATTR_UINT,
    KND_ATTR_RATIO,
    KND_ATTR_URATIO,
    KND_ATTR_REAL,
    KND_ATTR_UREAL,
    KND_ATTR_TIME,
    KND_ATTR_DATE,
    KND_ATTR_DATE_TIME,
    KND_ATTR_BOOL,
    KND_ATTR_PROB,
    KND_ATTR_PROC_REF,
    KND_ATTR_PROC_ARG_REF,
    KND_ATTR_FILE
} knd_attr_type;

static const char* const knd_attr_names[] = {
    "none",
    "atom",
    "str",
    "bin",
    "cdata",
    "inner",
    "cls-ref",
    "cls-inst-ref",
    "text",
    "int",
    "uint",
    "ratio",
    "uratio",
    "real",
    "ureal",
    "time",
    "date",
    "date-time",
    "bool",
    "prob",
    "proc-ref",
    "proc-arg-ref",
    "file"
};

typedef enum knd_attr_quant_type {
    KND_ATTR_SINGLE,
    KND_ATTR_SET,
    KND_ATTR_LIST
} knd_attr_quant_type;

struct kndAttrRef
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    const char *name;
    size_t name_size;

    char owner_id[KND_ID_SIZE];
    size_t owner_id_size;

    struct kndAttr *attr;

    struct kndAttrStm *attr_stm;
    struct kndClassEntry *class_entry;

    struct kndAttrRef *next;
    struct kndAttrRef *tail;
};

struct kndClassRefAttr
{
    const char *name;
    size_t name_size;
    struct kndClassEntry *entry;
};

struct kndClassInstRefAttr
{
    const char *name;
    size_t name_size;
    struct kndClassEntry *entry;
};

struct kndAttr
{
    knd_attr_type type;
    void *impl;

    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;
    knd_attr_quant_type quant_type;

    const char *name;
    size_t name_size;

    struct kndClass *owner;

    bool is_a_set;
    bool set_is_unique;
    bool set_is_atomic;
    bool is_required;
    bool is_indexed;
    bool is_implied;
    bool is_unique;

    const char *classname;
    size_t classname_size;
    struct kndClassEntry *class_entry;
    struct kndClass *cls;

    const char *format_classname;
    size_t format_classname_size;
    struct kndClassEntry *format_class_entry;

    const char *ref_proc_name;
    size_t ref_proc_name_size;
    struct kndProc *proc;

    /* concise representation */
    size_t concise_level;

    struct kndAttrFacet *facets;
    size_t num_facets;

    struct kndState *states;
    size_t init_state;
    size_t num_states;

    struct kndText *tr;

    struct kndAttr *next;
};

int knd_export_inherited_attr(void *obj, const char *elem_id, size_t elem_id_size,
                              size_t count, void *elem);

int knd_apply_attr_stm_updates(struct kndClass *self, struct kndClassUpdate *update,
                               struct kndTask *task);
int knd_register_attr_ref(void *obj, const char *elem_id, size_t elem_id_size,
                          size_t count, void *elem);
int knd_get_arg_value(struct kndAttrStm *src, struct kndAttrStm *query,
                      struct kndProcCallArg *arg, struct kndTask *task);

int knd_attr_export_GSL(struct kndAttr *self, struct kndTask *task, size_t depth);
int knd_attr_export_JSON(struct kndAttr *self, struct kndTask *task, size_t depth);
int knd_attr_export_GSP(struct kndAttr *self, struct kndTask *task);

int knd_attr_export(struct kndAttr *self, knd_format format, struct kndTask *task);
void knd_attr_str(struct kndAttr *attr, size_t depth);

int knd_attr_new(struct kndAttr **result, struct kndMemPool *mempool);
int knd_attr_ref_new(struct kndAttrRef **result, struct kndMemPool *mempool);

// knd_attr.import.c
gsl_err_t knd_attr_import(struct kndAttr *attr, struct kndTask *task, const char *rec, size_t *total_size);

// knd_attr.gsp.c
gsl_err_t knd_attr_read(struct kndAttr *self, struct kndTask *task,
                        const char *rec, size_t *total_size);

int knd_attr_select_clause(struct kndAttr *attr, struct kndClass *cls,
                           const char *rec, size_t *total_size, struct kndTask *task);

// knd_attr.resolve.c
int knd_attr_resolve(struct kndAttr *attr, struct kndTask *task);
int knd_resolve_primary_attrs(struct kndClass *self, struct kndTask *task);

gsl_err_t knd_attr_idx(void *obj, const char *name, size_t name_size);
gsl_err_t knd_attr_implied(void *obj, const char *name, size_t name_size);
gsl_err_t knd_attr_required(void *obj, const char *name, size_t name_size);
gsl_err_t knd_attr_unique(void *obj, const char *name, size_t name_size);
gsl_err_t knd_parse_quant_type(void *obj, const char *rec, size_t *total_size);

int knd_attr_names_marshall(void *elem, size_t *output_size, struct kndTask *task);
int knd_attr_names_unmarshall(const char *elem_id, size_t elem_id_size,
                              const char *rec, size_t rec_size, struct kndTask *task);

int knd_attr_decode(struct kndAttr *attr, struct kndTask *task);

int knd_cls_ref_attr_new(struct kndClassRefAttr **result, const char *name, size_t name_size,
                         struct kndMemPool *mempool);
int knd_cls_inst_ref_attr_new(struct kndClassInstRefAttr **result, const char *name, size_t name_size,
                              struct kndMemPool *mempool);

void knd_attr_index_str(struct kndAttrFacet *owner, const char *seq, size_t seq_size, size_t depth);

int knd_attr_find(struct kndClass *cls, const char *name, size_t name_size,
                  struct kndAttr **result, struct kndTask *task);

int knd_attr_parse_query_stm(struct kndAttrStm *stm,
                             const char *rec, size_t *total_size, struct kndTask *task);
