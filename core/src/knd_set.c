#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_facet.h"
#include "knd_output.h"
#include "knd_repo.h"
#include "knd_sorttag.h"
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

static int
kndSet_read_term_idx_tags(struct kndSet *self,
			  const char    *rec,
			  size_t         rec_size);
/*static int
kndSet_export_item(struct kndSetElem *item,
                       char *output,
                       size_t *output_size);
*/

static int
kndSet_add_elem(struct kndSet     *self,
                  struct kndSetElem     *elem);

static int
kndSet_term_idx(struct kndSet *self,
                   struct kndSetElem *elem);
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
static int 
knd_compare_set_by_size_descend(const void *a,
                                   const void *b)
{
    struct kndSet **obj1, **obj2;

    obj1 = (struct kndSet**)a;
    obj2 = (struct kndSet**)b;

    if ((*obj1)->num_elems == (*obj2)->num_elems) return 0;

    if ((*obj1)->num_elems < (*obj2)->num_elems) return 1;

    return -1;
}
*/

static void
kndSet_del(struct kndSet *self)
{
    struct kndFacet *f;
    struct kndSetElem *elem;
    struct kndElemIdx *idx, *term_idx;
    size_t i, j, ri;

    for (i = 0; i < self->num_facets; i++) {
        f = self->facets[i];
        f->del(f);
    }

    /* terminal IDX */
    /*
    if (self->idx) {
        for (i = 0; i < KND_ID_BASE; i++) {
            idx = self->idx[i];
            if (!idx) continue;
            for (j = 0; j < KND_ID_BASE; j++) {
                term_idx = idx->idx[j];
                if (!term_idx) continue;

                for (ri = 0; ri < KND_ID_BASE; ri++) {
                    elem = term_idx->elems[ri];
                    if (!elem) continue;

                    //elem->del(elem);
                }
                free(term_idx);
            }
            free(idx);
        }
        free(self->idx);
    }
    */
    
    /* clean up the inbox */
    for (i = 0; i < self->inbox_size; i++) {
        elem = self->inbox[i];
        if (!elem) continue;
        
        //elem->del(elem);
        self->inbox[i] = NULL;
    }

    free(self);
}

static void
kndSetElem_str(struct kndSet *self, size_t depth)
{
    
}

static void 
kndSet_str(struct kndSet *self, size_t depth, size_t max_depth)
{
    struct kndSetElem *elem;
    struct kndFacet *f;

    char buf[KND_NAME_SIZE];
    //size_t buf_size;
    
    struct kndElemIdx *idx, *term_idx;
    size_t i, j, ri, offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;

    char *offset = malloc(offset_size + 1);
    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    if (depth > max_depth) return;
    
    knd_log("\n%s|__\"%s\" [in:%lu facets:%lu TOTAL: %lu]\n",
            offset, self->name,
            (unsigned long)self->inbox_size,
            (unsigned long)self->num_facets,
            (unsigned long)self->num_elems);

    for (i = 0; i < self->inbox_size; i++) {
        elem = self->inbox[i];
        kndSetElem_str(elem, depth + 1);
    }

    for (i = 0; i < self->num_facets; i++) {
        f = self->facets[i];
        f->str(f, depth, max_depth);
    }

    
    /* terminal IDX */
    if (self->idx) {
        if ((depth + 1) > max_depth) return;

	/*
        for (i = 0; i < KND_ID_BASE; i++) {
            idx = self->idx[i];
            if (!idx) continue;

            knd_log("%s%lu [tot:%lu]\n", offset,
                    (unsigned long)i, (unsigned long)idx->num_elems);

            for (j = 0; j < KND_ID_BASE; j++) {
                term_idx = idx->idx[j];
                if (!term_idx) continue;

                knd_log("%s%s|_%lu [tot:%lu]\n", offset, offset,
                        (unsigned long)j,
                        (unsigned long)idx->num_elems);

                for (ri = 0; ri < KND_ID_BASE; ri++) {
                    elem = term_idx->elems[ri];
                    if (!elem) continue;

                    kndSetElem_str(elem, depth + 1);
                }
            
            }
	    }*/
    }
}

