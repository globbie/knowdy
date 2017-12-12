/**
 *   Copyright (c) 2011-2017 by Dmitri Dmitriev
 *   All rights reserved.
 *
 *   This file is part of the Knowdy Search Engine, 
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
 *   knd_object.h
 *   Knowdy Object
 */
#pragma once

#include "knd_config.h"
#include "knd_concept.h"

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

struct kndAggrObject
{
    knd_state_phase phase;
    char state[KND_STATE_SIZE];

    struct kndElem *parent;
    struct kndConcept *conc;
    struct kndElem *elems;
    struct kndElem *tail;
    size_t num_elems;
   
    struct kndAggrObject *next;
};

struct kndObjEntry
{
    char name[KND_NAME_SIZE];
    size_t name_size;
    char *block;
    size_t block_size;
    size_t offset;

    struct kndObject *obj;
    struct kndRelRef *rels;
};

struct kndObjDir
{
    struct kndObjEntry **objs;
    size_t num_objs;

    struct kndObjDir **dirs;
    size_t num_dirs;
};

struct kndObject
{
    knd_obj_type type;

    /* unique name */
    const char *name;
    size_t name_size;

    char id[KND_ID_SIZE + 1];
    char batch_id[KND_ID_SIZE + 1];

    size_t numid;
    size_t numval;
    size_t name_hash;

    knd_state_phase phase;
    char state[KND_STATE_SIZE];

    bool is_subord;
    bool is_concise;

    struct kndMemPool *mempool;
    struct kndObject *root;
    struct kndElem *parent;
    
    struct kndConcept *conc;

    struct kndObjEntry *entry;
    struct kndSortTag *tag;
    
    struct kndOutput *out;
    struct kndOutput *log;
    struct kndTask *task;
    
    /* full structure */
    struct kndElem *elems;
    struct kndElem *tail;
    size_t num_elems;

    /* reverse rels */
    struct kndRelClass *reverse_rel_classes;

    /* list of hilited contexts */
    struct kndElem *contexts;
    size_t num_contexts;
    struct kndElem *last_context;

    const char *locale;
    size_t locale_size;
    knd_format format;
    size_t depth;
    bool is_expanded;

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

    /* for lists */
    struct kndObject *next;

    /******** public methods ********/
    void (*str)(struct kndObject *self);
    void (*del)(struct kndObject *self);

    int (*parse)(struct kndObject *self,
                 const char       *rec,
                 size_t           *total_size);
    int (*read)(struct kndObject *self,
                const char *rec,
                size_t *total_size);

    int (*expand)(struct kndObject *self, size_t depth);

    int (*import)(struct kndObject *self,
                  const char *rec,
                  size_t *total_size,
                  knd_format format);

    int (*update)(struct kndObject *self,
                  const char *rec,
                  size_t *total_size);
    
    int (*contribute)(struct kndObject *self, size_t point_num, size_t orig_pos);

    int (*resolve)(struct kndObject *self);
    int (*export)(struct kndObject *self);

    int (*sync)(struct kndObject *self);
};

/* constructors */
extern void kndObject_init(struct kndObject *self);
extern void kndObjEntry_init(struct kndObjEntry *self);
extern int kndObject_new(struct kndObject **self);

