/**
 *   Copyright (c) 2011-2018 by Dmitri Dmitriev
 *   All rights reserved.
 *
 *   This file is part of the Knowdy G, raph DB
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
 *   knd_rel_arg.h
 *   Knowdy Concept Rel Arg
 */

#pragma once

#include <glb-lib/output.h>

#include "knd_dict.h"
#include "knd_utils.h"
#include "knd_task.h"
#include "knd_config.h"

struct kndClass;
struct kndTranslation;
struct kndRel;
struct kndRelInstance;
struct kndRelArg;
struct kndRelArgInstance;
struct kndObject;

typedef enum knd_relarg_type {
    KND_RELARG_NONE,
    KND_RELARG_SUBJ,
    KND_RELARG_OBJ,
    KND_RELARG_INS
} knd_relarg_type;

static const char* const knd_relarg_names[] = {
    "none",
    "subj",
    "obj",
    "ins",
};

struct kndRelArgInstRef
{
    struct kndRelArgInstance *inst;
    struct kndRelArgInstRef *next;
};

struct kndRelArgInstance
{
    knd_task_spec_type type;
    struct kndRelArg *relarg;
    struct kndRelInstance *rel_inst;

    const char *classname;
    size_t classname_size;
    struct kndConcDir *conc_dir;

    const char *objname;
    size_t objname_size;
    struct kndObjEntry *obj;

    const char *val;
    size_t val_size;

    struct kndRelArgInstance *next;
};

struct kndRelArg 
{
    knd_relarg_type type;

    char name[KND_NAME_SIZE];
    size_t name_size;

    char classname[KND_NAME_SIZE];
    size_t classname_size;

    struct kndClass *conc;
    struct kndConcDir *conc_dir;
    struct kndRel *rel;

    const char *locale;
    size_t locale_size;
    knd_format format;

    int concise_level;
    int descr_level;
    int browse_level;

    char calc_oper[KND_NAME_SIZE];
    size_t calc_oper_size;

    char calc_relarg[KND_NAME_SIZE];
    size_t calc_relarg_size;

    char default_val[KND_NAME_SIZE];
    size_t default_val_size;

    char idx_name[KND_NAME_SIZE];
    size_t idx_name_size;

    struct kndRefSet *browser;
    struct kndObject *curr_obj;
    struct glbOutput *out;
    
    struct kndTranslation *tr;
    size_t depth;
    struct kndRelArg *next;
    
    /***********  public methods ***********/
    void (*init)(struct kndRelArg  *self);
    void (*del)(struct kndRelArg   *self);
    void (*str)(struct kndRelArg *self);

    int (*parse)(struct kndRelArg *self,
                 const char   *rec,
                 size_t *chunk_size);

    int (*validate)(struct kndRelArg *self,
                    const char   *val,
                    size_t val_size);
    int (*resolve)(struct kndRelArg   *self);
    int (*export)(struct kndRelArg   *self);

    int (*parse_inst)(struct kndRelArg *self,
                      struct kndRelArgInstance *inst,
                      const char *rec,
                      size_t *total_size);
    int (*resolve_inst)(struct kndRelArg *self,
			struct kndRelArgInstance *inst);
    int (*export_inst)(struct kndRelArg *self,
		       struct kndRelArgInstance *inst);
};


/* constructor */
extern void kndRelArgInstance_init(struct kndRelArgInstance *self);
extern void kndRelArgInstRef_init(struct kndRelArgInstRef *self);
extern int kndRelArg_new(struct kndRelArg **self);
