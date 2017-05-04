#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_facet.h"
#include "knd_output.h"
#include "knd_repo.h"
#include "knd_sorttag.h"
#include "knd_refset.h"
#include "knd_object.h"
#include "knd_attr.h"
#include "knd_elem.h"
#include "knd_coderef.h"

#include "knd_data_reader.h"
#include "knd_monitor.h"

#define DEBUG_REFSET_LEVEL_0 0
#define DEBUG_REFSET_LEVEL_1 0
#define DEBUG_REFSET_LEVEL_2 0
#define DEBUG_REFSET_LEVEL_3 0
#define DEBUG_REFSET_LEVEL_4 0
#define DEBUG_REFSET_LEVEL_TMP 1

static int
kndRefSet_sync(struct kndRefSet *self);

static int
kndRefSet_read_term_idx_tags(struct kndRefSet *self,
                             const char       *rec,
                             size_t            rec_size);

/*static int
kndRefSet_export_item(struct kndObjRef *item,
                       char *output,
                       size_t *output_size);
*/

static int
kndRefSet_add_ref(struct kndRefSet     *self,
                  struct kndObjRef     *ref);

static int
kndRefSet_term_idx(struct kndRefSet *self,
                   struct kndObjRef *ref);

/*
static int 
knd_compare_refset_by_alph_ascend(const void *a,
                                  const void *b)
{
    struct kndRefSet **obj1, **obj2;

    obj1 = (struct kndRefSet**)a;
    obj2 = (struct kndRefSet**)b;

    if ((*obj1)->sort_val == (*obj2)->sort_val) return 0;

    if ((*obj1)->sort_val > (*obj2)->sort_val) return 1;

    return -1;
    }
*/



static int 
knd_compare_refset_by_size_ascend(const void *a,
                                   const void *b)
{
    struct kndRefSet **obj1, **obj2;

    obj1 = (struct kndRefSet**)a;
    obj2 = (struct kndRefSet**)b;

    if ((*obj1)->num_refs == (*obj2)->num_refs) return 0;

    if ((*obj1)->num_refs > (*obj2)->num_refs) return 1;

    return -1;
}

/*
static int 
knd_compare_refset_by_size_descend(const void *a,
                                   const void *b)
{
    struct kndRefSet **obj1, **obj2;

    obj1 = (struct kndRefSet**)a;
    obj2 = (struct kndRefSet**)b;

    if ((*obj1)->num_refs == (*obj2)->num_refs) return 0;

    if ((*obj1)->num_refs < (*obj2)->num_refs) return 1;

    return -1;
}
*/

static void
kndRefSet_del(struct kndRefSet *self)
{
    struct kndFacet *f;
    struct kndObjRef *ref;
    struct kndTermIdx *idx, *term_idx;
    size_t i, j, ri;

    for (i = 0; i < self->num_facets; i++) {
        f = self->facets[i];
        f->del(f);
    }

    /* terminal IDX */
    if (self->idx) {
        for (i = 0; i < KND_ID_BASE; i++) {
            idx = self->idx[i];
            if (!idx) continue;
            for (j = 0; j < KND_ID_BASE; j++) {
                term_idx = idx->idx[j];
                if (!term_idx) continue;

                for (ri = 0; ri < KND_ID_BASE; ri++) {
                    ref = term_idx->refs[ri];
                    if (!ref) continue;

                    ref->del(ref);
                }
                free(term_idx);
            }
            free(idx);
        }
        free(self->idx);
    }
    
    /* clean up the inbox */
    for (i = 0; i < self->inbox_size; i++) {
        ref = self->inbox[i];
        if (!ref) continue;
        
        ref->del(ref);
        self->inbox[i] = NULL;
    }

    free(self);
}

static int 
kndRefSet_str(struct kndRefSet *self, size_t depth, size_t max_depth)
{
    struct kndObjRef *ref;
    struct kndFacet *f;
    struct kndTrans *trn;

    char buf[KND_NAME_SIZE];
    //size_t buf_size;
    
    struct kndTermIdx *idx, *term_idx;
    size_t i, j, ri, offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;

    char *offset = malloc(offset_size + 1);
    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    if (depth > max_depth) return knd_OK;
    
    knd_log("\n%s|__\"%s\" [in:%lu facets:%lu trivia: %lu   TOTAL: %lu]\n",
            offset, self->name,
            (unsigned long)self->inbox_size,
            (unsigned long)self->num_facets,
            (unsigned long)self->num_trivia,
            (unsigned long)self->num_refs);

    for (i = 0; i < self->inbox_size; i++) {
        ref = self->inbox[i];
        ref->str(ref, depth + 1);
    }

    for (i = 0; i < self->num_facets; i++) {
        f = self->facets[i];
        f->str(f, depth, max_depth);
    }

    if (self->num_trns) {
        for (i = 0; i < self->num_trns; i++) {
            trn = self->trns[i];

            strftime(buf,
                     KND_NAME_SIZE,
                     "%y:%m:%d:%H:%M",
                     (const struct tm*)&trn->timeinfo);

            knd_log("%s      + %s: %s  %s  %s   %d\n",
                    offset,
                    buf, trn->uid,
                    trn->action, trn->query,
                    trn->proc_state);
        }
    }
    
    /* terminal IDX */
    if (self->idx) {
        if ((depth + 1) > max_depth) return knd_OK;
        
        for (i = 0; i < KND_ID_BASE; i++) {
            idx = self->idx[i];
            if (!idx) continue;

            knd_log("%s%lu [tot:%lu]\n", offset,
                    (unsigned long)i, (unsigned long)idx->num_refs);

            for (j = 0; j < KND_ID_BASE; j++) {
                term_idx = idx->idx[j];
                if (!term_idx) continue;

                knd_log("%s%s|_%lu [tot:%lu]\n", offset, offset,
                        (unsigned long)j,
                        (unsigned long)idx->num_refs);

                for (ri = 0; ri < KND_ID_BASE; ri++) {
                    ref = term_idx->refs[ri];
                    if (!ref) continue;

                    ref->str(ref, depth + 1);
                }
            
            }
        }
    }
    
    return knd_OK;
}



/*
knd_log("  .. expand OBJ REF \"%s\".. %p\n", self->obj_id, update_inbox);

*/

    /*s_sendmore(update_inbox, data->spec, data->spec_size);
    s_sendmore(update_inbox, empty_msg, empty_msg_size);
    s_sendmore(update_inbox, empty_msg, empty_msg_size);
    s_sendmore(update_inbox, empty_msg, empty_msg_size);
    s_send(update_inbox, empty_msg, empty_msg_size);
    */



static int 
kndRefSet_export_JSON(struct kndRefSet *self,
                       size_t depth)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    struct kndOutput *out = self->out;

    struct kndFacet *f;
    struct kndObjRef *ref;
    size_t curr_batch_size = 0;
    int err;
    
    if (DEBUG_REFSET_LEVEL_3)
        knd_log("  .. jsonize refset \"%s\" depth: %lu of %lu..\n", self->name,
                (unsigned long)depth, (unsigned long)self->export_depth);
    
    err = out->write(out,  "{", 1);
    if (err) return err;

    buf_size = sprintf(buf,
                       "\"n\":\"%s\"",
                       self->name);
    err = out->write(out,
                     buf, buf_size);
    if (err) return err;

    buf_size = sprintf(buf,
                       ",\"srt\":%lu,\"tot\":%lu",
                       (unsigned long)self->numval,
                       (unsigned long)self->num_refs);
    err = out->write(out,  
                      buf, buf_size);
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
                           ",\"inbox\":{\"tot\":\"%lu\",\"refs\":[",
                           (unsigned long)self->inbox_size);
        err = out->write(out,  
                          buf, buf_size);
        if (err) return err;


        for (size_t i = 0; i < self->inbox_size; i++) {
            ref = self->inbox[i];
            if (i) {
                err = out->write(out,  ",", 1);
                if (err) return err;
            }

            ref->out = self->out;
            ref->cache = self->cache;
            
            err = ref->export(ref, KND_FORMAT_JSON);
            if (err) return err;
        }
        
        err = out->write(out,  "]}", 2);
        curr_batch_size = self->inbox_size;
    }


    
    if ((depth + 1) > KND_REFSET_MAX_DEPTH) 
        goto final;

    if ((depth + 1) > self->export_depth) {
        if (DEBUG_REFSET_LEVEL_3) 
            knd_log("  -- max depth reached in \"%s\": %lu of %lu\n",
                    self->name,
                    (unsigned long)depth,
                    (unsigned long)self->export_depth);

        goto final;
    }
    
    if (self->num_facets) {
        /* apply proper sorting */
        /*qsort(self->refset_refs,
              self->num_refsets,
              sizeof(struct kndRefSet*),
              knd_compare_refset_by_alph_ascend); */

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

            f->export_depth = self->export_depth;
            err = f->export(f, KND_FORMAT_JSON, depth + 1);
            if (err) return err;
        }

        err = out->write(out,  "]", 1);
        if (err) return err;
    }

 final:
    
    err = out->write(out,  "}", 1);
    if (err) return err;

    self->batch_size = curr_batch_size;
    
    return knd_OK;
}





