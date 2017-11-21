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
 *   knd_concept.h
 *   Knowdy Concept
 */
#pragma once

#include "knd_dict.h"
#include "knd_facet.h"
#include "knd_utils.h"
#include "knd_config.h"

struct kndAttr;
struct kndAttrItem;
struct kndConcept;
struct kndOutput;
struct kndTranslation;
struct kndConcept;
struct kndRel;
struct kndRelObj;
struct kndTask;
struct kndUser;
struct kndTaskArg;

typedef enum knd_conc_type {
    KND_CONC_CLASS,
    KND_CONC_PROC,
    KND_CONC_ATTR
} knd_conc_type;

struct kndObjStateIdx
{
    struct kndObject **objs;
    size_t num_objs;
    struct kndStateIdx **children;
};

struct kndConcRef
{
    size_t state;
    struct kndConcDirt *dir;
    struct kndConcept *conc;
};

struct kndConcRefSet
{
    size_t state;
    struct kndConcDirt *dir;
    struct kndConcept *conc;
};

struct kndConcItem
{
    char name[KND_NAME_SIZE];
    size_t name_size;
    char id[KND_ID_SIZE];
    size_t numid;

    char classname[KND_NAME_SIZE];
    size_t classname_size;

    struct kndAttrItem *attrs;
    struct kndAttrItem *tail;
    size_t num_attrs;

    struct kndConcept *parent;
    struct kndConcept *conc;

    struct kndConcItem *next;
};

struct kndConcFolder
{
    char name[KND_NAME_SIZE];
    size_t name_size;

    size_t num_concepts;
    struct kndConcFolder *next;
};

struct kndAttrEntry
{
    char name[KND_NAME_SIZE];
    size_t name_size;
    struct kndAttr *attr;
    struct kndConcDir *dir;

    /* TODO: facets */

    struct kndAttrEntry *next;
};

struct kndObjEntry
{
    char *name;
    size_t name_size;
    char *block;
    size_t block_size;
    size_t offset;
    struct kndObject *obj;
};

struct kndObjDir
{
    struct kndObjEntry **objs;
    size_t num_objs;

    struct kndObjDir **dirs;
    size_t num_dirs;
};

struct kndConcDir
{
    char id[KND_ID_SIZE];
    size_t numid;

    char name[KND_NAME_SIZE];
    size_t name_size;
    struct kndConcept *conc;

    knd_state_phase phase;

    size_t global_offset;
    size_t curr_offset;
    size_t block_size;

    size_t body_size;
    size_t obj_block_size;
    size_t dir_size;

    struct kndConcDir **children;
    size_t num_children;
    size_t num_terminals;

    struct kndRelDir **rels;
    size_t num_rels;

    char next_obj_id[KND_ID_SIZE];
    size_t next_obj_numid;

    struct kndObjDir **obj_dirs;
    size_t num_obj_dirs;
    struct kndObjEntry **objs;
    size_t num_objs;
    size_t total_objs;

    struct ooDict *obj_idx;

    struct kndTask *task;
    struct kndOutput *out;

    bool is_terminal;
    struct kndConcDir *next;
};

struct kndClassUpdateRef
{
    knd_state_phase phase;
    struct kndUpdate *update;
    struct kndClassUpdateRef *next;
};

struct kndConcept 
{
    knd_conc_type type;
    char name[KND_NAME_SIZE];
    size_t name_size;

    char id[KND_ID_SIZE];
    char next_id[KND_ID_SIZE];
    char next_obj_id[KND_ID_SIZE];
    size_t next_obj_numid;

    size_t numid;
    size_t next_numid;

    struct kndClassUpdateRef *updates;
    size_t num_updates;

    struct kndTranslation *tr;
    struct kndText *summary;

    /* initial scheme location */
    const char *dbpath;
    size_t dbpath_size;

    struct kndTask *task;

    knd_format format;
    size_t depth;

    struct kndProc *proc;
    struct kndRel *rel;

