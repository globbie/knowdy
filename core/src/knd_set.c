#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_facet.h"
#include "knd_output.h"
#include "knd_utils.h"
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

static int
kndSet_sync(struct kndSet *self);
static gsl_err_t confirm_read(void *obj,
			      const char *val __attribute__((unused)),
			      size_t val_size __attribute__((unused)));

static int
kndSet_read_term_idx_tags(struct kndSet *self,
			  const char    *rec,
			  size_t         rec_size);
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

static void
kndSetElem_str(struct kndSet *self, size_t depth)
{
   
}

static void 
kndSet_elem_idx_str(struct kndSet *self,
		    struct kndSetElemIdx *parent_idx, size_t depth)
{
    struct kndSetElem *elem;
    struct kndSetElemIdx *idx;

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
	elem = parent_idx->elems[i];
	if (!elem) continue;
	
	knd_log("%*s => %.*s",
		(depth + 1) * KND_OFFSET_SIZE, "",
		elem->conc_dir->name_size, elem->conc_dir->name);
    }

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
	idx = parent_idx->idxs[i];
	if (!idx) continue;
	
	kndSet_elem_idx_str(self, idx, depth);
    }
}

static void 
kndSet_str(struct kndSet *self, size_t depth)
{
    struct kndFacet *facet;
    struct ooDict *set_name_idx;
    struct kndSet *set;
    struct kndConcDir *conc_dir;
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

    /* name idx */
    if (self->name_idx) {
	key = NULL;
	self->name_idx->rewind(self->name_idx);
	do {
	    self->name_idx->next_item(self->name_idx, &key, &val);
	    if (!key) break;
	    conc_dir = val;
	    knd_log("%*s => %.*s",
		    (depth + 1) * KND_OFFSET_SIZE, "",
		    conc_dir->name_size, conc_dir->name);
	} while (key);
    }

    /* ID idx */
    if (self->idx) {
	kndSet_elem_idx_str(self, self->idx, depth + 1);
    }

    knd_log("%*s}", depth * KND_OFFSET_SIZE, "");
}

static int 
kndSet_elem_idx_JSON(struct kndSet *self,
		     struct kndSetElemIdx *parent_idx)
{
    struct kndSetElem *elem;
    struct kndSetElemIdx *idx;
    struct kndConcept *parent_conc, *c;
    int err;

    parent_conc = self->base->conc;

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
	elem = parent_idx->elems[i];
	if (!elem) continue;
	
	/* match count */
	
	/* separator needed? */
	if (self->task->batch_size) {
	    err = self->out->write(self->out, ",", 1);
	    if (err) return err;
	}
	
	err = parent_conc->get(parent_conc,
			       elem->conc_dir->name,
			       elem->conc_dir->name_size,
			       &c);                                           RET_ERR();
	
	c->format = self->format;
	c->out = self->out;
	c->task = self->task;
	
	err = c->export(c);
	if (err) return err;
	
	self->task->batch_size++;
	
    }

    for (size_t i = 0; i < KND_RADIX_BASE; i++) {
	idx = parent_idx->idxs[i];
	if (!idx) continue;

	err = kndSet_elem_idx_JSON(self, idx);                                RET_ERR();
    }

    return knd_OK;
}

