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
 *   knd_class_inst.h
 *   Knowdy Class Instance
 */
#pragma once

#include <stdatomic.h>

#include "knd_config.h"
#include "knd_state.h"
#include "knd_class.h"

struct kndState;
struct kndSortTag;
struct kndAttrInstRef;
struct kndTask;
struct kndRepo;
struct kndClassEntry;
struct kndOutput;
struct kndMemPool;

typedef enum knd_obj_type {
    KND_OBJ_ADDR,
    KND_OBJ_INNER
} knd_obj_type;

struct kndClassInstEntry
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;
    knd_state_phase phase;

    const char *name;
    size_t name_size;
    struct kndCharSeq *seq;

    struct kndRepo *repo;
    struct kndClassEntry *blueprint;

    struct kndClassInst * _Atomic inst;
    atomic_size_t cache_cell_num;
    atomic_int num_readers;

    struct kndSharedDictItem *dict_item;
    struct kndAttrIdx *attr_idxs;

    struct kndClassInstEntry *next;
};

struct kndClassInst
{
    knd_obj_type type;

    const char *name;
    size_t name_size;

    const char *alias;
    size_t alias_size;

    struct kndState *states;
    size_t init_state;
    size_t num_states;

    struct kndClassInstEntry *entry;
    struct kndClassInst *root;
    // struct kndClassEntry *blueprint;

    struct kndClassVar *class_var;

    size_t linear_pos;
    size_t linear_len;

    bool resolving_in_progress;
    bool is_resolved;
    bool autogenerate_name;

    struct kndClassInst *next;
};

/* constructors */
void kndClassInst_init(struct kndClassInst *self);
void kndClassInstEntry_init(struct kndClassInstEntry *self);
int kndClassInst_new(struct kndClassInst **self);

void knd_class_inst_str(struct kndClassInst *self, size_t depth);

int knd_class_inst_entry_new(struct kndMemPool *mempool,
                             struct kndClassInstEntry **result);
int knd_class_inst_new(struct kndMemPool *mempool,
                       struct kndClassInst **result);
int knd_class_inst_mem(struct kndMemPool *mempool,
                       struct kndClassInst **result);
int knd_class_inst_export(struct kndClassInst *self,
                          knd_format format,
                          bool is_list_item,
                          struct kndTask *task);
int knd_class_inst_set_export(struct kndClassInst *self, knd_format format,
                              struct kndTask *task);

int knd_class_inst_commit_state(struct kndClass *self, struct kndStateRef *children, size_t num_children,
                                struct kndTask *task);
int knd_class_inst_export_commit(struct kndStateRef *state_refs,
                                 struct kndTask *task);
int knd_class_inst_update_indices(struct kndRepo *repo, struct kndClassEntry *baseclass,
                                  struct kndStateRef *state_refs, struct kndTask *task);

// knd_class_inst.gsp.c
int knd_class_inst_export_GSP(struct kndClassInst *self,  struct kndTask *task);
int knd_class_inst_marshall(void *obj, size_t *output_size, struct kndTask *task);
int knd_class_inst_entry_unmarshall(const char *elem_id, size_t elem_id_size, const char *rec, size_t rec_size,
                                    void **result, struct kndTask *task);
int knd_class_inst_unmarshall(const char *elem_id, size_t elem_id_size, const char *rec, size_t rec_size,
                              void **result, struct kndTask *task);
int knd_class_inst_acquire(struct kndClassInstEntry *entry, struct kndClassInst **result, struct kndTask *task);
int knd_class_inst_read(struct kndClassInst *self, const char *rec, size_t *total_size, struct kndTask *task);

// knd_class_inst.gsl.c
int knd_class_inst_export_GSL(struct kndClassInst *self, bool is_list_item, knd_state_phase phase, struct kndTask *task, size_t depth);

// knd_class_inst.import.c
int knd_import_class_inst(struct kndClassEntry *entry, const char *rec, size_t *total_size, struct kndTask *task);
gsl_err_t knd_class_inst_read_state(struct kndClassInst *self, const char *rec, size_t *total_size, struct kndTask *task);

// knd_class_inst.json.c
int knd_class_inst_export_JSON(struct kndClassInst *self, bool is_list_item, knd_state_phase phase, struct kndTask *task, size_t depth);

// knd_class_inst.select.c
gsl_err_t knd_select_class_inst(struct kndClass *c, const char *rec, size_t *total_size, struct kndTask *task);

// knd_class_inst.resolve.c
int knd_class_inst_resolve(struct kndClassInst *self, struct kndTask *task);

int knd_class_inst_iterate_export_JSON(void *obj, const char *id, size_t id_size, size_t count, void *elem);