static int 
kndSet_export_JSON(struct kndSet *self,
                       size_t depth)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    struct kndOutput *out = self->out;

    struct kndFacet *f;
    struct kndSetElem *elem;
    size_t curr_batch_size = 0;
    int err;
    
    if (DEBUG_SET_LEVEL_3)
        knd_log("  .. jsonize set \"%s\" depth: %lu\n", self->name,
                (unsigned long)depth);

    err = out->write(out,  "{", 1);
    if (err) return err;

    buf_size = sprintf(buf,
                       "\"n\":\"%s\"",
                       self->name);
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
    
    if ((depth + 1) > KND_SET_MAX_DEPTH) 
        goto final;

    /*if ((depth + 1) > self->export_depth) {
        if (DEBUG_SET_LEVEL_3) 
            knd_log("  -- max depth reached in \"%s\": %lu of %lu\n",
                    self->name,
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

        for (size_t i = 0; i < self->num_facets; i++) {
            f = self->facets[i];

            if (i) {
                err = out->write(out,  ",", 1);
                if (err) return err;
            }
            
            f->out = out;
            //f->export_depth = self->export_depth;
            err = f->export(f, KND_FORMAT_JSON, depth + 1);
            if (err) return err;
        }

        err = out->write(out,  "]", 1);
        if (err) return err;
    }

 final:
    
    err = out->write(out,  "}", 1);
    if (err) return err;
    
    return knd_OK;
}


static int 
kndSet_export(struct kndSet *self,
                  knd_format         format,
                  size_t depth)
{
    int err;
    
    switch(format) {
    case KND_FORMAT_JSON:
        err = kndSet_export_JSON(self, depth);
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
    struct kndSet *base, *set;
    struct kndSetElem *elem;
    struct kndElemIdx *idx, *term_idx;
    //struct kndFacet *f;

    int i, j, ri, err;

    if (DEBUG_SET_LEVEL_2) 
        knd_log(" .. intersection by Set \"%s\"..\n",
                self->name);

    /* sort sets by size */
    qsort(sets,
          num_sets,
          sizeof(struct kndSet*),
          knd_compare_set_by_size_ascend);

    /* the smallest set */
    base = sets[0];
    set = sets[1];
    
    if (!base->idx) {
        knd_log("  -- no IDX in %s?\n",
                base->name);
        return knd_FAIL;
    }
    
    knd_log("  .. traverse base Set \"%s\"..\n",
            base->name);
    
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
                   const char       *facet_name,
                   size_t            facet_name_size,
                   struct kndFacet  **result)
{
    struct kndFacet *f;
    int err;
    
    for (size_t i = 0; i < self->num_facets; i++) {
        f = self->facets[i];

        if (!strcmp(f->name, facet_name)) {
            *result = f;
            return knd_OK;
        }
    }

    if (self->num_facets + 1 >= KND_MAX_ATTRS)
        return knd_NOMEM;
        
    /* facet not found, create one */
    /* TODO: mempool
   err = kndFacet_new(&f);
    if (err) return err;
    */
    
    memcpy(f->name, facet_name, facet_name_size);
    f->name[facet_name_size] = '\0';
    f->name_size = facet_name_size;
    
    //f->base = self->base;
    f->parent = self;
    f->out = self->out;
    
    self->facets[self->num_facets] = f;
    self->num_facets++;
    
    *result = f;

    
    return knd_OK;
}

static int
kndSet_term_idx(struct kndSet *self,
                   struct kndSetElem *elem)
{
    struct kndElemIdx *idx, *term_idx, **idxs;
    struct kndSetElem **elems;
    
    int numval = -1;
    
    if (DEBUG_SET_LEVEL_2) 
        knd_log(".. term idx: %p", elem);

    if (!elem->id_size) return knd_FAIL;
    
    if (DEBUG_SET_LEVEL_2) 
        knd_log("   .. add term idx for obj \"%s\" [%lu]",
                elem->id, (unsigned long)elem->id_size);

    if (!self->idx) {
        self->idx = malloc(sizeof(struct kndElemIdx*) * (KND_ID_BASE + 1));
        if (!self->idx)
            return knd_NOMEM;
        memset(self->idx, 0, sizeof(struct kndElemIdx*) * (KND_ID_BASE + 1));
    }
    
    /* first numeric position */
    //numval = id_base[(size_t)elem->id[0]];
    //if (numval == -1) return knd_FAIL;

    if (DEBUG_SET_LEVEL_3) 
        knd_log("   == term idx 0 pos: %d\n", numval);
    
