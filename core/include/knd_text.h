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
 *   knd_text.h
 *   Knowdy Text Element
 */
#pragma once

#include <glb-lib/output.h>

#include "knd_config.h"
#include "knd_state.h"

struct kndContent;

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

struct kndTranslation
{
    const char *curr_locale;
    size_t curr_locale_size;

    const char *locale;
    size_t locale_size;

    struct kndState *states;
    size_t num_states;

    size_t chunk_count;
    
    /* TODO: quality rating, spelling.. etc  */
    int verif_level;

    const char *val;
    size_t val_size;

    const char *seq;
    size_t seq_size;

    /*struct kndTextChunk *chunks;
    struct kndTextChunk *chunk_tail;
    size_t num_chunks;
    */

    /*
    struct kndTextSelect *selects;
    struct kndTextSelect *tail;
    size_t num_selects;
    */
    //struct kndTranslation *synt_roles;

    struct kndTranslation *next;
};

struct kndText
{
    /* translations of master text: manual or automatic */
    struct kndTranslation *tr;
    size_t num_trs;

    // TODO: sem graph
    // visual formatting

    struct kndState *states;
    size_t num_states;
};

extern int knd_text_export(struct kndText *self, knd_format format,
                           struct kndTask *task, struct glbOutput *out);

extern int knd_text_translation_new(struct kndMemPool *mempool,
                                    struct kndTranslation   **self);
extern int knd_text_new(struct kndMemPool *mempool,
                        struct kndText   **self);