static int 
kndSet_export_JSON(struct kndSet *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    struct kndOutput *out = self->out;
    struct kndFacet *f;
    struct kndSetElem *elem;
    size_t curr_batch_size = 0;
    int err;

    if (DEBUG_SET_LEVEL_1)
        knd_log(".. export set to JSON: "
		" batch size:%zu  batch from:%zu  total elems:%zu",
                self->task->batch_max, self->task->batch_from,
                self->num_elems);

    err = out->write(out,  "{", 1);                                               RET_ERR();
    err = out->write(out, "\"n\":\"", strlen("\"n\":\""));                        RET_ERR();
    err = out->write(out, self->base->name,  self->base->name_size);              RET_ERR();
    err = out->write(out, "\"", 1);                                               RET_ERR();

    buf_size = sprintf(buf, ",\"total\":%lu",
                       (unsigned long)self->num_elems);
    err = out->write(out, buf, buf_size);                             RET_ERR();

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

    if (self->idx) {
        err = out->write(out,
			 ",\"batch\":[", strlen(",\"batch\":["));                 RET_ERR();
	err = kndSet_elem_idx_JSON(self, self->idx);                              RET_ERR();
        err = out->write(out,  "]", 1);                                           RET_ERR();

	buf_size = sprintf(buf, ",\"batch_size\":%lu",
			   (unsigned long)self->task->batch_size);
	err = out->write(out, buf, buf_size);                                     RET_ERR();
        err = out->write(out,
			 ",\"batch_from\":", strlen(",\"batch_from\":"));         RET_ERR();
	buf_size = sprintf(buf, "%lu",
			   (unsigned long)self->task->batch_from);
	err = out->write(out, buf, buf_size);                                     RET_ERR();

        /*err = out->write(out,
			 ",\"batch_max_size\":",
			 strlen(",\"batch_max_size\":"));                         RET_ERR();
	buf_size = sprintf(buf, "%lu",
			   (unsigned long)self->task->batch_max);
	err = out->write(out, buf, buf_size);                                     RET_ERR();
	*/
    }

    err = out->write(out,  "}", 1);
    if (err) return err;
    
    return knd_OK;
}

