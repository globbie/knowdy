#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_facet.h"
#include "knd_output.h"
#include "knd_repo.h"
#include "knd_mempool.h"
#include "knd_set.h"
#include "knd_object.h"
#include "knd_attr.h"
#include "knd_elem.h"
#include "knd_task.h"

#include <gsl-parser.h>

#define DEBUG_SET_LEVEL_0 0
#define DEBUG_SET_LEVEL_1 0
#define DEBUG_SET_LEVEL_2 0
#define DEBUG_SET_LEVEL_3 0
#define DEBUG_SET_LEVEL_4 0
#define DEBUG_SET_LEVEL_TMP 1

//static int
//kndSet_sync(struct kndSet *self);

//static int
//kndSet_read_term_idx_tags(struct kndSet *self,
//  const char    *rec,
//  size_t         rec_size);

static int
kndSet_add_conc(struct kndSet     *self,
		struct kndConcept     *conc);

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

/*
static void
kndSetElem_str(struct kndSet *self, size_t depth)
{
    (void) self, (void) depth;
}
*/

static void 
kndSet_str(struct kndSet *self, size_t depth)
{
    struct kndFacet *facet;
    struct ooDict *set_idx;
    struct kndSet *set;
    struct kndConcDir *conc_dir;
    const char *key;
    void *val;

    knd_log("%*s{set %.*s [total:%zu]", depth * KND_OFFSET_SIZE, "",
	    self->base->name_size, self->base->name, self->idx->size);

    for (size_t i = 0; i < self->num_facets; i++) {
	facet = self->facets[i];
	knd_log("%*s[%.*s",
		(depth + 1) * KND_OFFSET_SIZE, "",
		facet->attr->name_size, facet->attr->name);

	if (facet->set_idx) {
	    set_idx = facet->set_idx;
	    key = NULL;
	    set_idx->rewind(set_idx);
	    do {
		set_idx->next_item(set_idx, &key, &val);
		if (!key) break;
		set = (struct kndSet*)val;
		set->str(set, depth + 2);
	    } while (key);
	}

	knd_log("%*s]",
		(depth + 1) * KND_OFFSET_SIZE, "");
	knd_log("%*s}", depth * KND_OFFSET_SIZE, "");
    }

    if (self->idx) {
	key = NULL;
	self->idx->rewind(self->idx);
	do {
	    self->idx->next_item(self->idx, &key, &val);
	    if (!key) break;
	    conc_dir = val;
	    knd_log("%*s => %.*s",
		    (depth + 1) * KND_OFFSET_SIZE, "",
		    conc_dir->name_size, conc_dir->name);
	} while (key);
    }
    
    knd_log("%*s}", depth * KND_OFFSET_SIZE, "");
}

static int 
kndSet_export_JSON(struct kndSet *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    struct kndOutput *out = self->out;

    struct kndSetElem *elem;
    size_t curr_batch_size = 0;
    int err;

    err = out->write(out,  "{", 1);
    if (err) return err;

    buf_size = sprintf(buf,
                       "\"n\":\"%s\"",
                       self->base->name);
    err = out->write(out,
                     buf, buf_size);
    if (err) return err;

    buf_size = sprintf(buf,
                       ",\"tot\":%lu",
                       (unsigned long)self->num_elems);
    err = out->write(out, buf, buf_size);
    if (err) return err;

    /*if (self->summaries_size) {
        err = out->write(out,  ",", 1);
        if (err) return err;
        err = out->write(out,  
                         self->summaries, self->summaries_size);
        if (err) return err;
    }*/
  
    if (self->inbox_size) {
        buf_size = sprintf(buf,
                           ",\"inbox\":{\"tot\":\"%lu\",\"elems\":[",
                           (unsigned long)self->inbox_size);
        err = out->write(out,  
                          buf, buf_size);
        if (err) return err;


        for (size_t i = 0; i < self->inbox_size; i++) {
            elem = self->inbox[i];
            if (i) {
                err = out->write(out,  ",", 1);
                if (err) return err;
            }

            
            //err = elem->export(elem, KND_FORMAT_JSON);
            //if (err) return err;
        }
        
        err = out->write(out,  "]}", 2);
        curr_batch_size = self->inbox_size;
    }
    
    //if ((depth + 1) > KND_SET_MAX_DEPTH) 
    //    goto final;

    /*if ((depth + 1) > self->export_depth) {
        if (DEBUG_SET_LEVEL_3) 
            knd_log("  -- max depth reached in \"%s\": %lu of %lu\n",
                    self->base->name,
                    (unsigned long)depth,
                    (unsigned long)self->export_depth);

        goto final;
	}*/
    
    if (self->num_facets) {
        /* apply proper sorting */
        /*qsort(self->set_elems,
              self->num_sets,
              sizeof(struct kndSet*),
              knd_compare_set_by_alph_ascend); */

        err = out->write(out,  
                          ",\"facets\":[", strlen(",\"facets\":["));
        if (err) return err;

        /*for (size_t i = 0; i < self->num_facets; i++) {
            f = self->facets[i];

            if (i) {
                err = out->write(out,  ",", 1);
                if (err) return err;
            }
            
            f->out = out;
            err = f->export(f, KND_FORMAT_JSON, depth + 1);
            if (err) return err;
	    }*/

        err = out->write(out,  "]", 1);
        if (err) return err;
    }

    
    err = out->write(out,  "}", 1);
    if (err) return err;
    
    return knd_OK;
}


