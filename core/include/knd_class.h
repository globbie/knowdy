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
 *   knd_class.h
 *   Knowdy Concept Class
 */
#pragma once

#include "knd_dict.h"
#include "knd_facet.h"
#include "knd_utils.h"
#include "knd_class_inst.h"
#include "knd_config.h"

#include <gsl-parser/gsl_err.h>

struct kndAttr;
struct kndAttrVar;
struct kndClass;
struct kndTranslation;
struct kndClass;
struct kndRel;
struct kndTask;
struct kndSet;
struct kndUser;
struct kndClassUpdate;
struct kndClassUpdateRef;
struct kndObjEntry;

struct kndClassVar
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    struct kndClassEntry *entry;

    struct kndAttrVar *attrs;
    struct kndAttrVar *tail;
    size_t num_attrs;

    struct kndClass *root_class;

    struct kndClassVar *next;
};

struct kndConcFolder
{
    char name[KND_NAME_SIZE];
    size_t name_size;
    struct kndConcFolder *parent;
    struct kndConcFolder *next;
};

struct kndClassEntry
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    char name[KND_NAME_SIZE];
    size_t name_size;
    struct kndClass *class;
    struct kndRepo *repo;

    knd_state_phase phase;
    size_t global_offset;
    size_t curr_offset;
    size_t block_size;
    size_t frozen_size;

    // TODO: is local fd absolutely necessary?
    int fd;
    size_t body_size;
    size_t obj_block_size;
    size_t dir_size;

    // TODO: what's the meaning of this?
    bool is_indexed;

    struct kndSet *descendants;
    struct kndSet *child_idx;
    struct kndClassEntry **children;
    size_t num_children;
    size_t num_terminals;

    struct kndRelEntry **rels;
    size_t num_rels;

    struct kndProcEntry **procs;
    size_t num_procs;

    struct kndObjDir *obj_dir;
    size_t num_objs;

    // TODO: all indices must be kept in kndRepo
    struct kndSet *class_idx;
    struct ooDict *class_name_idx;
    struct kndSet *obj_idx;
    struct ooDict *obj_name_idx;

    struct ooDict *reverse_attr_name_idx;

    size_t child_count;
    struct kndClassEntry *prev;
    struct kndClassEntry *next;
};

struct kndClass
{
    const char *name;
    size_t name_size;

    struct kndClassEntry *entry;
    struct kndState *states;
    size_t init_state;
    size_t num_states;

    struct kndState *inst_states;
    size_t init_inst_state;
    size_t num_inst_states;

    struct kndTranslation *tr;
    struct kndTranslation *summary;

    size_t depth;
    size_t max_depth;

    // TODO: remove
    struct kndProc *proc;
    struct kndRel *rel;

    struct kndAttr *attrs;
    struct kndAttr *tail_attr;
    size_t num_attrs;
    struct kndAttr *implied_attr;

    struct kndClassVar *baseclass_vars;
    size_t num_baseclass_vars;

    struct kndClassEntry *bases[KND_MAX_BASES];
    size_t num_bases;
    bool is_resolved;

    struct kndClass   *root_class;
    struct kndClass   *curr_class;
    struct kndClass   *curr_baseclass;
    struct kndAttr    *curr_attr;
    struct kndObject  *curr_obj;

    struct kndConcFolder *folders;
    size_t num_folders;

    /* incoming */
    struct kndClass *inbox;
    size_t inbox_size;

    struct kndObject *obj_inbox;
    size_t obj_inbox_size;
    size_t num_objs;


    bool batch_mode;

    /* indices */
    // TODO: move to repo
    struct kndSet *class_idx;
    struct ooDict *class_name_idx;

    struct ooDict *attr_name_idx;
    struct kndAttr *computed_attrs[KND_MAX_ATTRS];
    size_t num_computed_attrs;

    // TODO: need to find an alternative solution to this!
    //char dir_buf[KND_MAX_CONC_CHILDREN * KND_DIR_ENTRY_SIZE];
    //size_t dir_buf_size;

    struct kndUpdate *curr_update;
    struct kndClass *next;

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
    gsl_err_t (*read)(struct kndClass   *self,
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

    int (*coordinate)(struct kndClass *self);

    int (*resolve)(struct kndClass     *self,
		   struct kndClassUpdate *update);

    int (*update_state)(struct kndClass *self);
    gsl_err_t (*apply_liquid_updates)(struct kndClass *self,
                                      const char *rec,
                                      size_t *total_size);
    gsl_err_t (*select)(void  *self,
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
};

/* constructor */
extern void kndClass_init(struct kndClass *self);
extern int kndClass_new(struct kndClass **self,
                        struct kndMemPool *mempool);

/* exported functions */
extern gsl_err_t import_class_var(struct kndClassVar *self,
                                  const char *rec,
                                  size_t *total_size);
extern int knd_is_base(struct kndClass *self,
                       struct kndClass *base);
extern int knd_get_attr_var(struct kndClass *self,
                            const char *name,
                            size_t name_size,
                            struct kndAttrVar **result);
