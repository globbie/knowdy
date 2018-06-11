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
 *   knd_mempool.h
 *   Knowdy Memory Pool
 */
#pragma once

#include <stddef.h>
#include <glb-lib/output.h>
#include <gsl-parser.h>

#include "knd_config.h"

struct kndClass;
struct kndClassVar;
struct kndClassEntry;
struct kndObject;
struct kndElem;

struct kndRel;
struct kndRelInstance;
struct kndRelArg;
struct kndRelArgInstance;

struct kndProc;
struct kndProcEntry;
struct kndProcArg;
struct kndProcArgInstance;
struct kndProcInstance;

struct kndAttr;
struct kndUpdate;
struct kndState;
struct kndClassUpdate;
struct kndClassUpdateRef;
struct kndRelUpdate;
struct kndRelUpdateRef;
struct kndProcUpdate;
struct kndProcUpdateRef;

struct kndSet;
struct kndSetElemIdx;

struct kndMemPool
{
    char next_class_id[KND_ID_SIZE];
    size_t max_users;

    struct kndSet *sets;
    size_t max_sets;
    size_t num_sets;
    //struct kndSetElem *set_elems;
    //size_t max_set_elems;
    //size_t num_set_elems;
    struct kndSetElemIdx *set_elem_idxs;
    size_t max_set_elem_idxs;
    size_t num_set_elem_idxs;

    struct kndFacet *facets;
    size_t max_facets;
    size_t num_facets;

    struct kndUpdate *updates;
    struct kndUpdate **update_idx;
    size_t max_updates;
    size_t num_updates;
    struct kndUpdate **update_selected_idx;

    struct kndState *states;
    size_t max_states;
    size_t num_states;

    struct kndClassUpdate *class_updates;
    size_t max_class_updates;
    size_t num_class_updates;

    struct kndClassUpdateRef *class_update_refs;
    size_t max_class_update_refs;
    size_t num_class_update_refs;

    struct kndClass *classes;
    size_t max_classes;
    size_t num_classes;

    struct kndClassEntry *conc_dirs;
    size_t max_conc_dirs;
    size_t num_conc_dirs;

    struct kndClassVar *conc_items;
    size_t max_conc_items;
    size_t num_conc_items;
    
    struct kndAttrItem *attr_items;
    size_t max_attr_items;
    size_t num_attr_items;

    struct kndAttr *attrs;
    size_t max_attrs;
    size_t num_attrs;

    struct kndObject *objs;
    size_t max_objs;
    size_t num_objs;

    struct kndObjDir *obj_dirs;
    size_t max_obj_dirs;
    size_t num_obj_dirs;

    struct kndObjEntry *obj_entries;
    size_t max_obj_entries;
    size_t num_obj_entries;

    struct kndElem *elems;
    size_t max_elems;
    size_t num_elems;

    char next_rel_id[KND_ID_SIZE];
    struct kndRel *rels;
    size_t max_rels;
    size_t num_rels;

    struct kndRelUpdate *rel_updates;
    size_t max_rel_updates;
    size_t num_rel_updates;

    struct kndRelUpdateRef *rel_update_refs;
    size_t max_rel_update_refs;
    size_t num_rel_update_refs;

    struct kndRelDir *rel_dirs;
    size_t max_rel_dirs;
    size_t num_rel_dirs;

    struct kndRelRef *rel_refs;
    size_t max_rel_refs;
    size_t num_rel_refs;

    struct kndRelInstance *rel_insts;
    size_t max_rel_insts;
    size_t num_rel_insts;

    struct kndRelArg *rel_args;
    size_t max_rel_args;
    size_t num_rel_args;

    struct kndRelArgInstance *rel_arg_insts;
    size_t max_rel_arg_insts;
    size_t num_rel_arg_insts;

    struct kndRelArgInstRef *rel_arg_inst_refs;
    size_t max_rel_arg_inst_refs;
    size_t num_rel_arg_inst_refs;

    char next_proc_id[KND_ID_SIZE];
    struct kndProc *procs;
    size_t max_procs;
    size_t num_procs;

    struct kndProcEntry *proc_dirs;
    size_t max_proc_dirs;
    size_t num_proc_dirs;

