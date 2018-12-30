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

#include <glb-lib/output.h>

#include "knd_dict.h"
#include "knd_utils.h"
#include "knd_config.h"

#include "knd_proc_call.h"

struct kndClass;
struct kndMemPool;
struct kndTranslation;
struct kndProc;
struct kndProcInstance;
struct kndProcArg;
struct kndProcArgInstance;
struct kndClassInst;
struct kndClassVar;
struct kndTask;

//typedef enum knd_proc_arg_type {
//    KND_PROCARG_NONE,
//    KND_PROCARG_SUBJ,
//    KND_PROCARG_OBJ,
//    KND_PROCARG_INS
//} knd_proc_arg_type;

//static const char* const knd_proc_arg_names[] = {
//    "none",
//    "subj",
//    "obj",
//    "ins",
//};

struct kndProcArgRef
{
    struct kndProcArg    *arg;
    struct kndProc       *proc;
    struct kndProcArgRef *next;
};

struct kndProcArgInstance
{
//    knd_task_spec_type type;
//    struct kndProcArg *proc_arg;
//    struct kndProcInstance *proc_inst;

    const char *procname;
    size_t procname_size;
    struct kndProcEntry *proc_entry;

    const char *objname;
    size_t objname_size;
    struct kndObjEntry *obj;
    
//    struct kndProcArgInstance *next;
};

struct kndProcArg 
{
    char id[KND_ID_SIZE];
    size_t id_size;
    size_t numid;

    const char *name;
    size_t name_size;

    const char *classname;
    size_t classname_size;
    struct kndClass *class;

    struct kndProc *parent;

    struct kndProcCall  *proc_call;
    struct kndProcEntry *proc_entry;

    size_t numval;
    const char *val;
    size_t val_size;

    struct kndTranslation *tr;
    struct kndProcArg *next;
};

extern void kndProcArgInstance_init(struct kndProcArgInstance *self);
//extern void kndProcArgInstRef_init(struct kndProcArgInstRef *self);

gsl_err_t knd_proc_arg_parse(struct kndProcArg *self,
                             const char   *rec,
                             size_t *chunk_size,
                             struct kndTask *task);

extern int knd_proc_arg_export(struct kndProcArg *self,
                               knd_format format,
                               struct kndTask *task,
                               struct glbOutput *out);

extern void kndProcArg_init(struct kndProcArg *self);

void knd_proc_arg_str(struct kndProcArg *self,
                      size_t depth);

int knd_proc_resolve_arg(struct kndProcArg *self,
                         struct kndRepo *repo);
int knd_proc_arg_ref_new(struct kndMemPool *mempool,
                         struct kndProcArgRef **self);
int knd_proc_arg_new(struct kndMemPool *mempool,
                     struct kndProcArg **self);
