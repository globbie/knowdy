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

#include "knd_config.h"
#include "knd_state.h"
#include "knd_http_codes.h"

#include <glb-lib/output.h>
#include <gsl-parser/gsl_err.h>

struct glbOutput;
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

typedef enum knd_task_spec_type {
    KND_GET_STATE,
    KND_SELECT_STATE,
    KND_CHANGE_STATE,
    KND_UPDATE_STATE,
    KND_LIQUID_STATE,
    KND_SYNC_STATE,
    KND_DELTA_STATE
} knd_task_spec_type;

struct kndVisualFormat {
    size_t text_line_height;
    size_t text_hangindent_size;
};

struct kndTaskContext {
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    knd_task_spec_type type;
    knd_state_phase phase;
    size_t worker_id;
    struct kndTaskContext *next;
};

struct kndTask
{
    size_t id;
    knd_task_spec_type type;
    knd_state_phase phase;

    char tid[KND_NAME_SIZE];
    size_t tid_size;

    char curr_locale[KND_NAME_SIZE];
    size_t curr_locale_size;

    knd_format format;
    size_t format_offset;

    char timestamp[KND_NAME_SIZE];
    size_t timestamp_size;

    const char *input;
    size_t input_size;

    const char *locale;
    size_t locale_size;

    struct kndTranslation *tr;

    const char *report;
    size_t report_size;

    // TODO: subscription channel
    // to push any updates

    int error;
    knd_http_code_t http_code;

    size_t batch_max;
    size_t batch_from;
    size_t batch_size;
    size_t start_from;

    bool batch_mode;

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

    struct kndRepo *repo;  // FIXME(k15tfu): remove this

    struct kndClass *class;
    struct kndClassFacet *facet;
    struct kndSet *set;

    struct kndConcFolder *folders;
    size_t num_folders;

    // FIXME(k15tfu): remove these vv
    struct kndClassVar *class_var;
    struct kndAttr *attr;
    struct kndAttrVar *attr_var;
    struct kndClassInst *class_inst;
    struct kndElem *elem;
    //struct kndProc *proc;

    /* updates */
    struct kndStateRef  *class_state_refs;
    struct kndStateRef  *inner_class_state_refs;
    struct kndStateRef  *class_inst_state_refs;
    struct kndStateRef  *proc_state_refs;
    struct kndStateRef  *proc_inst_state_refs;
    struct kndUpdate *update;
    bool update_confirmed;

    struct kndSet *sets[KND_MAX_CLAUSES];
    size_t num_sets;

    struct kndShard *shard;
    struct glbOutput *storage;
    struct glbOutput *log;
    struct glbOutput *out;
    struct glbOutput *file_out;
    struct glbOutput *update_out;
    struct kndMemPool *mempool;
};

// knd_task.c
extern void kndTask_del(struct kndTask *self);
extern void kndTask_reset(struct kndTask *self);
extern int kndTask_run(struct kndTask *self, const char *rec, size_t rec_size, struct kndShard *shard);
extern int kndTask_build_report(struct kndTask *self);
extern int kndTask_new(struct kndTask **self);

int knd_task_context_new(struct kndMemPool *mempool,
                         struct kndTaskContext **ctx);

// knd_task.select.c
extern gsl_err_t knd_select_task(struct kndTask *self, const char *rec, size_t *total_size, struct kndShard *shard);