static int 
kndRefSet_export_HTML(struct kndRefSet *self,
                      size_t depth)
{
    //char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size;

    struct kndOutput *out = self->out;

    struct kndFacet *f;
    struct kndObjRef *ref;
    size_t curr_batch_size = 0;
    int err;

    
    if (DEBUG_REFSET_LEVEL_TMP)
        knd_log("  .. HTML output of refset \"%s\" depth: %lu of %lu..\n", self->name,
                (unsigned long)depth, (unsigned long)self->export_depth);

    
    err = out->write(out,  "<A HREF=\"/top/", strlen("<A HREF=\"/top/"));
    if (err) return err;

    err = out->write(out,
                     self->name, self->name_size);
    if (err) return err;

    err = out->write(out, "\">", strlen("\">"));
    if (err) return err;

    err = out->write(out,
                     self->name, self->name_size);
    if (err) return err;
    
    err = out->write(out, "</A>", strlen("</A>"));
    if (err) return err;
    
    /*buf_size = sprintf(buf,
                       ",\"srt\":%lu,\"tot\":%lu",
                       (unsigned long)self->numval,
                       (unsigned long)self->num_refs);
    err = out->write(out,  
                      buf, buf_size);
    if (err) return err;
    */
    
    if (self->inbox_size) {
        err = out->write(out,  
                         "<OL>", strlen("<OL>"));
        if (err) return err;


        for (size_t i = 0; i < self->inbox_size; i++) {
            ref = self->inbox[i];

            err = out->write(out, "<LI>", strlen("<LI>"));

            ref->out = self->out;
            ref->cache = self->cache;
            
            err = ref->export(ref, KND_FORMAT_HTML);
            if (err) return err;

            err = out->write(out, "</LI>", strlen("</LI>"));
        }
        
        err = out->write(out, "</OL>", strlen("</OL>"));
        curr_batch_size = self->inbox_size;
    }

    if ((depth + 1) > KND_REFSET_MAX_DEPTH) 
        goto final;

    if ((depth + 1) > self->export_depth) {
        if (DEBUG_REFSET_LEVEL_3) 
            knd_log("  -- max depth reached in \"%s\": %lu of %lu\n",
                    self->name,
                    (unsigned long)depth,
                    (unsigned long)self->export_depth);

        goto final;
    }
    
    if (self->num_facets) {
        /* apply proper sorting */
        /*qsort(self->refset_refs,
              self->num_refsets,
              sizeof(struct kndRefSet*),
              knd_compare_refset_by_alph_ascend); */

        err = out->write(out, 
                          "<UL>", strlen("<UL>"));
        if (err) return err;

        for (size_t i = 0; i < self->num_facets; i++) {
            f = self->facets[i];

            err = out->write(out, 
                             "<LI>", strlen("<LI>"));
            if (err) return err;

            f->out = out;

            f->export_depth = self->export_depth;
            err = f->export(f, KND_FORMAT_HTML, depth + 1);
            if (err) return err;

            err = out->write(out, 
                             "</LI>", strlen("</LI>"));
            if (err) return err;

        }

        err = out->write(out, 
                          "</UL>", strlen("</UL>"));
        if (err) return err;
    }

 final:
    
    err = out->write(out,  "}", 1);
    if (err) return err;

    self->batch_size = curr_batch_size;
    
    return knd_OK;
}



static int 
kndRefSet_export(struct kndRefSet *self,
                  knd_format         format,
                  size_t depth)
{
    int err;
    
    switch(format) {
    case KND_FORMAT_JSON:
        err = kndRefSet_export_JSON(self, depth);
        if (err) return err;
        break;
    case KND_FORMAT_HTML:
        err = kndRefSet_export_HTML(self, depth);
        if (err) return err;
        break;
    default:
        break;
    }
    
    return knd_OK;
}

/*
static int
kndRefSet_check_refset_name(struct kndRefSet *self,
                             const char        *name,
                             size_t name_size)
{
    const char *c;
    
    // no more sorting elems: stay in the same refset
    if (!name_size)
        return knd_FAIL;

    c = name;
    while (*c) {
        // filter out special symbols
        switch (*c) {
        case '&':
        case ' ':
        case '[':
        case ']':
        case '/':
        case '(':
        case '{':
        case '}':
        case '<':
        case '>':
        case '=':
            return knd_FAIL;
        default:
            break;
        }
        c++;
    }

    return knd_OK;
}
*/

static int
kndRefSet_lookup_name(struct kndRefSet *self,
                      const char *name,
                      size_t name_size,
                      const char *remainder,
                      size_t remainder_size,
                      char *guid)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;

    struct kndFacet *f = NULL;
    struct kndRefSet *rs = NULL;
    struct kndObjRef *ref = NULL;

    struct kndTermIdx *idx, *term_idx;
    size_t i, j, ri;

    //char *b;
    //char *c;
    
    const char *tail = NULL;
    size_t tail_size = 0;

    const char *val;
    size_t val_size;
    
    size_t UTF_val;
    //long numval = 0;
    int err;

    if (DEBUG_REFSET_LEVEL_3)
        knd_log("  .. refset \"%s\" looking up obj name: \"%s\"..\n",
                self->name, name);

    if (self->inbox_size) {
        for (i = 0; i < self->inbox_size; i++) {
            ref = self->inbox[i];

            if (!strcmp(ref->name, remainder)) {
                if (DEBUG_REFSET_LEVEL_3)
                    knd_log("  ++ got obj match \"%s\"!\n", ref->obj_id);

                memcpy(guid, ref->obj_id, KND_ID_SIZE);
                return knd_OK;
            }
        }
        return knd_FAIL;
    }

    if (!self->num_facets && self->idx) {
        for (i = 0; i < KND_ID_BASE; i++) {
            idx = self->idx[i];
            if (!idx) continue;
            
            for (j = 0; j < KND_ID_BASE; j++) {
                term_idx = idx->idx[j];
                if (!term_idx) continue;
                
                for (ri = 0; ri < KND_ID_BASE; ri++) {
                    ref = term_idx->refs[ri];
                    if (!ref) continue;

                    if (DEBUG_REFSET_LEVEL_3)
                        knd_log("  .. matching \"%s\" with REF \"%s\"..\n",
                                ref->name, ref->name);
                    
                    /*if (!ref->obj) {
                        ref->cache = self->cache;

                        err = ref->expand(ref);
                        if (err) return err;
                        }*/

                    if (!strcmp(ref->name, remainder)) {
                        if (DEBUG_REFSET_LEVEL_3)
                            knd_log("\n     ++ %s IDX got obj name match: \"%s\"!\n",
                                    self->name, remainder);

                        memcpy(guid, ref->obj_id, KND_ID_SIZE);
                        
                        return knd_OK;
                    }

                }
            
            }
        }

    }

    val = remainder;
    val_size = remainder_size;
    
    /* strip off a valid character */
    while (*val) {
        buf_size = 0;
        switch (*val) {
            /*case ' ':
              case '_':*/
        case '-':
            val++;
            val_size--;

            if (DEBUG_REFSET_LEVEL_3)
                knd_log(".. skipping over a non-alph char in \"%s\"..\n",
                        remainder);

            continue;
            
        default:
            break;
        }

        err = knd_read_UTF8_char(val,
                                 val_size,
                                 &UTF_val, &buf_size);
        if (err) return err;
        
        /* valid char found! */
        break;
    }
            
    if (!buf_size) return knd_FAIL;
    
    memcpy(buf, val, buf_size);
    buf[buf_size] = '\0';

    tail_size = val_size - buf_size;
    tail = val + buf_size;

    for (i = 0; i < self->num_facets; i++) {
        f = self->facets[i];

        if (!strcmp(f->name, "AZ")) {
            for (j = 0; j < f->num_refsets; j++) {
                rs = f->refsets[j];

                if (!strcmp(rs->name, buf)) {
                    err = kndRefSet_lookup_name(rs,
                                                name, name_size,
                                                tail, tail_size,
                                                guid);
                    return err;
                }

            }
            break;
        }
    }

    return knd_FAIL;
}
        
static int
kndRefSet_lookup_ref(struct kndRefSet     *self,
                     struct kndObjRef     *ref)
{
    struct kndTermIdx *idx, *term_idx, **idxs;
    //struct kndObjRef *curr_ref;
    long numval = 0;
    
    int err = knd_NO_MATCH;
    //int i;

    if (DEBUG_REFSET_LEVEL_3)
        knd_log("  .. looking up obj ref \"%s\" in %s ..\n",
                ref->obj_id, self->name);

    if (!self->idx) {
        knd_log("  -- no IDX in %s :(\n", self->name);
        return err;
    }
    
    /* first numeric position */
    // fixme: array subscript is of type char
    numval = obj_id_base[(size_t)ref->obj_id[0]];
    if (numval == -1) return err;
    
    idx = self->idx[numval];
    if (!idx) {
        knd_log("  -- no IDX in pos %lu :(\n", (unsigned long)numval);
        return err;
    }


    idxs = idx->idx;
    if (!idxs) return err;

    /* second numeric position */
    numval = obj_id_base[(size_t)ref->obj_id[1]];
    if (numval == -1) return err;

    term_idx = idxs[numval];
    if (!term_idx) return err;
    
    if (!term_idx->refs) {
        knd_log("  -- no refs in pos %lu :(\n", (unsigned long)numval);
        return err;
    }

    /* LAST numeric position */
    numval = obj_id_base[(size_t)ref->obj_id[2]];
    if (numval == -1) return err;
    
    ref = term_idx->refs[numval];
    if (!ref) return knd_NO_MATCH;

    return knd_OK;
}


static int
kndRefSet_contribute(struct kndRefSet   *self,
                     size_t seqnum)
{
    struct kndObjRef *ref;
    struct kndSortAttr *attr;
    struct kndTermIdx *idx, *term_idx;
    size_t numval;
    int err;

    for (size_t i = 0; i < self->inbox_size; i++) {
        ref = self->inbox[i];
        if (!ref) continue;
        
        numval = self->numval;
        if (ref->sorttag->num_attrs) {
            attr = ref->sorttag->attrs[0];
            if (attr->numval > (numval + 1)) continue;
        }
        
        err = ref->obj->contribute(ref->obj, numval, seqnum);
        if (err) continue;
    }

    if (!self->idx) return knd_OK;
    
    for (size_t i = 0; i < KND_ID_BASE; i++) {
        idx = self->idx[i];
        if (!idx) continue;

        for (size_t j = 0; j < KND_ID_BASE; j++) {
            term_idx = idx->idx[j];
            if (!term_idx) continue;

            for (size_t k = 0; k < KND_ID_BASE; k++) {
                ref = term_idx->refs[k];
                if (!ref) continue;

                err = ref->obj->contribute(ref->obj, self->numval, seqnum);
                if (err) continue;
            }
        }
    }

    return knd_OK;
}


