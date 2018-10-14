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

struct kndRelUpdate;
struct kndRelInstanceUpdate;
struct kndProcUpdate;
struct kndProcUpdateRef;
struct kndUserContext;
struct kndRelArgInstRef;
struct kndRelRef;

typedef enum knd_mempage_t { KND_MEMPAGE_LARGE,
                             KND_MEMPAGE_BASE_X4,
                             KND_MEMPAGE_BASE_X2,
                             KND_MEMPAGE_BASE,
                             KND_MEMPAGE_SMALL_X4,
                             KND_MEMPAGE_SMALL_X2,
                             KND_MEMPAGE_SMALL,
                             KND_MEMPAGE_TINY
} knd_mempage_t;

struct kndMemPageHeader
{
    struct kndMemPageHeader *prev;
    struct kndMemPageHeader *next;
};

struct kndMemPool
{
    size_t capacity;

    char *pages;
    size_t page_size;
    size_t page_payload_size;
    size_t num_pages;
    size_t pages_used;
    struct kndMemPageHeader *head_page;
    struct kndMemPageHeader *tail_page;

    char *small_x4_pages;
    size_t small_x4_page_size;
    size_t small_x4_page_payload_size;
    size_t num_small_x4_pages;
    size_t small_x4_pages_used;
    struct kndMemPageHeader *head_small_x4_page;
    struct kndMemPageHeader *tail_small_x4_page;

    /* 256 bytes */
    char *small_x2_pages;
    size_t small_x2_page_size;
    size_t small_x2_page_payload_size;
    size_t num_small_x2_pages;
    size_t small_x2_pages_used;
    struct kndMemPageHeader *head_small_x2_page;
    struct kndMemPageHeader *tail_small_x2_page;

    /* 128 bytes */
    char *small_pages;
    size_t small_page_size;
    size_t small_page_payload_size;
    size_t num_small_pages;
    size_t small_pages_used;
    struct kndMemPageHeader *head_small_page;
    struct kndMemPageHeader *tail_small_page;

    /* 64 bytes */
    char *tiny_pages;
    size_t tiny_page_size;
    size_t tiny_page_payload_size;
    size_t num_tiny_pages;
    size_t tiny_pages_used;
    struct kndMemPageHeader *head_tiny_page;
    struct kndMemPageHeader *tail_tiny_page;

    // test
    size_t num_classes;
    size_t num_sets;
    size_t num_set_idxs;

    size_t num_class_vars;
    size_t num_attr_vars;

    struct glbOutput *log;

    void (*del)(struct kndMemPool   *self);
    int (*alloc)(struct kndMemPool   *self);
    int (*present)(struct kndMemPool *self,
                   struct glbOutput  *out);
    gsl_err_t (*parse)(struct kndMemPool *self,
		       const char *rec, size_t *total_size);

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
    int (*new_proc_arg)(struct kndMemPool   *self,
                        struct kndProcArg **result);
};

extern int kndMemPool_new(struct kndMemPool **self);

extern int knd_mempool_alloc(struct kndMemPool *self,
                             knd_mempage_t page_size,
                             size_t obj_size, void **result);
extern void knd_mempool_free(struct kndMemPool *self,
                             knd_mempage_t page_size,
                             void *page_data);
