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
 *   knd_rel_arg.h
 *   Knowdy Concept RelArg
 */

#ifndef KND_RELARG_H
#define KND_RELARG_H

#include "knd_dict.h"
#include "knd_utils.h"
#include "knd_task.h"
#include "knd_config.h"

struct kndConcept;
struct kndOutput;
struct kndTranslation;
struct kndRel;
struct kndRelArg;

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

struct kndRelArgItem
{
    knd_task_spec_type type;
    char name[KND_NAME_SIZE];
    size_t name_size;

    char val[KND_NAME_SIZE];
    size_t val_size;

    struct kndRelArg *relarg;
    struct kndRelArgItem *children;
    struct kndRelArgItem *tail;
    size_t num_children;
    
    struct kndRelArgItem *next;
};

struct kndRelArg 
{
    knd_relarg_type type;

    char name[KND_NAME_SIZE];
    size_t name_size;

    char classname[KND_NAME_SIZE];
    size_t classname_size;

    struct kndConcept *conc;
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

    struct kndOutput *out;
    
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
};


/* constructor */
extern int kndRelArg_new(struct kndRelArg **self);
#endif
