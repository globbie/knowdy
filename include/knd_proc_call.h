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
 *   <http://www.globbie.net>
 *
 *   Initial author and maintainer:
 *         Dmitri Dmitriev aka M0nsteR <dmitri@globbie.net>
 *
 *   ----------
 *   knd_proc_call.h
 *   Knowdy Proc
 */

#pragma once

#include "knd_proc.h"
#include "knd_config.h"
#include "knd_task.h"

#include <stddef.h>
#include <string.h>

struct kndAttrVar;
struct kndMemPool;

typedef enum knd_proc_type {
    KND_PROC_USER,
    KND_PROC_SYSTEM,
    KND_PROC_ADD,
    KND_PROC_SUM,
    KND_PROC_MULT,
    KND_PROC_MULT_PERCENT,
    KND_PROC_DIV_PERCENT
} knd_proc_type;

struct kndProcCallArg
{
    const char *name;
    size_t name_size;

    const char *val;
    size_t val_size;

    long numval;

    struct kndProcArg *arg;
    struct kndAttrVar *attr_var;

    struct kndProcCallArg *next;
};

struct kndProcCall
{
    const char *name;
    size_t name_size;
    knd_proc_type type;

    struct kndProc *proc;
    struct kndProcCallArg *args;
    size_t num_args;

    struct kndProcEstimate *estimate;
    struct kndProcCall *next;
};

//
// TODO(k15tfu): ?? Move to knd_proc_call_impl.h
//

static inline void knd_proc_call_declare_arg(struct kndProcCall *proc_call,
                                             struct kndProcCallArg *call_arg)
{
    call_arg->next = proc_call->args;
    proc_call->args = call_arg;
    proc_call->num_args++;
}

int knd_proc_call_arg_new(struct kndMemPool *mempool,
                          struct kndProcCallArg **result);
int knd_proc_call_new(struct kndMemPool *mempool,
                      struct kndProcCall **result);

gsl_err_t knd_proc_call_parse(struct kndProcCall *self,
                              const char *rec,
                              size_t *total_size,
                              struct kndTask *task);
void knd_proc_call_str(struct kndProcCall *self,
                       size_t depth);
