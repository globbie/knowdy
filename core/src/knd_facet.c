#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_facet.h"
#include "knd_set.h"
#include "knd_output.h"

#define DEBUG_FACET_LEVEL_0 0
#define DEBUG_FACET_LEVEL_1 0
#define DEBUG_FACET_LEVEL_2 0
#define DEBUG_FACET_LEVEL_3 0
#define DEBUG_FACET_LEVEL_4 0
#define DEBUG_FACET_LEVEL_TMP 1

static int
kndFacet_sync(struct kndFacet *self);

/*static int
kndFacet_export_item(struct kndSetElem *item,
                       char *output,
                       size_t *output_size);
*/

static int 
knd_compare_elemset_by_alph_ascend(const void *a,
                                  const void *b)
{
    struct kndElemSet **obj1, **obj2;

    obj1 = (struct kndElemSet**)a;
    obj2 = (struct kndElemSet**)b;

    if ((*obj1)->numval == (*obj2)->numval) return 0;

    if ((*obj1)->numval > (*obj2)->numval) return 1;

    return -1;
}


static void
kndFacet_del(struct kndFacet *self)
{
    struct kndElemSet *rs;

    for (size_t i = 0; i < self->num_elemsets; i++) {
        rs = self->elemsets[i];
        rs->del(rs);
    }

    free(self);
}