    struct kndProcInstance *proc_insts;
    size_t max_proc_insts;
    size_t num_proc_insts;

    struct kndProcArg *proc_args;
    size_t max_proc_args;
    size_t num_proc_args;

    //struct kndProcArgInstance *proc_arg_insts;
    //size_t max_proc_arg_insts;
    //size_t num_proc_arg_insts;

    struct kndProcUpdate *proc_updates;
    size_t max_proc_updates;
    size_t num_proc_updates;

    struct kndProcUpdateRef *proc_update_refs;
    size_t max_proc_update_refs;
    size_t num_proc_update_refs;

    struct glbOutput *log;

    void (*del)(struct kndMemPool   *self);
    int (*alloc)(struct kndMemPool   *self);
    gsl_err_t (*parse)(struct kndMemPool *self,
		       const char *rec, size_t *total_size);

    int (*new_set)(struct kndMemPool   *self,
		     struct kndSet **result);
    int (*new_set_elem_idx)(struct kndMemPool   *self,
			    struct kndSetElemIdx **result);
    int (*new_facet)(struct kndMemPool   *self,
		     struct kndFacet **result);

    int (*new_attr)(struct kndMemPool   *self,
		     struct kndAttr **result);
    
    int (*new_update)(struct kndMemPool   *self,
                      struct kndUpdate **result);
    int (*new_state)(struct kndMemPool   *self,
                      struct kndState **result);
    int (*new_class_update)(struct kndMemPool   *self,
                            struct kndClassUpdate **result);
    int (*new_class_update_ref)(struct kndMemPool   *self,
                                struct kndClassUpdateRef **result);
    int (*new_class)(struct kndMemPool   *self,
                     struct kndClass **result);
    int (*new_conc_dir)(struct kndMemPool   *self,
                        struct kndClassEntry **result);
    int (*new_conc_item)(struct kndMemPool   *self,
                         struct kndClassVar **result);
    int (*new_attr_item)(struct kndMemPool   *self,
                         struct kndAttrItem **result);
    int (*new_obj)(struct kndMemPool   *self,
                   struct kndObject **result);
    int (*new_obj_dir)(struct kndMemPool   *self,
                       struct kndObjDir **result);
    int (*new_obj_entry)(struct kndMemPool   *self,
                         struct kndObjEntry **result);
    int (*new_obj_elem)(struct kndMemPool   *self,
                        struct kndElem     **result);
    int (*new_rel)(struct kndMemPool   *self,
                   struct kndRel **result);
    int (*new_rel_dir)(struct kndMemPool   *self,
                       struct kndRelDir **result);
    int (*new_rel_ref)(struct kndMemPool   *self,
                       struct kndRelRef **result);
    int (*new_rel_update)(struct kndMemPool   *self,
                          struct kndRelUpdate **result);
    int (*new_rel_update_ref)(struct kndMemPool   *self,
                              struct kndRelUpdateRef **result);
    int (*new_rel_inst)(struct kndMemPool   *self,
                        struct kndRelInstance **result);
    int (*new_rel_arg_inst)(struct kndMemPool   *self,
                            struct kndRelArgInstance **result);
    int (*new_rel_arg_inst_ref)(struct kndMemPool   *self,
                                struct kndRelArgInstRef **result);
    int (*new_proc)(struct kndMemPool   *self,
                    struct kndProc **result);
    //int (*new_proc_inst)(struct kndMemPool   *self,
    //                     struct kndProcInstance **result);
    int (*new_proc_dir)(struct kndMemPool   *self,
                        struct kndProcEntry **result);
    int (*new_proc_arg)(struct kndMemPool   *self,
                        struct kndProcArg **result);
    //int (*new_proc_arg_inst)(struct kndMemPool   *self,
    //                         struct kndProcArgInstance **result);
    int (*new_proc_update)(struct kndMemPool   *self,
                          struct kndProcUpdate **result);
    int (*new_proc_update_ref)(struct kndMemPool   *self,
                              struct kndProcUpdateRef **result);
};

/* constructor */
extern int kndMemPool_new(struct kndMemPool **self);

