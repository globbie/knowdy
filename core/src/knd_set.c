#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_facet.h"
#include "knd_utils.h"
#include "knd_repo.h"
#include "knd_mempool.h"
#include "knd_set.h"
#include "knd_object.h"
#include "knd_attr.h"
#include "knd_elem.h"
#include "knd_task.h"

#include <gsl-parser.h>
#include <glb-lib/output.h>

#define DEBUG_SET_LEVEL_0 0
#define DEBUG_SET_LEVEL_1 0
#define DEBUG_SET_LEVEL_2 0
#define DEBUG_SET_LEVEL_3 0
#define DEBUG_SET_LEVEL_4 0
#define DEBUG_SET_LEVEL_TMP 1

static int 
knd_compare_set_by_size_ascend(const void *a,
                                   const void *b)
{
    struct kndSet **obj1, **obj2;

    obj1 = (struct kndSet**)a;
    obj2 = (struct kndSet**)b;

    if ((*obj1)->num_elems == (*obj2)->num_elems) return 0;

    if ((*obj1)->num_elems > (*obj2)->num_elems) return 1;

    return -1;
}

static void 
kndSet_str(struct kndSet *self, size_t depth)
{
    struct kndFacet *facet;
    struct ooDict *set_name_idx;
    struct kndSet *set;
    const char *key;
    void *val;

    knd_log("%*s{set %.*s [total:%zu]", depth * KND_OFFSET_SIZE, "",
            self->base->name_size, self->base->name, self->num_elems);

    for (size_t i = 0; i < self->num_facets; i++) {
        facet = self->facets[i];
        knd_log("%*s[%.*s",
                (depth + 1) * KND_OFFSET_SIZE, "",
                facet->attr->name_size, facet->attr->name);
        if (facet->set_name_idx) {
            set_name_idx = facet->set_name_idx;
            key = NULL;
            set_name_idx->rewind(set_name_idx);
            do {
                set_name_idx->next_item(set_name_idx, &key, &val);
                if (!key) break;
                set = (struct kndSet*)val;
                set->str(set, depth + 2);
            } while (key);
        }

        knd_log("%*s]",
                (depth + 1) * KND_OFFSET_SIZE, "");
        knd_log("%*s}", depth * KND_OFFSET_SIZE, "");
    }

    knd_log("%*s}", depth * KND_OFFSET_SIZE, "");
}


static int
kndSet_intersect(struct kndSet   *self __attribute__((unused)),
                 struct kndSet **sets,
                 size_t num_sets)
{
    struct kndSet *smallset, *set;

    if (DEBUG_SET_LEVEL_2) 
        knd_log(" .. intersection by Set \"%.*s\"..\n",
                smallset->base->name_size, smallset->base->name);

    /* sort sets by size */
    qsort(sets,
          num_sets,
          sizeof(struct kndSet*),
          knd_compare_set_by_size_ascend);

    /* the smallest set */
    smallset = sets[0];
    set = sets[1];
        
    knd_log("  .. traverse the smaller Set \"%s\"..\n",
            smallset->base->name);


    
    return knd_OK;
}


static int
kndSet_get_facet(struct kndSet  *self,
                 struct kndAttr *attr,
                 struct kndFacet  **result)
{
    struct kndFacet *f;    
    for (size_t i = 0; i < self->num_facets; i++) {
        f = self->facets[i];
        if (f->attr == attr) {
            *result = f;
            return knd_OK;
        }
    }
    return knd_NO_MATCH;
}

static int
kndSet_alloc_facet(struct kndSet  *self,
                   struct kndAttr *attr,
                   struct kndFacet  **result)
{
    struct kndFacet *f;
    int err;
    
    for (size_t i = 0; i < self->num_facets; i++) {
        f = self->facets[i];
        if (f->attr == attr) {
            *result = f;
            return knd_OK;
        }
    }

    if (self->num_facets >= KND_MAX_ATTRS)
        return knd_LIMIT;

