/**
 *   Copyright (c) 2011-present by Dmitri Dmitriev
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
 *   knd_dict.c
 *   OOmnik Dictionary implementation
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <knd_dict.h>
#include <knd_list.h>

static size_t 
oo_hash(const char *key, size_t key_size)
{
    const char *p = key;
    size_t h = 0;

    if (!key_size) return 0;

    while (key_size) {
        h = (h << 1) ^ *p++;
        key_size--;
    }
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

            printf(" %zu) KEY: \"%s\" VAL: %p\n", i, item->key, item->data);

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

static struct ooDictItem* 
ooDict_find_item(struct ooDict *self,
                 const char *key,
                 size_t key_size)
{
    struct ooList *l;
    struct ooListItem *cur;
    struct ooDictItem *item = NULL;
    unsigned int h;

    h = oo_hash(key, key_size) % self->hash->size;
    l = (struct ooList*)self->hash->data[h];

    for (cur = l->head; cur; cur = cur->next) {
        item = cur->data;
        if (item->key_size != key_size) continue;
        if (!memcmp(item->key, key, key_size))
            return item;
    }

    return NULL;
}

static void*
ooDict_get(struct ooDict *self,
            const char *key,
            size_t key_size)
{
    struct ooDictItem *item;
    struct ooList *l;
    struct ooListItem *cur;
    const char *cur_key;
    size_t cur_key_size;
    unsigned int h;

    if (!key_size) return NULL;

    h = oo_hash(key, key_size) % self->hash->size;
    l = (struct ooList*)self->hash->data[h];

    for (cur = l->head; cur; cur = cur->next) {
        cur_key = ((struct ooDictItem*)cur->data)->key;
        cur_key_size = ((struct ooDictItem*)cur->data)->key_size;
        if (cur_key_size != key_size) continue;
        if (!memcmp(key, cur_key, key_size)) {
            item = (struct ooDictItem*)cur->data;
            if (!item) return NULL;
            return item->data;
        }
    }

    return NULL;
}

static int
ooDict_set(struct ooDict *self,
           const char *key,
           size_t key_size,
           void *data)
{
    struct ooList *l;
    struct ooListItem *cur;
    struct ooDictItem *item = NULL;
    const char *cur_key;
    size_t cur_key_size;
    unsigned int h;

    if (!key) return oo_FAIL;
    if (!data) return oo_FAIL;

    h = oo_hash(key, key_size) % self->hash->size;
    l = (struct ooList*)self->hash->data[h];
    for (cur = l->head; cur; cur = cur->next) {
        cur_key = ((struct ooDictItem*)cur->data)->key;
        cur_key_size = ((struct ooDictItem*)cur->data)->key_size;
        if (cur_key_size != key_size) continue;
        if (!memcmp(key, cur_key, cur_key_size)) {
            item = (struct ooDictItem*)cur->data;
            break;
        }
    }

    if (item) {
        return oo_CONFLICT;
    }

    item = (struct ooDictItem*)malloc(sizeof(struct ooDictItem));
    if (!item) return oo_NOMEM;

    item->data = data;
    item->key = key;
    item->key_size = key_size;
    l->add(l, (void*)item, NULL);
    self->size++;

    return oo_OK;
}

static int
ooDict_set_by_hash(struct ooDict *self,
                   const char *key,
                   size_t key_size,
                   size_t hash_val,
                   void *data)
{
    struct ooList *l;
    struct ooListItem *cur;
    struct ooDictItem *item = NULL;
    bool got_match = false;
    size_t h;

    h = hash_val % self->hash->size;
    l = (struct ooList*)self->hash->data[h];

    for (cur = l->head; cur; cur = cur->next) {
        item = cur->data;
        if (item->key_size != key_size) continue;
        if (!memcmp(item->key, key, key_size)) {
            got_match = true;
            break;
        }
    }

    if (got_match) {
        item->data = data;
        return oo_OK;
    }

    item = (struct ooDictItem*)malloc(sizeof(struct ooDictItem));
    if (!item) return oo_NOMEM;

    item->data = data;
    item->key = key;
    item->key_size = key_size;
    l->add(l, (void*)item, NULL);
    self->size++;

    return oo_OK;
}


static bool 
ooDict_key_exists(struct ooDict *self,
                  const char *key,
                  size_t key_size)
{
    struct ooDictItem *item = ooDict_find_item(self, key, key_size);
    return (item != NULL);
}

static int
ooDict_remove(struct ooDict *self,
              const char *key,
              size_t key_size)
{
    struct ooList *l;
    const char *cur_key;
    size_t cur_key_size;
    struct ooListItem *cur;
    unsigned int h;
    
    h = self->hash_func(key, key_size) % self->hash->size;
    l = (ooList*)self->hash->data[h];
    
    for (cur = l->head; cur; cur = cur->next) {
        cur_key = ((struct ooDictItem*)cur->data)->key;
        cur_key_size = ((struct ooDictItem*)cur->data)->key_size;

        if (cur_key_size != key_size) continue;
        if (!strncmp(key, cur_key, key_size)) {
            //data = ((struct ooDictItem*)cur->data)->data;

            cur->data = NULL;
            l->remove(l, cur);
            self->size--;
            return oo_OK;
        }
    }

    return oo_FAIL;
}

static void ooDict_del(struct ooDict *self)
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
                free(item);
                cur = cur->next;
            }
        }
        l->del(l);
    }

    self->hash->del(self->hash);

    free(self);
}

static void ooDict_reset(struct ooDict *self)
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
                free(item);
                cur = cur->next;
            }
            l->head = NULL;
            l->size = 0;
        }
    }
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
            h = self->hash_func(item->key, item->key_size) % self->hash->size;
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
    self->reset         = ooDict_reset;
    self->str           = ooDict_str;
    self->remove        = ooDict_remove;
    self->get           = ooDict_get;
    self->set           = ooDict_set;
    self->set_by_hash   = ooDict_set_by_hash;
    self->exists        = ooDict_key_exists;
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
