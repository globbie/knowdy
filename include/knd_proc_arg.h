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
 *   knd_proc_arg.h
 *   Knowdy Concept Proc Arg
 */
#pragma once


#include "knd_dict.h"
#include "knd_utils.h"
#include "knd_config.h"
#include "knd_output.h"

struct kndClass;
struct kndMemPool;
struct kndText;
struct kndClassInst;
struct kndClassVar;
struct kndTask;
struct kndProc;
struct kndProcInst;
struct kndProcArg;
struct kndProcCall;
struct kndProcCallArg;

struct kndProcArgRef
{
    struct kndProcArg    *arg;
    struct kndProcArgVar *var;
    struct kndProc       *proc;
    struct kndProcArgRef *next;
};

struct kndProcArgVar
{
    const char *name;
    size_t name_size;
    struct kndProcVar *parent;

    struct kndProcArg *arg;
    const char *val;
    size_t val_size;

    struct kndClassEntry *template;
    struct kndClassInst *inst;

    struct kndProcArgVar *next;
};

struct kndProcArgEntry
{
    const char *name;
    size_t name_size;

    struct kndProcArg *arg;

    struct kndProcArgVar *next;
};

struct kndProcArg 
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    const char *name;
    size_t name_size;

    struct kndProc *parent;

    const char *classname;
    size_t classname_size;
    struct kndClassEntry *template;

    struct kndAttrVar *attr_var;

    struct kndProcCall  *proc_call;
    struct kndProcEntry *proc_entry;

    size_t numval;
    const char *val;
    size_t val_size;
    struct kndText *tr;

    struct kndProcArg *next;
};

gsl_err_t knd_proc_arg_parse(struct kndProcArg *self, const char *rec, size_t *chunk_size, struct kndTask *task);
int knd_proc_arg_export_GSL(struct kndProcArg *self, struct kndTask *task, bool is_list_item, size_t depth);
int knd_proc_arg_var_export_GSL(struct kndProcArgVar *self, struct kndTask *task, size_t depth);
int knd_proc_arg_export(struct kndProcArg *self, knd_format format, struct kndTask *task, struct kndOutput *out);

int knd_proc_arg_resolve(struct kndProcArg *self, struct kndRepo *repo, struct kndTask *task);
int knd_resolve_proc_arg_var(struct kndProc *self, struct kndProcArgVar *var, struct kndTask *task);

int knd_proc_arg_compute(struct kndProcArg *self, struct kndTask *task);
void knd_proc_arg_str(struct kndProcArg *self, size_t depth);

/* allocators */
int knd_proc_arg_ref_new(struct kndMemPool *mempool, struct kndProcArgRef **self);
int knd_proc_arg_new(struct kndMemPool *mempool, struct kndProcArg **self);
int knd_proc_arg_var_new(struct kndMemPool *mempool, struct kndProcArgVar **result);