static int
kndRefSet_intersect(struct kndRefSet   *self,
                    struct kndRefSet **refsets,
                    size_t num_refsets)
{
    struct kndRefSet *base, *refset;
    struct kndObjRef *ref;
    struct kndTermIdx *idx, *term_idx;
    //struct kndFacet *f;

    int i, j, ri, err;

    if (DEBUG_REFSET_LEVEL_TMP) 
        knd_log(" .. intersection by RefSet \"%s\"..\n",
                self->name);

    /* sort refsets by size */
    qsort(refsets,
          num_refsets,
          sizeof(struct kndRefSet*),
          knd_compare_refset_by_size_ascend);

    /* the smallest set */
    base = refsets[0];
    refset = refsets[1];
    
    if (!base->idx) {
        knd_log("  -- no IDX in %s?\n",
                base->name);
        return knd_FAIL;
    }
    
    knd_log("  .. traverse base RefSet \"%s\"..\n",
            base->name);
    
    for (i = 0; i < KND_ID_BASE; i++) {
        idx = base->idx[i];
        if (!idx) continue;

        for (j = 0; j < KND_ID_BASE; j++) {
            term_idx = idx->idx[j];
            if (!term_idx) continue;
            
            for (ri = 0; ri < KND_ID_BASE; ri++) {
                ref = term_idx->refs[ri];
                if (!ref) continue;

                err = kndRefSet_lookup_ref(refset, ref);
                if (err) {
                    if (DEBUG_REFSET_LEVEL_3)
                        knd_log("  -- obj ref %s not found :(\n", ref->obj_id);
                    continue;
                }

                err = kndRefSet_term_idx(self, ref);
                if (err) return err;

                self->num_refs++;
            }
            
        }
    }

    return knd_OK;
}


static int
kndRefSet_get_facet(struct kndRefSet  *self,
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
    err = kndFacet_new(&f);
    if (err) return err;

    memcpy(f->name, facet_name, facet_name_size);
    f->name[facet_name_size] = '\0';
    f->name_size = facet_name_size;
    
    f->baseclass = self->baseclass;
    f->parent = self;
    f->cache = self->cache;
    f->out = self->out;
    
    self->facets[self->num_facets] = f;
    self->num_facets++;

    
    *result = f;

    
    return knd_OK;
}




static int
kndRefSet_term_idx(struct kndRefSet *self,
                   struct kndObjRef *ref)
{
    struct kndTermIdx *idx, *term_idx, **idxs;
    struct kndObjRef **refs;
    struct kndTrans **trns;
    size_t num_trns;
    
    int numval = -1;

    if (ref->type == KND_REF_TID) {

        if (ref->is_trivia) {
            self->num_trivia++;
            return knd_OK;
        }
        
        /* more storage needed? */
        if (self->num_trns + 1 >= self->max_trns) {
            num_trns = self->max_trns + (self->max_inbox_size * KND_INBOX_RESERVE_RATIO); // fixme
            trns = realloc(self->trns, (sizeof(struct kndTrans*)) * num_trns);
            if (!trns) return knd_NOMEM;

            self->trns = trns;
            self->max_trns = num_trns;
        }

        self->trns[self->num_trns] = ref->trn;
        self->num_trns++;

        return knd_OK;
    }

    if (!ref->obj_id_size) return knd_FAIL;
    
    if (DEBUG_REFSET_LEVEL_3) 
        knd_log("   .. add term idx for obj \"%s\" [%lu]: \"%s\"\n",
                ref->obj_id, (unsigned long)ref->obj_id_size, ref->name);
    
    if (!self->idx) {
        self->idx = malloc(sizeof(struct kndTermIdx*) * (KND_ID_BASE + 1));
        if (!self->idx)
            return knd_NOMEM;

        memset(self->idx, 0, sizeof(struct kndTermIdx*) * (KND_ID_BASE + 1));
    }
    
    /* first numeric position */
    numval = obj_id_base[(size_t)ref->obj_id[0]];
    if (numval == -1) return knd_FAIL;

    if (DEBUG_REFSET_LEVEL_3) 
        knd_log("   == term idx 0 pos: %d\n", numval);
    
    idx = self->idx[numval];
    if (!idx) {
        idx = malloc(sizeof(struct kndTermIdx));
        if (!idx) 
            return knd_NOMEM;

        memset(idx, 0, sizeof(struct kndTermIdx));
        self->idx[numval] = idx;
    }

    idx->num_refs++;
    
    idxs = idx->idx;
    if (!idxs) {
        idxs = malloc(sizeof(struct kndTermIdx*) * (KND_ID_BASE + 1));
        if (!idxs)
            return knd_NOMEM;

        memset(idxs, 0, sizeof(struct kndTermIdx*) * (KND_ID_BASE + 1));
        idx->idx = idxs;
    }

    /* second numeric position */
    numval = obj_id_base[(size_t)ref->obj_id[1]];
    if (numval == -1) return knd_FAIL;

    if (DEBUG_REFSET_LEVEL_3) 
        knd_log("== term idx 1 pos: %d\n", numval);

    term_idx = idxs[numval];
    if (!term_idx) {
        term_idx = malloc(sizeof(struct kndTermIdx));
        if (!term_idx) 
            return knd_NOMEM;

        memset(term_idx, 0, sizeof(struct kndTermIdx));
        idxs[numval] = term_idx;
    }

    term_idx->num_refs++;
    
    if (!term_idx->refs) {
        refs = malloc(sizeof(struct kndObjRef*) * (KND_ID_BASE + 1));
        if (!refs)
            return knd_NOMEM;

        memset(refs, 0, sizeof(struct kndObjRef*) * (KND_ID_BASE + 1));
        term_idx->refs = refs;
    }

    /* LAST numeric position */
    numval = obj_id_base[(size_t)ref->obj_id[2]];
    if (numval == -1) return knd_FAIL;

    if (DEBUG_REFSET_LEVEL_3) 
        knd_log("  == term idx 2 pos: %d\n", numval);

    term_idx->refs[numval] = ref;
    
    return knd_OK;
}


static int
kndRefSet_term_idx_add_tag(struct kndRefSet *self,
                           const char *obj_id,
                           struct kndSortTag *tag)
{
    struct kndTermIdx *idx, *term_idx, **idxs;
    //struct kndObjRef **refs;
    struct kndObjRef *ref = NULL;
    //struct kndTrans **trns;
    //size_t num_trns;
    
    int numval = -1;

    if (DEBUG_REFSET_LEVEL_TMP) 
        knd_log("   .. add TAG to  REF \"%s\"\n",
                obj_id);
    
    if (!self->idx) return knd_FAIL;

    /* first numeric position */
    numval = obj_id_base[(size_t)obj_id[0]];
    if (numval == -1) return knd_FAIL;

    if (DEBUG_REFSET_LEVEL_2) 
        knd_log("   == term idx 0 pos: %d\n", numval);
    
    idx = self->idx[numval];
    if (!idx) return knd_FAIL;
    
    idxs = idx->idx;
    if (!idxs) return knd_FAIL;

    /* second numeric position */
    numval = obj_id_base[(size_t)obj_id[1]];
    if (numval == -1) return knd_FAIL;

    if (DEBUG_REFSET_LEVEL_2) 
        knd_log("== term idx 1 pos: %d\n", numval);

    term_idx = idxs[numval];
    if (!term_idx) return knd_FAIL;
    
    if (!term_idx->refs) return knd_FAIL;

    /* LAST numeric position */
    numval = obj_id_base[(size_t)obj_id[2]];
    if (numval == -1) return knd_FAIL;

    if (DEBUG_REFSET_LEVEL_2) 
        knd_log("  == term idx 2 pos: %d\n", numval);

    ref = term_idx->refs[numval];
    if (!ref) return knd_FAIL;

    ref->sorttag = tag;

    knd_log("  ++ tag assigned!\n");
    
    return knd_OK;
}


static int
kndRefSet_facetize_ref(struct kndRefSet *self,
                       struct kndObjRef *ref)
{
    struct kndFacet *f;
    //struct kndObjRef *subref;
    //struct kndSortTag *tag;
    struct kndSortAttr *attr;
    //struct kndSortAttr *orig_attr;
    //struct kndSortAttr *sub_attr;
    int err = knd_FAIL;
    
    if (DEBUG_REFSET_LEVEL_3) {
        knd_log("\n    .. passing REF to facets of refset \"%s\"..\n",
                self->name);
    }
    
    /* conceptual facet? */
    /*if (item->type == KND_REF_CONC) {
        knd_log("    RefItem: HEAD: %s SPEC: %s\n", ref->head.name, ref->spec.name);

        return knd_OK;
        }*/
    
    if (!ref->sorttag->num_attrs) {
        if (DEBUG_REFSET_LEVEL_2)
            knd_log("   -- no more attrs in REF: add ref to term IDX!\n");

        err = kndRefSet_term_idx(self, ref);
        if (err) {
            knd_log("   -- term idx failed to add ref \"%s\" :(\n",
                    ref->obj_id);
            return err;
        }
        
        return knd_OK;
    }
    
    for (size_t i = 0; i < ref->sorttag->num_attrs; i++) {
        attr = ref->sorttag->attrs[i];

        if (DEBUG_REFSET_LEVEL_2) {
            knd_log("     == add ref attr \"%s\" : %s [numval:%lu]  type: %d\n",
                    attr->name,
                    attr->val, (unsigned long)attr->numval,
                    attr->type);
        }
        
        err = kndRefSet_get_facet(self,
                                  (const char*)attr->name, attr->name_size,
                                  &f);
        if (err) {
            if (DEBUG_REFSET_LEVEL_3)
                knd_log("   -- couldn't get facet \"%s\" :(\n", attr->name);
            return err;
        }
        
        f->numval = attr->numval;
        f->type = attr->type;
        
        err = f->add_ref(f, ref, i, f->type);
        if (err) {
            knd_log("   -- facet \"%s\" couldn't add ref \"%s\" :(\n", attr->name, ref->obj_id);
            return err;
        }
    }

        
    return knd_OK;
}



