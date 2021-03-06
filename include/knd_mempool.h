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

#include <stddef.h>
#include <stdatomic.h>

#include <gsl-parser.h>

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

typedef enum knd_mempool_t {
                             KND_ALLOC_INCR,
                             KND_ALLOC_LIST,
                             KND_ALLOC_SHARED                            
} knd_mempool_t;

struct kndMemPageHeader
{
    struct kndMemPageHeader *next;
};

struct kndMemPool
{
    knd_mempool_t type;
    int id;

    size_t capacity;

    /* 1024 bytes */
    char *pages;
    size_t page_size;
    size_t num_pages;
    size_t pages_used;
    struct kndMemPageHeader *page_list;
    struct kndMemPageHeader * _Atomic shared_page_list;
    atomic_size_t shared_pages_used;

    /* 512 bytes */
    char *small_x4_pages;
    size_t small_x4_page_size;
    size_t num_small_x4_pages;
    size_t small_x4_pages_used;
    struct kndMemPageHeader *small_x4_page_list;
    struct kndMemPageHeader * _Atomic shared_small_x4_page_list;
    atomic_size_t shared_small_x4_pages_used;

    /* 256 bytes */
    char *small_x2_pages;
    size_t small_x2_page_size;
    size_t num_small_x2_pages;
    size_t small_x2_pages_used;
    struct kndMemPageHeader *small_x2_page_list;
    struct kndMemPageHeader * _Atomic shared_small_x2_page_list;
    atomic_size_t shared_small_x2_pages_used;

    /* 128 bytes */
    char *small_pages;
    size_t small_page_size;
    size_t num_small_pages;
    size_t small_pages_used;
    struct kndMemPageHeader *small_page_list;
    struct kndMemPageHeader * _Atomic shared_small_page_list;
    atomic_size_t shared_small_pages_used;

    /* 64 bytes */
    char *tiny_pages;
    size_t tiny_page_size;
    size_t num_tiny_pages;
    size_t tiny_pages_used;
    struct kndMemPageHeader *tiny_page_list;
    struct kndMemPageHeader * _Atomic shared_tiny_page_list;
    atomic_size_t shared_tiny_pages_used;

    size_t max_set_size;

    int (*alloc)(struct kndMemPool   *self);
    int (*reset)(struct kndMemPool   *self);
    int (*present)(struct kndMemPool *self, struct kndOutput  *out);
    gsl_err_t (*parse)(struct kndMemPool *self, const char *rec, size_t *total_size);
};

int  knd_mempool_new(struct kndMemPool **self, knd_mempool_t type, int mempool_id);
void knd_mempool_del(struct kndMemPool *self);

int knd_mempool_page(struct kndMemPool *self, knd_mempage_t page_type, void **result);
// int knd_mempool_free_page(struct kndMemPool *self, knd_mempage_t page_type, void **result);

void knd_mempool_free(struct kndMemPool *self, knd_mempage_t page_type, void *page_data);
void knd_mempool_reset(struct kndMemPool *self);

