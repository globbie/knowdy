/**
 *   Copyright (c) 2011-2017 by Dmitri Dmitriev
 *   All rights reserved.
 *
 *   This file is part of the OOmnik Conceptual Processor, 
 *   and as such it is subject to the license stated
 *   in the LICENSE file which you have received 
 *   as part of this distribution.
 *
 *   Project homepage:
 *   <http://www.oomnik.ru>
 *
 *   Initial author and maintainer:
 *         Dmitri Dmitriev aka M0nsteR <dmitri@globbie.net>
 *
 *   Code contributors:
 *         * Jenia Krylov <info@e-krylov.ru>
 *
 *   --------
 *   knd_dict.h
 *   Knowdy Dictionary
 */

#ifndef OODICT_H
#define OODICT_H

#include "knd_array.h"

typedef size_t (*oo_hash_func)(const char *key);

struct ooDictItem
{
    char *key;
    void *data;

};

struct ooDict
{
    size_t size;

    /******** public methods ********/
    int (*init)(struct ooDict *self);
    void (*del)(struct ooDict *self);
    void (*reset)(struct ooDict *self);
    const char* (*str)(struct ooDict *self);

    /* get data */
    void* (*get)(struct ooDict *self,
                 const char *key);

    /* key with size */
    void* (*getn)(struct ooDict *self,
                  const char *key,
                  size_t key_size);

    /*
     * set data
     * return true, if key already exists, false otherwise
     */
    int (*set)(struct ooDict *self,
                const char *key,
                void *data);

    /* if exists */
    bool (*key_exists)(struct ooDict *self,
                       const char    *key);
    /* delete */
    int (*remove)(struct ooDict *self,
                    const char *key);

    /* Resize the hash */
    int (*resize)(struct ooDict *self,
		  unsigned int new_size);

    /* set hash function */
    int (*set_hash)(struct ooDict *self,
		    oo_hash_func new_hash);

    /* listing the keys and values */
    int (*rewind)(struct ooDict *self);
    int (*next_item)(struct ooDict *self,
		     const char **key,
		     void **data);

    /*int (*set_compare)(struct ooDict *self,
      oo_compar_func new_compar_func);
    */

    /******** private attributes ********/

    struct ooArray *hash;

    oo_hash_func hash_func;

    size_t curr_pos;
    struct ooListItem *curr_item;

};

/* constructor */
extern int ooDict_new(struct ooDict **self, size_t init_size);

#endif /* OODICT_H */