static int
kndRefSet_read_tags(struct kndRefSet *self,
                    const char       *rec,
                    size_t            rec_size)
{
    char buf[KND_LARGE_BUF_SIZE];
    size_t buf_size = 0;

    char namebuf[KND_NAME_SIZE];
    size_t namebuf_size = 0;

    //struct kndFacet *f;
    
    const char *facet_rec;
    size_t facet_rec_size;
    
    char *val;
    char *c;
    long numval;

    size_t offset = 0;
    size_t curr_size;
    size_t num_refs = 0;
    
    const char *delim = KND_FIELD_SEPAR;
    char *last = NULL;
    
    int err = knd_OK;

    if (DEBUG_REFSET_LEVEL_TMP) {
       knd_log("\n\n   .. Refset \"%s\" parsing IDX rec, input size [%lu]   depth: %lu\n",
               self->name, (unsigned long)rec_size, (unsigned long)self->export_depth);
    }
    
    err = knd_get_trailer(rec, rec_size,
                          namebuf, &namebuf_size,
                          &num_refs,
                          buf, &buf_size);
    if (err) return err;


    
    /*if (self->export_depth && namebuf_size) {
        memcpy(self->name, namebuf, namebuf_size);
        self->name[namebuf_size] = '\0';
        self->name_size = namebuf_size;
        }*/

    
    if (DEBUG_REFSET_LEVEL_TMP)
        knd_log("   == DIR REC: %s [size: %lu]\n\n",
                buf, (unsigned long)buf_size);
    
    if (!buf_size) {
        if (*(rec + 1) == '_') {

            knd_log("  ++ got TERM IDX with tags!\n\n");
            
            err =  kndRefSet_read_term_idx_tags(self,
                                                rec + 1 +  KND_REFSET_TERM_TAG_SIZE,
                                                rec_size);
            if (err) return err;
            
            return knd_OK;
        }
        return knd_FAIL;
    }


    self->num_refs = num_refs;

    /* parse trailer */
    for (c = strtok_r(buf, delim, &last);
         c;
         c = strtok_r(NULL, delim, &last)) {

        /* find the offset field */
        val = strstr(c, GSL_OFFSET);
        if (!val) continue;

        curr_size = val - c;
        
        *val = '\0';
        val++;
        
        if (DEBUG_REFSET_LEVEL_TMP)
            knd_log("   == Facet: \"%s\" offset: %s\n", c, val);
        
        /* get the numeric offset */
        err = knd_parse_num((const char*)val, &numval);
        if (err) return err;
        
        facet_rec_size = (size_t)numval;
        facet_rec = rec + offset;

        if (strncmp(c, self->parent->name, self->parent->name_size)) {
            offset += facet_rec_size;
            continue;
        }

        knd_log("  ++ got parent facet: %s!\n", self->parent->name);

        
        err = self->parent->read_tags(self->parent, facet_rec, facet_rec_size, self); 
        if (err) return err;

            
        break;
    }

    return knd_OK;
}



static int
kndRefSet_facetize(struct kndRefSet *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    struct kndObjRef *ref, *r;
    struct kndSortTag *tag;
    struct kndSortAttr *attr, *a;
    struct kndTermIdx *idx, *term_idx;
    size_t i, j, ri, attr_count;
    int err;
    
    if (DEBUG_REFSET_LEVEL_TMP) {
        knd_log("\n    .. further facetize the refset \"%s\"..\n",
                self->name);
    }

    /*  atomic IDX */
    if (self->parent) {
        if (DEBUG_REFSET_LEVEL_3)
            knd_log("\n    .. facet parent: \"%s\"\n",
                    self->parent->name);
        
        /* get the sorttags */
        if (self->name_size && !strcmp(self->name, "AZ")) {
            buf_size = sprintf(buf, "%s/%s/AZ.idx",
                               self->cache->repo->path,
                               self->cache->baseclass->name);
        } else {
            buf_size = sprintf(buf, "%s/%s/atom_%s.idx",
                               self->cache->repo->path,
                               self->cache->baseclass->name,
                               self->parent->name);
        }
        
        if (DEBUG_REFSET_LEVEL_TMP)
            knd_log("  .. reading atom IDX file: \"%s\" .. OUTPUT: %p\n",
                    buf, self->out);
        
        err = self->out->read_file(self->out,
                                   (const char*)buf, buf_size);
        if (err) {
            if (DEBUG_REFSET_LEVEL_TMP)
                knd_log("   -- no such DB found: \"%s\" :(\n",
                        buf);
            return knd_OK;
        }
        
        if (DEBUG_REFSET_LEVEL_TMP)
            knd_log("\n\n   ++  atom IDX DB rec size: %lu\n",
                    (unsigned long)self->out->file_size);
        
        
        err = kndRefSet_read_tags(self,
                                  (const char*)self->out->file,
                                  self->out->file_size);
        if (err) return err;
    }

    
    
    /* terminal IDX */
    if (self->idx) {
        for (i = 0; i < KND_ID_BASE; i++) {
            idx = self->idx[i];
            if (!idx) continue;
            for (j = 0; j < KND_ID_BASE; j++) {
                term_idx = idx->idx[j];
                if (!term_idx) continue;

                for (ri = 0; ri < KND_ID_BASE; ri++) {
                    ref = term_idx->refs[ri];
                    if (!ref) continue;

                    if (!ref->sorttag) {
                        /*knd_log("SORT TAG is needed to facetize ref %s!\n", ref->obj_id);*/
                        continue;
                    }
                    
                    /* master tag */
                    for (attr_count = 0; attr_count < ref->sorttag->num_attrs; attr_count++) {
                        attr = ref->sorttag->attrs[attr_count];

                        /* skipping attr */
                        if (self->parent) {
                            if (self->parent->name_size && !strcmp(self->parent->name, attr->name)) {
                                if (self->name_size && !strcmp(self->name, attr->val))
                                    continue;
                            }
                        }

                        
                        /* facetize by this attr */
                        err = kndObjRef_new(&r);
                        if (err) return err;

                        r->baseclass = ref->baseclass;
                        memcpy(r->obj_id, ref->obj_id, KND_ID_SIZE);
                        r->obj_id_size = KND_ID_SIZE;

                        err = kndSortTag_new(&tag);
                        if (err) return err;

                        a = malloc(sizeof(struct kndSortAttr));
                        if (!a) return knd_NOMEM;
                        memset(a, 0, sizeof(struct kndSortAttr));
                        a->type = attr->type;
                        
                        memcpy(a->name, attr->name, attr->name_size);
                        a->name_size = attr->name_size;
                        a->name[a->name_size] = '\0';

                        memcpy(a->val, attr->val, attr->val_size);
                        a->val_size = attr->val_size;
                        a->val[a->val_size] = '\0';

                        tag->attrs[0] = a;
                        tag->num_attrs = 1;
                        r->sorttag = tag;
                        
                        err = kndRefSet_facetize_ref(self, r);
                        if (err) return err;
                    }
                }
            }
        }
    }
   
    

    
    return knd_OK;
}

                   
static int
kndRefSet_find(struct kndRefSet     *self,
               const char *facet_name,
               const char *val,
               size_t val_size,
               struct kndRefSet **result)
{
    struct kndFacet *f;
    struct kndRefSet *refset = NULL;
    struct kndObjRef *ref;
    struct kndTermIdx *idx, *term_idx;
    size_t i, j, ri;
    int err;

    if (DEBUG_REFSET_LEVEL_TMP)
        knd_log("  .. finding facet \"%s\" val: %s..\n",
                facet_name, val);

    if (!self->num_facets) {
        
        if (DEBUG_REFSET_LEVEL_3)
            knd_log("    -- terminal inbox reached to find: \"%s\" ..\n",
                    val);

        if (!self->query_size) return knd_FAIL;
        
        self->num_matches = 0;
        
        if (self->idx) {
        
            for (i = 0; i < KND_ID_BASE; i++) {
                idx = self->idx[i];
                if (!idx) continue;
                
                for (j = 0; j < KND_ID_BASE; j++) {
                    term_idx = idx->idx[j];
                    if (!term_idx) continue;

                    for (ri = 0; ri < KND_ID_BASE; ri++) {
                        ref = term_idx->refs[ri];
                        if (!ref) continue;

                        if (!ref->obj) {
                            ref->cache = self->cache;

                            err = ref->expand(ref);
                            if (err) return err;
                        }

                        
                        knd_log("  .. matching ref obj \"%s\" against query \"%s\"..\n",
                                ref->obj->name, self->query);

                        if (!strncmp(ref->obj->name, self->query, self->query_size)) {
                            self->matches[self->num_matches] = ref;
                            self->num_matches++;
                        }
                        
                    }
                    
                }
            }
        }

        if (self->num_matches) {
            *result = self;
            return knd_OK;
        }
    }
    
    for (i = 0; i < self->num_facets; i++) {
        f = self->facets[i];

        if (DEBUG_REFSET_LEVEL_TMP)
            knd_log("  .. curr facet: %s..\n",
                f->name);

	if (!strcmp(f->name, facet_name)) {

            knd_log("  ++ got facet %s!\n", facet_name);
            
            f->query = self->query;
            f->query_size = self->query_size;

            
            err = f->find(f, val, val_size, &refset);
            if (err) return err;
            
            *result = refset;
            return knd_OK;
        }
    }
    
    return knd_FAIL;
}


/*
static int
kndRefSet_calc_aggr(struct kndRefSet     *self,
                    struct kndObjRef     *ref)
{
    struct kndElem *elem;
    int err;

    knd_log("   .. checking AGGR elems in obj \"%s\"..\n", ref->obj->name);

    elem = ref->obj->elems;

    while (elem) {

        knd_log("  .. elem \"%s\" to AGGR?\n", elem->name);

        elem = elem->next;
    }

    err = knd_OK;

    return err;
}
*/

static int
kndRefSet_add_ref(struct kndRefSet     *self,
                  struct kndObjRef     *ref)
{
    //struct kndRefSet *refset = NULL;
    //struct kndFacet *f;
    struct kndObjRef  *objref;
    //struct kndElemRef *elemref;
    int err;
    
    if (self->num_facets) {
        err = kndRefSet_facetize_ref(self, ref);
        if (err) {
            knd_log("  -- couldn't facetize ref %s :(\n", ref->obj_id);

            if (ref->obj)
                ref->obj->str(ref->obj, 0);
            
            ref->str(ref, 1);
            return err;
        }
        
        self->num_refs++;
        return knd_OK;
    }
    
