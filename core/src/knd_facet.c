#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_output.h"
#include "knd_objref.h"
#include "knd_sorttag.h"
#include "knd_refset.h"

#include "../../src/data_reader/knd_data_reader.h"
#include "../../src/data_writer/knd_data_writer.h"

#define DEBUG_FACET_LEVEL_0 0
#define DEBUG_FACET_LEVEL_1 0
#define DEBUG_FACET_LEVEL_2 0
#define DEBUG_FACET_LEVEL_3 0
#define DEBUG_FACET_LEVEL_4 0
#define DEBUG_FACET_LEVEL_TMP 1

static int
kndFacet_sync(struct kndFacet *self);

/*static int
kndFacet_export_item(struct kndObjRef *item,
                       char *output,
                       size_t *output_size);
*/

static int 
knd_compare_refset_by_alph_ascend(const void *a,
                                  const void *b)
{
    struct kndRefSet **obj1, **obj2;

    obj1 = (struct kndRefSet**)a;
    obj2 = (struct kndRefSet**)b;

    if ((*obj1)->numval == (*obj2)->numval) return 0;

    if ((*obj1)->numval > (*obj2)->numval) return 1;

    return -1;
}


static void
kndFacet_del(struct kndFacet *self)
{
    struct kndRefSet *rs;

    for (size_t i = 0; i < self->num_refsets; i++) {
        rs = self->refsets[i];
        rs->del(rs);
    }

    free(self);
}

static int 
kndFacet_str(struct kndFacet *self,
             size_t depth,
             size_t max_depth)
{
    //struct kndObjRef *ref;
    struct kndRefSet *rs;
    
    size_t i, offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;
    char *offset = malloc(offset_size + 1);
    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    if (depth > max_depth) return knd_OK;
    
    knd_log("\n%s|__Facet \"%s\" type: %d [num refsets: %lu]",
            offset, self->name, self->type,
            (unsigned long)self->num_refsets);

    if (self->unit_name_size) {
        knd_log(" (unit: %s)", self->unit_name);
    }

    knd_log("\n");
    
    qsort(self->refsets,
          self->num_refsets,
          sizeof(struct kndRefSet*), knd_compare_refset_by_alph_ascend);
    
    for (i = 0; i < self->num_refsets; i++) {
        rs = self->refsets[i];
        rs->str(rs, depth + 1, max_depth);
    }

    free(offset);
    
    return knd_OK;
}




static int 
kndFacet_export_JSON(struct kndFacet *self, size_t depth)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    struct kndOutput *out = self->out;

    struct kndRefSet *rs;
    size_t curr_batch_size = 0;
    int err;

    buf_size = sprintf(buf,
                       "{\"n\":\"%s\"", self->name);
    err = out->write(out,
                      buf, buf_size);
    if (err) return err;

    err = out->write(out,  
                      ",\"sets\":[", strlen(",\"sets\":["));
    if (err) return err;

    for (size_t i = 0; i < self->num_refsets; i++) {
        rs = self->refsets[i];

        if (i) {
            err = out->write(out,  ",", 1);
            if (err) return err;
        }

        rs->out = out;
        rs->export_depth = self->export_depth;

        err = rs->export(rs, KND_FORMAT_JSON, depth);
        if (err) return err;
    }

    err = out->write(out,  
                      "]", 1);
    if (err) return err;

    err = out->write(out,  "}", 1);
    if (err) return err;
    
    self->batch_size = curr_batch_size;

    return err;
}



static int 
kndFacet_export_HTML(struct kndFacet *self, size_t depth)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    struct kndOutput *out = self->out;

    struct kndRefSet *rs;
    size_t curr_batch_size = 0;
    int err;

    buf_size = sprintf(buf,
                       "{\"n\":\"%s\"", self->name);
    err = out->write(out,
                      buf, buf_size);
    if (err) return err;

    err = out->write(out,  
                      ",\"sets\":[", strlen(",\"sets\":["));
    if (err) return err;

    for (size_t i = 0; i < self->num_refsets; i++) {
        rs = self->refsets[i];

        if (i) {
            err = out->write(out,  ",", 1);
            if (err) return err;
        }

        rs->out = out;
        rs->export_depth = self->export_depth;

        err = rs->export(rs, KND_FORMAT_HTML, depth);
        if (err) return err;
    }

    err = out->write(out,  
                      "]", 1);
    if (err) return err;

    err = out->write(out,  "}", 1);
    if (err) return err;
    
    self->batch_size = curr_batch_size;

    return err;
}

