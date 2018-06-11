/**
 *   Copyright (c) 2011-2018 by Dmitri Dmitriev
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
 *   knd_concept.h
 *   Knowdy Concept
 */
#pragma once

#include "knd_dict.h"
#include "knd_facet.h"
#include "knd_utils.h"
#include "knd_object.h"
#include "knd_config.h"

#include <gsl-parser/gsl_err.h>

struct kndAttr;
struct kndAttrItem;
struct kndClass;
struct glbOutput;
struct kndTranslation;
struct kndClass;
struct kndRel;
//struct kndRelObj;
struct kndTask;
struct kndSet;
struct kndUser;
struct kndClassUpdate;
struct kndClassUpdateRef;
struct kndObjEntry;

//typedef enum knd_conc_type {
//    KND_CONC_CLASS,
//    KND_CONC_PROC,
//    KND_CONC_ATTR
//} knd_conc_type;

//struct kndObjStateIdx
//{
//    struct kndObject **objs;
//    size_t num_objs;
//    struct kndStateIdx **children;
//};

//struct kndConcRef
//{
//    size_t state;
//    struct kndClassRef *dir;
//    //struct kndClass *conc;
//};

//struct kndConcRefSet
//{
//    size_t state;
//    struct kndClassRef *dir;
//    struct kndClass *conc;
//};

struct kndClassVar
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    char classname[KND_NAME_SIZE];
    size_t classname_size;

    struct kndAttrItem *attrs;
    struct kndAttrItem *tail;
    size_t num_attrs;

    struct kndClass *parent;

    struct kndClass *conc;
    //struct kndClassRef *conc_dir;

    struct kndMemPool *mempool;
    struct kndClassVar *next;
};

struct kndConcFolder
{
    char name[KND_NAME_SIZE];
    size_t name_size;
    //size_t num_concepts;
    struct kndConcFolder *parent;
    struct kndConcFolder *next;
};

struct kndClassRef
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;
    //size_t owner_id;

    char name[KND_NAME_SIZE];
    size_t name_size;
    struct kndClass *conc;
    struct kndMemPool *mempool;

    knd_state_phase phase;

    size_t global_offset;
    size_t curr_offset;
    size_t block_size;

    int fd;
    size_t body_size;
    size_t obj_block_size;
    size_t dir_size;
    bool is_indexed;

    struct kndSet *descendants;
    struct kndSet *child_idx;
    struct kndClassRef **children;
    size_t num_children;
    size_t num_terminals;

    struct kndRelDir **rels;
    size_t num_rels;

    struct kndProcRef **procs;
    size_t num_procs;

    //size_t next_obj_numid;

    struct kndObjDir *obj_dir;
    size_t num_objs;

    struct kndSet *class_idx;
    struct ooDict *class_name_idx;
    struct kndSet *obj_idx;
    struct ooDict *obj_name_idx;

    struct ooDict *reverse_attr_name_idx;
    //struct kndProcRef *proc_dir;

    struct kndTask *task;
    struct glbOutput *out;

    size_t child_count;
    struct kndClassRef *prev;
    struct kndClassRef *next;
};

struct kndClassUpdateRef
{
    knd_state_phase phase;
    struct kndUpdate *update;
    struct kndClassUpdateRef *next;
};

struct kndClass
{
    //knd_conc_type type;
    char name[KND_NAME_SIZE];
    size_t name_size;

    size_t numid;
    //size_t owner_id;
    size_t next_numid;

    struct kndState *state;
    size_t num_states;

    struct kndClassUpdateRef *updates;
    size_t num_updates;

    struct kndTranslation *tr;
    struct kndTranslation *summary;

    /* initial scheme location */
    const char *dbpath;
    size_t dbpath_size;

    struct kndTask *task;

    knd_format format;
    size_t depth;
    size_t max_depth;

    struct kndProc *proc;
    struct kndRel *rel;

    struct kndAttr *attrs;
    struct kndAttr *tail_attr;
    size_t num_attrs;
    struct kndAttr *implied_attr;

    struct kndClassVar *base_items;
    size_t num_base_items;

