/**
 *   Copyright (c) 2011-2013 by Dmitri Dmitriev
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
 *   oodict.c
 *   OOmnik Dictionary implementation
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oodict.h"
#include "oolist.h"

static size_t 
oo_hash(const char *key)
{
    const char *p = key;
    size_t h = 0;

    if (!key) return 0;

    while (*p)
        h = (h << 1) ^ *p++;

    return h;
}

static const char*
ooDict_str(struct ooDict *self)
{
    struct ooListItem *cur;
    struct ooDictItem *item;
    struct ooList *l;
    size_t i;

    printf("  dict size: %lu\n", (unsigned long)self->hash->size);

    for (i = 0; i < self->hash->size; i++) {
        l = (struct ooList*)self->hash->data[i];
        if (l->size == 0) continue;

        cur = l->head;

        while (cur) {
            item = (struct ooDictItem*)cur->data;

            printf(" %zu) KEY: \"%s\"\n", i, item->key);

            cur = cur->next;
        }
    }

    printf("TOTAL KEYS: %lu\n", (unsigned long)self->size);

    return "";
}

static int
ooDict_set_hash(struct ooDict *self,
		oo_hash_func new_hash)
{
    if (!new_hash) return oo_FAIL;
    self->hash_func = new_hash;

    return oo_OK;
}

static ooDictItem* 
ooDict_find_item(struct ooDict *self,
		 const char *key)
{
    struct ooList *l;
    struct ooListItem *cur;
    const char *cur_key;
    unsigned int h;

    h = oo_hash(key) % self->hash->size;

    l = (struct ooList*)self->hash->data[h];

    cur = l->head;
    while (cur) {
        cur_key = ((struct ooDictItem*)cur->data)->key;
        if (!strcmp(key, cur_key))
            return (struct ooDictItem*)cur->data;
        cur = cur->next;
    }

    return NULL;
}

static void*
ooDict_get(struct ooDict *self,
	   const char *key)
{
    struct ooDictItem *item;
    if (!key) return NULL;

    item = ooDict_find_item(self, key);
    if (!item) return NULL;

    return item->data;
}

static int
ooDict_set(struct ooDict *self,
	   const char *key,
	   void *data)
{
    struct ooList *l;
    struct ooListItem *cur;
    struct ooDictItem *item = NULL;
    const char *cur_key;
    unsigned int h;

    if (!key) return oo_FAIL;

    h = oo_hash(key) % self->hash->size;

    l = (struct ooList*)self->hash->data[h];

    cur = l->head;

    while (cur) {
        cur_key = ((struct ooDictItem*)cur->data)->key;
        if (!strcmp(key, cur_key)) {
            item = (struct ooDictItem*)cur->data;
            break;
        }
        cur = cur->next;
    }

    if (item) {
        item->data = data;
        return oo_OK;
    }

    item = (struct ooDictItem*)malloc(sizeof(struct ooDictItem));
    if (!item) return oo_NOMEM;

    item->data = data;
    item->key = strdup(key);

    l->add(l, (void*)item, NULL);
    self->size++;

    return oo_OK;
}

static bool 
ooDict_key_exists(struct ooDict *self,
		  const char *key)
{
    struct ooDictItem *i = ooDict_find_item(self, key);
    return (i != NULL);
}

static int
ooDict_remove(struct ooDict *self,
	      const char *key)
{
    struct ooList *l;
    char *cur_key;
    void *data;
    struct ooListItem *cur;
    unsigned int h;
    
    h = self->hash_func(key) % self->hash->size;
    l = (ooList*)self->hash->data[h];

    cur = l->head;
    while (cur) {
        cur_key = ((struct ooDictItem*)cur->data)->key;

        if (!strcmp(key, cur_key)) {
            data = ((struct ooDictItem*)cur->data)->data;
            free(cur_key);

            cur->data = NULL;
            l->remove(l, cur);
	    self->size--;
            return oo_OK;
        }
        cur = cur->next;
    }

    return oo_FAIL;
}

static int ooDict_del(struct ooDict *self)
{
    unsigned int i;
    struct ooList *l;
    struct ooListItem *cur;
    struct ooDictItem *item;

    for (i = 0; i < self->hash->size; ++i) {
        l = (struct ooList*)self->hash->data[i];
        if (l->size) {
            cur = l->head;
            while (cur) {
                item = (struct ooDictItem*)cur->data;
                free(item->key);
                free(item);
                cur = cur->next;
            }
        }
        l->del(l);
    }

    self->hash->del(self->hash);

    free(self);

    return oo_OK;
}

static int
ooDict_resize(struct ooDict *self,
	      unsigned int new_size)
{
    unsigned int i;
    struct ooList *l;
    struct ooListItem *cur;
    struct ooDictItem *item;
    struct ooArray *tmp;
    size_t h;
    struct ooList *new_list;
    int ret;

    tmp = self->hash->get_subsequence(self->hash, 0, 
				      self->hash->size);
    ret = self->hash->resize(self->hash, new_size);

    for (i = 0; i < new_size; ++i) {
        ret = ooList_new(&l);
	if (ret != oo_OK) return ret;

        self->hash->data[i] = (void*)l;
    }

    /* rehash all items */
    for (i = 0; i < tmp->size; ++i) {
        l = (ooList*)tmp->data[i];
        if (l->size == 0) continue; 
	cur = l->head;
	while (cur) {
	    item = (struct ooDictItem*)cur->data;
	    h = self->hash_func(item->key) % self->hash->size;
	    new_list = (struct ooList*)self->hash->data[h];
	    new_list->add(new_list, (void*)item, NULL);
	    cur = cur->next;
        }
        l->del(l);
    }
    tmp->del(tmp);
    return oo_OK;
}