    idx = self->idx[numval];
    if (!idx) {
        idx = malloc(sizeof(struct kndElemIdx));
        if (!idx) 
            return knd_NOMEM;

        memset(idx, 0, sizeof(struct kndElemIdx));
        self->idx[numval] = idx;
    }

    idx->num_elems++;
    
    idxs = idx->idx;
    if (!idxs) {
        idxs = malloc(sizeof(struct kndElemIdx*) * (KND_ID_BASE + 1));
        if (!idxs)
            return knd_NOMEM;

        memset(idxs, 0, sizeof(struct kndElemIdx*) * (KND_ID_BASE + 1));
        idx->idx = idxs;
    }

    /* second numeric position */
    //numval = id_base[(size_t)elem->id[1]];
    //if (numval == -1) return knd_FAIL;

    if (DEBUG_SET_LEVEL_3) 
        knd_log("== term idx 1 pos: %d\n", numval);

    term_idx = idxs[numval];
    if (!term_idx) {
        term_idx = malloc(sizeof(struct kndElemIdx));
        if (!term_idx) 
            return knd_NOMEM;

        memset(term_idx, 0, sizeof(struct kndElemIdx));
        idxs[numval] = term_idx;
    }

    term_idx->num_elems++;
    
    if (!term_idx->elems) {
        elems = malloc(sizeof(struct kndSetElem*) * (KND_ID_BASE + 1));
        if (!elems)
            return knd_NOMEM;

        memset(elems, 0, sizeof(struct kndSetElem*) * (KND_ID_BASE + 1));
        term_idx->elems = elems;
    }

    /* LAST numeric position */
    //numval = id_base[(size_t)elem->id[2]];
    //if (numval == -1) return knd_FAIL;

    if (DEBUG_SET_LEVEL_3) 
        knd_log("  == term idx 2 pos: %d\n", numval);

    term_idx->elems[numval] = elem;
    
    return knd_OK;
}



static int
kndSet_facetize_elem(struct kndSet *self,
                       struct kndSetElem *elem)
{
    struct kndFacet *f;
    struct kndSortAttr *attr;
    int err = knd_FAIL;
    
    if (DEBUG_SET_LEVEL_2) {
        knd_log("\n    .. passing ELEM to facets of set \"%s\"",
                self->name);
    }

    /*if (!elem->sorttag->num_attrs) {
        if (DEBUG_SET_LEVEL_2)
            knd_log("   -- no more attrs in ELEM: add elem to term IDX!\n");

        err = kndSet_term_idx(self, elem);
        if (err) {
            knd_log("   -- term idx failed to add elem \"%s\" :(\n",
                    elem->id);
            return err;
        }
        
        return knd_OK;
    }
    
    for (size_t i = 0; i < elem->sorttag->num_attrs; i++) {
        attr = elem->sorttag->attrs[i];

        if (DEBUG_SET_LEVEL_2) {
            knd_log("     == add elem attr \"%s\" : %s [numval:%lu]  type: %d\n",
                    attr->name,
                    attr->val, (unsigned long)attr->numval,
                    attr->type);
        }
        
        err = kndSet_get_facet(self,
                                  (const char*)attr->name, attr->name_size,
                                  &f);
        if (err) {
            if (DEBUG_SET_LEVEL_3)
                knd_log("   -- couldn't get facet \"%s\" :(\n", attr->name);
            return err;
        }
        
        f->numval = attr->numval;
        f->type = attr->type;
        
        err = f->add_elem(f, elem, i, f->type);
        if (err) {
            knd_log("   -- facet \"%s\" couldn't add elem \"%s\" :(\n", attr->name, elem->id);
            return err;
        }
    }

    */ 
    return knd_OK;
}

static int
kndSet_facetize(struct kndSet *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    struct kndSetElem *elem, *r;
    struct kndSortTag *tag;
    struct kndSortAttr *attr, *a;
    struct kndElemIdx *idx, *term_idx;
    size_t i, j, ri, attr_count;
    int err;
    
    if (DEBUG_SET_LEVEL_1) {
        knd_log("\n    .. further facetize the set \"%s\"..\n",
                self->name);
    }

    /*  atomic IDX */
    if (self->parent_facet) {
        if (DEBUG_SET_LEVEL_3)
            knd_log("\n    .. facet parent: \"%s\"\n",
                    self->parent_facet->name);
        
    }
    
    return knd_OK;
}

                   
static int
kndSet_find(struct kndSet     *self,
	    const char *facet_name,
	    const char *val,
	    size_t val_size,
	    struct kndSet **result)
{
    struct kndFacet *f;
    struct kndSet *set = NULL;
    struct kndSetElem *elem;
    struct kndElemIdx *idx, *term_idx;
    size_t i, j, ri;
    int err;

