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
 *   knd_task.h
 *   Knowdy Task
 */

#pragma once

#include "knd_config.h"
#include "knd_http_codes.h"

#include <gsl-parser/gsl_err.h>

struct glbOutput;
struct kndTask;
struct kndUser;
struct kndStateControl;
struct kndClass;
struct kndQuery;
struct kndClassInst;

typedef enum knd_task_spec_type { KND_GET_STATE, 
				  KND_SELECT_STATE, 
                                  KND_CHANGE_STATE,
                                  KND_UPDATE_STATE,
                                  KND_LIQUID_STATE,
                                  KND_SYNC_STATE,
                                  KND_DELTA_STATE
} knd_task_spec_type;

typedef enum knd_iter_type {
    KND_ITER_NONE,
    KND_ITER_BREADTH,
    KND_ITER_DEPTH
} knd_iter_type;

typedef enum knd_delivery_type {
    KND_DELIVERY_CACHE,
    KND_DELIVERY_HTTP
} knd_delivery_type;

struct kndVisualFormat {
    size_t text_line_height;
    size_t text_hangindent_size;
};

struct kndTask
{
    knd_task_spec_type type;

    char tid[KND_NAME_SIZE];
    size_t tid_size;

    char curr_locale[KND_NAME_SIZE];
    size_t curr_locale_size;

    knd_format format;
    struct kndVisualFormat visual;

    char timestamp[KND_NAME_SIZE];
    size_t timestamp_size;

    const char *locale;
    size_t locale_size;
    
    const char *spec;
    size_t spec_size;

    const char *update_spec;
    size_t update_spec_size;

    const char *obj;
    size_t obj_size;

    const char *report;
    size_t report_size;

    int error;
    knd_http_code_t http_code;

    size_t batch_max;
    size_t batch_from;
    size_t batch_size;
    size_t match_count;
    size_t start_from;

    size_t batch_eq;
    size_t batch_gt;
    size_t batch_lt;

    size_t state_eq;
    size_t state_gt;
    size_t state_lt;

    struct kndClassInst *curr_inst;

    struct kndSet *sets[KND_MAX_CLAUSES];
    size_t num_sets;

    struct kndShard *shard;

    struct glbOutput *log;
    struct glbOutput *out;
    struct glbOutput *spec_out;
    struct glbOutput *file_out;
    struct glbOutput *update;
    struct kndMemPool *mempool;

    /******** public methods ********/
    void (*del)(struct kndTask *self);

    void (*reset)(struct kndTask *self);
    
    int (*run)(struct kndTask *self,
               const char     *rec,
               size_t   rec_size,
               const char *obj,
               size_t obj_size);

//    int (*parse)(struct kndTask *self,
//                 const char     *rec,
//                 size_t   *total_size);
    int (*build_report)(struct kndTask *self);
};

/* constructor */
extern int kndTask_new(struct kndTask **self);

