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
#include "knd_state.h"

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
struct kndClassInstEntry;
struct kndAttrRef;

struct kndClassUpdate
{
    struct kndUpdate     *update;
    struct kndClass      *class;
    struct kndClassEntry *entry;
    struct kndClassInst **insts;
    size_t num_insts;
    struct kndClassUpdate *next;
};

struct kndClassRef
{
    struct kndClass *class;
    struct kndClassEntry *entry;
    struct kndClassRef *next;
};

struct kndClassRel
{
    struct kndClassEntry *topic;
    struct kndAttr *attr;
    struct kndSet *set;
    struct kndClassRel *next;
};

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
    struct kndClass *parent;

    struct kndClassVar *next;
};

struct kndConcFolder
{
    const char *name;
    size_t name_size;
    struct kndConcFolder *parent;
    struct kndConcFolder *next;
};

struct kndClassEntry
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    const char *name;
    size_t name_size;

    struct kndClass *class;
    struct kndRepo *repo;
    struct kndClassEntry *orig;

    knd_state_phase phase;

    /* immediate children */
    struct kndClassRef *children;
    size_t num_children;
    size_t num_terminals;

    struct kndClassRef *ancestors;
    size_t num_ancestors;

    struct kndSet *descendants;

    struct kndRelEntry **rels;
    size_t num_rels;

    struct kndProcEntry **procs;
    size_t num_procs;

    //struct kndObjDir *obj_dir;

    struct kndSet *inst_idx;
    size_t num_insts;
    struct kndSet *attr_idx;

    struct kndClassRel *reverse_rels;

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

    struct kndState *desc_states;
    size_t init_desc_state;
    size_t num_desc_states;

    struct kndState *inst_states;
    size_t init_inst_state;
    size_t num_inst_states;

    struct kndTranslation *tr;
    struct kndTranslation *summary;

    struct kndClassVar *baseclass_vars;
    size_t num_baseclass_vars;

    struct kndAttr *attrs;
    struct kndAttr *tail_attr;
    size_t num_attrs;

    struct kndSet *attr_idx;
    struct kndAttr *implied_attr;

    struct kndProc *proc;

    struct kndConcFolder *folders;
    size_t num_folders;

    bool is_resolved;
    bool state_top;

    struct kndClass *next;

    /***********  public methods ***********/
    void (*str)(struct kndClass *self, size_t depth);
};

/* constructor */
extern void kndClass_init(struct kndClass *self);
extern int kndClass_new(struct kndClass **self,
                        struct kndMemPool *mempool);

/* exported functions */
extern int knd_read_GSL_file(struct kndClass *self,
                             struct kndConcFolder *parent_folder,
                             const char *filename,
                             size_t filename_size,
                             struct kndTask *task);

extern int knd_class_coordinate(struct kndClass *self, struct kndTask *task);

extern int knd_get_class(struct kndRepo *self,
                         const char *name, size_t name_size,
                         struct kndClass **result,
                         struct kndTask *task);

extern int knd_get_class_by_id(struct kndClass *self,
                               const char *id, size_t id_size,
                               struct kndClass **result,
                               struct kndTask *task);

extern gsl_err_t knd_import_class_var(struct kndClassVar *self,
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
                              struct kndAttrRef **result);

extern int knd_export_class_state_JSON(struct kndClass *self,
                                       struct kndTask *task);

extern int knd_class_set_export_JSON(struct kndSet *set,
                                     struct kndTask *task);

extern int knd_class_export_JSON(struct kndClass *self,
                                 struct kndTask *task);

extern int knd_class_export(struct kndClass *self,
                            knd_format format,
                            struct kndTask *task);

extern gsl_err_t knd_parse_gloss_array(void *obj,
                                       const char *rec,
                                       size_t *total_size);
extern gsl_err_t knd_parse_summary_array(void *obj,
                                         const char *rec,
                                         size_t *total_size);

extern int knd_class_export_GSL(struct kndClass *self,
                                struct kndTask *task,
                                size_t depth);
extern int knd_class_set_export(struct kndSet *self,
                                knd_format format,
                                struct kndTask *task);
extern int knd_class_set_export_GSL(struct kndSet *set,
                                     struct kndTask *task);

