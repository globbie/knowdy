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

#include "knd_config.h"
#include "knd_state.h"
#include "knd_class.h"

struct kndState;
struct glbOutput;
struct kndSortTag;
struct kndElemRef;
struct kndTask;
struct kndElem;
struct kndRelClass;

struct kndOutput;
struct kndFlatTable;
struct kndMemPool;

typedef enum knd_obj_type {
    KND_OBJ_ADDR,
    KND_OBJ_INNER
} knd_obj_type;

//struct kndMatchPoint
//{
//    bool is_accented;
//    size_t score;
//    size_t seqnum;
//    size_t orig_pos;
//};

struct kndClassInstEntry
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    const char *name;
    size_t name_size;

    char *block;
    size_t block_size;
    size_t offset;
    knd_state_phase phase;

    struct kndClassInst *inst;
    struct kndRelRef *rels;
};

struct kndClassInst
{
    knd_obj_type type;

    const char *name;
    size_t name_size;

    struct kndState *states;
    size_t init_state;
    size_t num_states;

    bool is_subord;
    bool is_concise;

    struct kndClassInstEntry *entry;
    struct kndClass *base;
    struct kndClassInst *root;

    struct kndElem *parent;
    struct kndClassInst *curr_inst;

    struct kndElem *elems;
    struct kndElem *tail;
    size_t num_elems;
    struct kndStateRef *elem_state_refs;

    size_t depth;
    size_t max_depth;

    /* relations */
    struct kndRelRef *rels;
    struct kndState *rel_state;

    /* rel selection */
    struct kndRelRef *curr_rel;

    struct kndClassInst *next;

    gsl_err_t (*parse)(struct kndClassInst *self,
                 const char       *rec,
                 size_t           *total_size);
//    gsl_err_t (*read)(struct kndClassInst *self,
//                      const char *rec,
//                      size_t *total_size);

//    gsl_err_t (*read_state)(struct kndClassInst *self,
//                            const char *rec,
//                            size_t *total_size);
    int (*resolve)(struct kndClassInst *self);

    int (*export)(struct kndClassInst *self,
                  knd_format format,
                  struct kndTask *task);
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
int knd_class_inst_export(struct kndClassInst *self, knd_format format,
                                  struct kndTask *task);
int knd_class_inst_set_export(struct kndClassInst *self, knd_format format,
                                     struct kndTask *task);

// knd_class_inst.gsp.c
int knd_class_inst_export_GSP(struct kndClassInst *self,  struct kndTask *task);

// knd_class_inst.import.c
gsl_err_t knd_import_class_inst(struct kndClassInst *self,
                                       const char *rec, size_t *total_size,
                                       struct kndTask *task);
gsl_err_t kndClassInst_read_state(struct kndClassInst *self,
                                         const char *rec, size_t *total_size,
                                         struct kndTask *task);

// knd_class_inst.json.c
int knd_class_inst_export_JSON(struct kndClassInst *self, struct kndTask *task);
int knd_class_inst_set_export_JSON(struct kndSet *set, struct kndTask *task);

// knd_class_inst.select.c
gsl_err_t knd_select_class_inst(struct kndClass *c,
                                       const char *rec, size_t *total_size,
                                       struct kndTask *task);