static int 
kndFacet_str(struct kndFacet *self,
             size_t depth,
             size_t max_depth)
{
    //struct kndSetElem *elem;
    struct kndElemSet *rs;
    
    size_t i, offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;
    char *offset = malloc(offset_size + 1);
    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    if (depth > max_depth) return knd_OK;
    
    knd_log("\n%s|__Facet \"%s\" type: %d [num elemsets: %lu]",
            offset, self->name, self->type,
            (unsigned long)self->num_elemsets);

    if (self->unit_name_size) {
        knd_log(" (unit: %s)", self->unit_name);
    }

    knd_log("\n");
    
    qsort(self->elemsets,
          self->num_elemsets,
          sizeof(struct kndElemSet*), knd_compare_elemset_by_alph_ascend);
    
    for (i = 0; i < self->num_elemsets; i++) {
        rs = self->elemsets[i];
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

    struct kndElemSet *rs;
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

    for (size_t i = 0; i < self->num_elemsets; i++) {
        rs = self->elemsets[i];

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

    struct kndElemSet *rs;
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

    for (size_t i = 0; i < self->num_elemsets; i++) {
        rs = self->elemsets[i];

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
kndFacet_get_elemset(struct kndFacet  *self,
                    const char       *elemset_name,
                    size_t           elemset_name_size,
                    size_t           numval,
                    struct kndElemSet **result)
{
    struct kndElemSet *rs;
    int err;
    
    for (size_t i = 0; i < self->num_elemsets; i++) {
        rs = self->elemsets[i];

	if (!strcmp(rs->name, elemset_name)) {
            *result = rs;
            
            return knd_OK;
        }
    }

    /*knd_log("   .. create new elemset..\n"); */

    if (self->num_elemsets + 1 >= KND_MAX_ATTRS) {

        if (DEBUG_FACET_LEVEL_TMP)
            knd_log("   -- Facet \"%s\": MAX limit of elemsets reached. Couldn't add elemset \"%s\" :(\n",
                    self->name, elemset_name);

        return knd_NOMEM;
    }


    
    /*knd_log("  .. creating elemset %s [%lu]..\n",
            elemset_name,  (unsigned long)elemset_name_size);
    */
    /* elemset not found, create one */
    err = kndElemSet_new(&rs);
    if (err) return err;

    
    memcpy(rs->name, elemset_name, elemset_name_size);
    rs->name[elemset_name_size] = '\0';
    
    rs->name_size = elemset_name_size;
    rs->numval = numval;

    rs->baseclass = self->baseclass;
    rs->parent = self;
    rs->out = self->out;
    
    self->elemsets[self->num_elemsets] = rs;
    self->num_elemsets++;

    *result = rs;
    
    return knd_OK;
}



static int
kndFacet_add_conc_base(struct kndFacet  *self,
                       struct kndSetElem *orig_elem,
                       size_t attr_id)
{
    //char unit_name[KND_LABEL_SIZE];
    //size_t unit_name_size;

    //char val_name[KND_LABEL_SIZE];
    //size_t val_name_size;
    
    struct kndElemSet *elemset = NULL;
    struct kndSetElem *elem = NULL;
    struct kndSortAttr *attr;
    
    char *b;
    //char *c;
    char *tail = NULL;
    size_t tail_size = 0;
    
    size_t sort_elem_size;
    //long numval = 0;
    int err;

    attr = orig_elem->sorttag->attrs[attr_id];
    
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
    
    err = kndFacet_get_elemset(self,
                              (const char*)attr->val,
                              strlen(attr->val), 0, &elemset);
    if (err) return err;

    if (DEBUG_FACET_LEVEL_TMP)
        knd_log("  + got elemset: %s [inbox: %lu]\n",
                elemset->name, (unsigned long)elemset->inbox_size);
    
    /* add elem */
    err = orig_elem->clone(orig_elem, attr_id, tail, tail_size, &elem); 
    if (err) return err;

    if (DEBUG_FACET_LEVEL_TMP) {
        elem->str(elem, 0);
        knd_log("   == input elemset: %s [inbox: %lu]\n",
            elemset->name, (unsigned long)elemset->inbox_size);
    }
    
    err = elemset->add_elem(elemset, elem);
    if (err) goto cleanup;

    return knd_OK;
    
 cleanup:
    
    if (elem)
        elem->del(elem);

    
    return err;
}

static int
kndFacet_add_conc_spec(struct kndFacet  *self,
                       struct kndSetElem *orig_elem,
                       size_t attr_id)
{
    //char unit_name[KND_LABEL_SIZE];
    //size_t unit_name_size;

    //char val_name[KND_LABEL_SIZE];
    //size_t val_name_size;

    struct kndElemSet *elemset = NULL;
    struct kndSetElem *elem = NULL;
    struct kndSortAttr *attr;

    char *b;
    //char *c;
    char *tail = NULL;
    size_t tail_size = 0;

    size_t sort_elem_size;
    //long numval = 0;
    int err;

    attr = orig_elem->sorttag->attrs[attr_id];

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
    
    err = kndFacet_get_elemset(self,
                              (const char*)attr->val,
                              strlen(attr->val), 0, &elemset);
    if (err) return err;

    if (DEBUG_FACET_LEVEL_TMP)
        knd_log("  + got elemset: %s [inbox: %lu]\n",
                elemset->name, (unsigned long)elemset->inbox_size);
    
    /* add elem */
    err = orig_elem->clone(orig_elem, attr_id, tail, tail_size, &elem); 
    if (err) return err;

    if (DEBUG_FACET_LEVEL_TMP) {
        elem->str(elem, 0);
        knd_log("   == input elemset: %s [inbox: %lu]\n",
            elemset->name, (unsigned long)elemset->inbox_size);
    }
    
    err = elemset->add_elem(elemset, elem);
    if (err) goto cleanup;

    return knd_OK;
    
 cleanup:
    
    if (elem)
        elem->del(elem);

    
    return err;
}


static int
kndFacet_add_categorical(struct kndFacet  *self,
                         struct kndSetElem *orig_elem,
                         size_t attr_id)
{
    //char unit_name[KND_LABEL_SIZE];
    //size_t unit_name_size;

    //char val_name[KND_LABEL_SIZE];
    //size_t val_name_size;
    
    struct kndElemSet *elemset = NULL;
    struct kndSetElem *elem = NULL;
    struct kndSortAttr *attr;
    
    char *b;
    //char *c;
    char *tail = NULL;
    size_t tail_size = 0;
    
    size_t sort_elem_size;
    //long numval = 0;
    int err;

    attr = orig_elem->sorttag->attrs[attr_id];
    
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
        knd_log("     adding CATEGORY ELEM:   current VAL: \"%s\" [%lu] Tail: \"%s\" [%lu]\n",
                attr->val,  (unsigned long)sort_elem_size,
                tail, (unsigned long)tail_size);


    err = kndFacet_get_elemset(self,
                              (const char*)attr->val,
                              strlen(attr->val), 0, &elemset);
    if (err) return err;
    
    elemset->numval = attr->numval;
    
    if (DEBUG_FACET_LEVEL_3)
        knd_log("  + got elemset: %s [inbox: %lu]\n",
                elemset->name, (unsigned long)elemset->inbox_size);
    
    /* add elem */
    
    err = orig_elem->clone(orig_elem, attr_id, tail, tail_size, &elem); 
    if (err) return err;

    if (DEBUG_FACET_LEVEL_3) {
        elem->str(elem, 0);
        knd_log("    input elemset: %s [inbox: %lu]\n",
            elemset->name, (unsigned long)elemset->inbox_size);
    }


    
    err = elemset->add_elem(elemset, elem);
    if (err) goto cleanup;

    return knd_OK;
    
 cleanup:
    
    if (elem)
        elem->del(elem);
    
    return err;
}



static int
kndFacet_add_atomic(struct kndFacet  *self,
                    struct kndSetElem *orig_elem,
                    size_t attr_id)
{
    //char unit_name[KND_LABEL_SIZE];
    //size_t unit_name_size;

    //char val_name[KND_LABEL_SIZE];
    //size_t val_name_size;

    struct kndElemSet *elemset = NULL;
    struct kndSetElem *elem = NULL;
    struct kndSortAttr *attr;

    //char *b;
    //char *c;
    char *tail = NULL;
    size_t tail_size = 0;

    size_t sort_elem_size;
    //long numval = 0;
    int err;

    attr = orig_elem->sorttag->attrs[attr_id];

    /* extract curr facet value,
       save the remainder */

    sort_elem_size = attr->val_size;

    if (DEBUG_FACET_LEVEL_3)
        knd_log("     ATOMIC FACET:   current: \"%s\" [%lu] Tail: \"%s\" [%lu]\n",
                attr->val,  (unsigned long)sort_elem_size,
                tail, (unsigned long)tail_size);

    err = kndFacet_get_elemset(self,
                              (const char*)attr->val,
                              attr->val_size, 0, &elemset);
    if (err) return err;

    elemset->numval = attr->numval;

    if (DEBUG_FACET_LEVEL_3)
        knd_log("  + got elemset: %s [inbox: %lu]\n",
                elemset->name, (unsigned long)elemset->inbox_size);

    /* add elem */
    err = orig_elem->clone(orig_elem, attr_id, tail, tail_size, &elem); 
    if (err) return err;

    if (DEBUG_FACET_LEVEL_3) {
        elem->str(elem, 0);
        knd_log("   input elemset: %s [inbox: %lu]\n",
            elemset->name, (unsigned long)elemset->inbox_size);
    }

    err = elemset->add_elem(elemset, elem);
    if (err) goto cleanup;

    return knd_OK;

cleanup:

    if (elem)
        elem->del(elem);


    return err;
}




static int
kndFacet_add_accumulated(struct kndFacet *self,
                         struct kndSetElem *orig_elem,
                         size_t attr_id)
{
    char unit_name[KND_LABEL_SIZE];
    size_t unit_name_size;

    //char val_name[KND_LABEL_SIZE];
    //size_t val_name_size;

    struct kndElemSet *elemset = NULL;
    struct kndSortAttr *attr;
    struct kndSetElem *elem = NULL;
    //struct kndSortTag *tag;
    
    char *b;
    char *c;
    
    char *tail = NULL;
    size_t tail_size = 0;
    
    size_t sort_elem_size;
    long numval = 0;
    int err;

    
    attr = orig_elem->sorttag->attrs[attr_id];
    
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
        knd_log("     .. ACCUMULATED IDX:   Pelemix: \"%s\" [%lu] Tail: \"%s\" [%lu]\n",
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
    
    err = kndFacet_get_elemset(self, (const char*)c, strlen(c), (size_t)numval, &elemset);
    if (err) return err;

    /* add elem */
    err = orig_elem->clone(orig_elem, attr_id, tail, tail_size, &elem); 
    if (err) return err;

    err = elemset->add_elem(elemset, elem);
    if (err) goto cleanup;
    
    return knd_OK;
    
 cleanup:
    
    if (elem)
        elem->del(elem);

    
    return err;
}




static int
kndFacet_add_positional(struct kndFacet *self,
                        struct kndSetElem *orig_elem,
                        size_t attr_id)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;

    struct kndElemSet *elemset = NULL;
    struct kndSortAttr *attr;
    struct kndSetElem *elem = NULL;
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

    attr = orig_elem->sorttag->attrs[attr_id];

    /* extract curr facet value,
       save the remainder */
    if (!attr->val_size) return knd_FAIL;

    val = attr->val;
    val_size = attr->val_size;

    /* strip off a valid character */
    while (*val) {

        switch (*val) {
        case ' ':
        case '_':
        case '-':
        case ':':
        case '/':
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
        knd_log("\n\n     POSITIONAL FACET UNIT:   Pelemix: \"%s\" [%lu] Tail: \"%s\" [%lu]\n",
                buf,  (unsigned long)buf_size,
                tail, (unsigned long)tail_size);


    /* TODO: check validity of elemset name,
       eg. UTF-8 value within range */



    err = kndFacet_get_elemset(self,
                              (const char*)buf,
                              buf_size, attr->numval, &elemset);
    if (err) return err;

    elemset->numval = UTF_val;

    /* add elem */
    err = orig_elem->clone(orig_elem, attr_id, tail, tail_size, &elem); 
    if (err) return err;

    /*elem->str(elem, 0);*/

    /*knd_log("   input elemset: %s [inbox: %lu]\n",
      f->name, (unsigned long)f->inbox_size); */

    err = elemset->add_elem(elemset, elem);
    if (err) goto cleanup;

    return knd_OK;

cleanup:

    if (elem)
        elem->del(elem);

    return err;
}


static int
kndFacet_add_elem(struct kndFacet  *self,
                 struct kndSetElem *orig_elem,
                 size_t attr_id,
                 knd_facet_type attr_type)
{
    int err;

    if (DEBUG_FACET_LEVEL_3) {
        knd_log("   .. add objelem \"%s\" to facet \"%s\" (type: %d)\n",
                orig_elem->obj_id, self->name, attr_type);
    }
    
    switch (attr_type) {
    case KND_FACET_ACCUMULATED:
        err = kndFacet_add_accumulated(self, orig_elem, attr_id);
        if (err) goto final;
        break;
    case KND_FACET_ATOMIC:
        err = kndFacet_add_atomic(self, orig_elem, attr_id);
        if (err) goto final;
        break;
    case KND_FACET_CATEGORICAL:
        err = kndFacet_add_categorical(self, orig_elem, attr_id);
        if (err) goto final;
        break;
    case KND_FACET_POSITIONAL:
        err = kndFacet_add_positional(self, orig_elem, attr_id);
        if (err) goto final;
        break;
    case KND_FACET_CONC_BASE:
        err = kndFacet_add_conc_base(self, orig_elem, attr_id);
        if (err) goto final;
        break;
    case KND_FACET_CONC_SPEC:
        err = kndFacet_add_conc_spec(self, orig_elem, attr_id);
        if (err) goto final;
        break;
    default:
        knd_log("  -- unrec attr %s type %d :(\n", orig_elem->obj_id, attr_type);

        orig_elem->str(orig_elem, 1);

        err = knd_FAIL;
        goto final;
    }

    return knd_OK;
    
 final:
    return err;
}



/*static int
kndFacet_export_item(struct kndSetElem *item,
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

    //struct kndSetElem *elem;
    struct kndElemSet *rs;
    
    const char *elemset_rec;
    size_t elemset_rec_size;
    
    char *val;
    char *c;

    long numval;
    
    size_t offset = 0;
    size_t total_items = 0;
    size_t num_elems = 0;
    
    const char *delim = KND_FIELD_SEPAR;
    char *last = NULL;
    char *s;
    int err;
    
    if (DEBUG_FACET_LEVEL_1)
        knd_log("  .. Reading Facet idx rec, input size [%lu]\n",
                (unsigned long)rec_size);

    /*    err = knd_get_trailer(rec, rec_size,
                          name, &name_size,
                          &total_items,
                          buf, &buf_size);
    if (err) {
        knd_log("  -- facet trailer not read: %d\n", err);
        goto final;
    }
    */
    
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

    if (DEBUG_FACET_LEVEL_1)
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
        
        elemset_rec_size = (size_t)numval;
        elemset_rec = rec + offset;

        /* default num items */
        numval = 1;
        val = strstr(c, GSL_TOTAL);
        if (val) {
            *val = '\0';
            val++;
        
            err = knd_parse_num((const char*)val, &numval);
            if (err) return err;
        }
        
        num_elems = (size_t)numval;

        if (DEBUG_FACET_LEVEL_2)
            knd_log("   == ElemSet: \"%s\" num_elems: %lu  size: %lu\n",
                    c, (unsigned long)num_elems, (unsigned long)elemset_rec_size);

        name_size = strlen(c);
        if (!name_size) return knd_FAIL;
        
        err = kndFacet_get_elemset(self, (const char*)c, name_size, 0, &rs);
        if (err) goto final;

        rs->num_elems = num_elems;
        rs->export_depth = self->export_depth + 1;
        rs->parent = self;

        if (DEBUG_FACET_LEVEL_2)
            knd_log(".. elemset reading: \"%s\"..", elemset_rec);

        err = rs->read(rs, elemset_rec, elemset_rec_size);
        if (err) {
            goto final;
        }
        
        offset += elemset_rec_size;
    }

    err = knd_OK;
    
 final:    
    return err;
}


static int
kndFacet_read_tags(struct kndFacet  *self,
                   const char       *rec,
                   size_t           rec_size,
                   struct kndElemSet *elemset)
{
    char buf[KND_LARGE_BUF_SIZE + 1];
    size_t buf_size = 0;

    char name[KND_NAME_SIZE];
    size_t name_size = 0;

    const char *elemset_rec;
    size_t elemset_rec_size;
    
    char *val;
    char *c;

    long numval;
    
    size_t offset = 0;
    size_t total_items = 0;
    size_t num_elems = 0;
    //size_t curr_size;
    
    const char *delim = KND_FIELD_SEPAR;
    char *last = NULL;
    
    int err;
    
    if (DEBUG_FACET_LEVEL_1)
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
    
    if (DEBUG_FACET_LEVEL_1)
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
        
        elemset_rec_size = (size_t)numval;
        elemset_rec = rec + offset;

        /* default num items */
        numval = 1;
        val = strstr(c, GSL_TOTAL);
        if (val) {
            *val = '\0';
            val++;
        
            err = knd_parse_num((const char*)val, &numval);
            if (err) return err;
        }
        
        num_elems = (size_t)numval;


        name_size = strlen(c);
        if (!name_size) return knd_FAIL;

        if (strncmp(c, elemset->name, elemset->name_size)) {
            offset += elemset_rec_size;
            continue;
        }

        if (DEBUG_FACET_LEVEL_TMP)
            knd_log("   == got ElemSet: \"%s\" num_elems: %lu  size: %lu\n",
                    c, (unsigned long)num_elems, (unsigned long)elemset_rec_size);

        err = elemset->read_tags(elemset, elemset_rec, elemset_rec_size);
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
                         struct kndElemSet  **result)
{
    char unit_name[KND_NAME_SIZE];
    size_t unit_name_size = 0;

    char elemset_name[KND_NAME_SIZE];
    size_t elemset_name_size = 0;

    char tail[KND_NAME_SIZE];
    size_t tail_size = 0;

    char buf[KND_NAME_SIZE];
    size_t buf_size = val_size;

    struct kndElemSet *rs;
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
    
    knd_log("     Pelemix: \"%s\" [%lu] Tail: \"%s\" [%lu]\n",
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

    elemset_name_size =  buf_size - unit_name_size;
    memcpy(elemset_name, c, elemset_name_size);
    elemset_name[elemset_name_size] = '\0';
    
    knd_log("       ELEMSET: %s  UNIT: \"%s\" [%lu] NUM: \"%lu\"\n\n",
            elemset_name,
            unit_name,
            (unsigned long)unit_name_size, (unsigned long)numval);

    err = knd_FAIL;

    for (size_t i = 0; i < self->num_elemsets; i++) {
        rs = self->elemsets[i];

        knd_log("    ?? elemset: %s\n", rs->name);

        if (!strcmp(rs->name, elemset_name)) {
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
                         struct kndElemSet  **result)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = val_size;

    struct kndElemSet *rs;

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
        knd_log("\n\n     POSITIONAL FACET UNIT:   Pelemix: \"%s\" [%lu] Tail: \"%s\" [%lu]\n",
                buf,  (unsigned long)buf_size,
                tail, (unsigned long)tail_size);

    for (size_t i = 0; i < self->num_elemsets; i++) {
        rs = self->elemsets[i];

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
    struct kndElemSet *rs;
    int err = knd_FAIL;

    if (DEBUG_FACET_LEVEL_3)
        knd_log("  facet \"%s\" to extract objs   (batch size: %lu)\n",
                self->name,
                (unsigned long)self->batch_size);

    for (size_t i = 0; i < self->num_elemsets; i++) {
        rs = self->elemsets[i];

        rs->batch_size = self->batch_size;

        err = rs->extract_objs(rs);
        if (err) return err;

        /*if (self->cache->num_matches >= self->batch_size)
            break;
        */
    }

    return knd_OK;
}

static int
kndFacet_find(struct kndFacet    *self,
              const char         *val,
              size_t              val_size,
              struct kndElemSet  **result)
{

    //struct kndFacet *f;
    struct kndElemSet *rs;
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
    
    for (size_t i = 0; i < self->num_elemsets; i++) {
        rs = self->elemsets[i];

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

    //struct kndSetElem *elem;
    struct kndElemSet *rs;
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

    /* linearize elemsets  */
    for (size_t i = 0; i < self->num_elemsets; i++) {
        rs = self->elemsets[i];

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

        if (rs->num_elems > 1) {
            curr_size = sprintf(rec, "=%lu",
                                (unsigned long)rs->num_elems);
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

extern void kndFacet_init(struct kndFacet *self)
{
    self->del = kndFacet_del;
    self->str = kndFacet_str;

    self->add_elem = kndFacet_add_elem;
    self->find = kndFacet_find;
    self->extract_objs = kndFacet_extract_objs;

    self->read = kndFacet_read;
    self->read_tags = kndFacet_read_tags;

    self->export = kndFacet_export;
    self->sync = kndFacet_sync;
}


extern int 
kndFacet_new(struct kndFacet **facet)
{
    struct kndFacet *self;

    self = malloc(sizeof(struct kndFacet));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndFacet));

    *facet = self;

    return knd_OK;
}
