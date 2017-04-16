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

#ifndef KND_OBJECT_H
#define KND_OBJECT_H

#ifdef $
#undef $
#endif
#define $ struct kndObject *self

#include "knd_config.h"
#include "knd_dataclass.h"

#define KND_OBJ_HEADER "<?xml version=\"1.0\" encoding=\"utf-8\"?>"

#define KND_OBJ_HEADER_SIZE strlen(KND_OBJ_HEADER)

struct kndDataClass;
struct kndRepoCache;
struct kndObjRef;
struct kndSortTag;
struct kndElemRef;

struct kndElem;
struct kndRelClass;
struct kndData;

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

    bool is_subord;
    bool is_concise;
    
    struct kndObject *root;
    struct kndElem *parent;
    struct kndDataClass *dc;

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
    void (*str)($, size_t depth);

    int (*reset)($);
    void (*cleanup)($);

    int (*del)($);


    int (*parse)(struct kndObject *self,
                 const char       *rec,
                 size_t           rec_size,
                 knd_format format);

    int (*parse_inline)(struct kndObject *self,
                        const char       *rec,
                        size_t           *total_size);

    int (*index_inline)(struct kndObject *self);

    int (*expand)($, size_t depth);


    /*int (*present_summary)($, knd_format format);

    int (*present_contexts)($,
                            struct kndElemRef *elemref,
                            knd_format        format);
    */
    
    int (*get)($, struct kndData *data,
               struct kndObject **obj);

    int (*get_by_id)($, const char *classname,
                     const char *obj_id,
                     struct kndObject **obj);

    int (*import)($,
                  struct kndData *data,
                  knd_format format);

    int (*update)($,
                  const char *rec,
                  size_t *total_size);
    
    int (*flatten)($, struct kndFlatTable *table, long *span);

    int (*match)($,  const char *rec,
                 size_t         rec_size);

    int (*contribute)($, size_t point_num, size_t orig_pos);

    int (*export)($,
                  knd_format format,
                  bool is_concise);

    int (*sync)($);

};

/* constructors */
extern void kndObject_init($);
extern int kndObject_new(struct kndObject **self);

#endif /* KND_OBJECT_H */
