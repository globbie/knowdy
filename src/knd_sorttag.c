#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_dataclass.h"
#include "knd_object.h"
#include "knd_sorttag.h"
#include "knd_output.h"
#include "knd_utils.h"

#define DEBUG_SORTTAG_LEVEL_0 0
#define DEBUG_SORTTAG_LEVEL_1 0
#define DEBUG_SORTTAG_LEVEL_2 0
#define DEBUG_SORTTAG_LEVEL_3 0
#define DEBUG_SORTTAG_LEVEL_TMP 1

extern int 
knd_compare_attr_ascend(const void *a,
                         const void *b)
{
    struct kndSortAttr **obj1, **obj2;

    obj1 = (struct kndSortAttr**)a;
    obj2 = (struct kndSortAttr**)b;

    if ((*obj1)->numval == (*obj2)->numval) return 0;

    if ((*obj1)->numval > (*obj2)->numval) return 1;

    return -1;
}

extern int 
knd_compare_attr_descend(const void *a,
                         const void *b)
{
    struct kndSortAttr **obj1, **obj2;

    obj1 = (struct kndSortAttr**)a;
    obj2 = (struct kndSortAttr**)b;

    if ((*obj1)->numval == (*obj2)->numval) return 0;

    if ((*obj1)->numval < (*obj2)->numval) return 1;

    return -1;
}


static void
kndSortTag_del(struct kndSortTag *self)
{
    struct kndSortAttr *attr;
    size_t i;

    for (i = 0; i < self->num_attrs; i++) {
        attr = self->attrs[i];
        if (attr)
            free(attr);
        self->attrs[i] = NULL;
    }
    
    free(self);
}

static int 
kndSortTag_str(struct kndSortTag *self)
{
    struct kndSortAttr *attr;
    size_t i;
    
    knd_log("   ==  SORTTAG [num_attrs: %lu]\n", (unsigned long)self->num_attrs);

    for (i = 0; i < self->num_attrs; i++) {
        attr = self->attrs[i];

        knd_log("     %d) ATTR   %s: \"%s\"  (numval: %d)\n",
                i, attr->name, attr->val, attr->numval);
    }
    
    return knd_OK;
}

static int 
kndSortTag_export_GSC(struct kndSortTag *self)
{
    struct kndSortAttr *attr;
    struct kndOutput *out;
    int i, err;

    out = self->out;

    for (i = 0; i < self->num_attrs; i++) {
        attr = self->attrs[i];

        err = out->write(out, "{", 1);
        if (err) return err;

        err = out->write(out, attr->name, attr->name_size);
        if (err) return err;

        err = out->write(out, " ", 1);
        if (err) return err;

        err = out->write(out, attr->val, attr->val_size);
        if (err) return err;

        err = out->write(out, "}", 1);
        if (err) return err;
    }
    
    return knd_OK;
}

static int 
kndSortTag_export(struct kndSortTag *self,
                 knd_format         format)
{
    int err;
    
    switch(format) {
    case KND_FORMAT_GSC:
        
        err = kndSortTag_export_GSC(self);
        if (err) return err;

        break;
    default:
        break;
    }
    
    return knd_OK;
}


/*

static int 
kndSortTag_parse(struct kndSortTag *self)
{
    char id_buf[KND_ID_SIZE + 1];

    knd_log(" SORTTAG: %s\n", self->buf);
    memset(id_buf, 0, KND_ID_SIZE + 1);

    self->inbox_size = buf_size / KND_ID_SIZE;
    
    
    ret = reader->obj_exists(reader,
                             (const char*)id_buf,
                             &obj);
    if (ret != knd_OK) continue;

    knd_log("obj exists!\n");
    

    return knd_OK;
}
*/



static int 
kndSortTag_import(struct kndSortTag   *self,
                  struct kndDataClass *baseclass,
                  char                *rec)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size = KND_TEMP_BUF_SIZE;

    struct kndDataIdx *idx;
    struct kndSortAttr *attr = NULL;
    char *b;
    char *c;
    char *n;
    long numval;
    size_t label_size;
    int i, err;
    
    /* parse DB rec */
    b = rec;
    c = rec;

    if (DEBUG_SORTTAG_LEVEL_3)
        knd_log("  .. import sorttag: %s\n", rec);
    
    while (*c) {
        switch (*c) {
        case ';':
            /**c = '\0';*/
            if (!attr) {
                err = knd_FAIL;
                goto final;
            }

            label_size = c - b;
            if (label_size > KND_NAME_SIZE) {
                err = knd_FAIL;
                goto final;
            }

            memcpy(buf, b, label_size);
            buf[label_size] = '\0';
            
            /* any hilites? */
            n = strstr(buf, GSL_OFFSET);
            if (n) {
                label_size = n - buf;
                n += GSL_OFFSET_SIZE;

                /* get the numeric offset */
                err = knd_parse_num((const char*)n, &numval);
                if (err) goto final;

                attr->start_pos = (size_t)numval;
                
                /* TODO: hilite len  */
            }
            
            memcpy(attr->val, buf, label_size);
            attr->val_size = label_size;
            attr->val[label_size] = '\0';
            
            self->attrs[self->num_attrs] = attr;
            self->num_attrs++;

            attr = NULL;
            b = c + 1;
            break;
        case ':':
            /**c = '\0';*/
            
            label_size = c - b;
            if (label_size > KND_NAME_SIZE) {
                err = knd_FAIL;
                goto final;
            }

            attr = malloc(sizeof(struct kndSortAttr));
            if (!attr) return knd_NOMEM;

            memset(attr, 0, sizeof(struct kndSortAttr));
            
            memcpy(attr->name, b, label_size);
            attr->name_size = label_size;
            attr->name[label_size] = '\0';

            /* get the idx */
            idx = baseclass->indices;
            while (idx) {
                if (!strcmp(attr->name, idx->abbr))
                    break;
                idx = idx->next;
            }

            if (!idx) {
                knd_log("  -- no IDX facet found: \"%s\"\n", attr->name);
                err = knd_FAIL;
                goto cleanup;
            }

            attr->is_default = idx->is_default;
            attr->type = idx->type;
            attr->numval = idx->numval;
            attr->idx = idx;
            
            b = c + 1;
            break;
            
        default:
            break;
        }
        c++;
    }


    label_size = c - b;
    if (!label_size) goto final;
    
    if (label_size > KND_NAME_SIZE) {
        err = knd_FAIL;
        goto final;
    }

    if (!attr) {
        err = knd_FAIL;
        goto final;
    }
    
    memcpy(attr->val, b, label_size);
    attr->val_size = label_size;
    attr->val[attr->val_size] = '\0';
    
    self->attrs[self->num_attrs] = attr;
    self->num_attrs++;

    /*knd_log("\n  == FINAL ATTR: %s [%lu] => %s [%lu]\n",
            attr->name, (unsigned long)attr->name_size,
            attr->val, (unsigned long)attr->val_size);
    */
    qsort(self->attrs,
          self->num_attrs,
          sizeof(struct kndSortAttr*), knd_compare_attr_ascend);
    
    return knd_OK;
    
 cleanup:

    if (attr)
        free(attr);

 final:

    return err;
}

extern int 
kndSortTag_new(struct kndSortTag **sorttag)
{
    struct kndSortTag *self;
    
    self = malloc(sizeof(struct kndSortTag));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndSortTag));

    self->del = kndSortTag_del;
    self->str = kndSortTag_str;
    self->import = kndSortTag_import;
    self->export = kndSortTag_export;

    *sorttag = self;

    return knd_OK;
}