static int 
kndFacet_export(struct kndFacet   *self,
                 knd_format         format,
                 size_t depth)
{
    int err;
    
    switch(format) {
    case KND_FORMAT_JSON:
        err = kndFacet_export_JSON(self, depth);
        if (err) return err;
        break;
    case KND_FORMAT_HTML:
        err = kndFacet_export_HTML(self, depth);
        if (err) return err;
        break;
    default:
        break;
    }
    
    return knd_OK;
}


/*
static int
kndFacet_check_facet_name(struct kndFacet *self __attribute__((unused)),
                          const char   *name,
                          size_t       name_size)
{
    const char *c;
    
    // no more sorting elems: stay in the same facet
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
kndFacet_get_refset(struct kndFacet  *self,
                    const char       *refset_name,
                    size_t           refset_name_size,
                    size_t           numval,
                    struct kndRefSet **result)
{
    struct kndRefSet *rs;
    int err;
    
    for (size_t i = 0; i < self->num_refsets; i++) {
        rs = self->refsets[i];

	if (!strcmp(rs->name, refset_name)) {
            *result = rs;
            
            return knd_OK;
        }
    }

    /*knd_log("   .. create new refset..\n"); */

    if (self->num_refsets + 1 >= KND_MAX_ATTRS) {

        if (DEBUG_FACET_LEVEL_TMP)
            knd_log("   -- Facet \"%s\": MAX limit of refsets reached. Couldn't add refset \"%s\" :(\n",
                    self->name, refset_name);

        return knd_NOMEM;
    }


    
    /*knd_log("  .. creating refset %s [%lu]..\n",
            refset_name,  (unsigned long)refset_name_size);
    */
    /* refset not found, create one */
    err = kndRefSet_new(&rs);
    if (err) return err;

    
    memcpy(rs->name, refset_name, refset_name_size);
    rs->name[refset_name_size] = '\0';
    
    rs->name_size = refset_name_size;
    rs->numval = numval;

    rs->baseclass = self->baseclass;
    rs->cache = self->cache;
    rs->parent = self;
    rs->out = self->out;
    
    self->refsets[self->num_refsets] = rs;
    self->num_refsets++;

    *result = rs;
    
    return knd_OK;
}



static int
kndFacet_add_conc_base(struct kndFacet  *self,
                       struct kndObjRef *orig_ref,
                       size_t attr_id)
{
    //char unit_name[KND_LABEL_SIZE];
    //size_t unit_name_size;

    //char val_name[KND_LABEL_SIZE];
    //size_t val_name_size;
    
    struct kndRefSet *refset = NULL;
    struct kndObjRef *ref = NULL;
    struct kndSortAttr *attr;
    
    char *b;
    //char *c;
    char *tail = NULL;
    size_t tail_size = 0;
    
    size_t sort_elem_size;
    //long numval = 0;
    int err;

    attr = orig_ref->sorttag->attrs[attr_id];
    
    /* extract curr facet value,
       save the remainder */

    sort_elem_size = attr->val_size;

    b = strchr(attr->val, '/');
    if (b) {
        sort_elem_size = b - attr->val;
        tail_size = attr->val_size - sort_elem_size - 1;
        *b = '\0';
        if (tail_size)
            tail = b + 1;
    }
    
    if (DEBUG_FACET_LEVEL_TMP)
        knd_log("\n     .. Facet \"%s\", add CONC base:   current: \"%s\" [%lu] Tail: \"%s\" [%lu]\n",
                self->name, attr->val,
                (unsigned long)sort_elem_size,
                tail, (unsigned long)tail_size);
    
    err = kndFacet_get_refset(self,
                              (const char*)attr->val,
                              strlen(attr->val), 0, &refset);
    if (err) return err;

    if (DEBUG_FACET_LEVEL_TMP)
        knd_log("  + got refset: %s [inbox: %lu]\n",
                refset->name, (unsigned long)refset->inbox_size);
    
    /* add ref */
    err = orig_ref->clone(orig_ref, attr_id, tail, tail_size, &ref); 
    if (err) return err;

    if (DEBUG_FACET_LEVEL_TMP) {
        ref->str(ref, 0);
        knd_log("   == input refset: %s [inbox: %lu]\n",
            refset->name, (unsigned long)refset->inbox_size);
    }
    
    err = refset->add_ref(refset, ref);
    if (err) goto cleanup;

    return knd_OK;
    
 cleanup:
    
    if (ref)
        ref->del(ref);

    
    return err;
}

