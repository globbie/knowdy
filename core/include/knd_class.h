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
struct kndProcCallArg;
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
struct glbOutput;

struct kndClassVar
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    struct kndClassEntry *entry;

    struct kndAttrVar *attrs;
    struct kndAttrVar *tail;
    size_t num_attrs;

    struct kndState *states;
    size_t init_state;
    size_t num_states;

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

    size_t num_insts;

    // TODO: move to kndRepo?
    struct kndSet *class_idx;
    struct ooDict *class_name_idx;
    struct kndSet *inst_idx;
    struct ooDict *inst_name_idx;
    struct kndSet *attr_idx;

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

    struct kndState *child_states;
    size_t init_child_state;
    size_t num_child_states;

    struct kndText *gloss;

    struct kndTranslation *tr;
    struct kndTranslation *summary;

    size_t depth;
    size_t max_depth;

    struct kndClassVar *baseclass_vars;
    size_t num_baseclass_vars;

    struct kndClassEntry *bases[KND_MAX_BASES];
    size_t num_bases;
    bool is_resolved;

    struct kndAttr *attrs;
    struct kndAttr *tail_attr;
    size_t num_attrs;
    struct kndAttr *implied_attr;

    struct kndProc *proc;
    struct kndRel *rel;

    struct kndClass      *root_class;
    struct kndClass      *curr_class;
    struct kndClass      *curr_baseclass;
    struct kndAttr       *curr_attr;
    struct kndAttrVar    *curr_attr_var;
    struct kndClassInst  *curr_inst;
    struct kndUpdate     *curr_update;

    struct kndConcFolder *folders;
    size_t num_folders;

    /* incoming */
    struct kndClass *inbox;
    size_t inbox_size;

    struct kndClassInst *obj_inbox;
    size_t obj_inbox_size;
    size_t num_objs;
    bool batch_mode;

    /* submodules */
    struct kndClassFormatter *formatter;

    /* indices */
    // TODO: move to repo
    struct kndSet *class_idx;
    struct ooDict *class_name_idx;

    struct ooDict *attr_name_idx;
    struct kndAttr *computed_attrs[KND_MAX_ATTRS];
    size_t num_computed_attrs;

    struct kndClass *next;

    /***********  public methods ***********/
    void (*init)(struct kndClass  *self);
    void (*del)(struct kndClass   *self);
    void (*reset_inbox)(struct kndClass   *self);
    void (*str)(struct kndClass *self);

    int (*open)(struct kndClass   *self,
                const char *filename);
    int (*load)(struct kndClass   *self,
		struct kndConcFolder *parent_folder,
                const char *filename,
                size_t filename_size);
    gsl_err_t (*read)(struct kndClass   *self,
                      const char *rec,
                      size_t *total_size);
    int (*read_obj_entry)(struct kndClass   *self,
                          struct kndObjEntry *entry,
                          struct kndClassInst **result);
    int (*restore)(struct kndClass   *self);

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

    int (*get_inst)(struct kndClass *self,
                    const char *name, size_t name_size,
                    struct kndClassInst **result);

    int (*get_attr)(struct kndClass *self,
                    const char *name, size_t name_size,
                    struct kndAttr **result);
    gsl_err_t (*import)(void *self,
                        const char *rec,
                        size_t *total_size);
    int (*export)(struct kndClass *self,
                  knd_format format,
                  struct glbOutput *out);
    int (*export_updates)(struct kndClass *self,
                          struct kndClassUpdate *update,
                          knd_format format,
                          struct glbOutput *out);
};

/* constructor */
extern void kndClass_init(struct kndClass *self);
extern int kndClass_new(struct kndClass **self,
                        struct kndMemPool *mempool);

/* exported functions */
extern int knd_get_class(struct kndClass *self,
                         const char *name, size_t name_size,
                         struct kndClass **result);
extern int knd_get_class_by_id(struct kndClass *self,
                               const char *id, size_t id_size,
                               struct kndClass **result);

extern gsl_err_t import_class_var(struct kndClassVar *self,
                                  const char *rec,
                                  size_t *total_size);
extern int knd_is_base(struct kndClass *self,
                       struct kndClass *base);

extern int knd_get_attr_var(struct kndClass *self,
                            const char *name,
                            size_t name_size,
                            struct kndAttrVar **result);

extern int knd_class_get_attr(struct kndClass *self,
                              const char *name, size_t name_size,
                              struct kndAttr **result);

extern int knd_class_export_set_JSON(struct kndClass *self,
                                     struct glbOutput *out,
                                     struct kndSet *set);

extern int knd_class_export_JSON(struct kndClass *self,
                                 struct glbOutput *out);
extern gsl_err_t knd_parse_gloss_array(void *obj,
                                       const char *rec,
                                       size_t *total_size);
extern gsl_err_t knd_parse_summary_array(void *obj,
                                         const char *rec,
                                         size_t *total_size);

extern int knd_class_export_GSP(struct kndClass *self,
                                struct glbOutput *out);
extern int knd_class_export_updates_GSP(struct kndClass *self,
                                        struct kndClassUpdate *update,
                                        struct glbOutput *out);

extern gsl_err_t knd_parse_import_class_inst(void *data,
                                             const char *rec,
                                             size_t *total_size);

extern gsl_err_t knd_import_class(void *obj,
                                  const char *rec,
                                  size_t *total_size);

extern int knd_resolve_classes(struct kndClass *self);
extern int knd_resolve_class(struct kndClass *self,
                             struct kndClassUpdate *class_update);
extern int knd_inherit_attrs(struct kndClass *self, struct kndClass *base);

extern int get_arg_value(struct kndAttrVar *src,
                         struct kndAttrVar *query,
                         struct kndProcCallArg *arg);

extern gsl_err_t knd_select_class(void *obj,
                                  const char *rec,
                                  size_t *total_size);

extern int knd_compute_class_attr_num_value(struct kndClass *self,
                                            struct kndClassVar *src_class_var,
                                            struct kndAttrVar *attr_var);

extern gsl_err_t knd_read_class_inst_state(struct kndClass *self,
                                           struct kndClassUpdate *update,
                                           const char *rec,
                                           size_t *total_size);
extern gsl_err_t knd_read_class_state(struct kndClass *self,
                                          struct kndClassUpdate *update,
                                          const char *rec,
                                          size_t *total_size);