    if (DEBUG_REFSET_LEVEL_3) {
        if (ref->type == KND_REF_TID)
            knd_log("    ++ \"%s\" refset to put TID ref \"%s\" to INBOX  [total: %lu]\n",
                    self->name, ref->trn->tid, (unsigned long)self->inbox_size);
        else
            knd_log("    ++ \"%s\" refset to put ref \"%s\" to INBOX  [total: %lu]\n",
                    self->name, ref->obj_id, (unsigned long)self->inbox_size);

    }

    /* terminal trn storage? */
    if (ref->type == KND_REF_TID) {
        if (self->num_trns) {
            err = kndRefSet_term_idx(self, ref);
            if (err) {
                return err;
            }
        }
    }
    
    /* objref already exists? */
    /*if (ref->type == KND_REF_CONC) { */

    /*for (i = 0; i < self->inbox_size; i++) {
        objref = self->inbox[i];

        if (strncmp(objref->obj_id, ref->obj_id, KND_ID_SIZE)) continue;
            
        if (DEBUG_REFSET_LEVEL_3)
            knd_log("   OBJ \"%s\" already in inbox!\n",
                objref->obj_id);

        
        if (ref->type != KND_REF_CODE)
            return knd_OK;
        
        err = objref->add_elemref(objref, ref->elemrefs);
        if (err) return err;
        }*/
    
    self->inbox[self->inbox_size] = ref;
    self->inbox_size++;
    self->num_refs++;
    
    if ((self->inbox_size + 1) < self->max_inbox_size)
        return knd_OK;

    /* inbox overflow?
       time to split the inbox into subrefsets */
    if (DEBUG_REFSET_LEVEL_3)
        knd_log("   .. Time to create facets...\n\n");


    for (size_t i = 0; i < self->inbox_size; i++) {
        objref = self->inbox[i];
        
        err = kndRefSet_facetize_ref(self, objref);
        if (err) {
            knd_log("  -- couldn't facetize ref from inbox %s :(\n", ref->obj_id);
            return err;
        }
        self->inbox[i] = NULL;
    }
    
    /* clean up the inbox */
    for (size_t i = 0; i < self->inbox_size; i++) {
        objref = self->inbox[i];
        if (!objref) continue;
        
        objref->del(objref);
    }
    

    memset(self->inbox, 0, sizeof(struct kndObjRef*) * (self->max_inbox_size + 1));
    self->inbox_size = 0;

    return knd_OK;
}



/*static int
kndRefSet_export_item(struct kndObjRef *item,
                       char   *output,
                       size_t *output_size)
{
    char *curr;
    size_t max_size = *output_size;
    size_t curr_size = 0;
    int err;

    curr = output;

    if (KND_ID_SIZE >= max_size)
        return knd_NOMEM;
    
    memcpy(curr, item->obj_id, KND_ID_SIZE);
    curr_size += KND_ID_SIZE;
    max_size -= KND_ID_SIZE;
    curr += KND_ID_SIZE;

    err = item->sorttag->export(item->sorttag, curr, &max_size);
    if (err) return err;

    curr_size += max_size;
    
    *output_size = curr_size;
    
    return err;
}
*/


static int
kndRefSet_merge_idx(struct kndRefSet *self,
                    struct kndRefSet *src)
{
    struct kndTermIdx *idx, *term_idx;
    struct kndObjRef *ref;
    size_t i, j, ri;
    int err;
    
    for (i = 0; i < KND_ID_BASE; i++) {
        idx = src->idx[i];
        if (!idx) continue;

        for (j = 0; j < KND_ID_BASE; j++) {
            term_idx = idx->idx[j];
            if (!term_idx) continue;


            for (ri = 0; ri < KND_ID_BASE; ri++) {
                ref = term_idx->refs[ri];
                if (!ref) continue;

                err = kndRefSet_term_idx(self, ref);
                if (err) return err;
            }
            
        }
    }
    
    return knd_OK;
}





static int
kndRefSet_read_term_idx_tags(struct kndRefSet *self,
                             const char       *rec,
                             size_t            rec_size __attribute__((unused)))
{
    //char buf[KND_NAME_SIZE + 1];
    size_t buf_size = 0;

    char namebuf[KND_NAME_SIZE + 1];
    size_t namebuf_size = 0;

    char idbuf[KND_ID_SIZE + 1];
    size_t depth = 0;

    struct kndSortAttr *attr = NULL;
    struct kndSortTag *tag = NULL;
    
    const char *c;
    const char *b;
    char *s;
    
    bool in_term = false;

    bool in_tag = false;
    bool in_attr = false;

    char *id;
    size_t num_refs = 0;
    long numval = 0;
    
    int err = 0;


    if (DEBUG_REFSET_LEVEL_TMP)
        knd_log("   .. reading tags from term IDX of refset \"%s\"   [DB num refs: %lu]\n",
                self->name, (unsigned long)self->num_refs);


    memset(idbuf, 0, KND_ID_SIZE + 1);

    c = rec;
    id = idbuf;
    
    while (*c) {
        switch (*c) {
        case '(':

            if (in_tag) break;
            
            if (depth > KND_ID_SIZE) {
                knd_log(" -- id size limit reached :(\n");
                return knd_LIMIT;
            }
            c++;
            memcpy(id, c, 1);
            id++;
            depth++;
            break;
            
        case ')':

            if (in_tag) break;

            if (!depth) {

                if (self->num_refs) {
                    if (num_refs != self->num_refs) {
                        knd_log("   -- WARN: num refs not matched:  DB: %lu  ACTUAL: %lu\n",
                                (unsigned long)self->num_refs, (unsigned long)num_refs);
                        /*return knd_FAIL;*/
                    }
                    
                    if (DEBUG_REFSET_LEVEL_3) {
                        knd_log("    ++ term IDX read OK: num refs: %lu\n",
                                (unsigned long)num_refs);
                    }
                }
                else {
                    self->num_refs = num_refs;
                }
                
                return knd_OK;
            }
            
            id--;
            depth--;

            *id = '\0';
            
            in_term = false;
            break;
            
        case '_':
            
            if (in_tag) break;

            in_term = true;
            break;
        case ' ':
            if (!in_attr) break;

            namebuf_size = c - b;
            if (namebuf_size >= KND_NAME_SIZE) return knd_LIMIT;
            
            memcpy(namebuf, b, namebuf_size);
            namebuf[namebuf_size] = '\0';

            b = c + 1;
            break;
        case '{':
            if (!in_tag) {

                err = kndSortTag_new(&tag);
                if (err) return err;

                in_tag = true;
                break;
            }

            if (!in_attr) {
                in_attr = true;

                b = c + 1;
                break;
            }
            
            break;
        case '}':
            
            if (in_attr) {

                buf_size = c - b;
                if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;

                attr = malloc(sizeof(struct kndSortAttr));
                if (!attr) return knd_NOMEM;
                memset(attr, 0, sizeof(struct kndSortAttr));
                attr->type = KND_FACET_POSITIONAL;

                memcpy(attr->name, namebuf, namebuf_size);
                attr->name[namebuf_size] = '\0';
                attr->name_size = namebuf_size;

                memcpy(attr->val, b, buf_size);
                attr->val[buf_size] = '\0';
                attr->val_size = buf_size;

                if (self->parent) {
                    if (!strcmp(self->parent->name, attr->name)) {
                        attr->skip = true;
                    }
                }

                /* get facet type */
                s = strchr(attr->name, ':');
                if (s) {
                    *s = '\0';
                    s++;
                    err = knd_parse_num((const char*)s, &numval);
                    if (err) return err;
                    
                    attr->name_size = strlen(attr->name);
                    attr->type = (knd_facet_type)numval;
                }
                
                if (DEBUG_REFSET_LEVEL_3)
                    knd_log("    == attr complete %s: \"%s\" [TYPE: %d] skip: %d\n",
                            attr->name, attr->val, attr->type, attr->skip);
                
                tag->attrs[tag->num_attrs] = attr;
                tag->num_attrs++;
                
                in_attr = false;
                attr = NULL;
                break;
            }
            
            if (in_tag) {
                
                err = kndRefSet_term_idx_add_tag(self,
                                                 (const char*)idbuf,
                                                 tag);
                if (err) {
                    knd_log("  -- failed to add ref %s :(\n", idbuf);
                    return err;
                }
                
                in_tag = false;
                tag = NULL;
                
                num_refs++;
                break;
            }
            
            break;
        default:
            if (in_attr) break;

            if (in_tag) break;
            
            if (in_term) {
                memcpy(id, c, 1);
                
                if (DEBUG_REFSET_LEVEL_TMP)
                    knd_log("\n\n   == CURR ID: \"%s\" depth: %lu\n",
                            idbuf, (unsigned long)depth);

                
                /*err = kndObjRef_new(&ref);
                if (err) return err;

                ref->baseclass = self->baseclass;
        
                memcpy(ref->obj_id, idbuf, KND_ID_SIZE);
                ref->obj_id_size = KND_ID_SIZE;
                */
                
                
            }
            break;
        }
        
        c++;
    }
    
    return knd_FAIL;
}