static int
kndFacet_add_conc_spec(struct kndFacet  *self,
                       struct kndObjRef *orig_ref,
                       size_t attr_id)
{
    //char unit_name[KND_LABEL_SIZE];
    //size_t unit_name_size;

    //char val_name[KND_LABEL_SIZE];
    //size_t val_name_size;

    struct kndRefSet *refset = NULL;
    struct kndObjRef *ref = NULL;
    struct kndSortAttr *attr;

    char *b;
    //char *c;
    char *tail = NULL;
    size_t tail_size = 0;

    size_t sort_elem_size;
    //long numval = 0;
    int err;

    attr = orig_ref->sorttag->attrs[attr_id];

    /* extract curr facet value,
       save the remainder */

    sort_elem_size = attr->val_size;

    b = strchr(attr->val, '/');
    if (b) {
        sort_elem_size = b - attr->val;
        tail_size = attr->val_size - sort_elem_size - 1;
        *b = '\0';
        if (tail_size)
            tail = b + 1;
    }
    
    if (DEBUG_FACET_LEVEL_TMP)
        knd_log("\n     .. Facet \"%s\", add CONC spec:   current: \"%s\" [%lu] Tail: \"%s\" [%lu]\n",
                self->name, attr->val,
                (unsigned long)sort_elem_size,
                tail, (unsigned long)tail_size);
    
    err = kndFacet_get_refset(self,
                              (const char*)attr->val,
                              strlen(attr->val), 0, &refset);
    if (err) return err;

    if (DEBUG_FACET_LEVEL_TMP)
        knd_log("  + got refset: %s [inbox: %lu]\n",
                refset->name, (unsigned long)refset->inbox_size);
    
    /* add ref */
    err = orig_ref->clone(orig_ref, attr_id, tail, tail_size, &ref); 
    if (err) return err;

    if (DEBUG_FACET_LEVEL_TMP) {
        ref->str(ref, 0);
        knd_log("   == input refset: %s [inbox: %lu]\n",
            refset->name, (unsigned long)refset->inbox_size);
    }
    
    err = refset->add_ref(refset, ref);
    if (err) goto cleanup;

    return knd_OK;
    
 cleanup:
    
    if (ref)
        ref->del(ref);

    
    return err;
}


static int
kndFacet_add_categorical(struct kndFacet  *self,
                         struct kndObjRef *orig_ref,
                         size_t attr_id)
{
    //char unit_name[KND_LABEL_SIZE];
    //size_t unit_name_size;

    //char val_name[KND_LABEL_SIZE];
    //size_t val_name_size;
    
    struct kndRefSet *refset = NULL;
    struct kndObjRef *ref = NULL;
    struct kndSortAttr *attr;
    
    char *b;
    //char *c;
    char *tail = NULL;
    size_t tail_size = 0;
    
    size_t sort_elem_size;
    //long numval = 0;
    int err;

    attr = orig_ref->sorttag->attrs[attr_id];
    
    /* extract curr facet value,
       save the remainder */

    sort_elem_size = attr->val_size;

    b = strchr(attr->val, '/');
    if (b) {
        sort_elem_size = b - attr->val;
        tail_size = attr->val_size - sort_elem_size - 1;
        *b = '\0';
        if (tail_size)
            tail = b + 1;
    }
    
    if (DEBUG_FACET_LEVEL_3)
        knd_log("     adding CATEGORY REF:   current VAL: \"%s\" [%lu] Tail: \"%s\" [%lu]\n",
                attr->val,  (unsigned long)sort_elem_size,
                tail, (unsigned long)tail_size);


    err = kndFacet_get_refset(self,
                              (const char*)attr->val,
                              strlen(attr->val), 0, &refset);
    if (err) return err;
    
    refset->numval = attr->numval;
    
    if (DEBUG_FACET_LEVEL_3)
        knd_log("  + got refset: %s [inbox: %lu]\n",
                refset->name, (unsigned long)refset->inbox_size);
    
    /* add ref */
    
    err = orig_ref->clone(orig_ref, attr_id, tail, tail_size, &ref); 
    if (err) return err;

    if (DEBUG_FACET_LEVEL_3) {
        ref->str(ref, 0);
        knd_log("    input refset: %s [inbox: %lu]\n",
            refset->name, (unsigned long)refset->inbox_size);
    }


    
    err = refset->add_ref(refset, ref);
    if (err) goto cleanup;

    return knd_OK;
    
 cleanup:
    
    if (ref)
        ref->del(ref);

    
    return err;
}



