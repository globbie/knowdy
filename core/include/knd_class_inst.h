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
 *   knd_object.h
 *   Knowdy Object
 */
#pragma once

#include "knd_config.h"
#include "knd_class.h"

struct kndState;
struct kndObjRef;
struct kndSortTag;
struct kndElemRef;
struct kndTask;
struct kndElem;
struct kndRelClass;

struct kndOutput;
struct kndFlatTable;
struct kndObjEntry;
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

/*struct kndAggrObject
{
    knd_state_phase phase;

    struct kndElem *parent;
    struct kndClass *conc;
    struct kndElem *elems;
    struct kndElem *tail;
    size_t num_elems;
   
    struct kndAggrObject *next;
    }; */

struct kndObjEntry
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    char name[KND_NAME_SIZE];
    size_t name_size;

    char *block;
    size_t block_size;
    size_t offset;
    knd_state_phase phase;

    struct kndClassInst *obj;
    struct kndRelRef *rels;
};

struct kndObjDir
{
    struct kndObjEntry *objs[KND_RADIX_BASE];
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

    struct kndClass *base;

    char batch_id[KND_ID_SIZE];
    size_t name_hash;

    struct kndState *states;
    size_t init_state;
    size_t num_states;

    bool is_subord;
    bool is_concise;

    struct kndMemPool *mempool;
    struct kndObjEntry *entry;
    struct kndClassInst *root;
    struct kndElem *parent;
    struct kndClassInst *curr_inst;

    struct kndElem *elems;
    struct kndElem *tail;
    size_t num_elems;

    /* list of hilited contexts */
    struct kndElem *contexts;
    size_t num_contexts;
    struct kndElem *last_context;

    const char *locale;
    size_t locale_size;

    knd_format format;
    size_t depth;
    size_t max_depth;

    size_t frozen_size;
    const char *file;
    size_t file_size;

    /* for matching */
    size_t match_state;
    struct kndMatchPoint *matchpoints;
    size_t num_matchpoints;
    size_t match_score;
    size_t max_score;
    float average_score;
    int match_idx_pos;
    int accented;


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

    int (*expand)(struct kndClassInst *self, size_t depth);

//    int (*import)(struct kndClassInst *self,
//                  const char *rec,
//                  size_t *total_size,
//                  knd_format format);

//    int (*update)(struct kndClassInst *self,
//                  const char *rec,
//                  size_t *total_size);
    
    int (*contribute)(struct kndClassInst *self, size_t point_num, size_t orig_pos);

    int (*resolve)(struct kndClassInst *self);
    int (*export)(struct kndClassInst *self);

    gsl_err_t (*select)(struct kndClassInst *self,
		  const char *rec,
		  size_t *total_size);
    gsl_err_t (*select_rels)(struct kndClassInst *self,
		       const char *rec,
		       size_t *total_size);
    int (*sync)(struct kndClassInst *self);
};

/* constructors */
extern void kndClassInst_init(struct kndClassInst *self);
extern void kndObjEntry_init(struct kndObjEntry *self);
extern int kndClassInst_new(struct kndClassInst **self);