static int
kndRefSet_read_term_idx(struct kndRefSet *self,
                        const char       *rec,
                        size_t            rec_size __attribute__((unused)))
{
    //char buf[KND_NAME_SIZE + 1];
    size_t buf_size = 0;

    char namebuf[KND_NAME_SIZE + 1];
    size_t namebuf_size = 0;

    char idbuf[KND_ID_SIZE + 1];
    size_t depth = 0;

    struct kndObjRef *ref = NULL;
    struct kndSortAttr *attr = NULL;
    struct kndSortTag *tag = NULL;

    const char *c;
    const char *b;
    char *s;
    
    bool in_term = false;

    bool in_tag = false;
    bool in_attr = false;

    char *id;
    size_t num_refs = 0;
    long numval = 0;
    
    int err = 0;


    if (DEBUG_REFSET_LEVEL_3)
        knd_log("   .. reading term IDX of refset \"%s\"   [DB num refs: %lu]\n",
                self->name, (unsigned long)self->num_refs);

    /*if (self->num_refs >= self->max_inbox_size) {
        knd_log("\n    == RefSet \"%s\" num refs: %lu  extra IDXs must be built!\n",
                self->name, (unsigned long)self->num_refs);
                }*/
    
    memset(idbuf, 0, KND_ID_SIZE + 1);

    c = rec;
    id = idbuf;
    
    while (*c) {
        switch (*c) {
        case '(':

            if (in_tag) break;
            
            if (depth > KND_ID_SIZE) {
                knd_log(" -- id size limit reached :(\n");
                return knd_LIMIT;
            }
            c++;
            memcpy(id, c, 1);
            id++;
            depth++;
            break;
            
        case ')':

            if (in_tag) break;

            if (!depth) {

                if (self->num_refs) {
                    
                    if (num_refs != self->num_refs) {
                        knd_log("   -- WARN: num refs not matched:  DB: %lu  ACTUAL: %lu\n",
                                (unsigned long)self->num_refs, (unsigned long)num_refs);
                        /*return knd_FAIL;*/
                    }
                    
                    if (DEBUG_REFSET_LEVEL_3) {
                        knd_log("    ++ term IDX read OK: num refs: %lu\n",
                                (unsigned long)num_refs);
                    }
                }
                else {
                    self->num_refs = num_refs;
                }
                
                return knd_OK;
            }
            
            id--;
            depth--;

            *id = '\0';
            
            in_term = false;
            break;
            
        case '_':
            
            if (in_tag) break;

            in_term = true;
            break;
        case ' ':
            if (!in_attr) break;

            namebuf_size = c - b;
            if (namebuf_size >= KND_NAME_SIZE) return knd_LIMIT;
            
            memcpy(namebuf, b, namebuf_size);
            namebuf[namebuf_size] = '\0';

            b = c + 1;
            break;
        case '{':
            if (!in_tag) {

                err = kndSortTag_new(&tag);
                if (err) return err;

                in_tag = true;
                break;
            }

            if (!in_attr) {
                in_attr = true;

                b = c + 1;
                break;
            }
            
            break;
        case '}':
            
            if (in_attr) {

                buf_size = c - b;
                if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;

                attr = malloc(sizeof(struct kndSortAttr));
                if (!attr) return knd_NOMEM;
                memset(attr, 0, sizeof(struct kndSortAttr));

                memcpy(attr->name, namebuf, namebuf_size);
                attr->name[namebuf_size] = '\0';
                attr->name_size = namebuf_size;

                memcpy(attr->val, b, buf_size);
                attr->val[buf_size] = '\0';
                attr->val_size = buf_size;

                if (self->parent) {
                    if (!strcmp(self->parent->name, attr->name)) {
                        attr->skip = true;
                    }
                }

                /* get facet type */
                s = strchr(attr->name, ':');
                if (s) {
                    *s = '\0';
                    s++;
                    err = knd_parse_num((const char*)s, &numval);
                    if (err) return err;
                    
                    attr->name_size = strlen(attr->name);
                    attr->type = (knd_facet_type)numval;
                }
                
                if (DEBUG_REFSET_LEVEL_3)
                    knd_log("    == attr complete %s: \"%s\" skip: %d\n",
                            attr->name, attr->val, attr->skip);
                
                tag->attrs[tag->num_attrs] = attr;
                tag->num_attrs++;

                /* AZ -> name */
                if (!strcmp(attr->name, "AZ")) {
                    memcpy(ref->name, attr->val, attr->val_size);
                    ref->name_size = attr->val_size;
                }
                
                in_attr = false;
                attr = NULL;
                break;
            }
            
            if (in_tag) {
                /* REF COMPLETE */
                ref->sorttag = tag;
                
                err = kndRefSet_term_idx(self, ref);
                if (err) {
                    knd_log("  -- failed to add ref %s :(\n", ref->obj_id);
                    return err;
                }
                

                /* TODO: centralized storage of OBJ sorting ATTRS */
                if (!self->tags_needed) {
                    tag->del(tag);
                    ref->sorttag = NULL;
                }
                
                in_tag = false;
                tag = NULL;
                ref = NULL;
                
                num_refs++;
                break;
            }
            
            break;
        default:
            if (in_attr) break;

            if (in_tag) break;
            
            if (in_term) {
                memcpy(id, c, 1);
                
                if (DEBUG_REFSET_LEVEL_4)
                    knd_log("\n\n   == CURR ID: \"%s\" depth: %lu\n",
                            idbuf, (unsigned long)depth);

                err = kndObjRef_new(&ref);
                if (err) return err;

                ref->baseclass = self->baseclass;
        
                memcpy(ref->obj_id, idbuf, KND_ID_SIZE);
                ref->obj_id_size = KND_ID_SIZE;

                
            }
            break;
        }
        
        c++;
    }
    
    return knd_FAIL;
}




static int
kndRefSet_read_inbox(struct kndRefSet *self,
                     const char        *rec,
                     size_t            rec_size)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size = KND_TEMP_BUF_SIZE;

    char recbuf[KND_TEMP_BUF_SIZE];
    //size_t recbuf_size = KND_TEMP_BUF_SIZE;

    struct kndObjRef *ref;
    struct kndElemRef *elemref;
    
    const char *delim = KND_FIELD_SEPAR;
    char *last = NULL;
    char *ref_rec;
    char *b;
    char *c;
    char *n;
    long space_left;
    long numval;
    //size_t curr_size = 0;
    size_t num_refs = 0;
    int err;

    if (DEBUG_REFSET_LEVEL_3)
        knd_log("    .. reading Inbox: \"%s\" [%lu]\n",
                rec, (unsigned long)rec_size);

    if (rec_size >= KND_TEMP_BUF_SIZE) {
        knd_log("  -- inbox rec too large :(\n");
        return knd_LIMIT;
    }
    
    memcpy(recbuf, rec, rec_size);
    recbuf[rec_size - 1] = '\0';
    
    if (strncmp(rec, GSL_OPEN_DELIM, GSL_OPEN_DELIM_SIZE)) {
        knd_log("  -- incorrect inbox: doesn't start with an open delim :(\n");
        return knd_FAIL;
    }

    c = recbuf;
    space_left = rec_size - GSL_OPEN_DELIM_SIZE;

    if (space_left < 1) return knd_FAIL;
    c += GSL_OPEN_DELIM_SIZE;

    if (strncmp(c, KND_REFSET_INBOX_TAG, KND_REFSET_INBOX_TAG_SIZE))
        return knd_FAIL;

    c += KND_REFSET_INBOX_TAG_SIZE;
    
    /* parse inbox */
    for (ref_rec = strtok_r(c, delim, &last);
         ref_rec;
         ref_rec = strtok_r(NULL, delim, &last)) {
        
        /*knd_log("REF: %s\n", ref_rec);*/

        err = kndObjRef_new(&ref);
        if (err) return err;

        ref->baseclass = self->baseclass;
        
        n = strstr(ref_rec, GSL_OFFSET);
        if (n) {
            n += GSL_OFFSET_SIZE;
            
            /* get the numeric offset */
            err = knd_parse_num((const char*)n, &numval);
            if (err) goto final;

            /*ref->hilite_pos = (size_t)numval;*/
            /* TODO: hilite len  */
        }

        memcpy(ref->obj_id, ref_rec, KND_ID_SIZE);
        ref->obj_id_size = KND_ID_SIZE;

        ref_rec += KND_ID_SIZE;
        
        /* the rest of the obj name */
        if (*ref_rec == '~') {
            ref_rec++;
            ref->name_size = strlen(ref_rec);
            memcpy(ref->name, ref_rec, ref->name_size);
            goto assign;
        }
        
        b = strchr(ref_rec, '/');
        if (!b) goto assign;

        /* elem ref */
        ref->type = KND_REF_ELEM;

        elemref = malloc(sizeof(struct kndElemRef));
        if (!elemref) {
            err = knd_NOMEM;
            goto final;
        }
        memset(elemref, 0, sizeof(struct kndElemRef));

        /* elem abbrev? */
        c = strchr(ref_rec, '~');
        if (c && c < b) {
            buf_size = b - c - 1;
            memcpy(buf, c + 1, buf_size);
            buf[buf_size] = '\0';

            /*knd_log("  == ABBR: %s\n", buf);*/
            /*elem = self->baseclass->elems;
            while (elem) {
                
                
                elem = elem->next;
                }*/
            
        }

        
        buf_size = strlen(b);
        if (elemref->name_size + buf_size >= KND_NAME_SIZE) {
            err = knd_LIMIT;
            goto final;
        }
        c = elemref->name + elemref->name_size;

        memcpy(c, b, buf_size);
        c[buf_size] = '\0';
        elemref->name_size += buf_size;
        
        if (DEBUG_REFSET_LEVEL_3)
            knd_log("   == FULL ELEMREF: \"%s\" [%lu]\n",
                    elemref->name, (unsigned long)elemref->name_size);
        
        elemref->next = ref->elemrefs;
        ref->elemrefs = elemref;

        
        /* detect coderefs */
        c = strchr(elemref->name, '@');
        if (!c)
            goto assign;

        /* conceptual ref */
        ref->type = KND_REF_CODE;

        /* truncate elemref name */
        elemref->name_size = c - elemref->name;
        elemref->name[elemref->name_size] = '\0';

        if (DEBUG_REFSET_LEVEL_3)
            knd_log("    ELEMREF: \"%s\" [%lu]\n\n",
                    elemref->name, (unsigned long)elemref->name_size);

        err = ref->read_coderefs(ref, elemref, c + 1);
        if (err) goto final;
        
        /* finally: assign the result to RefSet */
    assign:

        /* NB: unsorted refs! */
        if (self->inbox_size >= self->max_inbox_size) return knd_LIMIT;

        self->inbox[self->inbox_size] = ref;
        self->inbox_size++;
        num_refs++;
    }


    if (num_refs != self->num_refs) {
        knd_log("   -- num refs not matched:  DB: %lu  ACTUAL: %lu\n",
                self->num_refs, num_refs);
        return knd_FAIL;
    }
    
    err = knd_OK;
    
 final:

    /* TODO: release resources */
    
    return err;
}