    struct kndClassRef *bases[KND_MAX_BASES];
    size_t num_bases;

    //bool ignore_children;
    bool is_resolved;

    struct kndClass *root_class;
    struct kndClass *curr_class;
    struct kndClass *curr_baseclass;
    struct kndAttr *curr_attr;
    //size_t attrs_left;
    struct kndObject  *curr_obj;

    struct kndClassRef *dir;
    struct kndConcFolder *folders;
    size_t num_folders;

    /* allocator */
    struct kndMemPool *mempool;

    /* incoming */
    struct kndClass *inbox;
    size_t inbox_size;

    struct kndObject *obj_inbox;
    size_t obj_inbox_size;
    size_t num_objs;

    bool batch_mode;

    /* indices */
    struct kndSet *class_idx;
    struct ooDict *class_name_idx;

    //struct kndSet *attr_idx;
    struct ooDict *attr_name_idx;

    /* state idx */
    //struct kndObjStateIdx **obj_states;

    //struct kndConcRef children[KND_MAX_CONC_CHILDREN];
    //size_t num_children;
    //size_t num_terminals;
    //bool is_terminal;

    //struct kndConcRef frozen_dir[KND_MAX_CONC_CHILDREN];
    //size_t frozen_dir_size;

    /* TODO: move to mempool */
    char dir_buf[KND_MAX_CONC_CHILDREN * KND_DIR_ENTRY_SIZE];
    //size_t dir_buf_size;
    const char *frozen_output_file_name;
    size_t frozen_output_file_name_size;
    size_t frozen_size;

    const char *frozen_name_idx_path;
    size_t frozen_name_idx_path_size;

    struct kndUpdate *curr_update;
    struct kndClass *next;

    struct kndUser *user;
    struct glbOutput *out;
    struct glbOutput *dir_out;
    struct glbOutput *log;
    
    /***********  public methods ***********/
    void (*init)(struct kndClass  *self);
    void (*del)(struct kndClass   *self);
    void (*reset_inbox)(struct kndClass   *self);
    void (*str)(struct kndClass *self);

    int (*open)(struct kndClass   *self);
    int (*load)(struct kndClass   *self,
		struct kndConcFolder *parent_folder,
                const char *filename,
                size_t filename_size);
    int (*read)(struct kndClass   *self,
                const char *rec,
                size_t *total_size);
    int (*read_obj_entry)(struct kndClass   *self,
                          struct kndObjEntry *entry,
                          struct kndObject **result);
    int (*restore)(struct kndClass   *self);

    int (*select_delta)(struct kndClass *self,
                        const char *rec,
                        size_t *total_size);
    int (*freeze)(struct kndClass *self);
    gsl_err_t (*sync)(void *obj,
                      const char *rec,
                      size_t *total_size);

    //int (*build_diff)(struct kndClass   *self,
    //                  const char *start_state,
    //                  size_t global_state_count);
    int (*coordinate)(struct kndClass *self);

    int (*resolve)(struct kndClass     *self,
		   struct kndClassUpdate *update);

    int (*update_state)(struct kndClass *self);
    int (*apply_liquid_updates)(struct kndClass *self,
                                const char *rec,
                                size_t *total_size);
    int (*select)(void  *self,
                  const char *rec,
                  size_t *total_size);

    int (*get)(struct kndClass  *self,
               const char *name, size_t name_size,
               struct kndClass  **result);

    int (*get_obj)(struct kndClass *self,
                   const char *name, size_t name_size,
                   struct kndObject **result);

    int (*get_attr)(struct kndClass *self,
                    const char *name, size_t name_size,
                    struct kndAttr **result);
    gsl_err_t (*import)(void *self,
                        const char *rec,
                        size_t *total_size);
    int (*export)(struct kndClass *self);

    /* traversal */
    //void (*rewind)(struct kndClass   *self);
    //int (*next_attr)(struct kndClass   *self,
    //                 struct kndAttr **result);
};

/* constructor */
extern void kndClass_init(struct kndClass *self);
extern int kndClass_new(struct kndClass **self,
                        struct kndMemPool *mempool);
