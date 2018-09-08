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

#include "knd_config.h"

#include <stddef.h>
#include <string.h>

struct kndClassVar;
struct kndProc;
struct kndProcArg;
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
    char name[KND_SHORT_NAME_SIZE];
    size_t name_size;

    char val[KND_NAME_SIZE];
    size_t val_size;

    long numval;

    struct kndProcArg *arg;
    struct kndClassVar *class_var;

//    struct kndProcCall proc_call;

    struct kndProcCallArg *next;
};

struct kndProcCall
{
    char name[KND_NAME_SIZE];
    size_t name_size;
    knd_proc_type type;

    struct kndProc *proc;
    struct kndProcCallArg *args;
    size_t num_args;
    struct kndMemPool *mempool;
};

//
// TODO(k15tfu): ?? Move to knd_proc_call_impl.h
//

static inline void kndProcCall_declare_arg(struct kndProcCall *proc_call, struct kndProcCallArg *call_arg)
{
    call_arg->next = proc_call->args;
    proc_call->args = call_arg;
    proc_call->num_args++;
}

static inline void kndProcCallArg_init(struct kndProcCallArg *self,
                                       const char *name, size_t name_size,
                                       struct kndClassVar *class_var)
{
    memset(self, 0, sizeof *self);

    memcpy(self->name, name, name_size);
    self->name_size = name_size;
    self->name[name_size] = '\0';

    self->class_var = class_var;
}