    /* facet not found, create one */
    err = self->mempool->new_facet(self->mempool, &f);                                 RET_ERR();

    /* TODO: mempool alloc */
    err = ooDict_new(&f->set_name_idx, KND_MEDIUM_DICT_SIZE);                          RET_ERR();
    f->attr = attr;
    f->parent = self;
    f->mempool = self->mempool;

    self->facets[self->num_facets] = f;
    self->num_facets++;

    *result = f;
    return knd_OK;
}


static int
kndFacet_add_reverse_link(struct kndFacet  *self,
                          struct kndConcDir *base,
                          struct kndSet  *set)
{
    struct kndConcDir *topic = self->attr->parent_conc->dir;
    struct ooDict *name_idx;
    int err;

    err = ooDict_new(&name_idx, KND_SMALL_DICT_SIZE);                                  RET_ERR();

    err = name_idx->set(name_idx,
                        topic->name, topic->name_size, (void*)set);                    RET_ERR();

    base->reverse_attr_name_idx = name_idx;

    return knd_OK;
}

static int
kndFacet_alloc_set(struct kndFacet  *self,
                   struct kndConcDir *base,
                   struct kndSet  **result)
{
    struct kndSet *set;
    int err;

    set = self->set_name_idx->get(self->set_name_idx,
                                  base->name, base->name_size);
    if (set) {
        *result = set;
        return knd_OK;
    }

    err = self->mempool->new_set(self->mempool, &set);                            RET_ERR();
    set->type = KND_SET_CLASS;

    set->base = base;
    set->mempool = self->mempool;
    set->parent_facet = self;

    err = self->set_name_idx->set(self->set_name_idx,
                                  base->name, base->name_size, (void*)set);       RET_ERR();

    err = kndFacet_add_reverse_link(self, base, set);                             RET_ERR();

    *result = set;
    return knd_OK;
}

static int
kndSet_facetize(struct kndSet *self)
{

    if (DEBUG_SET_LEVEL_1) {
        knd_log("\n    .. further facetize the set \"%s\"..\n",
                self->base->name);
    }

    return knd_OK;
}

static int
kndFacet_add_ref(struct kndFacet *self,
                 struct kndConcDir *topic,
                 struct kndConcDir *spec)
{
    struct kndSet *set;
    int err;

    if (DEBUG_SET_LEVEL_TMP) {
        knd_log(".. add attr spec \"%.*s\" to topic \"%.*s\"..",
                spec->name_size, spec->name,
                topic->name_size, topic->name);
    }

    /* get baseclass set */
    err = kndFacet_alloc_set(self, spec, &set);                                   RET_ERR();


    err = set->add(set, topic->id, topic->id_size, (void*)topic);                 RET_ERR();

    return knd_OK;
}

static int
kndSet_add_ref(struct kndSet *self,
               struct kndAttr *attr,
               struct kndConcDir *topic,
               struct kndConcDir *spec)
{
    struct kndFacet *f;
    int err;

    if (DEBUG_SET_LEVEL_TMP)
        knd_log("  .. \"%.*s\" NAME_IDX to add attr ref \"%.*s\" "
                "(topic \"%.*s\" => spec \"%.*s\")",
                self->base->name_size, self->base->name,
                attr->name_size, attr->name,
                topic->name_size, topic->name,
                spec->name_size, spec->name);

    err = kndSet_alloc_facet(self, attr, &f);                                     RET_ERR();

    err = kndFacet_add_ref(f, topic, spec);                                       RET_ERR();

    return knd_OK;
}

