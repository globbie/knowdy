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
 *   knd_task.h
 *   Knowdy Task
 */

#pragma once

#include <stdatomic.h>
#include <pthread.h>
#include <time.h>

#include "knd_config.h"
#include "knd_state.h"
#include "knd_dict.h"
#include "knd_http_codes.h"

#include <gsl-parser/gsl_err.h>

struct kndTask;
struct kndShard;
struct kndUser;
struct kndUserContext;
struct kndStateControl;
struct kndClass;
struct kndClassFacet;
struct kndClassVar;
struct kndQuery;
struct kndClassInst;
struct kndConcFolder;
struct kndTranslation;

typedef int (*task_cb_func)(void *obj,
                            const char *msg,
                            size_t msg_size,
                            void *ctx);

typedef enum knd_task_spec_type {
    KND_GET_STATE,
    KND_SELECT_STATE,
    KND_UPDATE_STATE,
    KND_LIQUID_STATE,
    KND_SYNC_STATE,
    KND_DELTA_STATE,
    KND_LOAD_STATE,
    KND_STOP_STATE
} knd_task_spec_type;

typedef enum knd_task_phase_t { KND_REGISTER,
                                KND_SUBMIT,
                                KND_CANCEL,
                                KND_REJECT,
                                KND_IMPORT,
                                KND_CONFLICT,
                                KND_WAL_WRITE,
                                KND_WAL_COMMIT,
                                KND_DELIVER,
                                KND_COMPLETE } knd_task_phase_t;

struct kndTaskDestination
{
    char URI[KND_NAME_SIZE];
    size_t URI_size;

    //  auth 

};

struct kndTaskContext {
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    knd_task_spec_type type;
    knd_task_phase_t phase;
    struct timespec start_ts;
    struct timespec end_ts;

    void *obj;
    task_cb_func cb;
    void *external_obj;
    task_cb_func external_cb;

    char       *input_buf;
    const char *input;
    size_t      input_size;

    struct kndOutput  *out;
    struct kndOutput  *log;
    int error;
    knd_http_code_t http_code;

    char locale[KND_ID_SIZE];
    size_t locale_size;

    knd_format format;
    size_t format_offset;

    struct kndTranslation *tr;

    size_t batch_max;
    size_t batch_from;
    size_t batch_size;
    size_t start_from;

    // TODO: subscription channel
    // to push any updates

    struct kndTaskDestination *dest;
    struct kndRepo *repo;

    /* updates */
    struct kndStateRef  *class_state_refs;
    struct kndStateRef  *inner_class_state_refs;
    struct kndStateRef  *class_inst_state_refs;
    struct kndStateRef  *proc_state_refs;
    struct kndStateRef  *proc_inst_state_refs;

    struct kndDict *class_name_idx;
    struct kndDict *attr_name_idx;
    struct kndDict *proc_name_idx;
    struct kndDict *proc_arg_name_idx;

    struct kndUpdate *update;
    bool update_confirmed;

    struct kndTaskContext *next;
};

struct kndTask
{
    size_t id;
    knd_task_spec_type type;
    knd_state_phase phase;

    pthread_t thread;

    struct kndTaskContext *ctx;

    // int error;

    char timestamp[KND_NAME_SIZE];
    size_t timestamp_size;

    const char *input;
    size_t input_size;

    const char *report;
    size_t report_size;

    const char *path;
    size_t path_size;

    const char *filename;
    size_t filename_size;

    knd_http_code_t http_code;

    size_t batch_max;
    size_t batch_from;
    size_t batch_size;
    size_t start_from;

    size_t state_eq;
    size_t state_gt;
    size_t state_lt;
    size_t state_gte;
    size_t state_lte;
    bool show_removed_objs;

    size_t depth;
    size_t max_depth;

    struct kndUser *user;
    struct kndUserContext *user_ctx;

    struct kndRepo *system_repo;
    struct kndRepo *repo;

    struct kndClassFacet *facet;
    struct kndSet *set;

    struct kndConcFolder *folders;
    size_t num_folders;

    // FIXME(k15tfu): remove these vv
    struct kndAttr      *attr;
    struct kndAttrVar   *attr_var;
    struct kndClassInst *class_inst;

    struct kndElem *elem;

    struct kndSet *sets[KND_MAX_CLAUSES];
    size_t num_sets;

    struct kndStorage *storage;
    struct kndQueue   *input_queue;
    struct kndQueue   *output_queue;
    struct kndSet     *ctx_idx;

    struct kndOutput  *out;
    struct kndOutput  *log;
    struct kndOutput  *task_out;
    struct kndOutput  *file_out;
    struct kndOutput  *update_out;

    struct kndMemPool *mempool;
};

// knd_task.c
int knd_task_new(struct kndTask **self);
int knd_task_context_new(struct kndMemPool *mempool,
                         struct kndTaskContext **ctx);
void knd_task_del(struct kndTask *self);
void knd_task_reset(struct kndTask *self);
int knd_task_err_export(struct kndTaskContext *self);

// knd_task.select.c
int knd_task_run(struct kndTask *self);