static int
kndRefSet_read(struct kndRefSet *self,
               const char       *rec,
               size_t            rec_size)
{
    char buf[KND_LARGE_BUF_SIZE];
    size_t buf_size = 0;

    char namebuf[KND_NAME_SIZE];
    size_t namebuf_size = 0;

    //char recbuf[KND_NAME_SIZE];
    //size_t recbuf_size = 0;

    //struct kndObjRef *ref;
    struct kndFacet *f;
    struct kndRefSet *r;
    
    const char *facet_rec;
    size_t facet_rec_size;
    
    char *val;
    char *c;
    long numval;
    size_t offset = 0;
    size_t curr_size;
    size_t num_refs = 0;
    
    const char *delim = KND_FIELD_SEPAR;
    char *last = NULL;
    
    int err;

    if (DEBUG_REFSET_LEVEL_3) {
       knd_log("\n\n   .. Reading Refset \"%s\" IDX rec, input size [%lu]   depth: %lu\n",
               self->name, (unsigned long)rec_size, (unsigned long)self->export_depth);
    }
    
    err = knd_get_trailer(rec, rec_size,
                          namebuf, &namebuf_size,
                          &num_refs,
                          buf, &buf_size);
    if (err) return err;
    
    if (self->export_depth && namebuf_size) {
        memcpy(self->name, namebuf, namebuf_size);
        self->name[namebuf_size] = '\0';
        self->name_size = namebuf_size;
    }
    
    if (DEBUG_REFSET_LEVEL_3)
        knd_log("   == DIR REC: %s [size: %lu]\n\n",
                buf, (unsigned long)buf_size);
    
    /* valid rec found */
    if (!buf_size) {
        if (*(rec + 1) == '_') {
            err =  kndRefSet_read_term_idx(self,
                                           rec + 1 +  KND_REFSET_TERM_TAG_SIZE,
                                           rec_size);
            if (err) return err;

            return knd_OK;
        }
        
        err = kndRefSet_read_inbox(self, rec, rec_size);
        return err;
    }

    self->num_refs = num_refs;

    /* parse trailer */
    for (c = strtok_r(buf, delim, &last);
         c; // fixme
         c = strtok_r(NULL, delim, &last)) {

        /* find the offset field */
        val = strstr(c, GSL_OFFSET);
        if (!val) continue;

        curr_size = val - c;
        
        *val = '\0';
        val++;
        
        if (DEBUG_REFSET_LEVEL_3)
            knd_log("   == Facet: \"%s\" offset: %s\n", c, val);
        
        /* TODO: check facet name! */
        
        /* confirm intersection? */
        /*if (class_set) {
            knd_log("  check other classes...\n");
            }*/

        /* get the numeric offset */
        err = knd_parse_num((const char*)val, &numval);
        if (err) return err;
        
        facet_rec_size = (size_t)numval;
        facet_rec = rec + offset;

        err = kndRefSet_get_facet(self, (const char*)c, curr_size, &f);
        if (err) return err;

        f->export_depth = self->export_depth + 1;
        
        err = f->read(f, facet_rec, facet_rec_size);
        if (err) return err;
        
        /* merge IDX */
        for (size_t i = 0; i < f->num_refsets; i++) {
            r = f->refsets[i];

            err = kndRefSet_merge_idx(self, r);
            if (err) return err;
        }
        
        offset += facet_rec_size;
    }

    return knd_OK;
}


static int
kndRefSet_extract_objs(struct kndRefSet *self)
{
    struct kndFacet *f;
    struct kndObjRef *ref;
    struct kndTermIdx *idx, *term_idx;
    
    int err;

    if (DEBUG_REFSET_LEVEL_3) {
        knd_log("    .. RefSet \"%s\" expanding refs..  batch to fill: %lu\n",
                self->name, (unsigned long)self->batch_size);
    }
    
    for (size_t i = 0; i < self->inbox_size; i++) {
        ref = self->inbox[i];
        
        ref->cache = self->cache;
        
        err = ref->expand(ref);
        if (err) return err;

        self->cache->matches[self->cache->num_matches] = ref->obj;
        self->cache->num_matches++;

        if (self->cache->num_matches >= self->batch_size)
            break;
    }

    if (self->num_facets) {

        /* TODO: choose facet */
        f = self->facets[0];
        f->batch_size = self->batch_size;
        
        err = f->extract_objs(f);
        if (err) return err;

        return knd_OK;
    }


    /* terminal IDX */
    if (!self->idx) return knd_OK;
    
    for (size_t i = 0; i < KND_ID_BASE; i++) {
        idx = self->idx[i];
        if (!idx) continue;

        for (size_t j = 0; j < KND_ID_BASE; j++) {
            term_idx = idx->idx[j];
            if (!term_idx) continue;

            for (size_t k = 0; k < KND_ID_BASE; k++) {
                ref = term_idx->refs[k];
                if (!ref) continue;

                ref->cache = self->cache;
                
                err = ref->expand(ref);
                if (err) {
                    if (DEBUG_REFSET_LEVEL_TMP) {
                        knd_log("  -- couldn't expand ref \"%s\" :(\n",
                                ref->obj_id);
                    }
                    return err;
                }
                
                /* primary or subordinate OBJ? */
                if (ref->obj->is_subord)
                    continue;
                
                self->cache->matches[self->cache->num_matches] = ref->obj;
                self->cache->num_matches++;
                
                if (self->cache->num_matches >= self->batch_size)
                    return knd_OK;
            }
            
        }
    }

    
    return knd_OK;
}



static int
kndRefSet_export_summaries(struct kndRefSet *self,
                           knd_format format,
                           size_t depth,
                           size_t num_items)
{
    struct kndFacet *f;
    struct kndObjRef *ref;
    struct kndObject *obj;
    struct kndRefSet *rs;
    
    //size_t curr_size = 0;
    int err;

    
    if (DEBUG_REFSET_LEVEL_3)
        knd_log("    .. RefSet \"%s\" exporting obj summaries, depth: %lu\n",
                self->name, (unsigned long)depth);

    if (self->inbox_size) {
        for (size_t i = 0; i < self->inbox_size; i++) {
            ref = self->inbox[i];
            
            if (num_items) {
                err = self->out->write(self->out,  ",", 1);
                if (err) return err;
            }

            obj = self->cache->db->get(self->cache->db, ref->obj_id);
            if (!obj) return knd_FAIL;
        
            obj->out = self->out;
            obj->export_depth = 0;
            err = obj->export(obj, format, 1);
            if (err) return err;
            num_items++;
        }
        self->batch_size = num_items;
    }
    
    if (depth > self->export_depth) {
        return knd_OK;
    }

    for (size_t i = 0; i < self->num_facets; i++) {
        f = self->facets[i];
        for (size_t j = 0; j < f->num_refsets; j++) {
            rs = f->refsets[j];

            rs->out = self->out;
            err = rs->export_summaries(rs, format, depth + 1, num_items);
            if (err) return err;

            num_items += rs->batch_size;
        }
    }

    return knd_OK;
}


static int
kndRefSet_sync_objs(struct kndRefSet *self,
                    const char *path)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size = KND_TEMP_BUF_SIZE;

    unsigned char  root_dir[KND_ID_BASE * KND_MAX_INT_SIZE];
    size_t root_dir_size = KND_ID_BASE * KND_MAX_INT_SIZE;
    //size_t root_start_pos = 0;
    unsigned char *rd;

    unsigned char  med_dir[KND_ID_BASE * KND_MAX_INT_SIZE];
    size_t med_dir_size = KND_ID_BASE * KND_MAX_INT_SIZE;
    //size_t med_start_pos = 0;
    unsigned char *md;

    unsigned char term_dir[KND_ID_BASE * KND_MAX_INT_SIZE];
    size_t term_dir_size = KND_ID_BASE * KND_MAX_INT_SIZE;

    //size_t term_start_pos = 0;
    unsigned char *td;

    //unsigned char numbuf[KND_MAX_INT_SIZE];
    
    struct kndTermIdx *idx, *term_idx;
    struct kndObjRef *ref;
    struct kndObject *obj;

    size_t rec_size = 0;

    size_t root_offset = 0;
    size_t med_offset = 0;
    size_t rec_offset = 0;

    //unsigned char *c;
    //unsigned long numval = 0;

    const char *format_code = "GSC";
    size_t format_code_size = strlen(format_code);
    
    size_t i, j, ri, count;
    int err;

    /* terminal IDX */
    if (!self->idx) {
        knd_log("  -- no obj IDX :(\n");
        return knd_FAIL;
    }
    
    err = knd_write_file(path,
                         "objs.gsc", (void*)format_code, format_code_size);
    if (err) {
        knd_log("  -- file objs.gsc couldn't be opened in \"%s\" :(\n", path);
        return err;
    }
    
    buf_size = sprintf(buf, "%s/objs.gsc", path);

    memset(root_dir, 0, root_dir_size);
    rd = root_dir;
    root_offset = format_code_size;

    /*knd_log(" .. filename: %s\n", buf); */

    count = 0;
    
    /* first pos */
    for (i = 0; i < KND_ID_BASE; i++) {
        idx = self->idx[i];
        if (!idx) continue;

        if (DEBUG_REFSET_LEVEL_3)
            knd_log("%lu [tot:%lu]\n",
                    (unsigned long)i,
                    (unsigned long)idx->num_refs);
        
        memset(med_dir, 0, med_dir_size);
        md = med_dir;
        med_offset = 0;
        
        /* second pos */
        for (j = 0; j < KND_ID_BASE; j++) {
            term_idx = idx->idx[j];
            if (!term_idx) continue;

            memset(term_dir, 0, term_dir_size);
            td = term_dir;

            rec_offset = 0;

            self->out->reset(self->out);

            /* last pos */
            for (ri = 0; ri < KND_ID_BASE; ri++) {
                ref = term_idx->refs[ri];
                if (!ref) {
                    td = knd_pack_int(td, (unsigned int)0);
                    continue;
                }
                
                /*if (!ref->obj_id_size) continue;*/
                /*knd_log("%d)\n", ri);
                  ref->str(ref, 1); */

                if (!ref->obj) {
                    obj = self->cache->db->get(self->cache->db, ref->obj_id);
                    if (!obj) {
                        knd_log("  -- no obj found: %s\n", ref->obj_id);
                        return knd_FAIL;
                    }
                    ref->obj = obj;
                }

                obj = ref->obj;
                obj->out = self->out;

                err = obj->export(obj, KND_FORMAT_GSC, 0);
                if (err) {
                    knd_log("  -- obj \"%s\" not exported :(\n", ref->obj_id);
                    return err;
                }
                
                if (DEBUG_REFSET_LEVEL_3)
                    knd_log("%d) OBJ \"%s\" REC offset: %lu CURR BUF SIZE: %lu count: %lu\n",
                            ri, obj->id, (unsigned long)rec_offset,
                            (unsigned long)self->out->buf_size,
                            (unsigned long)count);
                
                td = knd_pack_int(td, (unsigned int)rec_offset);
                rec_offset = self->out->buf_size;
                count++;
            }

            /* save the directory offsets */
            err = knd_append_file((const char*)buf,
                                  self->out->buf, self->out->buf_size);
            if (err) {
                knd_log("  -- term items not written :(\n");
                return err;
            }
            
            err = knd_append_file((const char*)buf,
                                   term_dir, term_dir_size); 
            if (err) {
                knd_log("  -- term directory not written :(\n");
                return err;
            }
            
            rec_size = self->out->buf_size + term_dir_size;  

            md = knd_pack_int(md, (unsigned int)med_offset);
            
            if (DEBUG_REFSET_LEVEL_3) {
                knd_log("|_%lu [tot:%lu]\n",
                        (unsigned long)j,
                        (unsigned long)term_idx->num_refs);
                knd_log("     MED offset: %lu\n", (unsigned long)med_offset);
            }
            
            med_offset += rec_size;
        }

        
        /* output med dir */
        err = knd_append_file((const char*)buf,
                              med_dir, med_dir_size); 
        if (err) {
            knd_log("  -- med directory not written :(\n");
           return err;
        }
        
        rec_size = med_offset + med_dir_size;  

        rd = knd_pack_int(rd, (unsigned int)root_offset);

        if (DEBUG_REFSET_LEVEL_3)
            knd_log("%d)  ROOT offset: %lu\n",
                    i, (unsigned long)root_offset);
        
        root_offset += rec_size;
    }

    
    err = knd_append_file((const char*)buf,
                          root_dir, root_dir_size); 
    if (err) return err;
    
    return knd_OK;
}