extern int knd_class_export_GSP(struct kndClass *self,
                                struct kndTask *task);

extern int knd_class_export_updates_GSP(struct kndClass *self,
                                        struct kndClassUpdate *update,
                                        struct kndTask *task);

extern gsl_err_t knd_parse_import_class_inst(void *obj,
                                             const char *rec,
                                             size_t *total_size);

extern gsl_err_t knd_class_import(struct kndRepo *repo,
                                  const char *rec,
                                  size_t *total_size,
                                  struct kndTask *task);

extern int knd_inherit_attrs(struct kndClass *self,
                             struct kndClass *base,
                             struct kndTask *task);

extern int knd_compute_class_attr_num_value(struct kndClass *self,
                                            struct kndAttrVar *attr_var);

extern int knd_update_state(struct kndClass *self,
                            knd_state_phase phase,
                            struct kndTask *task);

extern gsl_err_t knd_read_class_inst_state(struct kndClass *self,
                                           struct kndClassUpdate *update,
                                           const char *rec,
                                           size_t *total_size);

extern gsl_err_t knd_read_class_state(struct kndClass *self,
                                      struct kndClassUpdate *update,
                                      const char *rec,
                                      size_t *total_size);

extern int knd_get_class_inst(struct kndClass *self,
                              const char *name, size_t name_size,
                              struct kndTask *task,
                              struct kndClassInst **result);

extern int knd_register_class_inst(struct kndClass *self,
                                   struct kndClassInstEntry *entry,
                                   struct kndMemPool *mempool);

extern int knd_unregister_class_inst(struct kndClass *self,
                                     struct kndClassInstEntry *entry,
                                     struct kndTask *task);

extern int knd_class_clone(struct kndClass *self,
                           struct kndRepo *target_repo,
                           struct kndClass **result,
                           struct kndMemPool *mempool);

extern int knd_class_copy(struct kndClass *self,
                          struct kndClass *target,
                          struct kndMemPool *mempool);

extern int knd_register_state(struct kndClass *self);
extern int knd_register_descendant_states(struct kndClass *self);
extern int knd_register_inst_states(struct kndClass *self);

extern int knd_export_class_inst_state_JSON(struct kndClass *self,
                                            struct kndTask *task);

extern int knd_get_class_attr_value(struct kndClass *src,
                                    struct kndAttrVar *query,
                                    struct kndProcCallArg *arg);

extern int knd_resolve_classes(struct kndClass *self, struct kndTask *task);
extern int knd_class_resolve(struct kndClass *self, struct kndTask *task);

extern int knd_class_update_new(struct kndMemPool *mempool,
                                struct kndClassUpdate **result);

extern int knd_class_var_new(struct kndMemPool *mempool,
                             struct kndClassVar **result);

extern int knd_class_ref_new(struct kndMemPool *mempool,
                             struct kndClassRef **result);
extern int knd_class_rel_new(struct kndMemPool *mempool,
                             struct kndClassRel **result);

extern int knd_conc_folder_new(struct kndMemPool *mempool,
                               struct kndConcFolder **result);

extern int knd_class_entry_new(struct kndMemPool *mempool,
                               struct kndClassEntry **result);
extern int knd_inner_class_new(struct kndMemPool *mempool,
                               struct kndClass **self);
extern int knd_class_new(struct kndMemPool *mempool,
                         struct kndClass **result);

// knd_class.select.c
extern gsl_err_t knd_class_select(struct kndRepo *repo,
                                  const char *rec, size_t *total_size,
                                  struct kndTask *task);

// knd_class.states.c
extern int knd_retrieve_class_updates(struct kndStateRef *ref,
                                      struct kndSet *set);
extern int knd_class_get_updates(struct kndClass *self,
                                 size_t gt, size_t lt,
                                 size_t unused_var(eq),
                                 struct kndSet *set);
extern int knd_class_get_desc_updates(struct kndClass *self,
                                      size_t gt, size_t lt,
                                      size_t unused_var(eq),
                                      struct kndSet *set);
extern int knd_class_get_inst_updates(struct kndClass *self,
                                      size_t gt, size_t lt, size_t eq,
                                      struct kndSet *set);
