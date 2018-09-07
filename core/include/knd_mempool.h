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
 *   knd_mempool.h
 *   Knowdy Memory Pool
 */
#pragma once

#include <stddef.h>
#include <glb-lib/output.h>
#include <gsl-parser.h>

#include "knd_config.h"

struct glbOutput;
struct kndClassVar;
struct kndClassInst;
struct kndElem;

struct kndRel;
struct kndRelEntry;
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
struct kndRelInstanceUpdate;
struct kndProcUpdate;
struct kndProcUpdateRef;
struct kndUserContext;
struct kndSet;
struct kndSetElemIdx;

typedef enum knd_mempage_t { KND_MEMPAGE_NORMAL,
                             KND_MEMPAGE_SMALL,
                             KND_MEMPAGE_LARGE
} knd_mempage_t;

struct kndMemPage
{
    struct kndMemPage *prev;
    struct kndMemPage *next;
    void *data;
};

struct kndMemPool
{
    size_t max_users;

    char *pages;
    size_t page_size;
    size_t page_payload_size;
    size_t num_pages;
    size_t pages_used;
    struct kndMemPage *head_page;
    struct kndMemPage *tail_page;

    char *small_pages;
    size_t small_page_size;
    size_t small_page_payload_size;
    size_t num_small_pages;
    size_t small_pages_used;

    char *large_pages;
    size_t large_page_size;
    size_t large_page_payload_size;
    size_t num_large_pages;
    size_t large_pages_used;
    
    struct kndSet *sets;
    size_t max_sets;
    size_t num_sets;

    struct kndSetElemIdx *set_elem_idxs;
    size_t max_set_elem_idxs;
    size_t num_set_elem_idxs;

    struct kndFacet *facets;
    size_t max_facets;
    size_t num_facets;

    struct kndUserContext *user_ctxs;
    size_t max_user_ctxs;
    size_t num_user_ctxs;

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

    struct kndClassInst *objs;
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

    struct kndRel *rels;
    size_t max_rels;
    size_t num_rels;

    struct kndRelUpdate *rel_updates;
    size_t max_rel_updates;
    size_t num_rel_updates;

    struct kndRelInstanceUpdate *rel_inst_updates;
    size_t max_rel_inst_updates;
    size_t num_rel_inst_updates;

    struct kndRelEntry *rel_entries;
    size_t max_rel_entries;
    size_t num_rel_entries;

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

    struct kndProc *procs;
    size_t max_procs;
    size_t num_procs;

    struct kndProcEntry *proc_entries;
    size_t max_proc_entries;
    size_t num_proc_entries;

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
    int (*present)(struct kndMemPool *self,
                   struct glbOutput  *out);
    gsl_err_t (*parse)(struct kndMemPool *self,
		       const char *rec, size_t *total_size);

    int (*new_set)(struct kndMemPool   *self,
		     struct kndSet **result);
    int (*new_set_elem_idx)(struct kndMemPool   *self,
			    struct kndSetElemIdx **result);
    int (*new_facet)(struct kndMemPool   *self,
		     struct kndFacet **result);

    
    int (*new_user_ctx)(struct kndMemPool   *self,
                        struct kndUserContext **result);
    int (*new_update)(struct kndMemPool   *self,
                      struct kndUpdate **result);
    int (*new_state)(struct kndMemPool   *self,
                      struct kndState **result);
    int (*new_class_update)(struct kndMemPool   *self,
                            struct kndClassUpdate **result);
    int (*new_class_update_ref)(struct kndMemPool   *self,
                                struct kndClassUpdateRef **result);

    int (*new_class_inst)(struct kndMemPool   *self,
                          struct kndClassInst **result);
    int (*new_class_inst_dir)(struct kndMemPool   *self,
                       struct kndObjDir **result);
    int (*new_class_inst_entry)(struct kndMemPool   *self,
                         struct kndObjEntry **result);
    int (*new_class_inst_elem)(struct kndMemPool   *self,
                        struct kndElem     **result);
    int (*new_rel)(struct kndMemPool   *self,
                   struct kndRel **result);
    int (*new_rel_entry)(struct kndMemPool   *self,
                         struct kndRelEntry **result);
    int (*new_rel_ref)(struct kndMemPool   *self,
                       struct kndRelRef **result);
    int (*new_rel_update)(struct kndMemPool   *self,
                          struct kndRelUpdate **result);
    int (*new_rel_inst_update)(struct kndMemPool   *self,
                               struct kndRelInstanceUpdate **result);
    int (*new_rel_inst)(struct kndMemPool   *self,
                        struct kndRelInstance **result);
    int (*new_rel_arg_inst)(struct kndMemPool   *self,
                            struct kndRelArgInstance **result);
    int (*new_rel_arg_inst_ref)(struct kndMemPool   *self,
                                struct kndRelArgInstRef **result);
    int (*new_proc)(struct kndMemPool   *self,
                    struct kndProc **result);
    int (*new_proc_entry)(struct kndMemPool   *self,
                        struct kndProcEntry **result);
    int (*new_proc_arg)(struct kndMemPool   *self,
                        struct kndProcArg **result);
    int (*new_proc_update)(struct kndMemPool   *self,
                          struct kndProcUpdate **result);
    int (*new_proc_update_ref)(struct kndMemPool   *self,
                              struct kndProcUpdateRef **result);
};

extern int kndMemPool_new(struct kndMemPool **self);

extern int knd_mempool_alloc(struct kndMemPool *self,
                             knd_mempage_t page_size,
                             size_t obj_size, void **result);
extern void knd_mempool_free(struct kndMemPool *self,
                             knd_mempage_t page_size,
                             void *page_data);