    struct kndAttr *attrs;
    struct kndAttr *tail_attr;
    size_t num_attrs;
    /* for traversal */
    struct kndAttr *curr_attr;
    size_t attrs_left;

    struct kndConcItem *base_items;
    size_t num_base_items;

    struct kndConcDir *bases[KND_MAX_BASES];
    size_t num_bases;

    struct kndDataIdx *indices;
    size_t num_indices;

    bool ignore_children;
    bool is_resolved;

    struct kndConcept *root_class;
    struct kndConcept *curr_class;
    struct kndConcept *curr_baseclass;
    struct kndObject *curr_obj;

    struct kndConcDir *dir;
    struct kndConcFolder *folders;
    size_t num_folders;

    /* allocator */
    struct kndMemPool *mempool;

    /* incoming */
    struct kndConcept *inbox;
    size_t inbox_size;

    struct kndObject *obj_inbox;
    size_t obj_inbox_size;
    size_t num_objs;

    bool batch_mode;

    /* indices */
    struct ooDict *class_idx;
    struct ooDict *attr_idx;

    /* state idx */
    struct kndObjStateIdx **obj_states;

    struct kndConcRef children[KND_MAX_CONC_CHILDREN];
    size_t num_children;
    size_t num_terminals;
    bool is_terminal;

    struct kndConcRef frozen_dir[KND_MAX_CONC_CHILDREN];
    size_t frozen_dir_size;

    char dir_buf[KND_MAX_CONC_CHILDREN * KND_DIR_ENTRY_SIZE];
    size_t dir_buf_size;
    const char *frozen_output_file_name;
    size_t frozen_output_file_name_size;
    size_t frozen_size;

    const char *frozen_name_idx_path;
    size_t frozen_name_idx_path_size;

    struct kndUpdate *curr_update;
    struct kndConcept *next;

    struct kndUser *user;
    struct kndOutput *out;
    struct kndOutput *dir_out;
    struct kndOutput *log;
    
    /***********  public methods ***********/
    void (*init)(struct kndConcept  *self);
    void (*del)(struct kndConcept   *self);
    void (*reset)(struct kndConcept   *self);
    void (*str)(struct kndConcept *self);
    int (*open)(struct kndConcept   *self);

    int (*load)(struct kndConcept   *self,
                const char *filename,
                size_t filename_size);
    int (*read)(struct kndConcept   *self,
                const char *rec,
                size_t *total_size);
    int (*read_obj_entry)(struct kndConcept   *self,
                          struct kndObjEntry *entry,
                          struct kndObject **result);
    int (*restore)(struct kndConcept   *self);

    int (*select_delta)(struct kndConcept *self,
                        const char *rec,
                        size_t *total_size);
    int (*freeze)(struct kndConcept *self);
    int (*sync)(void *obj,
                const char *rec,
                size_t *total_size);

    int (*build_diff)(struct kndConcept   *self,
                      const char *start_state,
                      size_t global_state_count);

    int (*coordinate)(struct kndConcept *self);
    int (*resolve)(struct kndConcept    *self);
    int (*update_state)(struct kndConcept *self);
    int (*apply_liquid_updates)(struct kndConcept *self,
                                const char *rec,
                                size_t *total_size);
    int (*select)(void  *self,
                  const char *rec,
                  size_t *total_size);

    int (*get)(struct kndConcept  *self,
               const char *name, size_t name_size,
               struct kndConcept  **result);

    int (*get_obj)(struct kndConcept *self,
                   const char *name, size_t name_size,
                   struct kndObject **result);

    int (*get_attr)(struct kndConcept *self,
                    const char *name, size_t name_size,
                    struct kndAttr **result);
    int (*import)(void *self,
                  const char *rec,
                  size_t *total_size);
    int (*export)(struct kndConcept *self);

    /* traversal */
    void (*rewind)(struct kndConcept   *self);
    int (*next_attr)(struct kndConcept   *self,
                     struct kndAttr **result);
};

/* constructor */
extern void kndConcept_init(struct kndConcept *self);
extern int kndConcept_new(struct kndConcept **self);

