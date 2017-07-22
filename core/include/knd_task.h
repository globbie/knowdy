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

struct kndOutput;
struct kndTask;
struct kndUser;

typedef enum knd_task_spec_type { KND_GET_STATE, 
                                  KND_CHANGE_STATE
} knd_task_spec_type;

struct kndTaskArg
{
    char name[KND_NAME_SIZE];
    size_t name_size;
    char val[KND_NAME_SIZE];
    size_t val_size;
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
    bool is_implied;
    bool is_validator;

    char *buf;
    size_t *buf_size;
    
    void *obj;
    
    int (*parse)(void *obj, const char *rec, size_t *total_size);
    int (*validate)(void *obj, const char *name, size_t name_size,
                    const char *rec, size_t *total_size);
    int (*run)(void *obj, struct kndTaskArg *args, size_t num_args);
};


struct kndTask
{
    char sid[KND_NAME_SIZE];
    size_t sid_size;

    char uid[KND_NAME_SIZE];
    size_t uid_size;

    char tid[KND_NAME_SIZE];
    size_t tid_size;

    const char *spec;
    size_t spec_size;

    const char *obj;
    size_t obj_size;

    int error;
    
    struct kndUser *admin;
    
    struct kndOutput *log;
    struct kndOutput *out;
    struct kndOutput *spec_out;

    void *delivery;
    
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
    
    int (*report)(struct kndTask *self);
};

/* constructor */
extern int kndTask_new(struct kndTask **self);

#endif /* KND_TASK_H */