static int
kndFacet_add_atomic(struct kndFacet  *self,
                    struct kndObjRef *orig_ref,
                    size_t attr_id)
{
    //char unit_name[KND_LABEL_SIZE];
    //size_t unit_name_size;

    //char val_name[KND_LABEL_SIZE];
    //size_t val_name_size;

    struct kndRefSet *refset = NULL;
    struct kndObjRef *ref = NULL;
    struct kndSortAttr *attr;

    //char *b;
    //char *c;
    char *tail = NULL;
    size_t tail_size = 0;

    size_t sort_elem_size;
    //long numval = 0;
    int err;

    attr = orig_ref->sorttag->attrs[attr_id];

    /* extract curr facet value,
       save the remainder */

    sort_elem_size = attr->val_size;

    if (DEBUG_FACET_LEVEL_3)
        knd_log("     ATOMIC FACET:   current: \"%s\" [%lu] Tail: \"%s\" [%lu]\n",
                attr->val,  (unsigned long)sort_elem_size,
                tail, (unsigned long)tail_size);

    err = kndFacet_get_refset(self,
                              (const char*)attr->val,
                              attr->val_size, 0, &refset);
    if (err) return err;

    refset->numval = attr->numval;

    if (DEBUG_FACET_LEVEL_3)
        knd_log("  + got refset: %s [inbox: %lu]\n",
                refset->name, (unsigned long)refset->inbox_size);

    /* add ref */
    err = orig_ref->clone(orig_ref, attr_id, tail, tail_size, &ref); 
    if (err) return err;

    if (DEBUG_FACET_LEVEL_3) {
        ref->str(ref, 0);
        knd_log("   input refset: %s [inbox: %lu]\n",
            refset->name, (unsigned long)refset->inbox_size);
    }

    err = refset->add_ref(refset, ref);
    if (err) goto cleanup;

    return knd_OK;

cleanup:

    if (ref)
        ref->del(ref);


    return err;
}




static int
kndFacet_add_accumulated(struct kndFacet *self,
                         struct kndObjRef *orig_ref,
                         size_t attr_id)
{
    char unit_name[KND_LABEL_SIZE];
    size_t unit_name_size;

    //char val_name[KND_LABEL_SIZE];
    //size_t val_name_size;

    struct kndRefSet *refset = NULL;
    struct kndSortAttr *attr;
    struct kndObjRef *ref = NULL;
    //struct kndSortTag *tag;
    
    char *b;
    char *c;
    
    char *tail = NULL;
    size_t tail_size = 0;
    
    size_t sort_elem_size;
    long numval = 0;
    int err;

    
    attr = orig_ref->sorttag->attrs[attr_id];
    
    /* extract curr facet value,
       save the remainder */

    sort_elem_size = attr->val_size;

    b = strchr(attr->val, '+');
    if (b) {
        sort_elem_size = b - attr->val;
        tail_size = attr->val_size - sort_elem_size - 1;
        *b = '\0';
        if (tail_size)
            tail = b + 1;
    }
    
    if (DEBUG_FACET_LEVEL_3)
        knd_log("     .. ACCUMULATED IDX:   Prefix: \"%s\" [%lu] Tail: \"%s\" [%lu]\n",
                attr->val,  (unsigned long)sort_elem_size,
                tail, (unsigned long)tail_size);

    c = attr->val;
    /* find first numeric */
    while (*c) {
        if ((*c) >= '0' && (*c) <= '9')
            break;
        c++;
    }

    unit_name_size = c - attr->val;
    
    if (unit_name_size)
        memcpy(unit_name, attr->val, unit_name_size);

    unit_name[unit_name_size] = '\0';

    err = knd_parse_num((const char*)c, &numval);
    if (err) return err;

    
    if (DEBUG_FACET_LEVEL_3)
        knd_log("       UNIT: \"%s\" [%lu] NUM: \"%lu\"\n\n",
                unit_name,
                (unsigned long)unit_name_size, (unsigned long)numval);
    
    /* TODO: is this unit used in valid position? */

    if (!self->unit_name_size) {
        strncpy(self->unit_name, unit_name, unit_name_size);
        self->unit_name_size = unit_name_size;
    }

    /* TODO: check valid range of numval */
    
    err = kndFacet_get_refset(self, (const char*)c, strlen(c), (size_t)numval, &refset);
    if (err) return err;

    /* add ref */
    err = orig_ref->clone(orig_ref, attr_id, tail, tail_size, &ref); 
    if (err) return err;

    err = refset->add_ref(refset, ref);
    if (err) goto cleanup;
    
    return knd_OK;
    
 cleanup:
    
    if (ref)
        ref->del(ref);

    
    return err;
}