static int 
kndSet_export_GSP(struct kndSet *self)
{
    struct kndOutput *out = self->out;
    struct kndSet *set;
    struct kndFacet *facet;
    struct kndConcDir *conc_dir;
    struct ooDict *set_idx;
    const char *key;
    void *val;
    int err;

    err = out->write(out,  "{_set ", strlen("{_set "));                           RET_ERR();
    err = out->write(out, self->base->name, self->base->name_size);               RET_ERR();

    for (size_t i = 0; i < self->num_facets; i++) {
	facet = self->facets[i];

	err = out->write(out,  "[fc", strlen("[fc"));                             RET_ERR();

	err = out->write(out,  "{", 1);                                           RET_ERR();
	err = out->write(out, facet->attr->name, facet->attr->name_size);

	if (facet->set_idx) {
	    err = out->write(out,  "[set", strlen("[set"));                       RET_ERR();
	
	    set_idx = facet->set_idx;
	    key = NULL;
	    set_idx->rewind(set_idx);
	    do {
		set_idx->next_item(set_idx, &key, &val);
		if (!key) break;
		set = (struct kndSet*)val;
		set->out = self->out;
		set->format = self->format;
		err = set->export(set);                                           RET_ERR();
	    } while (key);
	    err = out->write(out,  "]", 1);                                       RET_ERR();
	}
	err = out->write(out,  "}", 1);                                           RET_ERR();
    }

    if (self->idx) {
	key = NULL;
	self->idx->rewind(self->idx);
	err = out->write(out,  "[_elem", strlen("[_elem"));                       RET_ERR();
	do {
	    self->idx->next_item(self->idx, &key, &val);
	    if (!key) break;
	    conc_dir = val;
	    err = out->write(out, " ", 1);                                        RET_ERR();
	    err = out->write(out, conc_dir->id, conc_dir->id_size);               RET_ERR();
	} while (key);
	err = out->write(out,  "]", 1);                                           RET_ERR();
    }
    err = out->write(out,  "}", 1);                                               RET_ERR();

    return knd_OK;
}

static int 
kndSet_export(struct kndSet *self)
{
    int err;
    
    switch(self->format) {
    case KND_FORMAT_JSON:
        err = kndSet_export_JSON(self);
        if (err) return err;
        break;
    case KND_FORMAT_GSP:
        err = kndSet_export_GSP(self);
        if (err) return err;
        break;
    default:
        break;
    }
    
    return knd_OK;
}


static int
kndSet_intersect(struct kndSet   *self __attribute__((unused)),
                    struct kndSet **sets,
                    size_t num_sets)
{
    struct kndSet *smallset, *set;
    //struct kndFacet *f;

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
    
    if (!smallset->idx) {
        knd_log("  -- no IDX in %.*s?\n",
                smallset->base->name_size, smallset->base->name);
        return knd_FAIL;
    }
    
    knd_log("  .. traverse the smaller Set \"%s\"..\n",
            smallset->base->name);
    
    /*    for (i = 0; i < KND_ID_BASE; i++) {
        idx = base->idx[i];
        if (!idx) continue;

        for (j = 0; j < KND_ID_BASE; j++) {
            term_idx = idx->idx[j];
            if (!term_idx) continue;
            
            for (ri = 0; ri < KND_ID_BASE; ri++) {
                elem = term_idx->elems[ri];
                if (!elem) continue;

                err = kndSet_lookup_elem(set, elem);
                if (err) {
                    if (DEBUG_SET_LEVEL_3)
                        knd_log("  -- obj elem %s not found :(\n", elem->id);
                    continue;
                }

                err = kndSet_term_idx(self, elem);
                if (err) return err;

                self->num_elems++;
            }
            
        }
    }
    */
    
    return knd_OK;
}