static int save_elem(struct kndSet *self,
                     struct kndSetElemIdx *parent_idx,
                     void *elem,
                     const char *id,
                     size_t id_size)
{
    struct kndSetElemIdx *idx;
    int idx_pos;
    int err;

    if (DEBUG_SET_LEVEL_2) {
        knd_log("== set idx to save ID remainder: \"%.*s\" elem:%p",
                id_size, id, elem);
    }

    idx_pos = obj_id_base[(unsigned int)*id];
    if (id_size > 1) {
        idx = parent_idx->idxs[idx_pos];
        if (!idx) {
            err = self->mempool->new_set_elem_idx(self->mempool, &idx);
            if (err) {
                knd_log("-- set elem idx mempool limit reached :(");
                return err;
            }
            parent_idx->idxs[idx_pos] = idx;
        }

        err = save_elem(self, idx, elem, id + 1, id_size - 1);
        if (err) return err;

        return knd_OK;
    }

    /* assign elem */
    parent_idx->elems[idx_pos] = elem;
    self->num_elems++;

    return knd_OK;
}

static int get_elem(struct kndSet *self,
                    struct kndSetElemIdx *parent_idx,
                    void **result,
                    const char *id,
                    size_t id_size)
{
    struct kndSetElemIdx *idx;
    void *elem;
    int idx_pos;
    int err;

    idx_pos = obj_id_base[(unsigned int)*id];

    if (DEBUG_SET_LEVEL_2)
        knd_log(".. get elem by ID, remainder \"%.*s\" POS:%d  idx:%p",
                id_size, id, idx_pos, parent_idx);

    if (id_size > 1) {
        idx = parent_idx->idxs[idx_pos];
        if (!idx) return knd_NO_MATCH;

        err = get_elem(self, idx, result, id + 1, id_size - 1);
        if (err) return err;

        return knd_OK;
    }

    elem = parent_idx->elems[idx_pos];
    if (!elem) {
        return knd_NO_MATCH;
    }

    *result = elem;

    return knd_OK;
}

static int kndSet_add_elem(struct kndSet *self,
                           const char *key,
                           size_t key_size,
                           void *elem)
{
    int err;

    err = save_elem(self, &self->idx, elem, key, key_size);
    if (err) return err;

    return knd_OK;
}

static int kndSet_get_elem(struct kndSet *self,
                           const char *key,
                           size_t key_size,
                           void **elem)
{
    int err;

    err = get_elem(self, &self->idx, elem, key, key_size);
    if (err) return err;

    return knd_OK;
}

static int kndSet_traverse_idx(struct kndSetElemIdx *parent_idx,
                               map_cb_func cb,
                               void *obj,
                               size_t *count)
{
    char buf[KND_ID_SIZE];
    size_t buf_size = 0;
    struct kndSetElemIdx *idx;
    void *elem;
    int err;

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        elem = parent_idx->elems[i];
        if (!elem) continue;
        buf_size = 0;
        buf[buf_size] = obj_id_seq[i];
        buf_size = 1;

        err = cb(obj, buf, buf_size, *count, elem);
        if (err) return err;
        (*count)++;
    }

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
        idx = parent_idx->idxs[i];
        if (!idx) continue;

        err = kndSet_traverse_idx(idx, cb, obj, count);
        if (err) return err;
    }

    return knd_OK;
}

static int kndSet_map(struct kndSet *self,
                      map_cb_func cb,
                      void *obj)
{
    size_t count = 0;
    int err;

    err = kndSet_traverse_idx(&self->idx, cb, obj, &count);
    if (err) return err;

    return knd_OK;
}

extern int kndSet_init(struct kndSet *self)
{
    self->str = kndSet_str;
    self->add = kndSet_add_elem;
    self->get = kndSet_get_elem;
    self->map = kndSet_map;
    self->add_ref = kndSet_add_ref;
    self->intersect = kndSet_intersect;

    self->get_facet = kndSet_get_facet;
    self->facetize = kndSet_facetize;

    return knd_OK;
}

extern int 
kndSet_new(struct kndSet **set)
{
    struct kndSet *self = malloc(sizeof(struct kndSet));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndSet));

    kndSet_init(self);
    *set = self;

    return knd_OK;
}
