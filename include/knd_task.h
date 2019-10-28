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

typedef enum knd_agent_role_type {
    KND_READER,
    KND_WRITER
} knd_agent_role_type;

typedef enum knd_task_spec_type {
    KND_GET_STATE,
    KND_SELECT_STATE,
    KND_COMMIT_STATE,
    KND_LIQUID_STATE,
    KND_SYNC_STATE,
    KND_DELTA_STATE,
    KND_LOAD_STATE,
    KND_RESTORE_STATE,
    KND_STOP_STATE
} knd_task_spec_type;

typedef enum knd_task_phase_t { KND_REGISTER,
                                KND_SUBMIT,
                                KND_CANCEL,
                                KND_REJECT,
                                KND_IMPORT,
                                KND_CONFLICT,
                                KND_CONFIRM_COMMIT,
                                KND_WAL_WRITE,
                                KND_COMMIT_INDICES,
                                KND_DELIVER_RESULT,
                                KND_COMPLETE } knd_task_phase_t;

struct kndTaskDestination
{
    char URI[KND_NAME_SIZE];
    size_t URI_size;

    //  auth 

};

struct kndMemBlock {
    size_t tid;
    char *buf;
    size_t buf_size;
    struct kndMemBlock *next;
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

    size_t depth;
    size_t max_depth;

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

    struct kndTaskContext *next;
};

struct kndTask
{
    knd_agent_role_type role;
    knd_task_spec_type type;
    int id;
    knd_state_phase phase;

    struct kndShard *shard;
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

    struct kndSet *sets[KND_MAX_CLAUSES];
    size_t num_sets;

    struct kndSet     *ctx_idx;

    struct kndOutput  *out;
    struct kndOutput  *log;
    struct kndOutput  *file_out;

    struct kndMemPool *mempool;
    bool is_mempool_owner;

    struct kndMemBlock *blocks;
    size_t num_blocks;
    size_t total_block_size;

    struct kndDict *class_name_idx;
    struct kndDict *attr_name_idx;
    struct kndDict *proc_name_idx;
    struct kndDict *proc_arg_name_idx;
};

// knd_task.c
int knd_task_new(struct kndShard *shard,
                 struct kndMemPool *mempool,
                 int task_id,
                 struct kndTask **task);
int knd_task_mem(struct kndMemPool *mempool,
                 struct kndTask **result);
int knd_task_context_new(struct kndMemPool *mempool,
                         struct kndTaskContext **ctx);
int knd_task_block_new(struct kndMemPool *mempool,
                       struct kndTask **result);
int knd_task_copy_block(struct kndTask *self,
                        const char *input, size_t input_size,
                        const char **output, size_t *output_size);

int knd_task_read_file_block(struct kndTask *self,
                             const char *filename, size_t filename_size,
                             struct kndMemBlock **result);
void knd_task_free_blocks(struct kndTask *self);

void knd_task_del(struct kndTask *self);
void knd_task_reset(struct kndTask *self);
int knd_task_err_export(struct kndTask *self);
int knd_task_run(struct kndTask *self, const char *input, size_t input_size);

// knd_task.select.c
gsl_err_t knd_parse_task(void *obj, const char *rec, size_t *total_size);
