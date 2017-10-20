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

struct kndMemPool
{
    struct kndConcept *classes;
    size_t max_classes;
    size_t num_classes;

    struct kndObject *objs;
    size_t max_objs;
    size_t num_objs;

    struct kndElem *elems;
    size_t max_elems;
    size_t num_elems;

    struct kndRel *rels;
    size_t max_rels;
    size_t num_rels;

    struct kndRelInstance *rel_instances;
    size_t max_rel_instances;
    size_t num_rel_instances;

    struct kndProc *procs;
    size_t max_procs;
    size_t num_procs;

    struct kndProcInstance *proc_instances;
    size_t max_proc_instances;
    size_t num_proc_instances;

    struct kndOutput *log;

    int (*alloc)(struct kndMemPool   *self);

    int (*new_class)(struct kndMemPool   *self,
                     struct kndConcept **result);
    int (*new_obj)(struct kndMemPool   *self,
                   struct kndObject **result);
    int (*new_rel)(struct kndMemPool   *self,
                   struct kndRel **result);
    int (*new_rel_instance)(struct kndMemPool   *self,
                            struct kndRelInstance **result);
    int (*new_proc)(struct kndMemPool   *self,
                    struct kndProc **result);
    int (*new_proc_instance)(struct kndMemPool   *self,
                             struct kndProcInstance **result);
};

/* constructor */
extern int kndMemPool_new(struct kndMemPool **self);
