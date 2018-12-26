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

//struct kndProcArgInstRef
//{
//    struct kndProcArgInstance *inst;
//    struct kndProcArgInstRef *next;
//};

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
    const char *name;
    size_t name_size;

    struct kndProcCall proc_call;

    struct kndProc *parent;
    struct kndProcEntry *proc_entry;

    //const char *locale;
    //size_t locale_size;
    //knd_format format;

    const char *classname;
    size_t classname_size;
//    struct kndClass *conc;

    size_t numval;
    const char *val;
    size_t val_size;

    struct kndTranslation *tr;
    struct kndProcArg *next;
    
    /***********  public methods ***********/
    int (*validate)(struct kndProcArg *self,
                    const char   *val,
                    size_t val_size);
    int (*resolve)(struct kndProcArg   *self);

    int (*parse_inst)(struct kndProcArg *self,
                      struct kndProcArgInstance *inst,
                      const char *rec,
                      size_t *total_size);
    int (*resolve_inst)(struct kndProcArg *self,
			struct kndProcArgInstance *inst);
    int (*export_inst)(struct kndProcArg *self,
		       struct kndProcArgInstance *inst);
};


/* constructor */
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
int knd_proc_arg_new(struct kndProcArg **self,
                     struct kndMemPool *mempool);
