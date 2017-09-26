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
 *   knd_dataclass.h
 *   Knowdy Data Class
 */
#ifndef KND_DATACLASS_H
#define KND_DATACLASS_H

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
struct kndTask;
struct kndUser;
struct kndTaskArg;

struct kndDataIdx
{
    knd_facet_type type;
    
    char name[KND_NAME_SIZE];
    size_t name_size;

    char abbr[KND_NAME_SIZE];
    size_t abbr_size;

    size_t numval;
    bool is_default;
    
    struct kndDataIdx *next;
};


struct kndConcRef
{
    size_t state;
    struct kndConcept *conc;
};

struct kndConcItem
{
    char name[KND_NAME_SIZE];
    size_t name_size;

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


struct kndObjEntry
{
    size_t offset;
    size_t block_size;
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
    char name[KND_NAME_SIZE];
    size_t name_size;
    struct kndConcept *conc;

    size_t global_offset;
    size_t curr_offset;
    size_t block_size;

    size_t body_size;
    size_t obj_block_size;
    size_t dir_size;

    struct kndConcDir **children;
    size_t num_children;

    struct kndObjDir **obj_dirs;
    size_t num_obj_dirs;
    struct kndObjEntry **objs;
    size_t num_objs;

    bool is_terminal;
    struct kndConcDir *next;
};

struct kndConcept 
{
    char name[KND_NAME_SIZE];
    size_t name_size;

    char id[KND_ID_SIZE];
    char next_id[KND_ID_SIZE];
    char next_obj_id[KND_ID_SIZE];

    size_t numid;

    char state[KND_STATE_SIZE];
    char next_state[KND_STATE_SIZE];
    char diff_state[KND_STATE_SIZE];
    char next_obj_state[KND_STATE_SIZE];
    size_t global_state_count;

    knd_state_phase phase;

    struct kndTranslation *tr;
    struct kndText *summary;

    char namespace[KND_NAME_SIZE];
    size_t namespace_size;

    /* initial scheme location */
    const char *dbpath;
    size_t dbpath_size;

    struct kndTask *task;
    
    const char *locale;
    size_t locale_size;
    knd_format format;
    size_t depth;

    struct kndAttr *attrs;
    struct kndAttr *tail_attr;
    size_t num_attrs;
    /* for traversal */
    struct kndAttr *curr_attr;
    size_t attrs_left;
    
    struct kndConcItem *conc_items;
    size_t num_conc_items;

    struct kndDataIdx *indices;
    size_t num_indices;

    char curr_val[KND_NAME_SIZE];
    size_t curr_val_size;

    char idx_name[KND_NAME_SIZE];
    size_t idx_name_size;

    char style_name[KND_NAME_SIZE];
    size_t style_name_size;

    bool ignore_children;

    struct kndConcept *root_class;
    struct kndConcept *curr_class;
    struct kndObject *curr_obj;

    struct kndConcDir *dir;
    struct kndConcFolder *folders;
    size_t num_folders;

    /* allocator */
    struct kndObject *obj_storage;
    size_t obj_storage_size;
    size_t obj_storage_max;
    
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
    struct ooDict *obj_idx;
    
    struct kndConcRef children[KND_MAX_CONC_CHILDREN];
    size_t num_children;

    struct kndConcRef frozen_dir[KND_MAX_CONC_CHILDREN];
    size_t frozen_dir_size;
    bool is_terminal;

    char dir_buf[KND_MAX_CONC_CHILDREN * KND_DIR_ENTRY_SIZE];
    size_t dir_buf_size;
    const char *frozen_output_file_name;
    size_t frozen_output_file_name_size;
    size_t frozen_size;

    const char *frozen_name_idx_path;
    size_t frozen_name_idx_path_size;

    struct kndRefSet *obj_browser;

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
               const char *name, size_t name_size);

    int (*get_obj)(struct kndConcept *self,
                   const char *name, size_t name_size);

    int (*get_attr)(struct kndConcept *self,
                    const char *name, size_t name_size,
                    struct kndAttr **result);

    int (*import)(void *self,
                  const char *rec,
                  size_t *total_size);
    int (*export)(struct kndConcept *self);

    /*int (*is_a)(struct kndConcept *self,
                struct kndConcept *base);
    */
    
    /* traversal */
    void (*rewind)(struct kndConcept   *self);
    int (*next_attr)(struct kndConcept   *self,
                     struct kndAttr **result);
};

/* obj allocator */
extern int kndConcept_alloc_obj(struct kndConcept *self,
                                struct kndObject **result);

/* constructor */
extern int kndConcept_new(struct kndConcept **self);

#endif