static int
kndSet_get_facet(struct kndSet  *self,
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
    err = self->mempool->new_facet(self->mempool, &f);                            RET_ERR();
    /* TODO: mempool alloc */
    err = ooDict_new(&f->set_idx, KND_MEDIUM_DICT_SIZE);                          RET_ERR();
    f->attr = attr;
    f->parent = self;
    f->mempool = self->mempool;

    self->facets[self->num_facets] = f;
    self->num_facets++;

    *result = f;
    return knd_OK;
}

static int
kndFacet_get_set(struct kndFacet  *self,
		 struct kndConcept *base,
		 struct kndSet  **result)
{
    struct kndSet *set;
    int err;

    set = self->set_idx->get(self->set_idx,
			     base->name, base->name_size);
    if (set) {
	*result = set;
	return knd_OK;
    }

    err = self->mempool->new_set(self->mempool, &set);                            RET_ERR();
    set->type = KND_SET_CLASS;

    /* TODO: alloc */
    err = ooDict_new(&set->idx, KND_MEDIUM_DICT_SIZE);                            RET_ERR();
    set->base = base->dir;
    set->mempool = self->mempool;
    set->parent_facet = self;

    err = self->set_idx->set(self->set_idx,
			     base->name, base->name_size, (void*)set);           RET_ERR();

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
	       struct kndConcept *topic,
	       struct kndConcept *spec)
{
    struct kndSet *set;
    int err;

    knd_log(".. add spec %.*s to topic %.*s..",
	    spec->name_size, spec->name,
	    topic->name_size, topic->name);

    /* get baseclass set */
    err = kndFacet_get_set(self, spec, &set);                                     RET_ERR();

    /* add conc ref to a set */
    err = set->idx->set(set->idx,
			topic->name, topic->name_size, (void*)topic->dir);        RET_ERR();
    return knd_OK;
}

static int
kndSet_add_ref(struct kndSet *self,
	       struct kndAttr *attr,
	       struct kndConcept *topic,
	       struct kndConcept *spec)
{
    struct kndFacet *f;
    int err;

    if (DEBUG_SET_LEVEL_TMP)
        knd_log("  .. \"%.*s\" IDX to add attr ref \"%.*s\" "
		"(topic \"%.*s\" => spec \"%.*s\")",
		self->base->name_size, self->base->name,
		attr->name_size, attr->name,
		topic->name_size, topic->name,
		spec->name_size, spec->name);

    err = kndSet_get_facet(self, attr, &f);                                       RET_ERR();

    err = kndFacet_add_ref(f, topic, spec);                                       RET_ERR();

    return knd_OK;
}

static int
kndSet_add_conc(struct kndSet *self,
		struct kndConcept *conc)
{
    struct kndConcDir *dir;
    struct ooDict *idx;
    int err;

    idx = self->idx;

    if (DEBUG_SET_LEVEL_1) {
	knd_log("++ \"%.*s\" set to add conc \"%.*s\" TOTAL descendants: %zu",
		self->base->name_size, self->base->name,
		conc->name_size, conc->name,
		idx->size);
    }

    dir = idx->get(idx, conc->name, conc->name_size);
    if (!dir) {
	err = idx->set(idx, conc->name, conc->name_size, (void*)conc->dir);       RET_ERR();
    }

    return knd_OK;
}

/*
static int
kndSet_merge_idx(struct kndSet *self __attribute__((unused)),
		 struct kndSet *src)
{
    if (!src->idx) {
        if (DEBUG_SET_LEVEL_2)
            knd_log("?? no term idx in set %s?", src->base->name);
        return knd_OK;
    }
    
    for (i = 0; i < KND_ID_BASE; i++) {
        idx = src->idx[i];

        if (!idx) continue;

        for (j = 0; j < KND_ID_BASE; j++) {
            term_idx = idx->idx[j];
            if (!term_idx) continue;

            for (ri = 0; ri < KND_ID_BASE; ri++) {
                elem = term_idx->elems[ri];
                if (!elem) continue;

                err = kndSet_term_idx(self, elem);
                if (err) return err;
            }
            
        }
	}

    
    return knd_OK;
}
*/

extern int kndSet_init(struct kndSet *self)
{
    self->max_inbox_size = KND_SET_INBOX_SIZE;
    self->str = kndSet_str;
    self->add_conc = kndSet_add_conc;
    self->add_ref = kndSet_add_ref;

    self->intersect = kndSet_intersect;
    self->facetize = kndSet_facetize;
    self->export = kndSet_export;

    return knd_OK;
}

extern void kndFacet_init(struct kndFacet *self __attribute__((unused)))
{
    /*self->del = kndFacet_del;
    self->str = kndFacet_str;
    */

    /*self->find = kndFacet_find;
    self->extract_objs = kndFacet_extract_objs;

    self->read = kndFacet_read;
    self->read_tags = kndFacet_read_tags;

    self->export = kndFacet_export;
    self->sync = kndFacet_sync; */
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
