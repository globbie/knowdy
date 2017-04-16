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
 *   knd_conc.h
 *   Knowdy Conc
 */

#ifndef KND_CONC_H
#define KND_CONC_H

#include <libxml/parser.h>

#include "knd_utils.h"

struct kndRepoCache;

struct kndOutput;
struct kndConc;
struct kndDataWriter;
struct kndDataReader;
struct kndPolicy;
struct kndObject;
struct kndRefSet;
struct kndConcRef;

struct kndConcRole
{
    char name[KND_SMALL_BUF_SIZE];
    size_t name_size;
    
    struct kndConcRole *baserole;

    struct kndConcRef *specs;
    size_t num_specs;

    struct kndConcRef *topics;
    size_t num_topics;
    
    struct kndConcRole *next;
};


struct kndConc
{
    char name[KND_TEMP_BUF_SIZE];
    size_t name_size;

    char topic_id[KND_CONC_NAME_BUF_SIZE];
    char spec_id[KND_CONC_NAME_BUF_SIZE];
    char semrole_name[KND_SMALL_BUF_SIZE];
    bool is_complex;

    struct kndOutput *out;
    struct kndRepoCache *cache;
    
    struct kndRefSet *refset;

    /******** public methods ********/
    int (*init)(struct kndConc *self);
    int (*del)(struct kndConc *self);
    int (*reset)(struct kndConc *self);
    int (*str)(struct kndConc *self, size_t depth);

    int (*export)(struct kndConc *self,
                   knd_format format);

    int (*open)(struct kndConc *self);

    int (*read)(struct kndConc *self);
    
    int (*import)(struct kndConc *self,
                  struct kndObject *obj,
                  const char *rec,
                  size_t rec_size);

    int (*sync)(struct kndConc *self);

    int (*write)(struct kndConc *self,
		 output_dest_t dest,
		 const char    *buf,
		 size_t    buf_size);

    int (*intersect)(struct kndConc *self);

    /*int (*parse_terminals)(struct kndConc *self,
                      const char *locs);

    int (*add_elemlocs)(struct kndConc *self,
                        char *elemlocs);
    
    int (*add_clause)(struct kndConc *self,
    struct kndConc *clause);
    */
    
};

/* interfaces */
extern int kndConc_init(struct kndConc *self);
extern int kndConc_new(struct kndConc **self);

#endif /* KND_CONC_H */