static int
kndFacet_add_positional(struct kndFacet *self,
                        struct kndObjRef *orig_ref,
                        size_t attr_id)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;

    struct kndRefSet *refset = NULL;
    struct kndSortAttr *attr;
    struct kndObjRef *ref = NULL;
    //struct kndSortTag *tag;

    //char *b;
    //char *c;

    char *tail = NULL;
    size_t tail_size = 0;

    char *val;
    size_t val_size;

    size_t UTF_val;
    //long numval = 0;
    int err;

    attr = orig_ref->sorttag->attrs[attr_id];

    /* extract curr facet value,
       save the remainder */
    if (!attr->val_size) return knd_FAIL;

    val = attr->val;
    val_size = attr->val_size;

    /* strip off a valid character */
    while (*val) {

        switch (*val) {
            /*case ' ':
              case '_':*/
        case '-':
            val++;
            val_size--;

            /*knd_log(".. skipping over a non-alph char in \"%s\"..\n",
                    attr->val);
            */

            goto next_char;
        default:
            break;
        }

        err = knd_read_UTF8_char((const char*)val,
                                 val_size,
                                 &UTF_val, &buf_size);
        if (err) return err;

        /* valid char found! */

        /* HACK: cyrillic yo -> ye */
        if (UTF_val == 0x0401)
            UTF_val = 0x0415;

        attr->numval = UTF_val;

        break;

    next_char:
        buf_size = 0;
        continue;
    }

    if (!buf_size) return knd_FAIL;

    memcpy(buf, val, buf_size);
    buf[buf_size] = '\0';

    tail_size = val_size - buf_size;
    tail = val + buf_size;

    if (DEBUG_FACET_LEVEL_3)
        knd_log("\n\n     POSITIONAL FACET UNIT:   Prefix: \"%s\" [%lu] Tail: \"%s\" [%lu]\n",
                buf,  (unsigned long)buf_size,
                tail, (unsigned long)tail_size);


    /* TODO: check validity of refset name,
       eg. UTF-8 value within range */



    err = kndFacet_get_refset(self,
                              (const char*)buf,
                              buf_size, attr->numval, &refset);
    if (err) return err;

    refset->numval = UTF_val;

    /* add ref */
    err = orig_ref->clone(orig_ref, attr_id, tail, tail_size, &ref); 
    if (err) return err;

    /*ref->str(ref, 0);*/

    /*knd_log("   input refset: %s [inbox: %lu]\n",
      f->name, (unsigned long)f->inbox_size); */

    err = refset->add_ref(refset, ref);
    if (err) goto cleanup;

    return knd_OK;

cleanup:

    if (ref)
        ref->del(ref);

    return err;
}


static int
kndFacet_add_ref(struct kndFacet  *self,
                 struct kndObjRef *orig_ref,
                 size_t attr_id,
                 knd_facet_type attr_type)
{
    int err;

    if (DEBUG_FACET_LEVEL_3) {
        knd_log("   .. add objref \"%s\" to facet \"%s\" (type: %d)\n",
                orig_ref->obj_id, self->name, attr_type);
    }
    
    switch (attr_type) {
    case KND_FACET_ACCUMULATED:
        err = kndFacet_add_accumulated(self, orig_ref, attr_id);
        if (err) goto final;
        break;
    case KND_FACET_ATOMIC:
        err = kndFacet_add_atomic(self, orig_ref, attr_id);
        if (err) goto final;
        break;
    case KND_FACET_CATEGORICAL:
        err = kndFacet_add_categorical(self, orig_ref, attr_id);
        if (err) goto final;
        break;
    case KND_FACET_POSITIONAL:
        err = kndFacet_add_positional(self, orig_ref, attr_id);
        if (err) goto final;
        break;
    case KND_FACET_CONC_BASE:
        err = kndFacet_add_conc_base(self, orig_ref, attr_id);
        if (err) goto final;
        break;
    case KND_FACET_CONC_SPEC:
        err = kndFacet_add_conc_spec(self, orig_ref, attr_id);
        if (err) goto final;
        break;
    default:
        knd_log("  -- unrec attr %s type %d :(\n", orig_ref->obj_id, attr_type);

        orig_ref->str(orig_ref, 1);

        err = knd_FAIL;
        goto final;
    }

    return knd_OK;
    
 final:
    return err;
}



/*static int
kndFacet_export_item(struct kndObjRef *item,
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
    }*/



