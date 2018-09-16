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
    KND_OBJ_AGGR
} knd_obj_type;

struct kndMatchPoint
{
    bool is_accented;
    size_t score;
    size_t seqnum;
    size_t orig_pos;
};

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

struct kndObjDir
{
    struct kndClassInstEntry *objs[KND_RADIX_BASE];
    size_t num_objs;

    struct kndObjDir *dirs[KND_RADIX_BASE];
    size_t num_dirs;
};

struct kndClassInst
{
    knd_obj_type type;

    /* unique name */
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

    size_t depth;
    size_t max_depth;

    /* relations */
    struct kndRelRef *rels;
    struct kndState *rel_state;

    /* rel selection */
    struct kndRelRef *curr_rel;

    /* for lists */
    struct kndClassInst *next;

    /******** public methods ********/
    void (*str)(struct kndClassInst *self);
    void (*del)(struct kndClassInst *self);

    gsl_err_t (*parse)(struct kndClassInst *self,
                 const char       *rec,
                 size_t           *total_size);
    gsl_err_t (*read)(struct kndClassInst *self,
                      const char *rec,
                      size_t *total_size);

    gsl_err_t (*read_state)(struct kndClassInst *self,
                            const char *rec,
                            size_t *total_size);

    int (*resolve)(struct kndClassInst *self);

    int (*export)(struct kndClassInst *self,
                  knd_format format,
                  struct glbOutput *out);

    int (*export_state)(struct kndClassInst *self,
                        knd_format format,
                        struct glbOutput *out);
    gsl_err_t (*select_rels)(struct kndClassInst *self,
		       const char *rec,
		       size_t *total_size);
    int (*sync)(struct kndClassInst *self);
};

/* constructors */
extern void kndClassInst_init(struct kndClassInst *self);
extern void kndClassInstEntry_init(struct kndClassInstEntry *self);
extern int kndClassInst_new(struct kndClassInst **self);

extern gsl_err_t knd_parse_select_inst(void *obj,
                                       const char *rec,
                                       size_t *total_size);

extern int knd_class_inst_entry_new(struct kndMemPool *mempool,
                                    struct kndClassInstEntry **result);
extern int knd_class_inst_new(struct kndMemPool *mempool,
                              struct kndClassInst **result);