    if (DEBUG_SET_LEVEL_TMP)
        knd_log("  .. finding facet \"%s\" val: %s..\n",
                facet_name, val);

    
    return knd_FAIL;
}

static int
kndSet_add_elem(struct kndSet *self,
		struct kndSetElem *elem)
{
    int err;
    
    if (self->num_facets) {
        err = kndSet_facetize_elem(self, elem);
        if (err) {
            knd_log("  -- couldn't facetize elem %s :(\n", elem->id);

            //if (elem->obj)
            //    elem->obj->str(elem->obj);
            
            kndSetElem_str(elem, 1);
            return err;
        }

        self->num_elems++;
        return knd_OK;
    }
    
    if (DEBUG_SET_LEVEL_2) {
	knd_log("    ++ \"%s\" set to put elem \"%s\" to INBOX  [total: %lu]\n",
		self->name, elem->id, (unsigned long)self->inbox_size);
    }
    
    self->inbox[self->inbox_size] = elem;
    self->inbox_size++;
    self->num_elems++;
    
    if ((self->inbox_size + 1) < self->max_inbox_size) {
        if (DEBUG_SET_LEVEL_2)
            knd_log("Inbox size: %lu ELEM: %p",
                    (unsigned long)self->inbox_size, elem);
        return knd_OK;
    }
    
    /* inbox overflow?
       time to split the inbox into subsets */
    if (DEBUG_SET_LEVEL_1)
        knd_log("Inbox size: %lu   .. Time to create facets...\n\n",
                (unsigned long)self->inbox_size);

    for (size_t i = 0; i < self->inbox_size; i++) {
        elem = self->inbox[i];
        if (DEBUG_SET_LEVEL_2)
            knd_log("== %d) elem: %p", i, elem);
        
        err = kndSet_facetize_elem(self, elem);
        if (err) {
            knd_log("  -- couldn't facetize elem from inbox %s :(\n", elem->id);
            return err;
        }
        self->inbox[i] = NULL;
    }
 
    /* clean up the inbox */
    for (size_t i = 0; i < self->inbox_size; i++) {
        elem = self->inbox[i];
        if (!elem) continue;
        //elem->del(elem);
    }

    memset(self->inbox, 0, sizeof(struct kndSetElem*) * (self->max_inbox_size + 1));
    self->inbox_size = 0;

    return knd_OK;
}

static int
kndSet_merge_idx(struct kndSet *self,
		 struct kndSet *src)
{
    struct kndElemIdx *idx, *term_idx;
    struct kndSetElem *elem;
    size_t i, j, ri;
    int err;

    if (!src->idx) {
        if (DEBUG_SET_LEVEL_2)
            knd_log("?? no term idx in set %s?", src->name);
        return knd_OK;
    }
    
    /*for (i = 0; i < KND_ID_BASE; i++) {
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
	}*/

    
    return knd_OK;
}

extern int kndSet_init(struct kndSet *self)
{
    self->max_inbox_size = KND_SET_INBOX_SIZE;
    self->del = kndSet_del;
    self->str = kndSet_str;
    //self->term_idx = kndSet_term_idx;
    self->add_elem = kndSet_add_elem;

    //self->lookup_elem = kndSet_lookup_elem;
    //self->find = kndSet_find;

    self->intersect = kndSet_intersect;
    self->facetize = kndSet_facetize;
    self->export = kndSet_export;
}

extern void kndFacet_init(struct kndFacet *self)
{
    /*self->del = kndFacet_del;
    self->str = kndFacet_str;

    self->add_elem = kndFacet_add_elem;
    self->find = kndFacet_find;
    self->extract_objs = kndFacet_extract_objs;

    self->read = kndFacet_read;
    self->read_tags = kndFacet_read_tags;

    self->export = kndFacet_export;
    self->sync = kndFacet_sync; */
}

extern int 
kndSet_new(struct kndSet **set)
{
    struct kndSet *self;

    self = malloc(sizeof(struct kndSet));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndSet));

    kndSet_init(self);
    *set = self;

    return knd_OK;
}