static int
kndRefSet_sync_idx(struct kndRefSet *self)
{
    struct kndTermIdx *idx, *term_idx;
    struct kndOutput *out;
    struct kndObjRef *ref;
    int i, j, ri, err = knd_OK;


    if (DEBUG_REFSET_LEVEL_3)
        knd_log("   .. syncing term IDX of \"%s\"..\n", self->name);

    
    out = self->out;

    err = out->write(out, 
                     GSL_OPEN_DELIM, GSL_OPEN_DELIM_SIZE); 
    if (err) return err;

    err = out->write(out, 
                     KND_REFSET_TERM_TAG,
                     KND_REFSET_TERM_TAG_SIZE);
    if (err) return err;
            
    for (i = 0; i < KND_ID_BASE; i++) {
        idx = self->idx[i];
        if (!idx) continue;

        err = out->write(out, 
                         GSL_OPEN_DELIM, GSL_OPEN_DELIM_SIZE); 
        if (err) return err;

        err = out->write(out, 
                         obj_id_seq + i, 1); 
        if (err) return err;
            
        for (j = 0; j < KND_ID_BASE; j++) {
            term_idx = idx->idx[j];
            if (!term_idx) continue;
                    
            err = out->write(out, 
                             GSL_OPEN_DELIM, GSL_OPEN_DELIM_SIZE); 
            if (err) return err;
            
            err = out->write(out, 
                             obj_id_seq + j, 1); 
            if (err) return err;
                    
            err = out->write(out, 
                             "_", 1); 
            if (err) return err;
            
            for (ri = 0; ri < KND_ID_BASE; ri++) {
                ref = term_idx->refs[ri];
                if (!ref) continue;
                
                err = out->write(out, 
                                 obj_id_seq + ri, 1); 
                if (err) return err;


                /* add sort tag */

                ref->out = out;
                err = ref->export(ref, KND_FORMAT_GSC);
                if (err) return err;
                
            }
                    
            err = out->write(out,
                             GSL_CLOSE_DELIM, GSL_CLOSE_DELIM_SIZE); 
                    if (err) return err;
        }
        
        err = out->write(out,
                         GSL_CLOSE_DELIM, GSL_CLOSE_DELIM_SIZE); 
        if (err) return err;
    }
    
    err = out->write(out, 
                     GSL_CLOSE_DELIM, GSL_CLOSE_DELIM_SIZE); 
    if (err) return err;

    return knd_OK;
}


static int
kndRefSet_sync(struct kndRefSet *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size = 0;

    char rec_buf[KND_TEMP_BUF_SIZE];
    size_t rec_size = 0;

    struct kndFacet *f;
    struct kndOutput *out;
    struct kndObjRef *ref;

    char *rec;
    //char *c;
    size_t curr_size = 0;
    size_t trailer_size, inbox_rec_size;
    size_t trailer_pos;
    size_t start_pos;

    int err = knd_OK;

    if (DEBUG_REFSET_LEVEL_3)
        knd_log("\nREFSET \"%s\" in sync...\n", self->name);

    rec = rec_buf;
    out = self->out;

    start_pos = out->free_space;
    
    /* linearize the inbox */
    if (self->inbox_size) {
        if (!self->num_facets) {
            for (size_t i = 0; i < self->inbox_size; i++) {
                ref = self->inbox[i];

                /*ref->str(ref, 1);*/
                
                err = kndRefSet_term_idx(self, ref);
                if (err) return err;

                self->inbox[i] = NULL;
            }

            goto sync_idx;
        }

        err = out->write(out, 
                          GSL_OPEN_DELIM, GSL_OPEN_DELIM_SIZE); 
        if (err) return err;

        err = out->write(out, 
                           KND_REFSET_INBOX_TAG,
                           KND_REFSET_INBOX_TAG_SIZE);
        if (err) return err;

        /*err = out->write(out,  
                          GSL_TERM_SEPAR, GSL_TERM_SEPAR_SIZE);
        if (err) return err;
        */
        
        for (size_t i = 0; i < self->inbox_size; i++) {
            ref = self->inbox[i];
            if (i) {
                err = out->write(out, 
                                  KND_FIELD_SEPAR, KND_FIELD_SEPAR_SIZE); 
                if (err) return err;
            }
            
            /*ref->str(ref, 1);*/

            ref->out = out;
            err = ref->sync(ref);
            if (err) return err;
        }

        err = out->write(out, 
                          GSL_CLOSE_DELIM, GSL_CLOSE_DELIM_SIZE); 
        if (err) return err;
    }

    
 sync_idx:

    
    if (!self->num_facets) {
        if (self->idx) {
            err = kndRefSet_sync_idx(self);
            if (err) return err;
        }
    }
        
    inbox_rec_size = start_pos - out->free_space;
    
    /* linearize facets  */
    for (size_t i = 0; i < self->num_facets; i++) {
        f = self->facets[i];

        f->out = out;
        
        err = f->sync(f);
        if (err) return err;

        if (i) {
            memcpy(rec, KND_FIELD_SEPAR, KND_FIELD_SEPAR_SIZE); 
            rec += KND_FIELD_SEPAR_SIZE;
            rec_size += KND_FIELD_SEPAR_SIZE;
        }

        memcpy(rec, f->name, f->name_size);
        rec += f->name_size;
        rec_size += f->name_size;

        if (f->rec_size) {
            curr_size = sprintf(rec, "@%lu",
                                (unsigned long)f->rec_size);
            rec += curr_size;
            rec_size += curr_size;
        }
    }

    /* write trailer */
    if (rec_size) {
        trailer_pos = out->free_space;
        
        err = out->write(out,  GSL_OPEN_DELIM,
                GSL_OPEN_DELIM_SIZE);
        if (err) return err;
        
        err = out->write(out,  self->name, self->name_size);
        if (err) return err;

        if (self->num_refs > 1) {
            curr_size = sprintf(buf, "=%lu",
                                (unsigned long)self->num_refs);
            err = out->write(out,  
                              buf, curr_size);
            if (err) return err;
        }
        
        err = out->write(out,  
                          GSL_TERM_SEPAR, GSL_TERM_SEPAR_SIZE);
        if (err) return err;

        /*if (inbox_rec_size) {
            curr_size = sprintf(rec_buf, "%lu", (unsigned long)trailer_size);
            err = out->write(out,  
                              rec_buf, curr_size);
            if (err) return err;
            }*/
        
        err = out->write(out,  rec_buf, rec_size);
        if (err) return err;

        err = out->write(out,  
                          GSL_CLOSE_DELIM,
                          GSL_CLOSE_DELIM_SIZE);
        if (err) return err;

        trailer_size = trailer_pos - out->free_space;

        curr_size = sprintf(rec_buf, "%lu", (unsigned long)trailer_size);
        err = out->write(out,  
                          rec_buf, curr_size);
        if (err) return err;
    }

    self->rec_size = start_pos - out->free_space;  

    if (DEBUG_REFSET_LEVEL_3)
        knd_log("REFSET REC sync OK! [%lu]\n",
                (unsigned long)self->rec_size);
    
    return knd_OK;
}



extern int 
kndRefSet_new(struct kndRefSet **refset)
{
    struct kndRefSet *self;

    self = malloc(sizeof(struct kndRefSet));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndRefSet));

    self->export_depth = 1;
    self->max_inbox_size = KND_MAX_INBOX_SIZE;
    
    self->del = kndRefSet_del;
    self->str = kndRefSet_str;

    self->term_idx = kndRefSet_term_idx;
    self->read = kndRefSet_read;
    self->read_tags = kndRefSet_read_tags;

    self->add_ref = kndRefSet_add_ref;

    self->lookup_name = kndRefSet_lookup_name;
    self->lookup_ref = kndRefSet_lookup_ref;
    self->find = kndRefSet_find;

    self->extract_objs = kndRefSet_extract_objs;
    self->export_summaries = kndRefSet_export_summaries;
    self->intersect = kndRefSet_intersect;
    self->contribute = kndRefSet_contribute;

    self->facetize = kndRefSet_facetize;
    
    self->sync = kndRefSet_sync;
    self->sync_objs = kndRefSet_sync_objs;
    self->export = kndRefSet_export;

    *refset = self;

    return knd_OK;
}