static int
kndFacet_read(struct kndFacet  *self,
              const char       *rec,
              size_t           rec_size)
{
    char buf[KND_LARGE_BUF_SIZE + 1];
    size_t buf_size = 0;

    char name[KND_NAME_SIZE];
    size_t name_size = 0;

    //struct kndObjRef *ref;
    struct kndRefSet *rs;
    
    const char *refset_rec;
    size_t refset_rec_size;
    
    char *val;
    char *c;

    long numval;
    
    size_t offset = 0;
    size_t total_items = 0;
    size_t num_refs = 0;
    //size_t curr_size;
    
    const char *delim = KND_FIELD_SEPAR;
    char *last = NULL;
    char *s;
    int err;
    
    if (DEBUG_FACET_LEVEL_2)
        knd_log("  .. Reading Facet idx rec, input size [%lu]\n",
                (unsigned long)rec_size);

    err = knd_get_trailer(rec, rec_size,
                          name, &name_size,
                          &total_items,
                          buf, &buf_size);
    if (err) {
        knd_log("  -- facet trailer not read: %d\n", err);
        goto final;
    }

    s = strchr(name, '#');
    if (s) {
        *s = '\0';
        name_size = strlen(name);
        s++;
        
        err = knd_parse_num((const char*)s, &numval);
        if (err) return err;

        self->type = (knd_facet_type)numval;
    }

    memcpy(self->name, name, name_size);
    self->name[name_size] = '\0';
    self->name_size = name_size;

    
    if (DEBUG_FACET_LEVEL_2)
        knd_log("\n     == reading Facet \"%s\" (type: %s)    DIR: \"%s\"  depth: %lu\n",
                self->name, knd_facet_names[self->type],
                buf,
                (unsigned long)self->export_depth);

    /* parse trailer trailer */
    for (c = strtok_r(buf, delim, &last);
         c;
         c = strtok_r(NULL, delim, &last)) {

        /* find the offset field */
        val = strstr(c, GSL_OFFSET);
        if (!val) continue;

        *val = '\0';
        val++;

        /* get the numeric offset */
        err = knd_parse_num((const char*)val, &numval);
        if (err) return err;
        
        refset_rec_size = (size_t)numval;
        refset_rec = rec + offset;

        /* default num items */
        numval = 1;
        val = strstr(c, GSL_TOTAL);
        if (val) {
            *val = '\0';
            val++;
        
            err = knd_parse_num((const char*)val, &numval);
            if (err) return err;
        }
        
        num_refs = (size_t)numval;

        if (DEBUG_FACET_LEVEL_3)
            knd_log("   == RefSet: \"%s\" num_refs: %lu  size: %lu\n",
                    c, (unsigned long)num_refs, (unsigned long)refset_rec_size);

        name_size = strlen(c);
        if (!name_size) return knd_FAIL;
        
        err = kndFacet_get_refset(self, (const char*)c, name_size, 0, &rs);
        if (err) goto final;

        rs->num_refs = num_refs;
        rs->export_depth = self->export_depth + 1;
        rs->parent = self;
        
        err = rs->read(rs, refset_rec, refset_rec_size);
        if (err) goto final;

        offset += refset_rec_size;
    }

    err = knd_OK;
    
 final:    
    return err;
}


static int
kndFacet_read_tags(struct kndFacet  *self,
                   const char       *rec,
                   size_t           rec_size,
                   struct kndRefSet *refset)
{
    char buf[KND_LARGE_BUF_SIZE + 1];
    size_t buf_size = 0;

    char name[KND_NAME_SIZE];
    size_t name_size = 0;

    const char *refset_rec;
    size_t refset_rec_size;
    
    char *val;
    char *c;

    long numval;
    
    size_t offset = 0;
    size_t total_items = 0;
    size_t num_refs = 0;
    //size_t curr_size;
    
    const char *delim = KND_FIELD_SEPAR;
    char *last = NULL;
    
    int err;
    
    if (DEBUG_FACET_LEVEL_TMP)
        knd_log("  .. Reading Facet idx rec, input size [%lu]\n",
                (unsigned long)rec_size);

    err = knd_get_trailer(rec, rec_size,
                          name, &name_size,
                          &total_items,
                          buf, &buf_size);
    if (err) {
        knd_log("  -- facet trailer not read: %d\n", err);
        goto final;
    }
    
    if (DEBUG_FACET_LEVEL_TMP)
        knd_log("\n     == Facet \"%s\" dir: \"%s\"  depth: %lu\n",
                self->name, buf, (unsigned long)self->export_depth);

    /* parse trailer trailer */
    for (c = strtok_r(buf, delim, &last);
         c;
         c = strtok_r(NULL, delim, &last)) {

        /* find the offset field */
        val = strstr(c, GSL_OFFSET);
        if (!val) continue;

        *val = '\0';
        val++;

        /* get the numeric offset */
        err = knd_parse_num((const char*)val, &numval);
        if (err) return err;
        
        refset_rec_size = (size_t)numval;
        refset_rec = rec + offset;

        /* default num items */
        numval = 1;
        val = strstr(c, GSL_TOTAL);
        if (val) {
            *val = '\0';
            val++;
        
            err = knd_parse_num((const char*)val, &numval);
            if (err) return err;
        }
        
        num_refs = (size_t)numval;


        name_size = strlen(c);
        if (!name_size) return knd_FAIL;

        if (strncmp(c, refset->name, refset->name_size)) {
            offset += refset_rec_size;
            continue;
        }

        if (DEBUG_FACET_LEVEL_TMP)
            knd_log("   == got RefSet: \"%s\" num_refs: %lu  size: %lu\n",
                    c, (unsigned long)num_refs, (unsigned long)refset_rec_size);

        err = refset->read_tags(refset, refset_rec, refset_rec_size);
        if (err) goto final;

        break;
    }

