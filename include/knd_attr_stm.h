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
 *   knd_attr_stm.h
 *   Knowdy Concept Attr Statement
 */
#pragma once

struct kndClass;
struct kndClassEntry;
struct kndClassBasePred;

typedef enum knd_attr_stm_t {
    KND_ATTR_STM_DEFAULT,
    KND_ATTR_STM_QUERY_RANGE,
    KND_ATTR_STM_QUERY_SINGLE
} knd_attr_stm_t;

typedef enum knd_attr_stm_phase_t {
    KND_ATTR_STM_INIT,
    KND_ATTR_STM_RESOLVE_IN_PROGRESS,
    KND_ATTR_STM_RESOLVED
} knd_attr_stm_phase_t;

struct kndAttrStmCtx
{
    struct kndAttrStm *parent_stm;
    struct kndAttrStm *attr_stm;
    struct kndTask *task;
};

struct kndAttrStmRef
{
    struct kndAttrStm *stm;
    struct kndState *states;

    struct kndAttrStmRef *next;
    struct kndAttrStmRef *tail;
};

struct kndClassInnerAttrStm
{
    const char *cls_name;
    size_t cls_name_size;
    struct kndClassEntry *cls_entry;

    struct kndClass *cls;
    struct kndClass *template_cls;
};

struct kndClassRefAttrStm
{
    const char *cls_name;
    size_t cls_name_size;
    struct kndClassEntry *cls_entry;

    struct kndClass *cls;
    struct kndClass *template_cls;
};

struct kndAttrStm
{
    knd_attr_stm_t type;
    knd_attr_stm_phase_t phase;

    struct kndAttr *attr;
    struct kndClass *subj;

    char id[KND_ID_SIZE];
    size_t id_size;
    const char *name;
    size_t name_size;

    void *subtype;

    const char *val;
    size_t val_size;
    char val_id[KND_ID_SIZE];
    size_t val_id_size;

    struct kndCharSeq *seq;

    struct kndAttrStm *parent;
    struct kndAttrStm *children;
    struct kndAttrStm *tail;
    size_t num_children;

    bool is_list_item;
    size_t list_count;

    struct kndState *states;
    size_t init_state;
    size_t num_states;

    struct kndAttrStm *list;
    struct kndAttrStm *list_tail;
    size_t num_list_elems;

    struct kndSet *matchset;
    size_t num_matches;
    size_t min_query_ops;

    struct kndAttrStm *next;
};

// knd_attr_stm.import.c
int knd_import_attr_stm(struct kndAttrStm *attr_stm, const char *name, size_t name_size,
                        const char *rec, size_t *total_size, struct kndTask *task);
int knd_import_attr_stm_list(struct kndAttrStm *attr_stm, const char *name, size_t name_size,
                             const char *rec, size_t *total_size, struct kndTask *task);

int knd_attr_stm_export_GSL(struct kndAttrStm *self, struct kndTask *task, size_t depth);
int knd_attr_stms_export_GSL(struct kndAttrStm *stms, struct kndTask *task, size_t depth);

int knd_attr_stm_export_JSON(struct kndAttrStm *stm, struct kndTask *task, size_t depth);
int knd_attr_stms_export_JSON(struct kndAttrStm *stms, struct kndTask *task, size_t depth);

int knd_attr_stm_export_GSP(struct kndAttrStm *self, struct kndTask *task,
                            struct kndOutput *out, size_t depth);
int knd_attr_stms_export_GSP(struct kndAttrStm *stms, struct kndOutput *out, struct kndTask *task,
                             size_t depth);

int knd_attr_stm_subj_GSP(void *obj, void *ctx, struct kndStorageLeaf *leaf,
                          size_t *result_size, struct kndTask *task);

void knd_attr_stm_str(struct kndAttrStm *stm, size_t depth);

gsl_err_t knd_select_attr_stm(struct kndClass *class, const char *name, size_t name_size,
                              const char *rec, size_t *total_size,
                              struct kndTask *task);

// knd_attr_stm.resolve.c
int knd_resolve_attr_stm(struct kndClass *cls, struct kndAttrStm *stm, struct kndTask *task);

// knd_attr_stm.index.c
int knd_index_attr_stm(struct kndClassEntry *topic, struct kndAttr *attr,
                       struct kndAttrStm *stm, struct kndTask *task);
//int knd_index_inst_attr_stm(struct kndClassInstEntry *topic_inst,
//                            struct kndAttr *attr, struct kndAttrStm *stm, struct kndTask *task);

int knd_index_attr_stm_list(struct kndClassEntry *topic, struct kndAttr *attr,
                            struct kndAttrStm *stm, struct kndTask *task);
//int knd_index_inst_attr_stm_list(struct kndClassInstEntry *topic_inst, struct kndAttr *attr,
//                                 struct kndAttrStm *stm, struct kndTask *task);

int knd_attr_stm_inner_idx(struct kndClassEntry *topic, struct kndAttr *attr,
                           struct kndAttrStm *stm, struct kndTask *task);

int knd_attr_stm_plan(struct kndAttrStm *stm, struct kndTask *task);

int knd_cls_ref_attr_stm_new(struct kndClassRefAttrStm **result, struct kndMemPool *mempool);
int knd_cls_inner_attr_stm_new(struct kndClassInnerAttrStm **result, struct kndMemPool *mempool);

int knd_attr_stm_new(struct kndAttrStm **result, struct kndClass *subj, struct kndMemPool *mempool);

// knd_attr_stm.gsp.c
int knd_read_attr_stm(struct kndAttrStm *stm, const char *id, size_t id_size,
                      const char *rec, size_t *total_size, struct kndTask *task);

int knd_read_attr_stm_list(struct kndAttrStm *stm, const char *name, size_t name_size,
                           const char *rec, size_t *total_size, struct kndTask *task);

int knd_decode_attr_stms(struct kndClass *base, struct kndAttrStm *attr_stms, struct kndTask *task);

// knd_attr.select.c
int knd_attr_stm_match(struct kndAttrStm *self, struct kndAttrStm *template);
int knd_attr_parse_query_stm(struct kndAttrStm *stm,
                             const char *rec, size_t *total_size, struct kndTask *task);

int knd_attr_stm_present_subj(void *obj, void *ctx_obj);

int knd_facet_cls_key_get(void *elem, void **result, struct kndTask *task);
int knd_attr_stm_get_elem_key(void *obj, const char **key, size_t *key_size);
