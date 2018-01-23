/**
 *   Copyright (c) 2011-2017 by Dmitri Dmitriev
 *   All rights reserved.
 *
 *   This file is part of the Knowdy Search Engine, 
 *   and as such it is subject to the license stated
 *   in the LICENSE file which you have received 
 *   as part of this distribution.
 *
 *   Project homepage:
 *   <http://www.globbie.net>
 *
 *   Initial author and maintainer:
 *         Dmitri Dmitriev aka M0nsteR <dmitri@globbie.net>
 *
 *   ----------
 *   knd_task.h
 *   Knowdy Task
 */

#ifndef KND_TASK_H
#define KND_TASK_H

#include "knd_config.h"
#include "knd_http_codes.h"

#include <gsl-parser/gsl_err.h>

struct kndOutput;
struct kndTask;
struct kndUser;
struct kndStateControl;
struct kndConcept;

typedef enum knd_task_spec_type { KND_GET_STATE, 
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

struct kndTaskArg
{
    char name[KND_NAME_SIZE + 1];  // null-terminated string
    size_t name_size;
    char val[KND_NAME_SIZE + 1];  // null-terminated string
    const char *val_ref;
    size_t val_size;
    size_t hash_val;
};

struct kndTaskSpec
{
    knd_task_spec_type type;

    const char *name;
    size_t name_size;

    struct kndTaskSpec *specs;
    size_t num_specs;
    
    bool is_completed;
    bool is_default;
    bool is_selector;
    bool is_implied;
    bool is_validator;
    bool is_terminal;
    bool is_list;
    bool is_atomic;

    char *buf;
    size_t *buf_size;
    size_t max_buf_size;

    void *obj;
    void *accu;
    
    int (*parse)(void *obj, const char *rec, size_t *total_size);
    int (*validate)(void *obj, const char *name, size_t name_size,
                    const char *rec, size_t *total_size);
    int (*run)(void *obj, struct kndTaskArg *args, size_t num_args);
    int (*append)(void *accu, void *item);
    int (*alloc)(void *accu, const char *name, size_t name_size, size_t count, void **item);
};

struct kndTask
{
    knd_task_spec_type type;
    knd_iter_type iter_type;

    char sid[KND_NAME_SIZE];
    size_t sid_size;

    char uid[KND_NAME_SIZE];
    size_t uid_size;

    char agent_name[KND_NAME_SIZE];
    size_t agent_name_size;

    char schema_name[KND_NAME_SIZE];
    size_t schema_name_size;

    char tid[KND_NAME_SIZE];
    size_t tid_size;

    char curr_locale[KND_NAME_SIZE];
    size_t curr_locale_size;

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

    char state[KND_STATE_SIZE];
    bool is_state_changed;

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

    struct kndConcept *class_selects[KND_MAX_CLASS_BATCH];
    size_t num_class_selects;

    struct kndUser *admin;
    struct kndStateControl *state_ctrl;

    struct kndOutput *log;
    struct kndOutput *out;
    struct kndOutput *spec_out;
    struct kndOutput *update;

    void *delivery;
    void *publisher;

    /******** public methods ********/
    void (*str)(struct kndTask *self,
               size_t depth);

    void (*del)(struct kndTask *self);

    void (*reset)(struct kndTask *self);
    
    int (*run)(struct kndTask *self,
               const char     *rec,
               size_t   rec_size,
               const char *obj,
               size_t obj_size);

    int (*parse)(struct kndTask *self,
                 const char     *rec,
                 size_t   *total_size);
    gsl_err_t (*parse_iter)(void *data,
                            const char *rec,
                            size_t *total_size);
    int (*report)(struct kndTask *self);
};

/* constructor */
extern int kndTask_new(struct kndTask **self);

#endif /* KND_TASK_H */