    err = knd_OK;
    
 final:    
    return err;
}



static int
kndFacet_find_accumulated(struct kndFacet    *self,
                         const char         *val,
                         size_t val_size,
                         struct kndRefSet  **result)
{
    char unit_name[KND_NAME_SIZE];
    size_t unit_name_size = 0;

    char refset_name[KND_NAME_SIZE];
    size_t refset_name_size = 0;

    char tail[KND_NAME_SIZE];
    size_t tail_size = 0;

    char buf[KND_NAME_SIZE];
    size_t buf_size = val_size;

    struct kndRefSet *rs;
    //struct kndFacet *f;
    char *c;
    long numval = 0;
    
    int err;


    
    c = strchr(val, '+');
    if (c) {
        buf_size = c - val;
        tail_size = val_size - buf_size - 1;
        c++;
        if (!*c) return knd_FAIL;
        
        memcpy(tail, c, tail_size);
        tail[tail_size] = '\0';
    }

    memcpy(buf, val, buf_size);
    buf[buf_size] = '\0';
    
    knd_log("     Prefix: \"%s\" [%lu] Tail: \"%s\" [%lu]\n",
            buf,  (unsigned long)buf_size,
            tail, (unsigned long)tail_size); 

    c = buf;
    /* find first numeric */
    while (*c) {
        if ((*c) >= '0' && (*c) <= '9')
            break;
        c++;
    }

    unit_name_size = c - buf;
    if (unit_name_size) {
        memcpy(unit_name, buf, unit_name_size);
        unit_name[unit_name_size] = '\0';
    }
    
    /* TODO: check unit name against curr facet */

    err = knd_parse_num((const char*)c, &numval);
    if (err) return err;

    refset_name_size =  buf_size - unit_name_size;
    memcpy(refset_name, c, refset_name_size);
    refset_name[refset_name_size] = '\0';
    
    knd_log("       REFSET: %s  UNIT: \"%s\" [%lu] NUM: \"%lu\"\n\n",
            refset_name,
            unit_name,
            (unsigned long)unit_name_size, (unsigned long)numval);

    err = knd_FAIL;

    for (size_t i = 0; i < self->num_refsets; i++) {
        rs = self->refsets[i];

        knd_log("    ?? refset: %s\n", rs->name);

        if (!strcmp(rs->name, refset_name)) {
            /* nested search needed */
            if (tail_size) {

                return rs->find(rs, self->name,
                                tail, tail_size, result);
            }

            *result = rs;
            return knd_OK;
        }
    }

    return err;
}

static int
kndFacet_find_positional(struct kndFacet    *self,
                         const char         *val,
                         size_t val_size,
                         struct kndRefSet  **result)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = val_size;

    struct kndRefSet *rs;

    const char *tail;
    size_t tail_size = 0;
    
    size_t UTF_val;
    //long numval = 0;
    
    int err;

    /*knd_log("  == POSITIONAL VAL: %s\n", val);*/

    /* strip off a valid character */
    while (*val) {

        switch (*val) {
            /*case ' ':
              case '_':*/
        case '-':
            val++;
            val_size--;

            goto next_char;
        default:
            break;
        }

        err = knd_read_UTF8_char((const char*)val,
                                 val_size,
                                 &UTF_val, &buf_size);
        if (err) return err;

        /* valid char found! */
        break;

    next_char:
        buf_size = 0;
        continue;
    }

    if (!buf_size) return knd_FAIL;
    
    memcpy(buf, val, buf_size);
    buf[buf_size] = '\0';

    tail_size = val_size - buf_size;
    tail = val + buf_size;
    
    if (DEBUG_FACET_LEVEL_3)
        knd_log("\n\n     POSITIONAL FACET UNIT:   Prefix: \"%s\" [%lu] Tail: \"%s\" [%lu]\n",
                buf,  (unsigned long)buf_size,
                tail, (unsigned long)tail_size);

    for (size_t i = 0; i < self->num_refsets; i++) {
        rs = self->refsets[i];

        if (!strcmp(rs->name, buf)) {

            /* nested search needed */
            if (tail_size) {
                /*rs->query = self->query;
                rs->query_size = self->query_size;
                */

                return rs->find(rs, self->name,
                                tail, tail_size, result);
            }

            *result = rs;
            return knd_OK;
        }
    }

    return knd_FAIL;
}


static int
kndFacet_extract_objs(struct kndFacet    *self)
{
    struct kndRefSet *rs;
    int err = knd_FAIL;

    if (DEBUG_FACET_LEVEL_3)
        knd_log("  facet \"%s\" to extract objs   (batch size: %lu,   curr: %lu)\n",
                self->name,
                (unsigned long)self->batch_size,
                (unsigned long)self->cache->num_matches);

    for (size_t i = 0; i < self->num_refsets; i++) {
        rs = self->refsets[i];

        rs->batch_size = self->batch_size;

        err = rs->extract_objs(rs);
        if (err) return err;

        if (self->cache->num_matches >= self->batch_size)
            break;

    }

    return knd_OK;
}

