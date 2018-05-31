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
 *   knd_text.h
 *   Knowdy Text Element
 */
#pragma once

#include <glb-lib/output.h>

#include "knd_config.h"

struct kndElem;
struct kndElemRef;

#define KND_SYNT_ROLE_NAME_SIZE 3

typedef enum knd_synt_role_t { KND_SYNT_SUBJ, 
                               KND_SYNT_OBJ,
                               KND_SYNT_GEN,
                               KND_SYNT_DAT,
                               KND_SYNT_INS,
                               KND_SYNT_LOC
} knd_synt_role_t;

struct kndTextSelect
{
    char css_name[KND_NAME_SIZE];
    size_t css_name_size;

    size_t pos;
    size_t len;

    struct kndObjRef *ref;
    
    struct kndTextSelect *next;
};

struct kndTextChunk
{
    char val[KND_TEXT_CHUNK_SIZE];
    size_t val_size;
    struct kndTextChunk *next;
};

struct kndTranslation
{
    char curr_locale[KND_LOCALE_SIZE];
    size_t curr_locale_size;

    const char *locale;
    size_t locale_size;
    knd_synt_role_t synt_role; 
    
    size_t state;

    size_t chunk_count;
    
    /* TODO: quality rating, spelling.. etc  */
    int verif_level;

    char val[KND_VAL_SIZE];
    size_t val_size;

    const char *seq;
    size_t seq_size;

    struct kndTextChunk *chunks;
    struct kndTextChunk *chunk_tail;
    size_t num_chunks;

    struct kndTextSelect *selects;
    struct kndTextSelect *tail;
    size_t num_selects;

    struct kndTranslation *synt_roles;

    struct kndTranslation *next;
};

struct kndTextState
{
    knd_state_phase phase;
    char state[KND_STATE_SIZE];

    /* translations of master text: manual or automatic */
    struct kndTranslation *translations;
    size_t num_translations;

    struct kndTextState *next;
};


struct kndText
{
    struct kndElem *elem;
    struct glbOutput *out;

    struct kndTextState *states;
    size_t num_states;
    knd_format format;
    size_t depth;

    /******** public methods ********/
    void (*str)(struct kndText *self);
    void (*del)(struct kndText *self);

    int (*hilite)(struct kndText *self,
                  struct kndElemRef *elemref,
                  knd_format format);

    int (*parse)(struct kndText *self,
                 const char     *rec,
                 size_t          *total_size);

    int (*update)(struct kndText *self,
                 const char     *rec,
                 size_t          *total_size);

    int (*index)(struct kndText *self);
    int (*export)(struct kndText *self);
};

/* constructors */
extern int kndText_init(struct kndText *self);
extern int kndText_new(struct kndText **self);
