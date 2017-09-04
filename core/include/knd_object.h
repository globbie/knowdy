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

/* inverted rel */
struct kndRelType
{
    struct kndAttr *attr;
    struct ooDict *idx;

    struct kndObjRef *refs;
    struct kndObjRef *tail;
    size_t num_refs;
    
    struct kndRelType *next;
};

struct kndRelClass
{
    struct kndConcept *conc;
    struct kndRelType *rel_types;
    struct kndRelClass *next;
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

struct kndObject
{
    knd_obj_type type;

    /* unique name */
    char name[KND_SHORT_NAME_SIZE];
    size_t name_size;

    char id[KND_ID_SIZE + 1];
    char batch_id[KND_ID_SIZE + 1];

    knd_state_phase phase;
    char state[KND_STATE_SIZE];

    bool is_subord;
    bool is_concise;

    struct kndObject *root;
    struct kndElem *parent;
    
    struct kndConcept *conc;

    struct kndSortTag *tag;
    
    struct kndOutput *out;
    struct kndOutput *log;
    struct kndTask *task;
    
    /* full structure */
    struct kndElem *elems;
    struct kndElem *tail;
    size_t num_elems;

    /* backrefs */
    struct kndRelClass *rel_classes;

    struct kndRef **backrefs[KND_MAX_BACKREFS];
    size_t num_backrefs;

    /* list of hilited contexts */
    struct kndElem *contexts;
    size_t num_contexts;
    struct kndElem *last_context;

    const char *locale;
    size_t locale_size;
    knd_format format;
    size_t depth;
    bool is_expanded;
    
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
    
    /* for lists */
    struct kndObject *next;

    /******** public methods ********/
    void (*str)(struct kndObject *self, size_t depth);
    int (*reset)(struct kndObject *self);
    void (*cleanup)(struct kndObject *self);

    int (*del)(struct kndObject *self);

    int (*parse)(struct kndObject *self,
                 const char       *rec,
                 size_t           *total_size);

    int (*expand)(struct kndObject *self, size_t depth);

    int (*import)(struct kndObject *self,
                  const char *rec,
                  size_t *total_size,
                  knd_format format);

    int (*update)(struct kndObject *self,
                  const char *rec,
                  size_t *total_size);
    
    int (*flatten)(struct kndObject *self, struct kndFlatTable *table, unsigned long *span);

    int (*match)(struct kndObject *self,
                 const char *rec,
                 size_t      rec_size);

    int (*contribute)(struct kndObject *self, size_t point_num, size_t orig_pos);

    int (*resolve)(struct kndObject *self);
    int (*export)(struct kndObject *self);

    int (*sync)(struct kndObject *self);
};

/* constructors */
extern void kndObject_init(struct kndObject *self);
extern int kndObject_new(struct kndObject **self);