static int
kndFacet_find(struct kndFacet    *self,
              const char         *val,
              size_t              val_size,
              struct kndRefSet  **result)
{

    //struct kndFacet *f;
    struct kndRefSet *rs;
    //const char *b;

    int err = knd_FAIL;

    knd_log("   .. looking up facet %s  type: %d\n",
            self->name, self->type);
    
    switch (self->type) {
    case KND_FACET_ACCUMULATED:
        return kndFacet_find_accumulated(self,
                                         val, val_size,
                                         result);
    case KND_FACET_POSITIONAL:
        return kndFacet_find_positional(self,
                                        val, val_size,
                                        result);
    default:
        break;
    }
    
    for (size_t i = 0; i < self->num_refsets; i++) {
        rs = self->refsets[i];

        if (!strcmp(rs->name, val)) {
            *result = rs;
            return knd_OK;
        }
    }
    
    
    return err;
}

static int
kndFacet_sync(struct kndFacet *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;

    char rec_buf[KND_LARGE_BUF_SIZE];
    size_t rec_size = 0;

    //struct kndObjRef *ref;
    struct kndRefSet *rs;
    struct kndOutput *out;
    
    char *rec;
    //size_t total_size = 0;
    size_t curr_size = 0;
    size_t trailer_size = 0;
    size_t trailer_pos = 0;
    size_t start_pos = 0;

    int err = knd_OK;

    if (DEBUG_FACET_LEVEL_3)
        knd_log("\nFACET \"%s\" in sync...\n",
                self->name);

    out = self->out;
    rec = rec_buf;

    start_pos = out->free_space;

    /* linearize refsets  */
    for (size_t i = 0; i < self->num_refsets; i++) {
        rs = self->refsets[i];

        rs->out = out;
        err = rs->sync(rs);
        if (err) goto final;

        if (i) {
            memcpy(rec, KND_FIELD_SEPAR, KND_FIELD_SEPAR_SIZE); 
            rec += KND_FIELD_SEPAR_SIZE;
            rec_size += KND_FIELD_SEPAR_SIZE;
        }

        /* TODO: buf overflow */
        
        memcpy(rec, rs->name, rs->name_size);
        rec += rs->name_size;
        rec_size += rs->name_size;

        if (rs->num_refs > 1) {
            curr_size = sprintf(rec, "=%lu",
                                (unsigned long)rs->num_refs);
            rec += curr_size;
            rec_size += curr_size;
        }

        if (rs->rec_size) {
            curr_size = sprintf(rec, "@%lu",
                                (unsigned long)rs->rec_size);
            rec += curr_size;
            rec_size += curr_size;
        }
    }

    /* write trailer */
    if (rec_size) {
        trailer_pos = out->free_space;
 
        err = out->write(out,  GSL_OPEN_FACET_DELIM,
                GSL_OPEN_FACET_DELIM_SIZE);
        if (err) goto final;
        
        err = out->write(out,  self->name, self->name_size);
        if (err) goto final;

        buf_size = sprintf(buf, "#%lu",
                           (unsigned long)self->type);

        err = out->write(out,  
                          buf, buf_size);
        if (err) goto final;
        
        err = out->write(out,  
                          GSL_TERM_SEPAR, GSL_TERM_SEPAR_SIZE);
        if (err) goto final;

        err = out->write(out,  rec_buf, rec_size);
        if (err) goto final;

        err = out->write(out,  
                          GSL_CLOSE_FACET_DELIM,
                          GSL_CLOSE_FACET_DELIM_SIZE);
        if (err) goto final;

        trailer_size = trailer_pos - out->free_space;
        curr_size = sprintf(rec_buf, "%lu", (unsigned long)trailer_size);
        err = out->write(out,
                          rec_buf, curr_size);
        if (err) goto final;
    }

    self->rec_size = start_pos - out->free_space;  

    
 final:
    return err;
}



extern int 
kndFacet_new(struct kndFacet **facet)
{
    struct kndFacet *self;

    self = malloc(sizeof(struct kndFacet));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndFacet));

    self->del = kndFacet_del;
    self->str = kndFacet_str;

    self->add_ref = kndFacet_add_ref;
    self->find = kndFacet_find;
    self->extract_objs = kndFacet_extract_objs;

    self->read = kndFacet_read;
    self->read_tags = kndFacet_read_tags;

    self->export = kndFacet_export;
    self->sync = kndFacet_sync;

    *facet = self;

    return knd_OK;
}
