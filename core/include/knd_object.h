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

struct kndRepoCache;
struct kndObjRef;
struct kndSortTag;
struct kndElemRef;
struct kndTask;

struct kndElem;
struct kndRelClass;

struct kndOutput;
struct kndFlatTable;


struct kndMatchPoint
{
    bool is_accented;
    size_t score;
    size_t seqnum;
    size_t orig_pos;
};
    
struct kndObject
{
    /* unique name */
    char name[KND_NAME_SIZE];
    size_t name_size;

    char id[KND_ID_SIZE + 1];
    char batch_id[KND_ID_SIZE + 1];

    knd_state_phase phase;
    char state[KND_STATE_SIZE];

    bool is_subord;
    bool is_concise;

    struct kndObject *root;
    struct kndElem *parent;
    struct kndConcept *dc;

    struct kndSortTag *tag;
    
    /* filename */
    char filename[KND_NAME_SIZE];
    size_t filename_size;

    /* full path from root collection */
    char *filepath;
    size_t filepath_size;
    size_t filesize;

    char dbpath[KND_OBJ_METABUF_SIZE];
    size_t dbpath_size;

    /* mimetype */
    char mimetype[KND_OBJ_METABUF_SIZE];
    size_t mimetype_size;

    struct kndRepoCache *cache;
    struct kndOutput *out;
    struct kndOutput *log;
    struct kndTask *task;
    
    /* full structure */
    struct kndElem *elems;
    struct kndElem *tail;
    size_t num_elems;

    /* rel refs */
    struct kndRelClass *rel_classes;

    /* list of hilited contexts */
    struct kndElem *contexts;
    size_t num_contexts;
    struct kndElem *last_context;

    size_t export_depth;
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
                 size_t           rec_size,
                 knd_format format);

    int (*parse_inline)(struct kndObject *self,
                        const char       *rec,
                        size_t           *total_size);

    int (*index_inline)(struct kndObject *self);

    int (*expand)(struct kndObject *self, size_t depth);


    /*int (*present_summary)(struct kndObject *self, knd_format format);

    int (*present_contexts)(struct kndObject *self,
                            struct kndElemRef *elemref,
                            knd_format        format);
    */
    
    /*int (*get)(struct kndObject *self, struct kndData *data,
               struct kndObject **obj);
    */
    
    int (*get_by_id)(struct kndObject *self, const char *classname,
                     const char *obj_id,
                     struct kndObject **obj);

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

    int (*export)(struct kndObject *self,
                  knd_format format,
                  bool is_concise);

    int (*sync)(struct kndObject *self);

};

/* constructors */
extern void kndObject_init(struct kndObject *self);
extern int kndObject_new(struct kndObject **self);