static int 
kndSet_export_facets_GSP(struct kndSet *self)
{
    struct kndOutput *out = self->out;
    struct kndSet *set;
    struct kndFacet *facet;
    struct ooDict *set_name_idx;
    const char *key;
    void *val;
    int err;

    err = out->write(out,  "[fc ", strlen("[fc "));                             RET_ERR();
    for (size_t i = 0; i < self->num_facets; i++) {
	facet = self->facets[i];
	err = out->write(out,  "{", 1);                                       RET_ERR();
	err = out->write(out, facet->attr->name, facet->attr->name_size);
	err = out->write(out,  " ", 1);                                       RET_ERR();

	if (facet->set_name_idx) {
	    err = out->write(out,  "[set", strlen("[set"));                       RET_ERR();
	    set_name_idx = facet->set_name_idx;
	    key = NULL;
	    set_name_idx->rewind(set_name_idx);
	    do {
		set_name_idx->next_item(set_name_idx, &key, &val);
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
    err = out->write(out,  "]", 1);                                               RET_ERR();

    return knd_OK;
}

static int 
kndSet_export_GSP(struct kndSet *self)
{
    struct kndOutput *out = self->out;
    struct kndSet *set;
    struct kndFacet *facet;
    struct kndConcDir *conc_dir;
    const char *key;
    void *val;
    int err;

    if (self->parent_facet) {
	err = out->write(out,  "{", 1);                                           RET_ERR();
	err = out->write(out, self->base->id, self->base->id_size);               RET_ERR();
	err = out->write(out,  " ", 1);                                           RET_ERR();
    }

    if (self->num_facets) {
	err = kndSet_export_facets_GSP(self);                                   RET_ERR();
    }

    if (self->name_idx) {
	key = NULL;
	self->name_idx->rewind(self->name_idx);
	err = out->write(out,  "[c", strlen("[c"));                       RET_ERR();
	do {
	    self->name_idx->next_item(self->name_idx, &key, &val);
	    if (!key) break;
	    conc_dir = val;
	    err = out->write(out, " ", 1);                                        RET_ERR();
	    err = out->write(out, conc_dir->id, conc_dir->id_size);               RET_ERR();
	    //err = out->write(out, " 1}", strlen(" 1}"));                          RET_ERR();
	} while (key);
	err = out->write(out,  "]", 1);                                           RET_ERR();
    }

    if (self->parent_facet) {
	err = out->write(out,  "}", 1);                                               RET_ERR();
    }

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
kndSet_intersect(struct kndSet   *self,
                    struct kndSet **sets,
                    size_t num_sets)
{
    struct kndSet *smallset, *set;
    struct kndSetElem *elem;
    struct kndElemName_Idx *name_idx, *term_name_idx;
    //struct kndFacet *f;

    int i, j, ri, err;

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
    
    if (!smallset->name_idx) {
        knd_log("  -- no NAME_IDX in %.*s?\n",
                smallset->base->name_size, smallset->base->name);
        return knd_FAIL;
    }
    
    knd_log("  .. traverse the smaller Set \"%s\"..\n",
            smallset->base->name);
    
    /*    for (i = 0; i < KND_ID_BASE; i++) {
        name_idx = base->name_idx[i];
        if (!name_idx) continue;

        for (j = 0; j < KND_ID_BASE; j++) {
            term_name_idx = name_idx->name_idx[j];
            if (!term_name_idx) continue;
            
            for (ri = 0; ri < KND_ID_BASE; ri++) {
                elem = term_name_idx->elems[ri];
                if (!elem) continue;

                err = kndSet_lookup_elem(set, elem);
                if (err) {
                    if (DEBUG_SET_LEVEL_3)
                        knd_log("  -- obj elem %s not found :(\n", elem->id);
                    continue;
                }

                err = kndSet_term_name_idx(self, elem);
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
			  struct kndConcept *base,
			  struct kndSet  *set)
{
    struct kndConcDir *dir = base->dir;
    struct kndConcDir *topic = self->attr->parent_conc->dir;
    struct ooDict *name_idx;
    int err;

    err = ooDict_new(&name_idx, KND_SMALL_DICT_SIZE);                                  RET_ERR();

    err = name_idx->set(name_idx,
		   topic->name, topic->name_size, (void*)set);                    RET_ERR();

    dir->reverse_attr_name_idx = name_idx;
    return knd_OK;
}

static int
kndFacet_alloc_set(struct kndFacet  *self,
		   struct kndConcept *base,
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

    /* TODO: alloc */
    err = ooDict_new(&set->name_idx, KND_MEDIUM_DICT_SIZE);                       RET_ERR();
    set->base = base->dir;
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
    int err;
    
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

    knd_log(".. add attr spec %.*s to topic %.*s..",
	    spec->name_size, spec->name,
	    topic->name_size, topic->name);

    /* get baseclass set */
    err = kndFacet_alloc_set(self, spec, &set);                                     RET_ERR();

    /* add conc ref to a set */
    err = set->name_idx->set(set->name_idx,
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
        knd_log("  .. \"%.*s\" NAME_IDX to add attr ref \"%.*s\" "
		"(topic \"%.*s\" => spec \"%.*s\")",
		self->base->name_size, self->base->name,
		attr->name_size, attr->name,
		topic->name_size, topic->name,
		spec->name_size, spec->name);

    err = kndSet_alloc_facet(self, attr, &f);                                       RET_ERR();

    err = kndFacet_add_ref(f, topic, spec);                                       RET_ERR();

    return knd_OK;
}

static int
kndSet_add_conc(struct kndSet *self,
		struct kndConcept *conc)
{
    struct kndConcDir *dir;
    struct ooDict *name_idx;
    int err;

    name_idx = self->name_idx;

    if (DEBUG_SET_LEVEL_1) {
	knd_log("++ \"%.*s\" set to add conc \"%.*s\" TOTAL descendants: %zu",
		self->base->name_size, self->base->name,
		conc->name_size, conc->name,
		name_idx->size);
    }

    dir = name_idx->get(name_idx, conc->name, conc->name_size);
    if (!dir) {
	err = name_idx->set(name_idx, conc->name, conc->name_size, (void*)conc->dir);       RET_ERR();
    }

    return knd_OK;
}

static int
kndSet_merge_name_idx(struct kndSet *self,
		 struct kndSet *src)
{
    struct kndElemName_Idx *name_idx, *term_name_idx;
    struct kndSetElem *elem;
    size_t i, j, ri;
    int err;

    if (!src->name_idx) {
        if (DEBUG_SET_LEVEL_2)
            knd_log("?? no term name_idx in set %s?", src->base->name);
        return knd_OK;
    }
    
    /*for (i = 0; i < KND_ID_BASE; i++) {
        name_idx = src->name_idx[i];

        if (!name_idx) continue;

        for (j = 0; j < KND_ID_BASE; j++) {
            term_name_idx = name_idx->name_idx[j];
            if (!term_name_idx) continue;

            for (ri = 0; ri < KND_ID_BASE; ri++) {
                elem = term_name_idx->elems[ri];
                if (!elem) continue;

                err = kndSet_term_name_idx(self, elem);
                if (err) return err;
            }
            
        }
	}*/

    
    return knd_OK;
}

static gsl_err_t set_append(void *accu,
			    void *item)
{
    struct kndFacet *self = accu;
    struct kndSet *set = item;
    int err;

    err = self->set_name_idx->set(self->set_name_idx,
    			     set->base->name, set->base->name_size,
			     (void*)set);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_alloc(void *obj,
			   const char *name,
			   size_t name_size,
			   size_t count,
			   void **item)
{
    struct kndFacet *self = obj;
    struct kndSet *set;
    struct kndConcept *conc;
    struct kndConcDir *base;
    int err;

    conc = self->parent->base->conc;

    base = conc->class_idx->get(conc->class_idx, name, name_size);
    if (!base) {
        knd_log("-- no such class: \"%.*s\":(", name_size, name);
        return make_gsl_err_external(knd_NO_MATCH);
    }

    err = self->mempool->new_set(self->mempool, &set);
    if (err) return make_gsl_err_external(err);

    set->type = KND_SET_CLASS;
    set->mempool = self->mempool;
    set->parent_facet = self;
    set->base = base;

    *item = (void*)set;

    return make_gsl_err(gsl_OK);
}

static int add_elem(struct kndSet *self,
		    struct kndSetElemIdx *parent_idx,
		    struct kndSetElem *elem,
		    const char *id,
		    size_t id_size)
{
    struct kndSetElemIdx *idx;
    int idx_pos;
    int err;

    if (DEBUG_SET_LEVEL_2)
	knd_log("== ID remainder: \"%.*s\"", id_size, id);

    idx_pos = obj_id_base[(unsigned int)*id];

    if (id_size > 1) {
	err = self->mempool->new_set_elem_idx(self->mempool, &idx);
	if (err) return err;
	parent_idx->idxs[idx_pos] = idx;
	
	err = add_elem(self, idx, elem, id + 1, id_size - 1);
	if (err) return err;
	parent_idx->num_elems++;
    }

    /* assign elem */
    parent_idx->elems[idx_pos] = elem;
    parent_idx->num_elems++;
    self->num_elems++;

    return knd_OK;
}

static gsl_err_t atomic_elem_alloc(void *obj,
				   const char *val,
				   size_t val_size,
				   size_t count,
				   void **item)
{
    struct kndSet *self = obj;
    struct kndSetElem *elem;
    struct kndSetElemIdx *idx;
    struct ooDict *class_idx;
    struct kndConcDir *conc_dir;
    int idx_pos;
    int err;

    if (DEBUG_SET_LEVEL_2)
	knd_log(".. atomic set elem alloc: \"%.*s\"",
		val_size, val);

    class_idx = self->base->class_idx;
    conc_dir = class_idx->get(class_idx, val, val_size);
    if (!conc_dir) return make_gsl_err_external(knd_NO_MATCH);

    if (!self->idx) {
	err = self->mempool->new_set_elem_idx(self->mempool, &self->idx);
	if (err) return make_gsl_err_external(err);
    }

    idx_pos = obj_id_base[(unsigned int)*val];

    /* add elem to a terminal row of elems */
    err = self->mempool->new_set_elem(self->mempool, &elem);
    if (err) return make_gsl_err_external(err);
    elem->conc_dir = conc_dir;

    if (val_size == 1) {
	self->idx->elems[idx_pos] = elem;
	self->idx->num_elems++;
	self->num_elems++;
	return make_gsl_err(gsl_OK);
    }

    /* add to dirs */
    err = self->mempool->new_set_elem_idx(self->mempool, &idx);
    if (err) return make_gsl_err_external(err);
    self->idx->idxs[idx_pos] = idx;

    err = add_elem(self, idx, elem, val + 1, val_size - 1);
    if (err) return make_gsl_err_external(err);
    self->idx->num_elems++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t atomic_elem_append(void *accu,
					 void *item)
{
    struct kndFacet *self = accu;
    struct kndSet *set = item;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t atomic_elem_parse(void *obj,
				   const char *rec,
				   size_t *total_size)
{
    if (DEBUG_SET_LEVEL_2)
	knd_log(".. parse atomic obj entry: %s [size: %zu]", rec, *total_size);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_read(void *obj,
			  const char *rec,
			  size_t *total_size)
{
    struct kndSet *set = obj;
    struct gslTaskSpec specs[] = {
        { .is_list = true,
	  .is_atomic = true,
          .name = "c",
          .name_size = strlen("c"),
          .accu = set,
          .alloc =  atomic_elem_alloc,
          .append = atomic_elem_append,
          .parse =  atomic_elem_parse
	},
        { .is_default = true,
          .run = confirm_read,
          .obj = set
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t read_facet_name(void *obj,
				 const char *name, size_t name_size,
				 const char *rec, size_t *total_size)
{
    struct kndFacet *self = obj;
    gsl_err_t err;

    if (DEBUG_SET_LEVEL_TMP)
        knd_log(".. read facet name: \"%.*s\"", 16, rec);

    return make_gsl_err(gsl_OK);

}

static gsl_err_t read_facet(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndFacet *f = obj;
    struct gslTaskSpec specs[] = {
        { .is_list = true,
          .name = "set",
          .name_size = strlen("set"),
          .accu = f,
          .alloc = set_alloc,
          .append = set_append,
          .parse = set_read
	},
        { .is_default = true,
          .run = confirm_read,
          .obj = f
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t facet_append(void *accu,
                              void *item)
{
    struct kndSet *self = accu;
    struct kndFacet *f = item;

    self->facets[self->num_facets] = f;
    self->num_facets++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t facet_alloc(void *obj,
                             const char *name,
                             size_t name_size,
                             size_t count,
                             void **item)
{
    struct kndSet *self = obj;
    struct kndConcept *conc;
    struct kndAttr *attr;
    struct kndFacet *f;
    int err;

    if (DEBUG_SET_LEVEL_1)
        knd_log(".. set %.*s to create facet: %.*s count: %zu",
                self->base->name_size, self->base->name, name_size, name, count);

    conc = self->base->conc;
    err = conc->get_attr(conc, name, name_size, &attr);
    if (err) return make_gsl_err_external(err);

    err = self->mempool->new_facet(self->mempool, &f);
    if (err) return make_gsl_err_external(err);

    /* TODO: mempool alloc */
    err = ooDict_new(&f->set_name_idx, KND_MEDIUM_DICT_SIZE);
    if (err) return make_gsl_err_external(err);

    f->attr = attr;
    f->parent = self;
    f->mempool = self->mempool;

    *item = (void*)f;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_read(void *obj,
			      const char *val __attribute__((unused)),
			      size_t val_size __attribute__((unused)))
{
    struct kndFacet *self = obj;
    if (DEBUG_SET_LEVEL_2)
        knd_log(".. confirm read!");
    return make_gsl_err(gsl_OK);
}

static int read_GSP(struct kndSet *self,
                    const char *rec,
                    size_t *total_size)
{
    gsl_err_t parser_err;

    if (DEBUG_SET_LEVEL_1)
        knd_log(".. set reading GSP: \"%.*s\"..", 256, rec);

    struct gslTaskSpec specs[] = {
        { .is_list = true,
          .name = "fc",
          .name_size = strlen("fc"),
          .accu = self,
          .alloc = facet_alloc,
          .append = facet_append,
          .parse = read_facet
        },
        { .is_list = true,
	  .is_atomic = true,
          .name = "c",
          .name_size = strlen("c"),
          .accu =   self,
          .alloc =  atomic_elem_alloc,
          .append = atomic_elem_append,
          .parse =  atomic_elem_parse
	},
        { .is_default = true,
          .run = confirm_read,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}


extern int kndSet_init(struct kndSet *self)
{
    self->str = kndSet_str;
    self->add_conc = kndSet_add_conc;
    self->add_ref = kndSet_add_ref;

    self->read = read_GSP;
    self->intersect = kndSet_intersect;

    self->get_facet = kndSet_get_facet;
    self->facetize = kndSet_facetize;
    self->export = kndSet_export;
}

extern void kndFacet_init(struct kndFacet *self)
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
