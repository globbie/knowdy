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
#include <time.h>

#include "knd_config.h"
#include "knd_state.h"
#include "knd_steward.h"
#include "knd_repo.h"

#include <gsl-parser/gsl_err.h>

struct kndUser;
struct kndUserContext;
struct kndStateControl;
struct kndClass;
struct kndClassFacet;
struct kndClassVar;
struct kndQuery;
struct kndClassInst;
struct kndConcFolder;
struct kndText;
struct kndDict;
struct kndRepoCache;
struct kndCacheItem;
struct kndMemBlock;
struct kndStorage;

typedef int (*task_cb_t)(void *obj, const char *msg, size_t msg_size, void *ctx);

typedef enum knd_task_type {
    KND_TASK_DEFAULT,
    KND_TASK_QUERY,
    KND_TASK_COMMIT,
    KND_TASK_UPDATE_CACHE,
    KND_TASK_INNER,
    KND_TASK_INNER_COMMIT,
    KND_TASK_BULK_LOAD,
    KND_TASK_CACHE_UPDATE,
    KND_TASK_BUILD_SNAPSHOT,
    KND_TASK_RESTORE
} knd_task_type;

typedef enum knd_task_phase_t {
     KND_REGISTER,
     KND_SUBMIT,
     KND_CANCEL,
     KND_REJECT,
     KND_CONFLICT,
     KND_CONFIRM_COMMIT,
     KND_WAL_WRITE,
     KND_COMMIT_INDICES,
     KND_DELIVER_RESULT,
     KND_COMPLETE
} knd_task_phase_t;

typedef enum knd_task_mode_t {
     KND_TASK_DEFAULT_MODE,
     KND_TASK_TRACE_MODE,
     KND_TASK_COMMIT_MODE
} knd_task_mode_t;

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

    knd_task_type type;
    knd_task_phase_t phase;

    struct timespec start_ts;
    struct timespec end_ts;

    void *obj;
    task_cb_t cb;
    void *external_obj;
    task_cb_t external_cb;

    char       *input_buf;
    const char *input;
    size_t      input_size;

    int error;

    struct kndLocale *locale[KND_MAX_LOCALE];
    size_t num_locale;
    
    knd_format format;
    size_t format_indent;

    struct kndText *tr;

    //size_t batch_max;
    //size_t batch_from;
    //size_t batch_size;
    //size_t start_from;

    size_t depth;
    size_t max_depth;
    bool use_numid;
    bool use_alias;

    struct kndTaskDestination *dest;
    struct kndRepo *repo;

    /* temp refs to commits */
    //struct kndStateRef  *class_state_refs;
    struct kndStateRef  *inner_class_state_refs;
    struct kndStateRef  *class_inst_state_refs;
    size_t num_class_inst_state_refs;

    struct kndStateRef  *proc_state_refs;
    struct kndStateRef  *proc_inst_state_refs;

    struct kndCommit *commit;
    bool commit_confirmed;

    struct kndQuery *query;

    /* inner statement declarations */
    struct kndClassDeclar *declars;
    size_t num_declars;

    /* text search query & results */
    //struct kndStatement *query;
    struct kndTextSearchReport *reports;

    struct kndTaskContext *next;
};

struct kndTaskCache {
    struct kndMemPool *mempool;

    struct kndSet *attr_idx;
    struct kndDict *attr_name_idx;

    struct kndSet *cls_idx;
    struct kndDict *cls_name_idx;

    struct kndSet  *str_idx;
    struct kndDict *str_dict;

    struct kndCacheItem *cls_entries;
    struct kndCacheItem *cls_entries_tail;
    size_t num_cls_entries;
    size_t max_cls_entries;
};

struct kndTaskIndices
{
    struct kndDict *repo_name_idx;

    struct kndDict *cls_name_idx;
    struct kndSet *cls_idx;

    size_t   cls_id_count;
    size_t   num_cls;

    char cls_name_idx_path[KND_PATH_SIZE + 1];
    size_t cls_name_idx_path_size;

    char cls_idx_path[KND_PATH_SIZE + 1];
    size_t cls_idx_path_size;
    
    struct kndDict *attr_name_idx;
    struct kndSet  *attr_idx;
    size_t   attr_id_count;
    size_t   num_attrs;

    struct kndDict *proc_name_idx;
    struct kndSet *proc_idx;

    struct kndDict *proc_arg_name_idx;
    struct kndSet *proc_arg_idx;

    size_t proc_arg_id_count;

    struct kndSet  *str_idx;
    struct kndDict *str_dict;
};

struct kndTask
{
    knd_agent_role_type role;
    knd_task_type type;
    size_t id;
    knd_state_phase phase;
    knd_task_mode_t mode;

    struct kndSteward *steward;

    /* ctx can be persisted and continued by another task */
    struct kndTaskContext *ctx;
    struct kndUser *user;

    const char *input;
    size_t input_size;

    const char *output;
    size_t output_size;

    const char *report;
    size_t report_size;

    const char *path;
    size_t path_size;

    const char *filename;
    size_t filename_size;
    char filepath[KND_PATH_SIZE];
    size_t filepath_size;
    int fd;

    size_t depth;
    size_t max_depth;

    struct kndUserContext *user_ctx;
    struct kndUserContext *default_user_ctx;
 
    struct kndTaskCache cache;
    struct kndTaskIndices idxs;

    struct kndConcFolder *folders;
    size_t num_folders;

    struct kndOutput  *out;
    struct kndOutput  *log;
    struct kndOutput  *file_out;

    struct kndMemPool *mempool;

    struct kndMemBlock *blocks;
    size_t num_blocks;
    size_t total_block_size;

    size_t trace_level;
};

int knd_task_new(struct kndTask **result, knd_agent_role_type role, size_t task_id,
                 struct kndMemConfig *main_memconf, struct kndMemConfig *cache_memconf,
                 struct kndSteward *steward);

void knd_task_del(struct kndTask *task);

void knd_task_reset(struct kndTask *task);
void knd_task_cleanup(struct kndTask *task);
void knd_task_monitor(struct kndTask *task, struct kndStorage *store, struct kndResourceReport *report);

int knd_task_err_export(struct kndTask *task);

int knd_task_run(struct kndTask *task, const char *input, size_t input_size);

// knd_task.cache.c
int knd_task_cache_update(struct kndTask *task);

// knd_task.select.c
gsl_err_t knd_parse_task(void *obj, const char *rec, size_t *total_size);
