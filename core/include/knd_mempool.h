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
struct kndRel;
struct kndRelInstance;
struct kndProc;
struct kndProcInstance;
struct kndUpdate;
struct kndClassUpdate;

struct kndMemPool
{
    char next_class_id[KND_ID_SIZE];
    size_t max_users;
    
    struct kndUpdate *updates;
    struct kndUpdate **update_idx;
    size_t max_updates;
    size_t num_updates;
    struct kndUpdate **update_selected_idx;

    struct kndClassUpdate *class_updates;
    size_t max_class_updates;
    size_t num_class_updates;

    struct kndClassUpdateRef *class_update_refs;
    size_t max_class_update_refs;
    size_t num_class_update_refs;

    struct kndConcept *classes;
    size_t max_classes;
    size_t num_classes;

    struct kndObject *objs;
    size_t max_objs;
    size_t num_objs;

    struct kndElem *elems;
    size_t max_elems;
    size_t num_elems;

    char next_rel_id[KND_ID_SIZE];
    struct kndRel *rels;
    size_t max_rels;
    size_t num_rels;

    struct kndRelInstance *rel_insts;
    size_t max_rel_insts;
    size_t num_rel_insts;

    char next_proc_id[KND_ID_SIZE];
    struct kndProc *procs;
    size_t max_procs;
    size_t num_procs;

    struct kndProcInstance *proc_insts;
    size_t max_proc_insts;
    size_t num_proc_insts;

    struct kndOutput *log;

    int (*alloc)(struct kndMemPool   *self);

    int (*new_update)(struct kndMemPool   *self,
                      struct kndUpdate **result);
    int (*new_class_update)(struct kndMemPool   *self,
                            struct kndClassUpdate **result);
    int (*new_class_update_ref)(struct kndMemPool   *self,
                                struct kndClassUpdateRef **result);
    int (*new_class)(struct kndMemPool   *self,
                     struct kndConcept **result);
    int (*new_obj)(struct kndMemPool   *self,
                   struct kndObject **result);
    int (*new_rel)(struct kndMemPool   *self,
                   struct kndRel **result);
    int (*new_rel_inst)(struct kndMemPool   *self,
                        struct kndRelInstance **result);
    int (*new_proc)(struct kndMemPool   *self,
                    struct kndProc **result);
    int (*new_proc_inst)(struct kndMemPool   *self,
                         struct kndProcInstance **result);
};

/* constructor */
extern int kndMemPool_new(struct kndMemPool **self);
