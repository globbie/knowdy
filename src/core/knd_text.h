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
 *   knd_text.h
 *   Knowdy Text Element
 */

#ifndef KND_TEXT_H
#define KND_TEXT_H

#include "../knd_config.h"

struct kndElem;
struct kndElemRef;
struct kndOutput;

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
    char lang_code[KND_LANG_CODE_SIZE];
    size_t lang_code_size;

    size_t state;

    size_t chunk_count;
    
    /* TODO: quality rating, spelling.. etc  */
    int verif_level;
    
    char *seq;
    size_t seq_size;

    struct kndTextSelect *selects;
    struct kndTextSelect *tail;
    size_t num_selects;
    
    struct kndTranslation *next;
};

struct kndTextState
{
    knd_update_status update_oper;
    size_t state;
    
    /*struct kndConcRef *cg;*/

    /* translations of master text: manual or automatic */
    struct kndTranslation *translations;
    size_t num_translations;

    struct kndTextState *next;
};


struct kndText
{
    struct kndElem *elem;
    struct kndOutput *out;

    struct kndTextState *states;
    size_t num_states;
    
    /******** public methods ********/
    int (*str)(struct kndText *self,
               size_t depth);

    int (*del)(struct kndText *self);

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
    
    int (*export)(struct kndText *self,
                  knd_format format);
};

/* constructors */
extern int kndText_init(struct kndText *self);
extern int kndText_new(struct kndText **self);

#endif /* KND_TEXT_H */