static int 
ooDict_rewind(struct ooDict *self)
{
    self->curr_pos = 0;
    self->curr_item = NULL;
    return oo_OK;
}

static int 
ooDict_next_item(struct ooDict *self,
		 const char **key,
		 void **data)
{
    struct ooList *l;
    struct ooListItem *item = self->curr_item;
    struct ooDictItem *item_data = NULL;
    size_t i;

    /* if curr_item is not NULL, continue listing */

    if (item) {
	item_data = (struct ooDictItem*)item->data;
	*key = item_data->key;
	*data = item_data->data;

	self->curr_item = item->next;
	return oo_OK;
    }

    /* jump to the next position in hash */

    if (self->curr_pos >= self->hash->size)
	goto no_results;
    
    for (i = self->curr_pos; i <  self->hash->size; i++) {
	l = (struct ooList*)self->hash->data[i];
	if (l->size == 0) continue;
	item = l->head;
	break;
    }

    if (!item)
	goto no_results;
    
    item_data = (struct ooDictItem*)item->data;
    *key = item_data->key;
    *data = item_data->data;

    self->curr_item = item->next;
    self->curr_pos = i + 1;

    return oo_OK;

no_results:
    *key = NULL; 
    *data = NULL;
    return oo_FAIL;
}



static int 
ooDict_init(struct ooDict *self)
{
    self->size          = 0;
    self->del           = ooDict_del;
    self->str           = ooDict_str;
    self->remove        = ooDict_remove;
    self->get           = ooDict_get;
    self->set           = ooDict_set;
    self->key_exists    = ooDict_key_exists;
    self->resize        = ooDict_resize;
    self->rewind        = ooDict_rewind;
    self->next_item     = ooDict_next_item;
    self->set_hash      = ooDict_set_hash;
    self->hash_func     = oo_hash;

    self->curr_item = NULL;
    self->curr_pos = 0;

    return oo_OK;
}

/* constructor */
extern int
ooDict_new(struct ooDict **dict, 
	   size_t init_size)
{
    size_t i;
    struct ooList *l;
    int ret;

    struct ooDict *self = malloc(sizeof(struct ooDict));
    if (!self) return oo_NOMEM;

    if (init_size == 0) return oo_FAIL;

    ooDict_init(self);

    ooArray_new(&self->hash);
    if (!self->hash) {
	self->del(self);
	return oo_NOMEM;
    }

    ret = self->hash->resize(self->hash, init_size);
    if (ret != oo_OK) {
	self->del(self);
	return ret;
    }

    for (i = 0; i < init_size; ++i) {
        ret = ooList_new(&l);
	if (ret != oo_OK) {
	    self->del(self);
	    return ret;
	}

        self->hash->data[i] = l;
    }

    *dict = self;

    return oo_OK;
}
