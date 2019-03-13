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

#include "knd_config.h"

#include <gsl-parser.h>
#include <stddef.h>

struct kndOutput;

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
    struct kndMemPageHeader *next;
};

struct kndMemPool
{
    size_t capacity;

    /* 1024 bytes */
    char *pages;
    size_t page_size;
    size_t num_pages;
    size_t pages_used;
    struct kndMemPageHeader *page_list;

    /* 512 bytes */
    char *small_x4_pages;
    size_t small_x4_page_size;
    size_t num_small_x4_pages;
    size_t small_x4_pages_used;
    struct kndMemPageHeader *small_x4_page_list;

    /* 256 bytes */
    char *small_x2_pages;
    size_t small_x2_page_size;
    size_t num_small_x2_pages;
    size_t small_x2_pages_used;
    struct kndMemPageHeader *small_x2_page_list;

    /* 128 bytes */
    char *small_pages;
    size_t small_page_size;
    size_t num_small_pages;
    size_t small_pages_used;
    struct kndMemPageHeader *small_page_list;

    /* 64 bytes */
    char *tiny_pages;
    size_t tiny_page_size;
    size_t num_tiny_pages;
    size_t tiny_pages_used;
    struct kndMemPageHeader *tiny_page_list;

    size_t max_set_size;

    void (*del)(struct kndMemPool   *self);
    int (*alloc)(struct kndMemPool   *self);
    int (*present)(struct kndMemPool *self,
                   struct kndOutput  *out);
    gsl_err_t (*parse)(struct kndMemPool *self,
		       const char *rec, size_t *total_size);
};

extern int kndMemPool_new(struct kndMemPool **self);

extern int knd_mempool_alloc(struct kndMemPool *self,
                             knd_mempage_t page_type,
                             size_t obj_size, void **result);
extern void knd_mempool_free(struct kndMemPool *self,
                             knd_mempage_t page_type,
                             void *page_data);
