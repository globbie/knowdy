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
 *   knd_mempool.h
 *   Knowdy Memory Pool
 */
#pragma once

#include "knd_config.h"

struct kndConcept;
struct kndObject;
struct kndElem;

struct kndRel;
struct kndRelInstance;
struct kndRelArg;
struct kndRelArgInstance;

struct kndProc;
struct kndProcDir;
struct kndProcArg;
struct kndProcArgInstance;
struct kndProcInstance;

struct kndUpdate;
struct kndState;
struct kndClassUpdate;
struct kndClassUpdateRef;
struct kndRelUpdate;
struct kndRelUpdateRef;
struct kndProcUpdate;
struct kndProcUpdateRef;

struct kndMemPool
{
    char next_class_id[KND_ID_SIZE];
    size_t max_users;
    
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

    struct kndConcept *classes;
    size_t max_classes;
    size_t num_classes;

    struct kndConcDir *conc_dirs;
    size_t max_conc_dirs;
    size_t num_conc_dirs;

    struct kndConcItem *conc_items;
    size_t max_conc_items;
    size_t num_conc_items;

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

    struct kndProcDir *proc_dirs;
    size_t max_proc_dirs;
    size_t num_proc_dirs;

    struct kndProcInstance *proc_insts;
    size_t max_proc_insts;
    size_t num_proc_insts;

    struct kndProcArg *proc_args;
    size_t max_proc_args;
    size_t num_proc_args;

    struct kndProcArgInstance *proc_arg_insts;
    size_t max_proc_arg_insts;
    size_t num_proc_arg_insts;

    struct kndProcUpdate *proc_updates;
    size_t max_proc_updates;
    size_t num_proc_updates;

    struct kndProcUpdateRef *proc_update_refs;
    size_t max_proc_update_refs;
    size_t num_proc_update_refs;

    struct kndOutput *log;

    void (*del)(struct kndMemPool   *self);
    int (*alloc)(struct kndMemPool   *self);

    int (*new_update)(struct kndMemPool   *self,
                      struct kndUpdate **result);
    int (*new_state)(struct kndMemPool   *self,
                      struct kndState **result);
    int (*new_class_update)(struct kndMemPool   *self,
                            struct kndClassUpdate **result);
    int (*new_class_update_ref)(struct kndMemPool   *self,
                                struct kndClassUpdateRef **result);
    int (*new_class)(struct kndMemPool   *self,
                     struct kndConcept **result);
    int (*new_conc_dir)(struct kndMemPool   *self,
                        struct kndConcDir **result);
    int (*new_conc_item)(struct kndMemPool   *self,
			 struct kndConcItem **result);
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
    int (*new_proc_inst)(struct kndMemPool   *self,
                         struct kndProcInstance **result);
    int (*new_proc_dir)(struct kndMemPool   *self,
			struct kndProcDir **result);
    int (*new_proc_arg)(struct kndMemPool   *self,
			struct kndProcArg **result);
    int (*new_proc_arg_inst)(struct kndMemPool   *self,
			     struct kndProcArgInstance **result);
    int (*new_proc_update)(struct kndMemPool   *self,
                          struct kndProcUpdate **result);
    int (*new_proc_update_ref)(struct kndMemPool   *self,
                              struct kndProcUpdateRef **result);
};

/* constructor */
extern int kndMemPool_new(struct kndMemPool **self);
