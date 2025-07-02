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
#include "knd_memblock.h"
#include "knd_steward.h"
#include "knd_repo.h"
#include "knd_dict.h"
#include "knd_http_codes.h"

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
struct kndRepoCache;

typedef int (*task_cb_func)(void *obj, const char *msg, size_t msg_size, void *ctx);

typedef enum knd_task_type {
    KND_TASK_DEFAULT,
    KND_TASK_QUERY,
    KND_TASK_COMMIT,
    KND_TASK_READ_SNAPSHOT,
    KND_TASK_BUILD_SNAPSHOT,
    KND_TASK_INNER,
    KND_TASK_INNER_COMMIT,
    KND_TASK_BULK_LOAD,
    KND_TASK_CACHE_UPDATE,
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
    task_cb_func cb;
    void *external_obj;
    task_cb_func external_cb;

    char       *input_buf;
    const char *input;
    size_t      input_size;

    int error;
    knd_http_code_t http_code;

    char locale[KND_ID_SIZE];
    size_t locale_size;

    knd_format format;
    size_t format_offset;
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

    // TODO: subscription channel
    // to push any commits

    struct kndTaskDestination *dest;
    struct kndRepo *repo;

    /* temp refs to commits */
    struct kndStateRef  *class_state_refs;
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

struct kndTask
{
    knd_agent_role_type role;
    knd_task_type type;
    int id;
    knd_state_phase phase;
    knd_task_mode_t mode;

    struct kndSteward *steward;

    /* ctx can be persisted and continued by another task */
    struct kndTaskContext *ctx;

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

    knd_http_code_t http_code;

    size_t depth;
    size_t max_depth;

    struct kndUserContext *user_ctx;
    struct kndUserContext *default_user_ctx;

    struct kndRepo *system_repo;
    struct kndRepo *repo;
    struct kndRepoSnapshot *snapshot;

    struct kndRepoCache *cache;
    struct kndRepoCache *local_cache;
    struct kndRepoIndices *idxs;

    void *payload;

    struct kndConcFolder *folders;
    size_t num_folders;

    struct kndSet *sets[KND_MAX_CLAUSES];
    size_t num_sets;

    struct kndSet     *ctx_idx;

    struct kndOutput  *out;
    struct kndOutput  *log;
    struct kndOutput  *file_out;

    struct kndMemPool *mempool;
    struct kndMemPool *cache_mempool;

    struct kndMemPool *ctx_mempool;
    struct kndMemPool *ctx_cache_mempool;

    bool keep_local_WAL;

    struct kndMemBlock *blocks;
    size_t num_blocks;
    size_t total_block_size;

    struct kndDict *repo_name_idx;

    struct kndDict *class_name_idx;
    struct kndSet  *class_idx;
    struct kndDict *class_inst_alias_idx;

    struct kndDict *attr_name_idx;
    struct kndDict *proc_name_idx;
    struct kndDict *proc_arg_name_idx;

    size_t trace_level;
    /* cache */
    struct kndSet  *cache_class_idx;
};

int knd_task_new(struct kndTask **result,
                 knd_agent_role_type role, int task_id, struct kndSteward *steward);
int knd_task_init(struct kndTask *task, struct kndSteward *steward);
void knd_task_del(struct kndTask *self);
void knd_task_reset(struct kndTask *self);
void knd_task_cleanup(struct kndTask *task, struct kndSteward *steward);

int knd_task_err_export(struct kndTask *self);
int knd_task_run(struct kndTask *self, const char *input, size_t input_size);

// knd_task.select.c
gsl_err_t knd_parse_task(void *obj, const char *rec, size_t *total_size);

int knd_task_fetch_memblock(struct kndTask *task, size_t space_required, struct kndMemBlock **result);
