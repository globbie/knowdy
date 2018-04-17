#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

/* numeric conversion by strtol */
#include <errno.h>
#include <limits.h>

#include "knd_config.h"
#include "knd_mempool.h"
#include "knd_state.h"
#include "knd_concept.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_object.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>
#include <glb-lib/output.h>

#define DEBUG_CONC_LEVEL_1 0
#define DEBUG_CONC_LEVEL_2 0
#define DEBUG_CONC_LEVEL_3 0
#define DEBUG_CONC_LEVEL_4 0
#define DEBUG_CONC_LEVEL_5 0
#define DEBUG_CONC_LEVEL_TMP 1

static int build_attr_name_idx(struct kndConcept *self);

static int unfreeze_class(struct kndConcept *self,
                          struct kndConcDir *dir,
                          struct kndConcept **result);

static gsl_err_t run_set_translation_text(void *obj, const char *val, size_t val_size);

static int read_obj_entry(struct kndConcept *self,
                          struct kndObjEntry *entry,
                          struct kndObject **result);

static int get_class(struct kndConcept *self,
                     const char *name, size_t name_size,
                     struct kndConcept **result);

static int get_dir_trailer(struct kndConcept *self,
                           struct kndConcDir *parent_dir,
                           int fd,
                           int encode_base);
static int parse_dir_trailer(struct kndConcept *self,
                             struct kndConcDir *parent_dir,
                             int fd,
                             int encode_base);
static int get_obj_dir_trailer(struct kndConcept *self,
                               struct kndConcDir *parent_dir,
                               int fd,
                               int encode_base);
static gsl_err_t run_set_name(void *obj, const char *name, size_t name_size);

static int freeze(struct kndConcept *self);

static int read_GSL_file(struct kndConcept *self,
                         struct kndConcFolder *parent_folder,
                         const char *filename,
                         size_t filename_size);

static int get_attr(struct kndConcept *self,
                    const char *name, size_t name_size,
                    struct kndAttr **result);

static int attr_items_export_GSP(struct kndConcept *self,
                                 struct kndAttrItem *items,
                                 size_t depth);
static gsl_err_t read_nested_attr_item(void *obj,
                                       const char *name, size_t name_size,
                                       const char *rec, size_t *total_size);
static void append_attr_item(struct kndConcItem *ci,
                             struct kndAttrItem *attr_item);

static void reset_inbox(struct kndConcept *self)
{
    struct kndConcept *c, *next_c;
    struct kndObject *obj, *next_obj;

    c = self->inbox;
    while (c) {
        c->reset_inbox(c);
        next_c = c->next;
        c->next = NULL;
        c = next_c;
    }

    obj = self->obj_inbox;
    while (obj) {
        next_obj = obj->next;
        obj->next = NULL;
        obj = next_obj;
    }

    self->obj_inbox = NULL;
    self->obj_inbox_size = 0;
    self->inbox = NULL;
    self->inbox_size = 0;
}

static void del_conc_dir(struct kndConcDir *dir)
{
    struct kndConcDir *subdir;
    size_t i;

    for (i = 0; i < dir->num_children; i++) {
        subdir = dir->children[i];
        if (!subdir) continue;
        del_conc_dir(subdir);
        dir->children[i] = NULL;
    }

    if (dir->children) {
        free(dir->children);
        dir->children = NULL;
    }

    if (dir->obj_name_idx) {
        dir->obj_name_idx->del(dir->obj_name_idx);
        dir->obj_name_idx = NULL;
    }

    if (dir->rels) {
        free(dir->rels);
        dir->rels = NULL;
    }
}

/*  class destructor */
static void kndConcept_del(struct kndConcept *self)
{
    if (self->attr_name_idx) self->attr_name_idx->del(self->attr_name_idx);
    if (self->summary) self->summary->del(self->summary);
    if (self->dir) del_conc_dir(self->dir);
}

static void str_attr_items(struct kndAttrItem *items, size_t depth)
{
    struct kndAttrItem *item;
    struct kndAttrItem *list_item;
    const char *classname = "None";
    size_t classname_size = strlen("None");
    struct kndConcept *conc;
    size_t count = 0;

    for (item = items; item; item = item->next) {
        if (item->attr && item->attr->parent_conc) {
            conc = item->attr->parent_conc;
            classname = conc->name;
            classname_size = conc->name_size;
        }

        if (item->attr && item->attr->is_a_set) {
            knd_log("%*s_list attr: \"%.*s\" (base: %.*s) size: %zu [",
                    depth * KND_OFFSET_SIZE, "",
                    item->name_size, item->name,
                    classname_size, classname,
                    item->num_list_elems);
            count = 0;
            if (item->val_size) {
                count = 1;
                knd_log("%*s%zu)  %.*s",
                        depth * KND_OFFSET_SIZE, "",
                        count,
                        item->val_size, item->val);
            }

            for (list_item = item->list;
                 list_item;
                 list_item = list_item->next) {
                count++;
                knd_log("%*s%zu)  %.*s",
                        depth * KND_OFFSET_SIZE, "",
                        count,
                        list_item->val_size, list_item->val);
            }
            knd_log("%*s]", depth * KND_OFFSET_SIZE, "");
            continue;
        }

        knd_log("%*s_attr: \"%.*s\" (base: %.*s)  => %.*s",
                depth * KND_OFFSET_SIZE, "",
                item->name_size, item->name,
                classname_size, classname,
                item->val_size, item->val);

        if (item->children)
            str_attr_items(item->children, depth + 1);
    }
}

static void str(struct kndConcept *self)
{
    struct kndAttr *attr;
    struct kndAttrEntry *attr_entry;
    struct kndTranslation *tr, *t;
    struct kndConcRef *ref;
    struct kndConcItem *item;
    struct kndConcept *c;
    const char *name;
    size_t name_size;
    char resolved_state = '-';
    const char *key;
    void *val;

    knd_log("\n%*s{class %.*s    id:%.*s numid:%zu",
            self->depth * KND_OFFSET_SIZE, "",
            self->name_size, self->name,
            self->dir->id_size, self->dir->id,
            self->dir->numid);

    for (tr = self->tr; tr; tr = tr->next) {
        knd_log("%*s~ %s %.*s",
                (self->depth + 1) * KND_OFFSET_SIZE, "",
                tr->locale, tr->val_size, tr->val);
        if (tr->synt_roles) {
            for (t = tr->synt_roles; t; t = t->next) {
                knd_log("%*s  %d: %.*s",
                        (self->depth + 2) * KND_OFFSET_SIZE, "",
                        t->synt_role, t->val_size, t->val);
            }
        }
    }

    if (self->summary) {
        self->summary->depth = self->depth + 1;
        self->summary->str(self->summary);
    }

    if (self->num_base_items) {
        for (item = self->base_items; item; item = item->next) {
            resolved_state = '-';
            name = item->classname;
            name_size = item->classname_size;
            if (item->conc) {
                name = item->conc->name;
                name_size = item->conc->name_size,
                resolved_state = '+';
            }

            knd_log("%*s_base \"%.*s\" id:%.*s numid:%zu [%c]",
                    (self->depth + 1) * KND_OFFSET_SIZE, "",
                    name_size, name,
                    item->id_size, item->id, item->numid,
                    resolved_state);

            if (item->attrs) {
                str_attr_items(item->attrs, self->depth + 1);
            }
        }
    }

    if (self->depth) {
        if (self->attr_name_idx) {
            key = NULL;
            self->attr_name_idx->rewind(self->attr_name_idx);
            do {
                self->attr_name_idx->next_item(self->attr_name_idx, &key, &val);
                if (!key) break;
            attr_entry = val;
            attr = attr_entry->attr;
            attr->depth = self->depth + 1;
            attr->str(attr);
            } while (key);
        }
    } else {
        for (attr = self->attrs; attr; attr = attr->next) {
            attr->depth = self->depth + 1;
            attr->str(attr);
        }
    }

    for (size_t i = 0; i < self->num_children; i++) {
        ref = &self->children[i];
        c = ref->dir->conc;
        if (!c) continue;

        knd_log("%*sbase of --> %.*s [%zu]",
                (self->depth + 1) * KND_OFFSET_SIZE, "",
                c->name_size, c->name, c->num_terminals);
        c->str(c);
    }

    /*if (self->dir->descendants) {
        set = self->dir->descendants;
        err = set->map(set, str_conc_elem, NULL);
        if (err) return;
        }*/

    /*if (self->dir->reverse_attr_name_idx) {
        idx = self->dir->reverse_attr_name_idx;
        key = NULL;
        idx->rewind(idx);
        do {
            idx->next_item(idx, &key, &val);
            if (!key) break;
            set = val;
            set->str(set, self->depth + 1);
        } while (key);
        }*/

    knd_log("%*s}", self->depth * KND_OFFSET_SIZE, "");
}

static gsl_err_t alloc_gloss_item(void *obj,
                                  const char *name,
                                  size_t name_size,
                                  size_t count,
                                  void **item)
{
    struct kndAttr *self = obj;
    struct kndTranslation *tr;

    assert(name == NULL && name_size == 0);

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. %.*s: allocate gloss translation,  count: %zu",
                self->name_size, self->name, count);

    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return make_gsl_err_external(knd_NOMEM);

    memset(tr, 0, sizeof(struct kndTranslation));
    *item = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t append_gloss_item(void *accu,
                                   void *item)
{
    struct kndConcept *self =   accu;
    struct kndTranslation *tr = item;

    tr->next = self->tr;
    self->tr = tr;
   
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss_item(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndTranslation *tr = obj;
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = tr->curr_locale,
          .buf_size = &tr->curr_locale_size,
          .max_buf_size = sizeof tr->curr_locale
        },
        { .name = "t",
          .name_size = strlen("t"),
          .buf = tr->val,
          .buf_size = &tr->val_size,
          .max_buf_size = sizeof tr->val
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (tr->curr_locale_size == 0 || tr->val_size == 0)
        return make_gsl_err(gsl_FORMAT);  // error: both of them are required

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. read gloss translation: \"%.*s\",  text: \"%.*s\"",
                tr->locale_size, tr->locale, tr->val_size, tr->val);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss_array(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndConcept *self = obj;

    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .alloc = alloc_gloss_item,
        .append = append_gloss_item,
        .accu = self,
        .parse = parse_gloss_item
    };

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. %.*s: reading gloss",
                self->name_size, self->name);

    return gsl_parse_array(&item_spec, rec, total_size);
}

static int inherit_attrs(struct kndConcept *self, struct kndConcept *base)
{
    struct kndConcDir *dir;
    struct kndAttr *attr;
    struct kndConcept *c;
    struct kndAttrEntry *entry;
    struct kndConcItem *item;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. \"%.*s\" class to inherit attrs from \"%.*s\"..",
                self->name_size, self->name, base->name_size, base->name);

    if (!base->is_resolved) {
        err = base->resolve(base, NULL);                                          RET_ERR();
    }

    /* check circled relations */
    for (size_t i = 0; i < self->num_bases; i++) {
        dir = self->bases[i];
        c = dir->conc;
        if (DEBUG_CONC_LEVEL_2)
            knd_log("== (%zu of %zu)  \"%.*s\" is a base of \"%.*s\"", 
                    i, self->num_bases, c->name_size, c->name,
                    self->name_size, self->name);
        if (dir->conc == base) {
            knd_log("-- circle inheritance detected for \"%.*s\" :(",
                    base->name_size, base->name);
            return knd_FAIL;
        }
    }
    
    /* get attrs from base */
    for (attr = base->attrs; attr; attr = attr->next) {
        /* compare with exiting attrs */
        entry = self->attr_name_idx->get(self->attr_name_idx,
                                         attr->name, attr->name_size);
        if (entry) {
            knd_log("-- %.*s attr collision between \"%.*s\" and base class \"%.*s\"?",
                    entry->name_size, entry->name,
                    self->name_size, self->name,
                    base->name_size, base->name);
            return knd_FAIL;
        }

        /* register attr entry */
        entry = malloc(sizeof(struct kndAttrEntry));
        if (!entry) return knd_NOMEM;
        memset(entry, 0, sizeof(struct kndAttrEntry));
        memcpy(entry->name, attr->name, attr->name_size);
        entry->name_size = attr->name_size;
        entry->attr = attr;

        err = self->attr_name_idx->set(self->attr_name_idx,
                                       entry->name, entry->name_size,
                                       (void*)entry);
        if (err) return err;
    }

    if (self->num_bases >= KND_MAX_BASES) {
        knd_log("-- max bases exceeded for %.*s :(",
                self->name_size, self->name);

        return knd_FAIL;
    }
    self->bases[self->num_bases] = base->dir;
    self->num_bases++;

    if (DEBUG_CONC_LEVEL_1)
        knd_log(" .. add %.*s parent to %.*s", 
                base->dir->conc->name_size,
                base->dir->conc->name,
                self->name_size, self->name);

    /* contact the grandparents */
    for (item = base->base_items; item; item = item->next) {
        if (item->conc) {
            err = inherit_attrs(self, item->conc);                                RET_ERR();
        }
    }
    
    return knd_OK;
}

static int is_base(struct kndConcept *self,
                   struct kndConcept *child)
{
    if (DEBUG_CONC_LEVEL_2) {
        knd_log(".. check inheritance: %.*s [resolved: %d] => %.*s [resolved:%d]?",
                child->name_size, child->name, child->is_resolved,
                self->name_size, self->name, self->is_resolved);
    }

    /* make sure that a child inherits from the base */
    for (size_t i = 0; i < child->num_bases; i++) {
        if (child->bases[i]->conc == self) {
            return knd_OK;
        }
    }

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log("-- no inheritance from  \"%.*s\" to \"%.*s\" :(",
                self->name_size, self->name,
                child->name_size, child->name);
    return knd_FAIL;
}

static int index_attr(struct kndConcept *self,
                      struct kndAttr *attr,
                      struct kndAttrItem *item)
{
    struct kndConcept *base;
    struct kndConcept *c;
    struct kndSet *set;
    int err;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log("\n.. indexing CURR CLASS: \"%.*s\" .. index attr: \"%.*s\" [type:%d]"
                " refclass: \"%.*s\" (val: %.*s)",
                self->name_size, self->name,
                attr->name_size, attr->name, attr->type,
                attr->ref_classname_size, attr->ref_classname,
                item->val_size, item->val);
    }
    if (!attr->ref_classname_size) return knd_OK;

    /* template base class */
    err = get_class(self,
                    attr->ref_classname,
                    attr->ref_classname_size,
                    &base);                                                       RET_ERR();
    if (!base->is_resolved) {
        err = base->resolve(base, NULL);                                          RET_ERR();
    }

    /* specific class */
    err = get_class(self, item->val,
                    item->val_size, &c);                                          RET_ERR();
    item->conc = c;

    if (!c->is_resolved) {
        err = c->resolve(c, NULL);                                                RET_ERR();
    }
    err = is_base(base, c);                                                       RET_ERR();

    set = attr->parent_conc->dir->descendants;

    /* add curr class to the reverse index */
    err = set->add_ref(set, attr, self->dir, c->dir);
    if (err) return err;

    return knd_OK;
}

static int resolve_class_ref(struct kndConcept *self,
                             const char *name, size_t name_size,
                             struct kndConcept *base,
                             struct kndConcept **result)
{
    struct kndConcDir *dir;
    struct kndConcept *c;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. checking class ref:  %.*s base resolved:%d",
                name_size, name, base->is_resolved);
    
    dir = self->class_name_idx->get(self->class_name_idx, name, name_size);
    if (!dir) {
        knd_log("-- no such class: \"%.*s\"", name_size, name);
        return knd_FAIL;
    }
    c = dir->conc;
    if (!c->is_resolved) {
        err = c->resolve(c, NULL);                                                RET_ERR();
    }
    err = is_base(base, c);                                                       RET_ERR();
    *result = c;

    return knd_OK;
}

static int resolve_aggr_item(struct kndConcept *self,
                             struct kndAttrItem *item,
                             struct kndAttr *attr)
{
    struct kndConcDir *dir;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log(".. resolve aggr item: %p \"%.*s\": class:%.*s",
                item, item->name_size, item->name,
                item->val_size, item->val);
    }

    dir = self->class_name_idx->get(self->class_name_idx,
                                    item->val, item->val_size);
    if (!dir) {
        if (DEBUG_CONC_LEVEL_TMP) {
            knd_log("-- %.*s: aggr item not resolved: no such class: \"%.*s\" :(",
                    self->name_size, self->name, item->val_size, item->val);
        }
        // TODO
        item->conc = self;
        return knd_OK;
    }

    item->conc = dir->conc;
    item->attr = attr;

    /* TODO: resolve nested elems */

    
    return knd_OK;
}

static int resolve_attr_item_list(struct kndConcept *self,
                                  struct kndAttrItem *parent_item,
                                  struct kndAttr *attr)
{
    struct kndAttrItem *item;
    struct kndConcept *c;
    int err;

    assert(attr->conc != NULL);

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. class %.*s to resolve attr item list: %.*s REF CLASS:%.*s attr type:%d",
                self->name_size, self->name,
                parent_item->name_size, parent_item->name,
                attr->ref_classname_size, attr->ref_classname, attr->type);

    /* base template class */
    c = attr->conc;
    if (!c->is_resolved) {
        err = c->resolve(c, NULL);                                                RET_ERR();
    }

    /* first item */
    if (parent_item->val_size) {
        switch (attr->type) {
        case KND_ATTR_AGGR:
            err = resolve_aggr_item(self, parent_item, attr);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            err = resolve_class_ref(self,
                                    parent_item->val, parent_item->val_size,
                                    c, &parent_item->conc);
            if (err) return err;
            break;
        default:
            break;
        }
    }

    for (item = parent_item->list; item; item = item->next) {
        switch (attr->type) {
        case KND_ATTR_AGGR:
            err = resolve_aggr_item(self, item, attr);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            err = resolve_class_ref(self, item->val, item->val_size,
                                    c, &item->conc);
            if (err) return err;
            break;
        default:
            break;
        }
    }
    return knd_OK;
}

static int resolve_attr_items(struct kndConcept *self,
                              struct kndConcItem *parent_item)
{
    struct kndAttrItem *item;
    struct kndAttrEntry *entry;
    struct kndConcept *c;
    int err;

    if (DEBUG_CONC_LEVEL_1){
        knd_log(".. resolving attr items of class %.*s",
                self->name_size, self->name);
    }

    for (item = parent_item->attrs; item; item = item->next) {
        entry = self->attr_name_idx->get(self->attr_name_idx,
                                    item->name, item->name_size);
        if (!entry) {
            knd_log("-- no such attr: %.*s", item->name_size, item->name);
            return knd_FAIL;
        }

        if (entry->attr->is_a_set) {
            err = resolve_attr_item_list(self, item, entry->attr);
            if (err) return err;
            if (item->val_size) 
                item->num_list_elems++;
            item->attr = entry->attr;
            continue;
        }

        /* single attr */
        switch (entry->attr->type) {
        case KND_ATTR_AGGR:
            /* TODO */
            break;
        case KND_ATTR_REF:
            c = entry->attr->conc;
            if (!c->is_resolved) {
                err = c->resolve(c, NULL);                                        RET_ERR();
            }
            err = resolve_class_ref(self, item->val, item->val_size,
                                    c, &item->conc);
            if (err) return err;
            break;
        default:
            /* atomic value, call a validation function? */
            break;
        }
        if (entry->attr->is_indexed) {
            err = index_attr(self, entry->attr, item);
            if (err) return err;
        }
        item->attr = entry->attr;
    }
    return knd_OK;
}

static int resolve_attrs(struct kndConcept *self)
{
    struct kndAttr *attr;
    struct kndAttrEntry *entry;
    struct kndConcDir *dir;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. resolve attrs ..");

    err = ooDict_new(&self->attr_name_idx, KND_SMALL_DICT_SIZE);                       RET_ERR();

    for (attr = self->attrs; attr; attr = attr->next) {
        entry = self->attr_name_idx->get(self->attr_name_idx, attr->name, attr->name_size);
        if (entry) {
            knd_log("-- %.*s attr already exists?", attr->name_size, attr->name);
            return knd_FAIL;
        }

        /* TODO: mempool */
        entry = malloc(sizeof(struct kndAttrEntry));
        if (!entry) return knd_NOMEM;

        memset(entry, 0, sizeof(struct kndAttrEntry));
        memcpy(entry->name, attr->name, attr->name_size);
        entry->name_size = attr->name_size;
        entry->attr = attr;

        err = self->attr_name_idx->set(self->attr_name_idx,
                                  entry->name, entry->name_size, (void*)entry);   RET_ERR();
        if (DEBUG_CONC_LEVEL_2)
            knd_log("++ register primary attr: \"%.*s\"",
                    attr->name_size, attr->name);

        switch (attr->type) {
        case KND_ATTR_AGGR:
        case KND_ATTR_REF:
            if (attr->ref_procname_size) {
                /* TODO: resolve proc ref */
                break;
            }
            if (!attr->ref_classname_size) {
                knd_log("-- no classname specified for attr \"%s\"",
                        attr->name);
                return knd_FAIL;
            }
            dir = self->class_name_idx->get(self->class_name_idx,
                                            attr->ref_classname,
                                            attr->ref_classname_size);
            if (!dir) {
                knd_log("-- no such class: \"%.*s\" .."
                        "couldn't resolve the \"%.*s\" attr of %.*s :(",
                        attr->ref_classname_size,
                        attr->ref_classname,
                        attr->name_size, attr->name,
                        self->name_size, self->name);
                return knd_FAIL;
            }
            attr->conc = dir->conc;
            break;
        default:
            break;
        }
    }

    return knd_OK;
}

static int resolve_objs(struct kndConcept     *self,
                        struct kndClassUpdate *class_update)
{
    struct kndObject *obj;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("..resolving objs, num objs: %zu",
                self->obj_inbox_size);

    if (class_update) {
        class_update->objs = calloc(self->obj_inbox_size,
                                    sizeof(struct kndObject*));
        if (!class_update->objs) {
            err = knd_NOMEM;
            goto final;
        }
    }

    for (obj = self->obj_inbox; obj; obj = obj->next) {
        if (obj->state->phase == KND_REMOVED) {
            knd_log("NB: \"%.*s\" obj to be removed", obj->name_size, obj->name);
            goto update;
        }
        obj->task = self->task;
        obj->log = self->log;

        err = obj->resolve(obj);
        if (err) {
            knd_log("-- %.*s obj not resolved :(",
                    obj->name_size, obj->name);
            goto final;
        }
        obj->state->phase = KND_CREATED;

    update:

        if (class_update) {
            /* NB: should never happen: mismatch of num objs */
            if (class_update->num_objs >= self->obj_inbox_size) {
                knd_log("-- num objs mismatch in %.*s:  %zu vs %zu:(",
                        self->name_size, self->name,
                        class_update->num_objs, self->obj_inbox_size);
                return knd_FAIL;
            }
            class_update->objs[class_update->num_objs] = obj;
            class_update->num_objs++;
        }
    }
    err =  knd_OK;

 final:
 
    return err;
}

static int resolve_base_classes(struct kndConcept *self)
{
    struct kndConcItem *item;
    void *result;
    struct kndConcDir *dir;
    struct kndConcept *c;
    struct kndConcRef *ref;
    struct kndSet *set;
    const char *classname;
    size_t classname_size;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. resolving base classes of \"%.*s\"..",
                self->name_size, self->name);

    /* resolve refs to base classes */
    for (item = self->base_items; item; item = item->next) {
        if (item->parent == self) {
            /* TODO */
            if (DEBUG_CONC_LEVEL_2)
                knd_log(".. \"%.*s\" class to check the update request: \"%s\"..",
                        self->name_size, self->name,
                        item->classname_size, item->classname);
            continue;
        }

        classname = item->classname;
        classname_size = item->classname_size;
        if (item->id_size) {
            err = self->class_idx->get(self->class_idx,
                                       item->id, item->id_size, &result);
            if (err) {
                knd_log("-- no such class: %.*s :(",
                        item->id_size, item->id);
                return knd_FAIL;
            }
            dir = result;
            memcpy(item->classname, dir->name, dir->name_size);
            item->classname_size = dir->name_size;

            classname = dir->name;
            classname_size = dir->name_size;
        }

        if (!classname_size) {
            knd_log("-- no base class specified in class item \"%.*s\" :(",
                    self->name_size, self->name);
            return knd_FAIL;
        }

        if (DEBUG_CONC_LEVEL_2)
            knd_log("\n.. \"%.*s\" class to get its base class: \"%.*s\"..",
                    self->name_size, self->name,
                    classname_size, classname);

        err = get_class(self, classname, classname_size, &c);         RET_ERR();
        if (c == self) {
            knd_log("-- self reference detected in \"%.*s\" :(",
                    item->classname_size, item->classname);
            return knd_FAIL;
        }

        if (DEBUG_CONC_LEVEL_2)
            knd_log("++ \"%.*s\" ref established as a base class for \"%.*s\"!",
                    item->classname_size, item->classname, self->name_size, self->name);

        item->conc = c;

        /* check item doublets */
        for (size_t i = 0; i < self->num_children; i++) {
            ref = &self->children[i];
            if (ref->dir == self->dir) {
                knd_log("-- doublet conc item found in \"%.*s\" :(",
                        self->name_size, self->name);
                return knd_FAIL;
            }
        }

        if (c->num_children >= KND_MAX_CONC_CHILDREN) {
            knd_log("-- %.*s as child to %.*s - max conc children exceeded :(",
                    self->name_size, self->name, item->classname_size, item->classname);
            return knd_FAIL;
        }

        ref = &c->children[c->num_children];
        ref->dir = self->dir;
        c->num_children++;

        /* register as a descendant */
        set = c->dir->descendants;
        if (!set) {
            err = self->mempool->new_set(self->mempool, &set);                     RET_ERR();
            set->type = KND_SET_CLASS;
            set->base = c->dir;
            c->dir->descendants = set;
        }

        dir = self->dir;
        err = set->add(set, dir->id, dir->id_size, (void*)dir);                   RET_ERR();

        err = inherit_attrs(self, item->conc);                                    RET_ERR();
    }

    return knd_OK;
}

static int resolve_refs(struct kndConcept *self,
                        struct kndClassUpdate *update)
{
    struct kndConcept *root;
    struct kndConcRef *ref;
    struct kndConcItem *item;
    struct kndConcDir *dir;
    int err;

    if (self->is_resolved) {
        if (self->obj_inbox_size) {
            err = resolve_objs(self, update);                                     RET_ERR();
        }
        return knd_OK;
    } else {
        self->root_class->next_numid++;
        dir = self->dir;
        dir->numid = self->root_class->next_numid;
        knd_num_to_str(dir->numid, dir->id, &dir->id_size, KND_RADIX_BASE);
    }

    if (DEBUG_CONC_LEVEL_1) {
        knd_log(".. resolving class \"%.*s\" dir numid:%zu",
                self->name_size, self->name, self->dir->numid);
    }

    /* a child of the root class
     * TODO: refset */
    if (!self->base_items) {
        root = self->root_class;
        ref = &root->children[root->num_children];
        ref->dir = self->dir;
        root->num_children++;
    }

    /* resolve and index the attrs */
    if (!self->attr_name_idx) {
        err = resolve_attrs(self);                                                RET_ERR();
    }

    if (self->base_items) {
        err = resolve_base_classes(self);                                         RET_ERR();
    }

    for (item = self->base_items; item; item = item->next) {
        if (item->attrs) {
            err = resolve_attr_items(self, item);                             RET_ERR();
        }
    }

    if (self->obj_inbox_size) {
        err = resolve_objs(self, update);                                         RET_ERR();
    }

    self->is_resolved = true;
    return knd_OK;
}

static int build_attr_name_idx(struct kndConcept *self)
{
    struct kndConcItem *item;
    struct kndAttr *attr;
    struct kndAttrEntry *entry;
    int err;

    err = ooDict_new(&self->attr_name_idx, KND_SMALL_DICT_SIZE);                  RET_ERR();

    for (attr = self->attrs; attr; attr = attr->next) {
        /* TODO: mempool */
        entry = malloc(sizeof(struct kndAttrEntry));
        if (!entry) return knd_NOMEM;
        memset(entry, 0, sizeof(struct kndAttrEntry));
        memcpy(entry->name, attr->name, attr->name_size);
        entry->name_size = attr->name_size;
        entry->attr = attr;

        err = self->attr_name_idx->set(self->attr_name_idx,
                                       entry->name, entry->name_size,
                                       (void*)entry);                              RET_ERR();
    }
    
    for (item = self->base_items; item; item = item->next) {
        if (DEBUG_CONC_LEVEL_2)
            knd_log(".. class \"%.*s\" to inherit attrs from baseclass \"%.*s\"..",
                    self->name_size, self->name,
                    item->conc->name_size,
                    item->conc->name);
        err = inherit_attrs(self, item->conc);
    }

    return knd_OK;
}

static int get_attr(struct kndConcept *self,
                    const char *name, size_t name_size,
                    struct kndAttr **result)
{
    struct kndAttr *attr;
    struct kndAttrEntry *entry;
    int err;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log(".. \"%.*s\" class to check attr \"%.*s\"",
                self->name_size, self->name, name_size, name);
    }

    if (!self->attr_name_idx) {
        err = ooDict_new(&self->attr_name_idx, KND_SMALL_DICT_SIZE);
        if (err) return err;

        for (attr = self->attrs; attr; attr = attr->next) {
            entry = malloc(sizeof(struct kndAttrEntry));
            if (!entry) return knd_NOMEM;
            memset(entry, 0, sizeof(struct kndAttrEntry));
            memcpy(entry->name, attr->name, attr->name_size);
            entry->name_size = attr->name_size;
            entry->name[entry->name_size] = '\0';
            entry->attr = attr;

            err = self->attr_name_idx->set(self->attr_name_idx,
                                      entry->name, entry->name_size, (void*)entry);
            if (err) return err;
            if (DEBUG_CONC_LEVEL_2)
                knd_log("++ register primary attr: \"%.*s\"",
                        attr->name_size, attr->name);
        }
    }

    entry = self->attr_name_idx->get(self->attr_name_idx, name, name_size);
    if (!entry) {
        knd_log("-- attr idx has no entry: %.*s :(", name_size, name);
        return knd_NO_MATCH;
    }

    *result = entry->attr;
    return knd_OK;
}

static gsl_err_t run_set_translation_text(void *obj,
                                          const char *val, size_t val_size)
{
    struct kndTranslation *tr = obj;

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size >= sizeof tr->val) return make_gsl_err(gsl_LIMIT);

    memcpy(tr->val, val, val_size);
    tr->val[val_size] = '\0';
    tr->val_size = val_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_synt_role(void *obj,
                                 const char *name, size_t name_size,
                                 const char *rec, size_t *total_size)
{
    struct kndTranslation *self = obj;
    struct kndTranslation *tr;
    gsl_err_t err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing gloss synt role: \"%.*s\"", 16, rec);

    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return make_gsl_err_external(knd_NOMEM);
    memset(tr, 0, sizeof(struct kndTranslation));

    if (name_size != KND_SYNT_ROLE_NAME_SIZE) return make_gsl_err(gsl_FORMAT);

    switch (name[0]) {
    case 's':
        tr->synt_role = KND_SYNT_SUBJ;
        break;
    case 'o':
        tr->synt_role = KND_SYNT_OBJ;
        break;
    case 'g':
        tr->synt_role = KND_SYNT_GEN;
        break;
    case 'd':
        tr->synt_role = KND_SYNT_DAT;
        break;
    case 'i':
        tr->synt_role = KND_SYNT_INS;
        break;
    case 'l':
        tr->synt_role = KND_SYNT_LOC;
        break;
    default:
        return make_gsl_err(gsl_FORMAT);
    }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_translation_text,
          .obj = tr
        }
    };

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    /* assign translation */
    tr->next = self->synt_roles;
    self->synt_roles = tr;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log("== gloss: \"%.*s\" (synt role: %d)",
            tr->val_size, tr->val, tr->synt_role);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t atomic_elem_alloc(void *obj,
                                   const char *val,
                                   size_t val_size,
                                   size_t count  __attribute__((unused)),
                                   void **item __attribute__((unused)))
{
    struct kndConcept *self = obj;
    struct kndSet *class_idx, *set;
    struct kndConcDir *dir;
    void *elem;
    int err;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log("Conc %.*s: atomic elem alloc: \"%.*s\"",
                self->name_size, self->name,
                val_size, val);
    }
    class_idx = self->class_idx;

    err = class_idx->get(class_idx, val, val_size, &elem);
    if (err) {
        knd_log("-- IDX:%p couldn't resolve class id: \"%.*s\" [size:%zu] :(",
                class_idx, val_size, val, val_size);
        return make_gsl_err_external(knd_NO_MATCH);
    }
    dir = elem;

    set = self->dir->descendants;
    err = set->add(set, dir->id, dir->id_size, (void*)dir);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t atomic_elem_append(void *accu  __attribute__((unused)),
                                    void *item __attribute__((unused)))
{

    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_descendants(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndConcept *self = obj;
    struct kndSet *set;
    size_t total_elems = 0;

    struct gslTaskSpec c_item_spec = {
        .is_list_item = true,
        .alloc = atomic_elem_alloc,
        .append = atomic_elem_append,
        .accu = self
    };
    struct gslTaskSpec specs[] = {
        {  .name = "tot",
           .name_size = strlen("tot"),
           .parse = gsl_parse_size_t,
           .obj = &total_elems
        },
        { .is_list = true,
          .name = "c",
          .name_size = strlen("c"),
          .parse = gsl_parse_array,
          .obj = &c_item_spec
        }
    };
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CONC_LEVEL_1)
        knd_log(".. parsing a set of descendants: \"%.*s\"", 300, rec);

    err = self->mempool->new_set(self->mempool, &set);
    if (err) return make_gsl_err_external(err);
    set->type = KND_SET_CLASS;
    set->base = self->dir;
    self->dir->descendants = set;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (total_elems != set->num_elems) {
        knd_log("-- set total elems mismatch: %zu vs actual %zu",
                total_elems, set->num_elems);
        return make_gsl_err(gsl_FAIL);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss(void *obj,
                             const char *rec, size_t *total_size)
{
    struct kndTranslation *tr = obj;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log("..  parse gloss translation in \"%.*s\" OBJ:%p\n",
                16, rec, obj); }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_translation_text,
          .obj = tr
        },
        { .is_validator = true,
          .validate = parse_synt_role,
          .obj = tr
        }
    };


    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_summary(void *obj,
                               const char *rec,
                               size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndText *text;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing the summary change: \"%s\"", rec);

    text = self->summary;
    if (!text) {
        err = kndText_new(&text);
        if (err) return make_gsl_err_external(err);
        self->summary = text;
    }

    err = text->parse(text, rec, total_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_aggr(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndConcept *self = obj;
    struct kndAttr *attr;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing the AGGR attr: \"%.*s\"", 32, rec);

    err = kndAttr_new(&attr);
    if (err) return make_gsl_err_external(err);
    attr->parent_conc = self;
    attr->type = KND_ATTR_AGGR;

    err = attr->parse(attr, rec, total_size);
    if (err) {
        if (DEBUG_CONC_LEVEL_TMP)
            knd_log("-- failed to parse the AGGR attr: %d", err);
        return make_gsl_err_external(err);
    }

    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    self->num_attrs++;

    if (DEBUG_CONC_LEVEL_2)
        attr->str(attr);

    /* TODO: resolve attr if read from GSP */
    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_str(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndAttr *attr;
    int err;

    err = kndAttr_new(&attr);
    if (err) return make_gsl_err_external(err);
    attr->parent_conc = self;
    attr->type = KND_ATTR_STR;

    err = attr->parse(attr, rec, total_size);
    if (err) {
        knd_log("-- failed to parse the STR attr of \"%.*s\" :(",
                self->name_size, self->name);
        return make_gsl_err_external(err);
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_bin(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndAttr *attr;
    int err;

    err = kndAttr_new(&attr);
    if (err) return make_gsl_err_external(err);
    attr->parent_conc = self;
    attr->type = KND_ATTR_BIN;

    err = attr->parse(attr, rec, total_size);
    if (err) {
        knd_log("-- failed to parse the BIN attr: %d", err);
        return make_gsl_err_external(err);
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_num(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndAttr *attr;
    int err;

    err = kndAttr_new(&attr);
    if (err) return make_gsl_err_external(err);
    attr->parent_conc = self;
    attr->type = KND_ATTR_NUM;

    err = attr->parse(attr, rec, total_size);
    if (err) {
        knd_log("-- failed to parse the NUM attr: %d", err);
        return make_gsl_err_external(err);
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_ref(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndAttr *attr;
    int err;

    err = kndAttr_new(&attr);
    if (err) return make_gsl_err_external(err);
    attr->parent_conc = self;
    attr->type = KND_ATTR_REF;

    err = attr->parse(attr, rec, total_size);
    if (err) {
        knd_log("-- failed to parse the REF attr: %d", err);
        return make_gsl_err_external(err);
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_text(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndAttr *attr;
    int err;

    err = kndAttr_new(&attr);
    if (err) return make_gsl_err_external(err);
    attr->parent_conc = self;
    attr->type = KND_ATTR_TEXT;

    err = attr->parse(attr, rec, total_size);
    if (err) {
        knd_log("-- failed to parse the TEXT attr: %d", err);
        return make_gsl_err_external(err);
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_attr_item(void *obj,
                                  const char *name, size_t name_size,
                                  const char *rec, size_t *total_size)
{
    struct kndConcItem *self = obj;
    struct kndAttrItem *item;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== import attr item: \"%.*s\" REC: %.*s, %p",
                name_size, name, 16, rec, self->mempool);

    err = self->mempool->new_attr_item(self->mempool, &item);
    if (err) return make_gsl_err_external(err);

    memcpy(item->name, name, name_size);
    item->name_size = name_size;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = item->val,
          .buf_size = &item->val_size,
          .max_buf_size = KND_NAME_SIZE
        },
        { .type = GSL_CHANGE_STATE,
          .is_validator = true,
          .validate = read_nested_attr_item,
          .obj = item
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    append_attr_item(self, item);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_nested_attr_item(void *obj,
                                       const char *name, size_t name_size,
                                       const char *rec, size_t *total_size)
{
    struct kndAttrItem *self = obj;
    struct kndAttrItem *item;
    gsl_err_t parser_err;
    int err;

    err = self->mempool->new_attr_item(self->mempool, &item);
    if (err) return make_gsl_err_external(err);

    memcpy(item->name, name, name_size);
    item->name_size = name_size;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== reading attr item: \"%.*s\" REC: %.*s",
                item->name_size, item->name, 16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = item->val,
          .buf_size = &item->val_size,
          .max_buf_size = KND_NAME_SIZE
        },
        { .type = GSL_CHANGE_STATE,
          .is_validator = true,
          .validate = read_nested_attr_item,
          .obj = item
        },
        { .is_validator = true,
          .validate = read_nested_attr_item,
          .obj = item
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    item->next = self->children;
    self->children = item;
    self->num_children++;
    return make_gsl_err(gsl_OK);
}

static void append_attr_item(struct kndConcItem *ci,
                             struct kndAttrItem *attr_item)
{
    struct kndAttrItem *item;

    for (item = ci->attrs; item; item = item->next) {
        if (item->name_size != attr_item->name_size) continue;
        if (!memcmp(item->name, attr_item->name, attr_item->name_size)) {
            if (!item->list_tail) {
                item->list_tail = attr_item;
                item->list = attr_item;
            }
            else {
                item->list_tail->next = attr_item;
                item->list_tail = attr_item;
            }
            item->num_list_elems++;
            return;
        }
    }

    if (!ci->tail) {
        ci->tail = attr_item;
        ci->attrs = attr_item;
    }
    else {
        ci->tail->next = attr_item;
        ci->tail = attr_item;
    }
    ci->num_attrs++;
}


static gsl_err_t attr_item_alloc(void *obj,
                                 const char *name,
                                 size_t name_size,
                                 size_t count  __attribute__((unused)),
                                 void **result)
{
    struct kndAttrItem *self = obj;
    struct kndAttrItem *item;
    struct kndSet *class_idx;
    struct kndConcDir *dir;
    void *elem;
    int err;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log(".. alloc attr item conc id: \"%.*s\" attr:%p",
                name_size, name, self->attr);
    }

    class_idx = self->attr->parent_conc->class_idx;

    err = class_idx->get(class_idx, name, name_size, &elem);
    if (err) {
        knd_log("-- couldn't resolve class id: \"%.*s\" [size:%zu] :(",
                name_size, name, name_size);
        return make_gsl_err_external(knd_NO_MATCH);
    }

    dir = elem;
    
    err = self->mempool->new_attr_item(self->mempool, &item);
    if (err) return make_gsl_err_external(err);

    item->conc_dir = dir;
    item->attr = self->attr;

    *result = item;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t attr_item_append(void *accu,
                                  void *item)
{
    struct kndAttrItem *self = accu;
    struct kndAttrItem *attr_item = item;

    if (!self->list_tail) {
        self->list_tail = attr_item;
        self->list = attr_item;
    }
    else {
        self->list_tail->next = attr_item;
        self->list_tail = attr_item;
    }
    self->num_list_elems++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t aggr_item_set_baseclass(void *obj,
                                         const char *id, size_t id_size)
{
    struct kndAttrItem *item = obj;
    struct kndSet *class_idx;
    struct kndConcDir *dir;
    struct kndConcept *conc  = item->attr->parent_conc;
    void *result;
    int err;

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) {
        /* TODO */
        return make_gsl_err(gsl_OK);
        //return make_gsl_err(gsl_LIMIT);
    }

    memcpy(item->id, id, id_size);
    item->id_size = id_size;

    class_idx = conc->class_idx;

    err = class_idx->get(class_idx, id, id_size, &result);
    if (err) {
        /* TODO */
        return make_gsl_err(gsl_OK);
        //return make_gsl_err(gsl_FAIL);
    }
    dir = result;

    if (!dir->conc) {
        err = unfreeze_class(conc, dir, &dir->conc);
        if (err) return make_gsl_err_external(err);
    }

    item->conc = dir->conc;
    item->conc_dir = dir;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== AGGR item baseclass: %.*s (id:%.*s) CONC:%p",
                dir->name_size, dir->name, id_size, id, dir->conc);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t aggr_item_parse(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct kndAttrItem *item = obj;
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = aggr_item_set_baseclass,
          .obj = item
        },
        { .is_validator = true,
          .validate = read_nested_attr_item,
          .obj = item
        }
    };

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log(".. parsing the attr item: \"%.*s\"", 32, rec);

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t aggr_item_alloc(void *obj,
                                 const char *name,
                                 size_t name_size,
                                 size_t count  __attribute__((unused)),
                                 void **result)
{
    struct kndAttrItem *self = obj;
    struct kndAttrItem *item;
    struct kndSet *class_idx;
    struct kndConcDir *dir;
    void *elem;
    int err;

    if (DEBUG_CONC_LEVEL_1) {
        knd_log(".. alloc AGGR attr item..  conc id: \"%.*s\" attr:%p  parent:%p",
                name_size, name, self->attr,  self->attr->parent_conc);
    }
    
    err = self->mempool->new_attr_item(self->mempool, &item);
    if (err) return make_gsl_err_external(err);

    item->attr = self->attr;

    *result = item;
    return make_gsl_err(gsl_OK);
}


static gsl_err_t validate_attr_item(void *obj,
                                    const char *name, size_t name_size,
                                    const char *rec, size_t *total_size)
{
    struct kndConcItem *ci = obj;
    struct kndAttrItem *attr_item;
    struct kndAttr *attr;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. conc item \"%.*s\" to validate attr item: %.*s..",
                ci->conc->name_size, ci->conc->name,
                name_size, name);

    err = ci->mempool->new_attr_item(ci->mempool, &attr_item);
    if (err) return make_gsl_err_external(err);

    err = get_attr(ci->conc, name, name_size, &attr);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name,
                ci->conc->name_size, ci->conc->name);
        return make_gsl_err_external(err);
    }

    attr_item->attr = attr;
    memcpy(attr_item->name, name, name_size);
    attr_item->name_size = name_size;

    struct gslTaskSpec attr_item_spec = {
        .is_list_item = true,
        .accu = attr_item,
        .alloc = attr_item_alloc,
        .append = attr_item_append
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = attr_item->val,
          .buf_size = &attr_item->val_size,
          .max_buf_size = KND_NAME_SIZE
        },
        { .is_validator = true,
          .validate = read_nested_attr_item,
          .obj = attr_item
        },
        { .is_list = true,
          .name = "r",
          .name_size = strlen("r"),
          .parse = gsl_parse_array,
          .obj = &attr_item_spec
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    append_attr_item(ci, attr_item);

    if (DEBUG_CONC_LEVEL_2)
        knd_log("++ conc item \"%.*s\" confirms attr item: %.*s  type:%d!",
                ci->conc->name_size, ci->conc->name,
                name_size, name, attr_item->attr->type);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_baseclass(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct kndConcept *self = obj;
    struct kndConcItem *item;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing the base class: \"%.*s\"", 32, rec);

    err = self->mempool->new_conc_item(self->mempool, &item);
    if (err) {
        knd_log("-- conc item alloc failed :(");
        return make_gsl_err_external(err);
    }

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = item->classname,
          .buf_size = &item->classname_size,
          .max_buf_size = KND_NAME_SIZE
        },
        { .type = GSL_CHANGE_STATE,
          .is_validator = true,
          .validate = import_attr_item,
          .obj = item
        },
        { .is_validator = true,
          .validate = validate_attr_item,
          .obj = item
        }
    };

    item->next = self->base_items;
    self->base_items = item;
    self->num_base_items++;

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int assign_ids(struct kndConcept *self)
{
    struct kndConcDir *dir;
    const char *key;
    void *val;
    size_t count = 0;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. assign class ids..");

    /* TODO: get statistics on usage from retrievers? */
    key = NULL;
    self->class_name_idx->rewind(self->class_name_idx);
    do {
        self->class_name_idx->next_item(self->class_name_idx, &key, &val);
        if (!key) break;
        dir = (struct kndConcDir*)val;
        count++;

        dir->id_size = 0;
        knd_num_to_str(count, dir->id, &dir->id_size, KND_RADIX_BASE);

        if (DEBUG_CONC_LEVEL_2)
            knd_log("ID: %zu => \"%.*s\" [size: %zu]",
                    count, dir->id_size, dir->id, dir->id_size);

        /* TODO: assign obj ids */
    } while (key);

    return knd_OK;
}

static gsl_err_t run_sync_task(void *obj, const char *val __attribute__((unused)),
                               size_t val_size __attribute__((unused)))
{
    struct kndConcept *self = obj;
    int err;

    /* assign numeric ids as defined by a sorting function */
    err = assign_ids(self);
    if (err) return make_gsl_err_external(err);

    /* merge earlier frozen DB with liquid updates */
    err = freeze(self);
    if (err) {
        knd_log("-- freezing failed :(");
        return make_gsl_err_external(err);
    }

    return make_gsl_err(gsl_OK);
}
static gsl_err_t parse_sync_task(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;

    struct gslTaskSpec specs[] = {
        { .is_default = true,
          .run = run_sync_task,
          .obj = self
        }
    };

    if (DEBUG_CONC_LEVEL_1)
        knd_log(".. freezing DB to GSP files..");

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_import_class(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndConcept *self = obj;
    struct kndConcept *c;
    struct kndConcDir *dir;
    int err;
    gsl_err_t parser_err;
    // TODO(ki.stfu): Don't ignore this field
    char time[KND_NAME_SIZE];
    size_t time_size = 0;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. import \"%.*s\" class..", 128, rec);

    err  = self->mempool->new_class(self->mempool, &c);
    if (err) return make_gsl_err_external(err);
    c->out = self->out;
    c->log = self->log;
    c->task = self->task;
    c->mempool = self->mempool;
    c->class_idx = self->class_idx;
    c->class_name_idx = self->class_name_idx;
    c->root_class = self;
    
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = c
        },
        { .type = GSL_CHANGE_STATE,
          .name = "base",
          .name_size = strlen("base"),
          .parse = parse_baseclass,
          .obj = c
        },
        { .is_list = true,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = parse_gloss_array,
          .obj = c
        },
        { .is_list = true,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = parse_gloss_array,
          .obj = c
        },
        { .name = "_summary",
          .name_size = strlen("_summary"),
          .parse = parse_summary,
          .obj = c
        },
        { .type = GSL_CHANGE_STATE,
          .name = "_summary",
          .name_size = strlen("_summary"),
          .parse = parse_summary,
          .obj = c
        },
        { .type = GSL_CHANGE_STATE,
          .name = "gloss",
          .name_size = strlen("gloss"),
          .parse = parse_gloss,
          .obj = c
        },
        { .type = GSL_CHANGE_STATE,
          .name = "aggr",
          .name_size = strlen("aggr"),
          .parse = parse_aggr,
          .obj = c
        },
        { .type = GSL_CHANGE_STATE,
          .name = "str",
          .name_size = strlen("str"),
          .parse = parse_str,
          .obj = c
        },
        { .type = GSL_CHANGE_STATE,
          .name = "bin",
          .name_size = strlen("bin"),
          .parse = parse_bin,
          .obj = c
        },
        { .type = GSL_CHANGE_STATE,
          .name = "num",
          .name_size = strlen("num"),
          .parse = parse_num,
          .obj = c
        },
        // FIXME(ki.stfu): Temporary spec to ignore the time tag
        { .type = GSL_CHANGE_STATE,
          .name = "time",
          .name_size = strlen("time"),
          .buf = time,
          .buf_size = &time_size,
          .max_buf_size = KND_NAME_SIZE
        },
        {  .type = GSL_CHANGE_STATE,
           .name = "ref",
          .name_size = strlen("ref"),
          .parse = parse_ref,
          .obj = c
        },
        { .type = GSL_CHANGE_STATE,
          .name = "text",
          .name_size = strlen("text"),
          .parse = parse_text,
          .obj = c
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) goto final;

    if (!c->name_size) {
        parser_err = make_gsl_err(gsl_FAIL);
        goto final;
    }

    dir = self->class_name_idx->get(self->class_name_idx,
                               c->name, c->name_size);
    if (dir) {
        knd_log("-- %s class name doublet found :(", c->name);
        self->log->reset(self->log);
        err = self->log->write(self->log,
                               c->name,
                               c->name_size);
        if (err) {
            parser_err = make_gsl_err_external(err);
            goto final;
        }
        
        err = self->log->write(self->log,
                               " class name already exists",
                               strlen(" class name already exists"));
        if (err) {
            parser_err = make_gsl_err_external(err);
            goto final;
        }
        
        parser_err = make_gsl_err(gsl_FAIL);
        goto final;
    }

    if (!self->batch_mode) {
        c->next = self->inbox;
        self->inbox = c;
        self->inbox_size++;
    }

    err = self->mempool->new_conc_dir(self->mempool, &dir);
    if (err) { parser_err = make_gsl_err_external(err); goto final; }

    memcpy(dir->name, c->name, c->name_size);
    dir->name_size = c->name_size;


    dir->conc = c;
    c->dir = dir;
    dir->mempool = self->mempool;
    dir->class_idx = self->class_idx;
    dir->class_name_idx = self->class_name_idx;

    err = self->class_name_idx->set(self->class_name_idx,
                                    c->name, c->name_size, (void*)dir);
    if (err) { parser_err = make_gsl_err_external(err); goto final; }

    if (DEBUG_CONC_LEVEL_1)
        c->str(c);
     
    return make_gsl_err(gsl_OK);
 final:
    
    c->del(c);
    return parser_err;
}

static gsl_err_t parse_import_obj(void *data,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndConcept *self = data;
    struct kndConcept *c;
    struct kndObject *obj;
    struct kndObjEntry *entry;
    int err;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log(".. import \"%.*s\" obj.. conc: %p", 128, rec, self->curr_class);
    }

    if (!self->curr_class) {
        knd_log("-- class not set :(");
        return make_gsl_err(gsl_FAIL);
    }

    err = self->mempool->new_obj(self->mempool, &obj);
    if (err) {
        return make_gsl_err_external(err);
    }
    err = self->mempool->new_state(self->mempool, &obj->state);
    if (err) {
        return make_gsl_err_external(err);
    }

    obj->state->phase = KND_SUBMITTED;
    obj->conc = self->curr_class;
    obj->out = self->out;
    obj->log = self->log;
    obj->task = self->task;
    obj->mempool = self->mempool;

    err = obj->parse(obj, rec, total_size);
    if (err) return make_gsl_err_external(err);

    c = obj->conc;
    obj->next = c->obj_inbox;
    c->obj_inbox = obj;
    c->obj_inbox_size++;
    c->num_objs++;

    if (DEBUG_CONC_LEVEL_1)
        knd_log("++ %.*s obj parse OK! total objs in %.*s: %zu",
                obj->name_size, obj->name,
                c->name_size, c->name, c->obj_inbox_size);

    obj->numid = c->num_objs;
    knd_num_to_str(obj->numid, obj->id, &obj->id_size, KND_RADIX_BASE);
    if (DEBUG_CONC_LEVEL_2)
        knd_log("== obj ID: %zu => \"%.*s\"",
                obj->numid, obj->id_size, obj->id);
    
    if (!c->dir) {
        if (c->root_class) {
            knd_log("-- no dir in %.*s :(", c->name_size, c->name);
            return make_gsl_err(gsl_FAIL);
        }
        return make_gsl_err(gsl_OK);
    }

    /* automatic name assignment if no explicit name given */
    if (!obj->name_size) {
        knd_num_to_str(obj->numid, obj->id, &obj->id_size, KND_RADIX_BASE);

        obj->name = obj->id;
        obj->name_size = obj->id_size;
    }

    if (!c->dir->obj_name_idx) {
        err = ooDict_new(&c->dir->obj_name_idx, KND_HUGE_DICT_SIZE);
        if (err) return make_gsl_err_external(err);
    }

    err = self->mempool->new_obj_entry(self->mempool, &entry);
    if (err) return make_gsl_err_external(err);

    memcpy(entry->id, obj->id, obj->id_size);
    entry->id_size = obj->id_size;

    entry->obj = obj;
    obj->entry = entry;

    err = c->dir->obj_name_idx->set(c->dir->obj_name_idx,
                               obj->name, obj->name_size,
                               (void*)entry);
    if (err) return make_gsl_err_external(err);

    c->dir->num_objs++;

    if (DEBUG_CONC_LEVEL_1) {
        knd_log("++ OBJ registered in \"%.*s\" IDX:  [total:%zu valid:%zu]",
                c->name_size, c->name, c->dir->obj_name_idx->size, c->dir->num_objs);
        obj->depth = self->depth + 1;
        obj->str(obj);
    }

    self->task->type = KND_UPDATE_STATE;
  
    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_select_obj(void *data,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndConcept *self = data;
    struct kndObject *obj = self->curr_obj;
    int err;

    if (!self->curr_class) {
        knd_log("-- base class not set :(");
        /* TODO: log*/
        return make_gsl_err(gsl_FAIL);
    }

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. select \"%.*s\" obj.. task type: %d", 16, rec,
                self->curr_class->task->type);

    self->curr_class->task->type = KND_GET_STATE;
    obj->conc = self->curr_class;
    obj->task = self->task;
    obj->out = self->out;
    obj->log = self->log;
    obj->mempool = self->mempool;

    err = obj->select(obj, rec, total_size);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_CONC_LEVEL_2) {
        if (obj->curr_obj) 
            obj->curr_obj->str(obj->curr_obj);
    }
    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t select_by_baseclass(void *obj,
                                     const char *name, size_t name_size)
{
    struct kndConcept *self = obj;
    struct kndSet *set;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    err = get_class(self, name, name_size, &self->curr_baseclass);
    if (err) return make_gsl_err_external(err);

    self->curr_baseclass->dir->conc = self->curr_baseclass;

    if (!self->curr_baseclass->dir->descendants) {
        knd_log("-- no set of descendants found :(");
        return make_gsl_err(gsl_OK);
    }

    set = self->curr_baseclass->dir->descendants;
    
    set->next = self->task->sets;
    self->task->sets = set;
    self->task->num_sets++;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t select_by_attr(void *obj,
                                const char *name, size_t name_size)
{
    struct kndConcept *self = obj;
    struct kndConcept *c;
    struct kndSet *set;
    struct kndFacet *facet;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== attr val: \"%.*s\"", name_size, name);

    if (!self->curr_baseclass->dir->descendants)
        return make_gsl_err(gsl_FAIL);

    set = self->curr_baseclass->dir->descendants;

    err = set->get_facet(set, self->curr_attr, &facet);
    if (err) return make_gsl_err_external(err);
    
    set = facet->set_name_idx->get(facet->set_name_idx,
                                   name, name_size);
    if (!set) return make_gsl_err(gsl_FAIL); 

    err = get_class(self, name, name_size, &c);
    if (err) return make_gsl_err_external(err);
    set->base->conc = c;

    if (DEBUG_CONC_LEVEL_2)
        set->str(set, 1);

    set->next = self->task->sets;
    self->task->sets = set;
    self->task->num_sets++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_attr_select(void *obj,
                                   const char *name, size_t name_size,
                                   const char *rec, size_t *total_size)
{
    struct kndConcept *self = obj;
    struct kndAttr *attr;
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = select_by_attr,
          .obj = self
        }
    };
    int err;

    if (!self->curr_baseclass) return make_gsl_err_external(knd_FAIL);

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. select by attr \"%.*s\"..", name_size, name);

    err = get_attr(self->curr_baseclass, name, name_size, &attr);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name,
                self->curr_baseclass->name_size,
                self->curr_baseclass->name);
    }

    self->curr_attr = attr;

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_baseclass_select(void *obj,
                                        const char *rec,
                                        size_t *total_size)
{
    struct kndConcept *self = obj;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. select by baseclass \"%.*s\"..", 16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = select_by_baseclass,
          .obj = self
        },
        { .is_validator = true,
          .validate = parse_attr_select,
          .obj = self
        }
    };

    self->task->type = KND_SELECT_STATE;

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}


static gsl_err_t run_get_schema(void *obj, const char *name, size_t name_size)
{
    struct kndConcept *self = obj;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    /* TODO: get current schema */
    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. select schema %.*s from: \"%.*s\"..",
                name_size, name, self->name_size, self->name);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_set_name(void *obj, const char *name, size_t name_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. set conc name: %.*s", name_size, name);

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= sizeof self->name) return make_gsl_err(gsl_LIMIT);

    memcpy(self->name, name, name_size);
    self->name_size = name_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_rel_import(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndConcept *self = obj;
    int err;

    err = self->rel->import(self->rel, rec, total_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_proc_import(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndConcept *self = obj;
    int err;

    err = self->proc->import(self->proc, rec, total_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_read_include(void *obj, const char *name, size_t name_size)
{
    struct kndConcept *self = obj;
    struct kndConcFolder *folder;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. running include file func.. name: \"%.*s\" [%zu]",
                (int)name_size, name, name_size);

    if (!name_size) return make_gsl_err(gsl_FORMAT);

    folder = malloc(sizeof(struct kndConcFolder));
    if (!folder) return make_gsl_err_external(knd_NOMEM);
    memset(folder, 0, sizeof(struct kndConcFolder));

    memcpy(folder->name, name, name_size);
    folder->name_size = name_size;
    folder->name[name_size] = '\0';

    folder->next = self->folders;
    self->folders = folder;
    self->num_folders++;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_schema(void *self,
                              const char *rec,
                              size_t *total_size)
{
    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parse schema REC: \"%.*s\"..", 32, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_get_schema,
          .obj = self
        },
        { .type = GSL_CHANGE_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = parse_import_class,
          .obj = self
        },
        { .type = GSL_CHANGE_STATE,
          .name = "rel",
          .name_size = strlen("rel"),
          .parse = parse_rel_import,
          .obj = self
        },
        { .type = GSL_CHANGE_STATE,
          .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_import,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_include(void *self,
                               const char *rec,
                               size_t *total_size)
{
    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parse include REC: \"%s\"..", rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_read_include,
          .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int parse_GSL(struct kndConcept *self,
                     const char *rec,
                     size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        { .name = "schema",
          .name_size = strlen("schema"),
          .parse = parse_schema,
          .obj = self
        },
        { .name = "include",
          .name_size = strlen("include"),
          .parse = parse_include,
          .obj = self
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}

static int knd_get_dir_size(struct kndConcept *self,
                            size_t *dir_size,
                            size_t *chunk_size,
                            unsigned int encode_base)
{
    char buf[KND_DIR_ENTRY_SIZE + 1] = {0};
    size_t buf_size = 0;
    const char *rec = self->out->buf;
    size_t rec_size = self->out->buf_size;
    char *invalid_num_char = NULL;

    bool in_field = false;
    bool got_separ = false;
    bool got_tag = false;
    bool got_size = false;
    long numval;
    const char *c, *s = NULL;
    int i = 0;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. get size of DIR in %.*s", self->name_size, self->name);

    for (i = rec_size - 1; i >= 0; i--) { 
        c = rec + i;
        switch (*c) {
        case '\n':
        case '\r':
            break;
        case '}':
            if (in_field) return knd_FAIL;
            in_field = true;
            break;
        case '{':
            if (!in_field) return knd_FAIL;
            if (got_tag) got_size = true;
            break;
        case ' ':
            got_separ = true;
            break;
        case 'L':
            got_tag = true;
            break;
        default:
            if (!in_field) return knd_FAIL;
            if (got_tag) return knd_FAIL;
            if (!isalnum(*c)) return knd_FAIL;

            buf[i] = *c;
            buf_size++;
            s = buf + i;
            break;
        }
        if (got_size) {
            if (DEBUG_CONC_LEVEL_2)
                knd_log("  ++ got size value to parse: %.*s!", buf_size, s);
            break;
        }
    }

    if (!got_size) return knd_FAIL;

    numval = strtol(s, &invalid_num_char, encode_base);
    if (*invalid_num_char) {
        knd_log("-- invalid char: %.*s", 1, invalid_num_char);
        return knd_FAIL;
    }
    
    /* check for various numeric decoding errors */
    if ((errno == ERANGE && (numval == LONG_MAX || numval == LONG_MIN)) ||
            (errno != 0 && numval == 0)) {
        return knd_FAIL;
    }

    if (numval <= 0) return knd_FAIL;
    if (numval >= KND_DIR_TRAILER_MAX_SIZE) return knd_LIMIT;
    if (DEBUG_CONC_LEVEL_3)
        knd_log("  == DIR size: %lu    CHUNK SIZE: %lu",
                (unsigned long)numval, (unsigned long)rec_size - i);

    *dir_size = numval;
    *chunk_size = rec_size - i;

    return knd_OK;
}

static gsl_err_t run_set_dir_size(void *obj, const char *val, size_t val_size)
{
    char buf[KND_SHORT_NAME_SIZE] = {0};
    struct kndConcDir *self = obj;
    char *invalid_num_char = NULL;
    long numval;

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    if (val_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    /* null terminated string is needed for strtol */
    memcpy(buf, val, val_size);

    numval = strtol(buf, &invalid_num_char, KND_NUM_ENCODE_BASE);
    if (*invalid_num_char) {
        knd_log("-- invalid char: %.*s in \"%.*s\"",
                1, invalid_num_char, val_size, val);
        return make_gsl_err(gsl_FORMAT);
    }
    
    /* check for various numeric decoding errors */
    if ((errno == ERANGE && (numval == LONG_MAX || numval == LONG_MIN)) ||
            (errno != 0 && numval == 0))
    {
        return make_gsl_err(gsl_LIMIT);
    }

    if (numval <= 0) return make_gsl_err(gsl_LIMIT);
    if (DEBUG_CONC_LEVEL_2)
        knd_log("== DIR size: %lu", (unsigned long)numval);

    self->block_size = numval;
    
    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_parent_dir_size(void *obj,
                                       const char *rec,
                                       size_t *total_size)
{
    struct kndConcDir *self = obj;
    gsl_err_t err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing parent dir size: \"%.*s\"", 16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_dir_size,
          .obj = self
        }
    };

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    self->curr_offset += self->block_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_obj_dir_size(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndConcDir *self = obj;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing obj dir size: \"%.*s\"", 16, rec);

    struct gslTaskSpec specs[] = {
        {  .name = "size",
           .name_size = strlen("size"),
           .parse = gsl_parse_size_t,
           .obj = &self->obj_block_size
        },
        {  .name = "tot",
           .name_size = strlen("tot"),
           .parse = gsl_parse_size_t,
           .obj = &self->num_objs
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t dir_entry_append(void *accu,
                                  void *item)
{
    struct kndConcDir *parent_dir = accu;
    struct kndConcDir *dir = item;
    int err;

    if (!parent_dir->child_idx) {
        err = parent_dir->mempool->new_set(parent_dir->mempool,
                                           &parent_dir->child_idx);
        if (err) return make_gsl_err_external(err);
    }

    if (!parent_dir->children) {
        parent_dir->children = calloc(KND_MAX_CONC_CHILDREN,
                                      sizeof(struct kndConcDir*));
        if (!parent_dir->children) {
            knd_log("-- no memory :(");
            return make_gsl_err_external(knd_NOMEM);
        }
    }

    if (parent_dir->num_children >= KND_MAX_CONC_CHILDREN) {
        knd_log("-- warning: num of subclasses of \"%.*s\" exceeded :(",
                parent_dir->name_size, parent_dir->name);
        return make_gsl_err(gsl_OK);
    }

    parent_dir->children[parent_dir->num_children] = dir;
    parent_dir->num_children++;

    dir->global_offset += parent_dir->curr_offset;
    parent_dir->curr_offset += dir->block_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t dir_entry_alloc(void *self,
                                 const char *name,
                                 size_t name_size,
                                 size_t count,
                                 void **item)
{
    struct kndConcDir *parent_dir = self;
    struct kndConcDir *dir;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. %.*s to add list item: %.*s count: %zu"
                " [total children: %zu]",
                parent_dir->id_size, parent_dir->id, name_size, name,
                count, parent_dir->num_children);

    if (name_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    err = parent_dir->mempool->new_conc_dir(parent_dir->mempool, &dir);
    if (err) return make_gsl_err_external(err);

    dir->mempool = parent_dir->mempool;
    dir->class_idx = parent_dir->class_idx;
    dir->class_name_idx = parent_dir->class_name_idx;
    knd_calc_num_id(name, name_size, &dir->block_size);

    *item = dir;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t reldir_entry_alloc(void *self,
                                    const char *name,
                                    size_t name_size,
                                    size_t count,
                                    void **item)
{
    struct kndConcDir *parent_dir = self;
    struct kndRelDir *dir;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. create REL DIR ENTRY: %.*s count: %zu",
                name_size, name, count);

    if (name_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    err = parent_dir->mempool->new_rel_dir(parent_dir->mempool, &dir);
    if (err) return make_gsl_err_external(err);
    knd_calc_num_id(name, name_size, &dir->block_size);
    
    *item = dir;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t reldir_entry_append(void *accu,
                                     void *item)
{
    struct kndConcDir *parent_dir = accu;
    struct kndRelDir *dir = item;

    if (!parent_dir->rels) {
        parent_dir->rels = calloc(KND_MAX_RELS,
                                  sizeof(struct kndRelDir*));
        if (!parent_dir->rels) return make_gsl_err_external(knd_NOMEM);
    }

    if (parent_dir->num_rels + 1 > KND_MAX_RELS) {
        knd_log("-- warning: max rels of \"%.*s\" exceeded :(",
                parent_dir->name_size, parent_dir->name);
        return make_gsl_err(gsl_OK);
    }

    parent_dir->rels[parent_dir->num_rels] = dir;
    parent_dir->num_rels++;

    dir->global_offset += parent_dir->curr_offset;
    parent_dir->curr_offset += dir->block_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t procdir_entry_alloc(void *self,
                                     const char *name,
                                     size_t name_size,
                                     size_t count,
                                     void **item)
{
    struct kndConcDir *parent_dir = self;
    struct kndProcDir *dir;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. create PROC DIR ENTRY: %.*s count: %zu",
                name_size, name, count);

    if (name_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    err = parent_dir->mempool->new_proc_dir(parent_dir->mempool, &dir);
    if (err) return make_gsl_err_external(err);

    knd_calc_num_id(name, name_size, &dir->block_size);

    *item = dir;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t procdir_entry_append(void *accu,
                                      void *item)
{
    struct kndConcDir *parent_dir = accu;
    struct kndProcDir *dir = item;

    if (!parent_dir->procs) {
        parent_dir->procs = calloc(KND_MAX_PROCS,
                                   sizeof(struct kndProcDir*));
        if (!parent_dir->procs) return make_gsl_err_external(knd_NOMEM);
    }

    if (parent_dir->num_procs + 1 > KND_MAX_PROCS) {
        knd_log("-- warning: max procs of \"%.*s\" exceeded :(",
                parent_dir->name_size, parent_dir->name);
        return make_gsl_err(gsl_OK);
    }

    parent_dir->procs[parent_dir->num_procs] = dir;
    parent_dir->num_procs++;

    dir->global_offset += parent_dir->curr_offset;
    parent_dir->curr_offset += dir->block_size;

    return make_gsl_err(gsl_OK);
}

static int idx_class_name(struct kndConcept *self,
                          struct kndConcDir *dir,
                          int fd)
{
    char buf[KND_NAME_SIZE + 1];
    size_t buf_size;
    off_t offset = 0;
    gsl_err_t parser_err;
    void *result;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("  .. get conc name in DIR: \"%.*s\""
                " global off:%zu  block size:%zu",
                dir->name_size, dir->name,
                dir->global_offset, dir->block_size);

    buf_size = dir->block_size;
    if (dir->block_size > KND_NAME_SIZE)
        buf_size = KND_NAME_SIZE;

    offset = dir->global_offset;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return knd_IO_FAIL;
    }
    err = read(fd, buf, buf_size);
    if (err == -1) return knd_IO_FAIL;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("\n  .. CONC BODY: %.*s",
                buf_size, buf);
    buf[buf_size] = '\0';

    dir->id_size = KND_ID_SIZE;
    dir->name_size = KND_NAME_SIZE;
    parser_err = gsl_parse_incipit(buf, buf_size,
                                   dir->id, &dir->id_size,
                                   dir->name, &dir->name_size);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    knd_calc_num_id(dir->id, dir->id_size, &dir->numid);

    err = self->class_idx->add(self->class_idx,
                               dir->id, dir->id_size, (void*)dir);                RET_ERR();

    err = self->class_name_idx->set(self->class_name_idx,
                                    dir->name, dir->name_size, dir);              RET_ERR();


    err = self->class_idx->get(self->class_idx,
                               dir->id, dir->id_size, &result);                   RET_ERR();
    dir = result;

    return knd_OK;
}

static int get_dir_trailer(struct kndConcept *self,
                           struct kndConcDir *parent_dir,
                           int fd,
                           int encode_base)
{
    size_t block_size = parent_dir->block_size;
    struct glbOutput *out = self->out;
    off_t offset = 0;
    size_t dir_size = 0;
    size_t chunk_size = 0;
    int err;

    if (block_size <= KND_DIR_ENTRY_SIZE)
        return knd_NO_MATCH;

    offset = (parent_dir->global_offset + block_size) - KND_DIR_ENTRY_SIZE;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return knd_IO_FAIL;
    }
    out->reset(out);
    out->buf_size = KND_DIR_ENTRY_SIZE;
    err = read(fd, out->buf, out->buf_size);
    if (err == -1) return knd_IO_FAIL;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("  .. DIR size field read: \"%.*s\" [%zu]",
                out->buf_size, out->buf, out->buf_size);

    err =  knd_get_dir_size(self, &dir_size, &chunk_size, encode_base);
    if (err) {
        if (DEBUG_CONC_LEVEL_1)
            knd_log("-- couldn't find the ConcDir size field in \"%.*s\" :(",
                    out->buf_size, out->buf);
        return knd_NO_MATCH;
    }

    parent_dir->body_size = block_size - dir_size - chunk_size;
    parent_dir->dir_size = dir_size;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("  .. DIR: offset: %lu  block: %lu  body: %lu  dir: %lu",
                (unsigned long)parent_dir->global_offset,
                (unsigned long)parent_dir->block_size,
                (unsigned long)parent_dir->body_size,
                (unsigned long)parent_dir->dir_size);

    offset = (parent_dir->global_offset + block_size) - chunk_size - dir_size;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return knd_IO_FAIL;
    }

    if (dir_size >= out->capacity) return knd_LIMIT;

    out->reset(out);
    out->buf_size = dir_size;
    err = read(fd, out->buf, out->buf_size);
    if (err == -1) return knd_IO_FAIL;
    out->buf[out->buf_size] = '\0';

    if (DEBUG_CONC_LEVEL_1) {
        chunk_size = out->buf_size > KND_MAX_DEBUG_CHUNK_SIZE ?\
            KND_MAX_DEBUG_CHUNK_SIZE :  out->buf_size;
        knd_log(".. parsing DIR: \"%.*s\" [size:%zu]",
                chunk_size, out->buf, out->buf_size);
    }

    err = parse_dir_trailer(self, parent_dir, fd, encode_base);
    if (err) {
        chunk_size =  out->buf_size > KND_MAX_DEBUG_CHUNK_SIZE ?\
                KND_MAX_DEBUG_CHUNK_SIZE :  out->buf_size;

        knd_log("-- failed to parse dir trailer: \"%.*s\" [size:%zu]",
                chunk_size, out->buf, out->buf_size);
        return err;
    }

    return knd_OK;
}


static int parse_dir_trailer(struct kndConcept *self,
                             struct kndConcDir *parent_dir,
                             int fd,
                             int encode_base)
{
    char *dir_buf = self->out->buf;
    size_t dir_buf_size = self->out->buf_size;
    struct kndConcDir *dir;
    struct kndRelDir *reldir;
    struct kndProcDir *procdir;
    size_t parsed_size = 0;
    int err;
    gsl_err_t parser_err;

    struct gslTaskSpec class_dir_spec = {
        .is_list_item = true,
        .accu = parent_dir,
        .alloc = dir_entry_alloc,
        .append = dir_entry_append,
    };

    struct gslTaskSpec rel_dir_spec = {
        .is_list_item = true,
        .accu = parent_dir,
        .alloc = reldir_entry_alloc,
        .append = reldir_entry_append,
    };

    struct gslTaskSpec proc_dir_spec = {
        .is_list_item = true,
        .accu = parent_dir,
        .alloc = procdir_entry_alloc,
        .append = procdir_entry_append
    };
    
    struct gslTaskSpec specs[] = {
        { .name = "C",
          .name_size = strlen("C"),
          .parse = parse_parent_dir_size,
          .obj = parent_dir
        },
        { .name = "O",
          .name_size = strlen("O"),
          .parse = parse_obj_dir_size,
          .obj = parent_dir
        },
        { .is_list = true,
          .name = "c",
          .name_size = strlen("c"),
          .parse = gsl_parse_array,
          .obj = &class_dir_spec
        },
        { .is_list = true,
          .name = "R",
          .name_size = strlen("R"),
          .parse = gsl_parse_array,
          .obj = &rel_dir_spec
        },
        { .is_list = true,
          .name = "P",
          .name_size = strlen("P"),
          .parse = gsl_parse_array,
          .obj = &proc_dir_spec
        }
    };

    parent_dir->curr_offset = parent_dir->global_offset;
    if (DEBUG_CONC_LEVEL_2)
        knd_log("  .. parsing \"%.*s\" DIR REC: \"%.*s\"  curr offset: %zu   [dir size:%zu]",
                KND_ID_SIZE, parent_dir->id, dir_buf_size, dir_buf,
                parent_dir->curr_offset, dir_buf_size);

    parser_err = gsl_parse_task(dir_buf, &parsed_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    /* get conc name */
    if (parent_dir->block_size) {
        if (!parent_dir->is_indexed) {
            err = idx_class_name(self, parent_dir, fd);
            if (err) return err;
            parent_dir->is_indexed = true;
        }
    }

    /* try reading the objs */
    if (parent_dir->obj_block_size) {
        err = get_obj_dir_trailer(self, parent_dir, fd, encode_base);
        if (err) {
            knd_log("-- no obj dir trailer loaded :(");
            return err;
        }
    }

    if (DEBUG_CONC_LEVEL_2)
        knd_log("DIR: %.*s   num_children: %zu obj block:%zu",
                parent_dir->id_size, parent_dir->id, parent_dir->num_children,
                parent_dir->obj_block_size);

    /* try reading each dir */
    for (size_t i = 0; i < parent_dir->num_children; i++) {
        dir = parent_dir->children[i];
        if (!dir) continue;
        dir->mempool = parent_dir->mempool;
        dir->class_idx = parent_dir->class_idx;
        dir->class_name_idx = parent_dir->class_name_idx;

        err = idx_class_name(self, dir, fd);
        if (err) return err;

        if (DEBUG_CONC_LEVEL_2)
            knd_log(".. read DIR  ID:%.*s NAME:%.*s"
                    " block size: %zu  num terminals:%zu",
                    dir->id_size, dir->id,
                    dir->name_size, dir->name,
                    dir->block_size, dir->num_terminals);

        err = get_dir_trailer(self, dir, fd, encode_base);
        if (err) {
            if (err != knd_NO_MATCH) {
                knd_log("-- error reading trailer of \"%.*s\" DIR: %d",
                        dir->name_size, dir->name, err);
                return err;
            } else {
                if (DEBUG_CONC_LEVEL_2)
                    knd_log(".. terminal class:%.*s", dir->name_size, dir->name);
                parent_dir->num_terminals++;
            }
        } else {
            parent_dir->num_terminals += dir->num_terminals;

            if (DEBUG_CONC_LEVEL_2)
                knd_log(".. class:%.*s num_terminals:%zu",
                        parent_dir->name_size, parent_dir->name, parent_dir->num_terminals);
            
        }
    }

    /* read rels */
    for (size_t i = 0; i < parent_dir->num_rels; i++) {
        reldir = parent_dir->rels[i];
        reldir->mempool = self->mempool;
        self->rel->fd = fd;
        err = self->rel->read_rel(self->rel, reldir);
    }

    /* read procs */
    for (size_t i = 0; i < parent_dir->num_procs; i++) {
        procdir = parent_dir->procs[i];
        procdir->mempool = self->mempool;
        self->proc->fd = fd;
        err = self->proc->read_proc(self->proc, procdir);
    }

    return knd_OK;
}

static gsl_err_t obj_entry_alloc(void *obj,
                                 const char *val,
                                 size_t val_size,
                                 size_t count,
                                 void **item)
{
    struct kndConcDir *parent_dir = obj;
    struct kndObjEntry *entry = NULL;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. create OBJ ENTRY: %.*s  count: %zu",
                val_size, val, count);

    err = parent_dir->mempool->new_obj_entry(parent_dir->mempool, &entry);
    if (err) return make_gsl_err_external(err);

    knd_calc_num_id(val, val_size, &entry->block_size);

    *item = entry;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t obj_entry_append(void *accu,
                                  void *item)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndConcDir *parent_dir = accu;
    struct kndObjEntry *entry = item;
    struct kndSet *set;
    off_t offset = 0;
    int fd = parent_dir->fd;
    gsl_err_t parser_err;
    int err;

    entry->offset = parent_dir->curr_offset;

    if (DEBUG_CONC_LEVEL_1)
        knd_log("\n.. ConcDir: %.*s to append atomic obj entry"
                " (block size: %zu) offset:%zu",
                parent_dir->name_size, parent_dir->name,
                entry->block_size, entry->offset);

    buf_size = entry->block_size;
    if (entry->block_size > KND_NAME_SIZE)
        buf_size = KND_NAME_SIZE;

    offset = entry->offset;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return make_gsl_err_external(knd_IO_FAIL);
    }

    err = read(fd, buf, buf_size);
    if (err == -1) return make_gsl_err_external(knd_IO_FAIL);

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. OBJ BODY INCIPIT: \"%.*s\"",
                buf_size, buf);

    entry->id_size = KND_ID_SIZE;
    entry->name_size = KND_NAME_SIZE;
    parser_err = gsl_parse_incipit(buf, buf_size,
                                   entry->id, &entry->id_size,
                                   entry->name, &entry->name_size);
    if (parser_err.code) return parser_err;

    parent_dir->curr_offset += entry->block_size;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== OBJ id:%.*s name:%.*s",
                entry->id_size, entry->id,
                entry->name_size, entry->name);

    set = parent_dir->obj_idx;
    err = set->add(set, entry->id, entry->id_size, (void*)entry);
    if (err) {
        knd_log("-- failed to update the obj idx :(");
        return make_gsl_err_external(err);
    }
    /* update name idx */
    err = parent_dir->obj_name_idx->set(parent_dir->obj_name_idx,
                                        entry->name, entry->name_size,
                                        entry);
    if (err) {
        knd_log("-- failed to update the obj name idx entry :(");
        return make_gsl_err_external(err);
    }

    return make_gsl_err(gsl_OK);
}

static int parse_obj_dir_trailer(struct kndConcept *self,
                                 struct kndConcDir *parent_dir,
                                 int fd)
{
    struct gslTaskSpec obj_dir_spec = {
        .is_list_item = true,
        .accu = parent_dir,
        .alloc = obj_entry_alloc,
        .append = obj_entry_append
    };

    struct gslTaskSpec specs[] = {
        { .is_list = true,
          .name = "o",
          .name_size = strlen("o"),
          .parse = gsl_parse_array,
          .obj = &obj_dir_spec
        }
    };
    size_t parsed_size = 0;
    size_t *total_size = &parsed_size;
    char *obj_dir_buf = self->out->buf;
    size_t obj_dir_buf_size = self->out->buf_size;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CONC_LEVEL_1)
        knd_log(".. parsing OBJ DIR REC: %.*s [size %zu]",
                128, obj_dir_buf, obj_dir_buf_size);

    if (!parent_dir->obj_dir) {
        err = self->mempool->new_obj_dir(self->mempool, &parent_dir->obj_dir);
        if (err) return err;
    }
    parent_dir->fd = fd;

    if (!parent_dir->obj_name_idx) {
        err = ooDict_new(&parent_dir->obj_name_idx, parent_dir->num_objs);            RET_ERR();

        err = self->mempool->new_set(self->mempool, &parent_dir->obj_idx);            RET_ERR();
        parent_dir->obj_idx->type = KND_SET_CLASS;
    }

    parser_err = gsl_parse_task(obj_dir_buf, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== \"%.*s\" total objs: %zu",
                parent_dir->name_size, parent_dir->name,
                parent_dir->num_objs);

    return knd_OK;
}

static int get_obj_dir_trailer(struct kndConcept *self,
                                struct kndConcDir *parent_dir,
                                int fd,
                                int encode_base)
{
    off_t offset = 0;
    size_t dir_size = 0;
    size_t chunk_size = 0;
    size_t block_size = parent_dir->block_size;
    struct glbOutput *out = self->out;
    int err;

    offset = parent_dir->global_offset + block_size +\
        parent_dir->obj_block_size - KND_DIR_ENTRY_SIZE;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return knd_IO_FAIL;
    }
    out->reset(out);
    out->buf_size = KND_DIR_ENTRY_SIZE;
    err = read(fd, out->buf, out->buf_size);
    if (err == -1) return knd_IO_FAIL;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log("\n  .. OBJ DIR ENTRY SIZE REC: \"%.*s\"",
                out->buf_size, out->buf);
    }

    err =  knd_get_dir_size(self, &dir_size, &chunk_size, encode_base);
    if (err) {
        knd_log("-- failed to read dir size :(");
        return err;
    }

    if (DEBUG_CONC_LEVEL_2)
        knd_log("\n  .. OBJ DIR REC SIZE: %lu [size field size: %lu]",
                dir_size, (unsigned long)chunk_size);

    offset = (parent_dir->global_offset + block_size +\
              parent_dir->obj_block_size) - chunk_size - dir_size;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return knd_IO_FAIL;
    }

    out->reset(out);
    if (dir_size >= out->capacity) {
        knd_log("-- DIR size %zu exceeds current capacity: %zu",
                dir_size, out->capacity);
        return knd_LIMIT;
    }
    out->buf_size = dir_size;
    err = read(fd, out->buf, out->buf_size);
    if (err == -1) return knd_IO_FAIL;
    out->buf[out->buf_size] = '\0';

    if (DEBUG_CONC_LEVEL_2) {
        chunk_size = out->buf_size > KND_MAX_DEBUG_CHUNK_SIZE ? \
            KND_MAX_DEBUG_CHUNK_SIZE :  out->buf_size;

        knd_log("== OBJ DIR REC: %.*s [size:%zu]",
                chunk_size, out->buf, out->buf_size);
    }

    err = parse_obj_dir_trailer(self, parent_dir, fd);
    if (err) return err;

    return knd_OK;
}


static int open_frozen_DB(struct kndConcept *self)
{
    const char *filename;
    size_t filename_size;
    struct stat st;
    int fd;
    size_t file_size = 0;
    struct stat file_info;
    int err;

    filename = self->frozen_output_file_name;
    filename_size = self->frozen_output_file_name_size;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. open \"%.*s\" ..", filename_size, filename);

    if (stat(filename, &st)) {
        knd_log("-- no such file: %.*s", filename_size, filename);
        return knd_NO_MATCH; 
    }

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        knd_log("-- error reading FILE \"%.*s\": %d", filename_size, filename, fd);
        return knd_IO_FAIL;
    }

    fstat(fd, &file_info);
    file_size = file_info.st_size;  
    if (file_size <= KND_DIR_ENTRY_SIZE) {
        err = knd_LIMIT;
        goto final;
    }

    self->dir->block_size = file_size;
    self->dir->id[0] = '/';
    self->dir->mempool = self->mempool;
    self->dir->class_idx = self->class_idx;
    self->dir->class_name_idx = self->class_name_idx;

    /* TODO: get encode base from config */
    err = get_dir_trailer(self, self->dir, fd, KND_DIR_SIZE_ENCODE_BASE);
    if (err) {
        knd_log("-- error reading dir trailer in \"%.*s\"", filename_size, filename);
        goto final;
    }

    err = knd_OK;
    
 final:
    if (err) {
        knd_log("-- failed to open the frozen DB :(");
    }
    close(fd);
    return err;
}

static int 
kndConcept_write_filepath(struct glbOutput *out,
                          struct kndConcFolder *folder)
{
    int err;
    
    if (folder->parent) {
        err = kndConcept_write_filepath(out, folder->parent);
        if (err) return err;
    }

    err = out->write(out, folder->name, folder->name_size);
    if (err) return err;

    return knd_OK;
}

static int read_GSL_file(struct kndConcept *self,
                         struct kndConcFolder *parent_folder,
                         const char *filename,
                         size_t filename_size)
{
    struct glbOutput *out = self->out;
    struct glbOutput *file_out = self->task->file;
    struct kndConcFolder *folder, *folders;
    const char *c;
    size_t folder_name_size;
    const char *index_folder_name = "index";
    size_t index_folder_name_size = strlen("index");
    const char *file_ext = ".gsl";
    size_t file_ext_size = strlen(".gsl");
    size_t chunk_size = 0;
    int err;

    out->reset(out);
    err = out->write(out, self->dbpath, self->dbpath_size);
    if (err) return err;
    err = out->write(out, "/", 1);
    if (err) return err;

    if (parent_folder) {
        err = kndConcept_write_filepath(out, parent_folder);
        if (err) return err;
    }

    err = out->write(out, filename, filename_size);
    if (err) return err;
    err = out->write(out, file_ext, file_ext_size);
    if (err) return err;

    file_out->reset(file_out);
    err = file_out->write_file_content(file_out, (const char*)out->buf);
    if (err) {
        knd_log("-- couldn't read GSL class file \"%s\" :(", out->buf);
        return err;
    }

    err = parse_GSL(self, (const char*)file_out->buf, &chunk_size);
    if (err) return err;

    /* high time to read our folders */
    folders = self->folders;
    self->folders = NULL;
    self->num_folders = 0;

    for (folder = folders; folder; folder = folder->next) {
        folder->parent = parent_folder;

        /* reading a subfolder */
        if (folder->name_size > index_folder_name_size) {
            folder_name_size = folder->name_size - index_folder_name_size;
            c = folder->name + folder_name_size;
            if (!memcmp(c, index_folder_name, index_folder_name_size)) {
                /* right trim the folder's name */
                folder->name_size = folder_name_size;

                err = read_GSL_file(self, folder,
                                    index_folder_name, index_folder_name_size);
                if (err) {
                    knd_log("-- failed to read file: %.*s",
                            index_folder_name_size, index_folder_name);
                    return err;
                }
                continue;
            }
        }

        err = read_GSL_file(self, parent_folder, folder->name, folder->name_size);
        if (err) {
            knd_log("-- failed to read file: %.*s",
                    folder->name_size, folder->name);
            return err;
        }
    }
    return knd_OK;
}

static gsl_err_t conc_item_alloc(void *obj,
                                 const char *name __attribute__((unused)),
                                 size_t name_size __attribute__((unused)),
                                 size_t count  __attribute__((unused)),
                                 void **item)
{
    struct kndConcept *self = obj;
    struct kndConcItem *ci;
    int err;

    err = self->mempool->new_conc_item(self->mempool, &ci);
    if (err) return make_gsl_err_external(err);

    ci->parent = self;

    *item = ci;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t conc_item_append(void *accu,
                                  void *item)
{
    struct kndConcept *self = accu;
    struct kndConcItem *ci = item;

    ci->next = self->base_items;
    self->base_items = ci;
    self->num_base_items++;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t set_conc_item_baseclass(void *obj,
                                         const char *id, size_t id_size)
{
    struct kndConcItem *ci = obj;
    struct kndSet *class_idx;
    struct kndConcDir *dir;
    void *result;
    int err;

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    memcpy(ci->id, id, id_size);
    ci->id_size = id_size;

    class_idx = ci->parent->class_idx;
    err = class_idx->get(class_idx, id, id_size, &result);
    if (err) return make_gsl_err(gsl_FAIL);
    dir = result;

    if (!dir->conc) {
        err = unfreeze_class(ci->parent, dir, &dir->conc);
        if (err) return make_gsl_err_external(err);
    }

    ci->conc = dir->conc;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== conc item baseclass: %.*s (id:%.*s) CONC:%p",
                dir->name_size, dir->name, id_size, id, dir->conc);

    return make_gsl_err(gsl_OK);
}


static gsl_err_t validate_attr_item_list(void *obj,
                                         const char *name, size_t name_size,
                                         const char *rec, size_t *total_size)
{
    struct kndConcItem *ci = obj;
    struct kndAttrItem *attr_item;
    struct kndAttr *attr;
    int err;

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log("\n.. ARRAY: conc item \"%.*s\" to validate list item: %.*s..",
                ci->conc->name_size, ci->conc->name,
                name_size, name);

    err = ci->mempool->new_attr_item(ci->mempool, &attr_item);
    if (err) return make_gsl_err_external(err);

    err = get_attr(ci->conc, name, name_size, &attr);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name,
                ci->conc->name_size, ci->conc->name);
        return make_gsl_err_external(err);
    }

    attr_item->attr = attr;
    memcpy(attr_item->name, name, name_size);
    attr_item->name_size = name_size;

    struct gslTaskSpec aggr_item_spec = {
        .is_list_item = true,
        .accu = attr_item,
        .alloc = aggr_item_alloc,
        .append = attr_item_append,
        .parse = aggr_item_parse
    };

    append_attr_item(ci, attr_item);

    return gsl_parse_array(&aggr_item_spec, rec, total_size);
}

static gsl_err_t conc_item_read(void *obj,
                                const char *rec, size_t *total_size)
{
    struct kndConcItem *ci = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_conc_item_baseclass,
          .obj = ci
        },
        { .is_validator = true,
          .is_list = true,
          .validate = validate_attr_item_list,
          .obj = ci
        },
        { .is_validator = true,
          .validate = validate_attr_item,
          .obj = ci
        }
    };

    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size,
                                specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    knd_calc_num_id(ci->id, ci->id_size, &ci->numid);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_conc_item_array(void *obj,
                                       const char *rec,
                                       size_t *total_size)
{
    struct kndConcept *self = obj;

    struct gslTaskSpec ci_spec = {
        .is_list_item = true,
        .accu = self,
        .alloc = conc_item_alloc,
        .append = conc_item_append,
        .parse = conc_item_read
    };

    return gsl_parse_array(&ci_spec, rec, total_size);
}

static int read_GSP(struct kndConcept *self,
                    const char *rec,
                    size_t *total_size)
{
    gsl_err_t parser_err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. reading GSP: \"%.*s\"..", 256, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = self
        },
        { .is_list = true,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = parse_gloss_array,
          .obj = self
        },
        { .is_list = true,
          .name = "_ci",
          .name_size = strlen("_ci"),
          .parse = parse_conc_item_array,
          .obj = self
        },
        { .name = "aggr",
          .name_size = strlen("aggr"),
          .parse = parse_aggr,
          .obj = self
        },
        { .name = "str",
          .name_size = strlen("str"),
          .parse = parse_str,
          .obj = self
        },
        { .name = "bin",
          .name_size = strlen("bin"),
          .parse = parse_bin,
          .obj = self
        },
        { .name = "num",
          .name_size = strlen("num"),
          .parse = parse_num,
          .obj = self
        },
        { .name = "ref",
          .name_size = strlen("ref"),
          .parse = parse_ref,
          .obj = self
        },
        { .name = "text",
          .name_size = strlen("text"),
          .parse = parse_text,
          .obj = self
        },
        { .name = "_desc",
          .name_size = strlen("_desc"),
          .parse = parse_descendants,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}


static int resolve_classes(struct kndConcept *self)
{
    struct kndConcept *c;
    struct kndConcDir *dir;
    const char *key;
    void *val;
    int err;

    if (DEBUG_CONC_LEVEL_1)
        knd_log(".. resolving class refs in \"%.*s\"",
                self->name_size, self->name);

    key = NULL;
    self->class_name_idx->rewind(self->class_name_idx);
    do {
        self->class_name_idx->next_item(self->class_name_idx, &key, &val);
        if (!key) break;

        dir = (struct kndConcDir*)val;
        c = dir->conc;
        if (c->is_resolved) continue;
        err = c->resolve(c, NULL);
        if (err) {
            knd_log("-- couldn't resolve the \"%s\" class :(", c->name);
            return err;
        }
        c->is_resolved = true;
    } while (key);

    if (DEBUG_CONC_LEVEL_2)
        knd_log("++ classes resolved!\n\n");

    return knd_OK;
}

static int coordinate(struct kndConcept *self)
{
    struct kndConcept *c;
    struct kndConcDir *dir;
    const char *key;
    void *val;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. class coordination in progress ..");

    /* names to refs */
    err = resolve_classes(self);
    if (err) return err;

    /* display classes */
    if (DEBUG_CONC_LEVEL_2) {
        key = NULL;
        self->class_name_idx->rewind(self->class_name_idx);
        do {
            self->class_name_idx->next_item(self->class_name_idx, &key, &val);
            if (!key) break;
            dir = (struct kndConcDir*)val;
            c = dir->conc;

            /* terminal class */
            if (!c->num_children) {
                c->is_terminal = true;
                self->num_terminals++;
            } else {
                self->num_terminals += c->num_terminals;
            }

            if (c->dir->reverse_attr_name_idx) {
                c->str(c);
                continue;
            }
            if (!c->num_children) continue;
            if (!c->dir->descendants) continue;
            if (!c->dir->descendants->num_facets) continue;

            c->depth = 0;
            //c->str(c);

        } while (key);
    }

    err = self->proc->coordinate(self->proc);                                     RET_ERR();
    err = self->rel->coordinate(self->rel);                                       RET_ERR();
    
    return knd_OK;
}

static int expand_attr_ref_list(struct kndConcept *self,
                                struct kndAttrItem *parent_item)
{
    struct kndAttrItem *item;
    int err;
    for (item = parent_item->list; item; item = item->next) {
        if (!item->conc) {
            err = unfreeze_class(self, item->conc_dir, &item->conc);
            if (err) return err;
        }
    }

    return knd_OK;
}

static int expand_attrs(struct kndConcept *self,
                        struct kndAttrItem *parent_item)
{
    struct kndAttrItem *item;
    int err;
    for (item = parent_item; item; item = item->next) {
        switch (item->attr->type) {
        case KND_ATTR_REF:
            if (item->attr->is_a_set) {
                err = expand_attr_ref_list(self, item);
                if (err) return err;
            }
            break;
        default:
            break;
        }
    }

    return knd_OK;
}

static int expand_refs(struct kndConcept *self)
{
    struct kndConcItem *item;
    int err;

    for (item = self->base_items; item; item = item->next) {
        if (!item->attrs) continue;
        err = expand_attrs(self, item->attrs);
    }
    
    return knd_OK;
}

static int unfreeze_class(struct kndConcept *self,
                          struct kndConcDir *dir,
                          struct kndConcept **result)
{
    char buf[KND_MED_BUF_SIZE];
    size_t buf_size = 0;
    struct kndConcept *c;
    size_t chunk_size;
    const char *filename;
    size_t filename_size;
    const char *b;
    struct stat st;
    int fd;
    size_t file_size = 0;
    struct stat file_info;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. unfreezing class: \"%.*s\".. global offset:%zu",
                dir->name_size, dir->name, dir->global_offset);

    /* parse DB rec */
    filename = self->frozen_output_file_name;
    filename_size = self->frozen_output_file_name_size;
    if (stat(filename, &st)) {
        knd_log("-- no such file: %.*s", filename_size, filename);
        return knd_NO_MATCH; 
    }

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        knd_log("-- error reading FILE \"%.*s\": %d", filename_size, filename, fd);
        return knd_IO_FAIL;
    }

    fstat(fd, &file_info);
    file_size = file_info.st_size;  
    if (file_size <= KND_DIR_ENTRY_SIZE) {
        err = knd_LIMIT;
        goto final;
    }

    if (lseek(fd, dir->global_offset, SEEK_SET) == -1) {
        err = knd_IO_FAIL;
        goto final;
    }

    buf_size = dir->block_size;
    if (buf_size >= KND_MED_BUF_SIZE) {
        knd_log("-- memory limit exceeded :( buf size:%zu", buf_size);
        return knd_LIMIT;
    }
    err = read(fd, buf, buf_size);
    if (err == -1) {
        err = knd_IO_FAIL;
        goto final;
    }
    buf[buf_size] = '\0';

    if (DEBUG_CONC_LEVEL_1)
        knd_log("\n== frozen Conc REC: \"%.*s\"",
                buf_size, buf);
    /* done reading */
    close(fd);

    err = self->mempool->new_class(self->mempool, &c);
    if (err) goto final;
    memcpy(c->name, dir->name, dir->name_size);
    c->name_size = dir->name_size;
    c->out = self->out;
    c->log = self->log;
    c->task = self->task;
    c->root_class = self->root_class ? self->root_class : self;
    c->class_idx = self->class_idx;
    c->class_name_idx = self->class_name_idx;
    c->dir = dir;
    c->mempool = self->mempool;
    dir->conc = c;

    c->frozen_output_file_name = self->frozen_output_file_name;
    c->frozen_output_file_name_size = self->frozen_output_file_name_size;

    b = buf + 1;
    bool got_separ = false;
    /* ff the name */
    while (*b) {
        switch (*b) {
        case '{':
        case '}':
        case '[':
        case ']':
            got_separ = true;
            break;
        default:
            break;
        }
        if (got_separ) break;
        b++;
    }

    if (!got_separ) {
        knd_log("-- conc name not found in %.*s :(",
                buf_size, buf);
        err = knd_FAIL;
        goto final;
    }

    err = c->read(c, b, &chunk_size);
    if (err) {
        knd_log("-- failed to parse a rec for \"%.*s\" :(",
                c->name_size, c->name);
        goto final;
    }

    /* inherit attrs */
    err = build_attr_name_idx(c);
    if (err) {
        knd_log("-- failed to build attr idx for %.*s :(",
                c->name_size, c->name);
        goto final;
    }

    err = expand_refs(c);
    if (err) {
        knd_log("-- failed to expand refs of %.*s :(",
                c->name_size, c->name);
        goto final;
    }

    if (DEBUG_CONC_LEVEL_2) {
        c->depth = 1;
        c->str(c);
    }
    c->is_resolved = true;

    *result = c;
    return knd_OK;

 final:
    close(fd);
    return err;
}

static int get_class(struct kndConcept *self,
                     const char *name, size_t name_size,
                     struct kndConcept **result)
{
    struct kndConcDir *dir;
    struct kndConcept *c;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. %.*s to get class: \"%.*s\"..",
                self->name_size, self->name, name_size, name);

    dir = self->class_name_idx->get(self->class_name_idx, name, name_size);
    if (!dir) {
        knd_log("-- no such class: \"%.*s\":(", name_size, name);
        self->log->reset(self->log);
        err = self->log->write(self->log, name, name_size);
        if (err) return err;
        err = self->log->write(self->log, " class name not found",
                               strlen(" class name not found"));
        if (err) return err;
        if (self->task)
            self->task->http_code = HTTP_NOT_FOUND;

        return knd_NO_MATCH;
    }

    if (DEBUG_CONC_LEVEL_2)
        knd_log("++ got Conc Dir: %.*s from \"%.*s\" block size: %zu conc:%p",
                name_size, name,
                self->frozen_output_file_name_size,
                self->frozen_output_file_name, dir->block_size, dir->conc);

    if (dir->phase == KND_REMOVED) {
        knd_log("-- \"%s\" class was removed", name);
        self->log->reset(self->log);
        err = self->log->write(self->log, name, name_size);
        if (err) return err;
        err = self->log->write(self->log, " class was removed",
                               strlen(" class was removed"));
        if (err) return err;
        
        self->task->http_code = HTTP_GONE;
        return knd_NO_MATCH;
    }

    if (dir->conc) {
        c = dir->conc;
        c->task = self->task;
        c->next = NULL;

        if (DEBUG_CONC_LEVEL_2)
            c->str(c);

        *result = c;
        return knd_OK;
    }

    err = unfreeze_class(self, dir, &c);
    if (err) {
        knd_log("-- failed to unfreeze class: %.*s",
                dir->name_size, dir->name);
        return err;
    }

    c->task = self->task;
    c->class_idx = self->class_idx;
    c->class_name_idx = self->class_name_idx;
    
    *result = c;
    return knd_OK;
}

static int get_obj(struct kndConcept *self,
                   const char *name, size_t name_size,
                   struct kndObject **result)
{
    struct kndObjEntry *entry;
    struct kndObject *obj;
    int err, e;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("\n\n.. \"%.*s\" class to get obj: \"%.*s\"..",
                self->name_size, self->name,
                name_size, name);

    if (!self->dir) {
        knd_log("-- no frozen dir rec in \"%.*s\" :(",
                self->name_size, self->name);
    }
    
    if (!self->dir->obj_name_idx) {
        knd_log("-- no obj name idx in \"%.*s\" :(", self->name_size, self->name);

        self->log->reset(self->log);
        e = self->log->write(self->log, self->name, self->name_size);
        if (e) return e;
        e = self->log->write(self->log, " class has no instances",
                             strlen(" class has no instances"));
        if (e) return e;

        return knd_FAIL;
    }

    entry = self->dir->obj_name_idx->get(self->dir->obj_name_idx, name, name_size);
    if (!entry) {
        knd_log("-- no such obj: \"%.*s\" :(", name_size, name);
        self->log->reset(self->log);
        err = self->log->write(self->log, name, name_size);
        if (err) return err;
        err = self->log->write(self->log, " obj name not found",
                               strlen(" obj name not found"));
        if (err) return err;
        self->task->http_code = HTTP_NOT_FOUND;
        return knd_NO_MATCH;
    }

    if (DEBUG_CONC_LEVEL_2)
        knd_log("++ got obj entry %.*s  size: %zu OBJ: %p",
                name_size, name, entry->block_size, entry->obj);

    if (!entry->obj) goto read_entry;

    if (entry->obj->state->phase == KND_REMOVED) {
        knd_log("-- \"%s\" obj was removed", name);
        self->log->reset(self->log);
        err = self->log->write(self->log, name, name_size);
        if (err) return err;
        err = self->log->write(self->log, " obj was removed",
                               strlen(" obj was removed"));
        if (err) return err;
        return knd_NO_MATCH;
    }

    obj = entry->obj;
    obj->state->phase = KND_SELECTED;
    obj->task = self->task;
    *result = obj;
    return knd_OK;

 read_entry:
    err = read_obj_entry(self, entry, result);
    if (err) return err;

    return knd_OK;
}

static int read_obj_entry(struct kndConcept *self,
                          struct kndObjEntry *entry,
                          struct kndObject **result)
{
    struct kndObject *obj;
    const char *filename;
    size_t filename_size;
    const char *c, *b, *e;
    size_t chunk_size;
    struct stat st;
    int fd;
    size_t file_size = 0;
    struct stat file_info;
    int err;

    /* parse DB rec */
    filename = self->frozen_output_file_name;
    filename_size = self->frozen_output_file_name_size;
    if (!filename_size) {
        knd_log("-- no file name to read in conc %.*s :(",
                self->name_size, self->name);
        return knd_FAIL;
    }

    if (stat(filename, &st)) {
        knd_log("-- no such file: \"%.*s\"", filename_size, filename);
        return knd_NO_MATCH; 
    }

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        knd_log("-- error reading FILE \"%.*s\": %d", filename_size, filename, fd);
        return knd_IO_FAIL;
    }
    fstat(fd, &file_info);
    file_size = file_info.st_size;  
    if (file_size <= KND_DIR_ENTRY_SIZE) {
        err = knd_LIMIT;
        goto final;
    }

    if (lseek(fd, entry->offset, SEEK_SET) == -1) {
        err = knd_IO_FAIL;
        goto final;
    }

    entry->block = malloc(entry->block_size + 1);
    if (!entry->block) return knd_NOMEM;

    err = read(fd, entry->block, entry->block_size);
    if (err == -1) {
        err = knd_IO_FAIL;
        goto final;
    }
    /* NB: current parser expects a null terminated string */
    entry->block[entry->block_size] = '\0';

    if (DEBUG_CONC_LEVEL_2)
        knd_log("   == OBJ REC: \"%.*s\"",
                entry->block_size, entry->block);

    /* done reading */
    close(fd);

    err = self->mempool->new_obj(self->mempool, &obj);                           RET_ERR();
    err = self->mempool->new_state(self->mempool, &obj->state);                  RET_ERR();

    obj->state->phase = KND_FROZEN;
    obj->out = self->out;
    obj->log = self->log;
    obj->task = self->task;
    obj->entry = entry;
    obj->conc = self;
    obj->mempool = self->mempool;
    entry->obj = obj;
    obj->entry = entry;

    /* skip over initial brace '{' */
    c = entry->block + 1;
    b = c;
    bool got_separ = false;
    /* ff the name */
    while (*c) {
        switch (*c) {
        case '{':
        case '}':
        case '[':
        case ']':
            got_separ = true;
            e = c;
            break;
        default:
            break;
        }
        if (got_separ) break;
        c++;
    }

    if (!got_separ) {
        knd_log("-- obj name not found in \"%.*s\" :(",
                entry->block_size, entry->block);
        obj->del(obj);
        return knd_FAIL;
    }

    obj->name = entry->name;
    obj->name_size = entry->name_size;

    err = obj->read(obj, c, &chunk_size);
    if (err) {
        knd_log("-- failed to parse obj %.*s :(",
                obj->name_size, obj->name);
        obj->del(obj);
        return err;
    }

    if (DEBUG_CONC_LEVEL_2)
        obj->str(obj);

    *result = obj;
    return knd_OK;
    
 final:
    close(fd);
    return err;
}


static int export_conc_elem_JSON(void *obj,
                                 const char *elem_id,
                                 size_t elem_id_size,
                                 size_t count,
                                 void *elem)
{
    struct kndConcept *self = obj;
    struct glbOutput *out = self->out;
    struct kndConcDir *dir = elem;
    struct kndConcept *c = dir->conc;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("..export elem: %.*s  conc:%p",
                elem_id_size, elem_id, c);

    if (!c) {
        err = unfreeze_class(self, dir, &c);                          RET_ERR();
    }

    /* separator */
    if (count) {
        err = out->writec(out, ',');                                          RET_ERR();
    }

    c->out = out;
    c->format = KND_FORMAT_JSON;
    err = c->export(c);
    if (err) return err;

    return knd_OK;
}

static int kndConcept_export_set_JSON(struct kndConcept *self,
                                      struct kndSet *set)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct glbOutput *out = self->out;
    int err;

    err = out->write(out, "{\"_set\":{\"_base\":\"",
                     strlen("{\"_set\":{\"_base\":\""));                          RET_ERR();
    err = out->write(out, set->base->name,  set->base->name_size);                RET_ERR();
    err = out->write(out, "\"}", strlen("\"}"));                                  RET_ERR();

    buf_size = sprintf(buf, ",\"total\":%lu",
                       (unsigned long)set->num_elems);
    err = out->write(out, buf, buf_size);                                         RET_ERR();

    err = out->write(out, ",\"batch\":[",
                     strlen(",\"batch\":["));                                     RET_ERR();

    err = set->map(set, export_conc_elem_JSON, (void*)self);
    if (err) return err;
    err = out->writec(out, ']');                                                  RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();
    return knd_OK;
}

static int export_conc_id_GSP(void *obj,
                              const char *elem_id  __attribute__((unused)),
                              size_t elem_id_size  __attribute__((unused)),
                              size_t count __attribute__((unused)),
                              void *elem)
{
    struct glbOutput *out = obj;
    struct kndConcDir *dir = elem;
    int err;
    err = out->writec(out, ' ');                                                  RET_ERR();
    err = out->write(out, dir->id, dir->id_size);                                 RET_ERR();
    return knd_OK;
}

static int kndConcept_export_descendants_GSP(struct kndConcept *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct glbOutput *out = self->out;
    struct kndSet *set;
    int err;

    set = self->dir->descendants;

    err = out->write(out, "{_desc", strlen("{_desc"));                            RET_ERR();
    buf_size = sprintf(buf, "{tot %zu}", set->num_elems);
    err = out->write(out, buf, buf_size);                                         RET_ERR();

    err = out->write(out, "[c", strlen("[c"));                                    RET_ERR();
    err = set->map(set, export_conc_id_GSP, (void*)out);
    if (err) return err;
    err = out->writec(out, ']');                                                  RET_ERR();

    err = out->writec(out, '}');                                                  RET_ERR();
    
    return knd_OK;
}

static gsl_err_t present_class_selection(void *obj,
                                         const char *val __attribute__((unused)),
                                         size_t val_size __attribute__((unused)))
{
    struct kndConcept *self = obj;
    struct kndConcept *c;
    struct kndSet *set;
    struct glbOutput *out = self->out;
    int err;

    if (DEBUG_CONC_LEVEL_1)
        knd_log(".. presenting \"%.*s\" selection: "
                " curr_class:%p",
                self->name_size, self->name,
                self->curr_class);

    out->reset(out);

    if (self->task->type == KND_SELECT_STATE) {
        /* TODO: execute query / intersect sets */
        set = self->task->sets;

        if (!set) {
            err = out->write(out, "{}", strlen("{}"));
            if (err) return make_gsl_err_external(err);
            return make_gsl_err(gsl_OK);
        }

        err = kndConcept_export_set_JSON(self, set);
        if (err) return make_gsl_err_external(err);

        return make_gsl_err(gsl_OK);
    }

    if (!self->curr_class) {
        knd_log("-- no class to present :(");
        return make_gsl_err(gsl_FAIL);
    }

    c = self->curr_class;
    c->out = out;
    c->task = self->task;
    c->format = KND_FORMAT_JSON;
    c->depth = 0;
    c->max_depth = 1;
    if (self->max_depth) {
        c->max_depth = self->max_depth;
    }
    err = c->export(c);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_get_class(void *obj, const char *name, size_t name_size)
{
    struct kndConcept *self = obj;
    struct kndConcept *c;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    self->curr_class = NULL;
    err = get_class(self, name, name_size, &c);
    if (err) return make_gsl_err_external(err);

    c->frozen_output_file_name = self->frozen_output_file_name;
    c->frozen_output_file_name_size = self->frozen_output_file_name_size;

    self->curr_class = c;

    if (DEBUG_CONC_LEVEL_1) {
        c->str(c);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_get_class_by_numid(void *obj, const char *id, size_t id_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct kndConcept *self = obj;
    struct kndConcept *c;
    struct kndConcDir *dir;
    void *result;
    long numval;
    int err;

    if (id_size >= KND_NAME_SIZE)
        return make_gsl_err(gsl_FAIL);

    memcpy(buf, id, id_size);
    buf[id_size] = '0';

    err = knd_parse_num((const char*)buf, &numval);
    if (err) return make_gsl_err_external(err);

    if (numval <= 0) return make_gsl_err(gsl_FAIL);
    
    buf_size = 0;
    knd_num_to_str((size_t)numval, buf, &buf_size, KND_RADIX_BASE);

    if (DEBUG_CONC_LEVEL_2)
        knd_log("ID: %zu => \"%.*s\" [size: %zu]",
                numval, buf_size, buf, buf_size);

    err = self->class_idx->get(self->class_idx, buf, buf_size, &result);
    if (err) return make_gsl_err(gsl_FAIL);
    dir = result;

    self->curr_class = NULL;
    err = get_class(self, dir->name, dir->name_size, &c);
    if (err) return make_gsl_err_external(err);

    c->frozen_output_file_name = self->frozen_output_file_name;
    c->frozen_output_file_name_size = self->frozen_output_file_name_size;
    self->curr_class = c;

    if (DEBUG_CONC_LEVEL_2) {
        c->str(c);
    }

    return make_gsl_err(gsl_OK);
}


static gsl_err_t run_remove_class(void *obj, const char *name, size_t name_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndConcept *c;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    if (!self->curr_class) {
        knd_log("-- remove operation: class name not specified:(");
        self->log->reset(self->log);
        err = self->log->write(self->log, name, name_size);
        if (err) return make_gsl_err_external(err);
        err = self->log->write(self->log, " class name not specified",
                               strlen(" class name not specified"));
        if (err) return make_gsl_err_external(err);
        return make_gsl_err(gsl_NO_MATCH);
    }

    c = self->curr_class;

    if (DEBUG_CONC_LEVEL_1)
        knd_log("== class to remove: \"%.*s\"\n", c->name_size, c->name);

    c->dir->phase = KND_REMOVED;

    self->log->reset(self->log);
    err = self->log->write(self->log, name, name_size);
    if (err) return make_gsl_err_external(err);
    err = self->log->write(self->log, " class removed",
                           strlen(" class removed"));
    if (err) return make_gsl_err_external(err);

    c->next = self->inbox;
    self->inbox = c;
    self->inbox_size++;

    return make_gsl_err(gsl_OK);
}


static int select_delta(struct kndConcept *self,
                        const char *rec,
                        size_t *total_size)
{
    struct kndStateControl *state_ctrl = self->task->state_ctrl;
    struct kndUpdate *update;
    struct kndClassUpdate *class_update;
    struct kndConcept *c;
    int err;
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .name = "eq",
          .name_size = strlen("eq"),
          .parse = gsl_parse_size_t,
          .obj = &self->task->batch_eq
        },
        { .name = "gt",
          .name_size = strlen("gt"),
          .parse = gsl_parse_size_t,
          .obj = &self->task->batch_gt
        },
        { .name = "lt",
          .name_size = strlen("lt"),
          .parse = gsl_parse_size_t,
          .obj = &self->task->batch_lt
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log(".. select delta:  gt %zu  lt %zu ..",
                self->task->batch_gt,
                self->task->batch_lt);

    err = state_ctrl->select(state_ctrl);                                         RET_ERR();

    for (size_t i = 0; i < state_ctrl->num_selected; i++) {
        update = state_ctrl->selected[i];
        
        for (size_t j = 0; j < update->num_classes; j++) {
            class_update = update->classes[j];
            c = class_update->conc;

            //c->str(c);
            if (!c) return knd_FAIL;

            /*if (!self->curr_baseclass) {
                self->task->class_selects[self->task->num_class_selects] = c;
                self->task->num_class_selects++;
                continue;
                }*/

            /* filter by baseclass */
            /*for (size_t j = 0; j < c->num_bases; j++) {
                dir = c->bases[j];
                knd_log("== base class: %.*s", dir->name_size, dir->name);
                }*/
        }
    }

    /* TODO: JSON export */

    return knd_OK;
}

static gsl_err_t parse_select_class_delta(void *data,
                                          const char *rec,
                                          size_t *total_size)
{
    struct kndConcept *self = data;
    int err;

    err = select_delta(self, rec, total_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static int parse_select_class(void *obj,
                              const char *rec,
                              size_t *total_size)
{
    struct kndConcept *self = obj;
    struct kndConcept *c;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing class select rec: \"%.*s\"", 32, rec);

    self->depth = 0;
    self->curr_class = NULL;
    self->curr_baseclass = NULL;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_class,
          .obj = self
        },
        { .name = "_id",
          .name_size = strlen("_id"),
          .is_selector = true,
          .run = run_get_class_by_numid,
          .obj = self
        }/*,
        { .type = GSL_CHANGE_STATE,
          .is_validator = true,
          .validate = parse_set_attr,
          .obj = self
          }*/,
        { .type = GSL_CHANGE_STATE,
          .name = "_rm",
          .name_size = strlen("_rm"),
          .run = run_remove_class,
          .obj = self
        },
        { .type = GSL_CHANGE_STATE,
          .name = "obj",
          .name_size = strlen("obj"),
          .parse = parse_import_obj,
          .obj = self
        },
        { .name = "obj",
          .name_size = strlen("obj"),
          .parse = parse_select_obj,
          .obj = self
        },
        { .name = "_base",
          .name_size = strlen("_base"),
          .is_selector = true,
          .parse = parse_baseclass_select,
          .obj = self
        },
        { .name = "_term_iterator",
          .name_size = strlen("_term_iterator"),
          .is_selector = true,
          .parse = self->task->parse_iter,
          .obj = self->task
        },
        {  .name = "_depth",
           .name_size = strlen("_depth"),
           .parse = gsl_parse_size_t,
           .obj = &self->max_depth
        },
        { .name = "_delta",
          .name_size = strlen("_delta"),
          .is_selector = true,
          .parse = parse_select_class_delta,
          .obj = self
        },
        { .is_default = true,
          .run = present_class_selection,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        if (DEBUG_CONC_LEVEL_2)
            knd_log("-- class parse error %d: \"%.*s\"",
                    parser_err.code, self->log->buf_size, self->log->buf);
        if (!self->log->buf_size) {
            err = self->log->write(self->log, "class parse failure",
                                 strlen("class parse failure"));
            if (err) return err;
        }

        /* TODO: release resources */
        if (self->curr_class) {
            c = self->curr_class;
            c->reset_inbox(c);
        }
        return gsl_err_to_knd_err_codes(parser_err);
    }

    /* any updates happened? */
    if (self->curr_class) {
        c = self->curr_class;
        if (c->inbox_size || c->obj_inbox_size) {
            c->next = self->inbox;
            self->inbox = c;
            self->inbox_size++;
        }
    }

    return knd_OK;
}

static int aggr_item_export_JSON(struct kndConcept *self,
                                 struct kndAttrItem *parent_item)
{
    struct glbOutput *out = self->out;
    struct kndAttrItem *item;
    struct kndConcept *c;
    int err;

    if (!parent_item->conc) {
        err = out->writec(out, '{');
        if (err) return err;
        err = out->writec(out, '}');
        if (err) return err;
        return knd_OK;
    }

    err = out->writec(out, '{');
    if (err) return err;

    err = out->write(out, "\"product\":", strlen("\"product\":"));
    if (err) return err;
    c = parent_item->conc;
    c->out = self->out;
    c->task = self->task;
    c->format =  KND_FORMAT_JSON;
    c->depth = self->depth + 1;
    c->max_depth = self->max_depth;
    err = c->export(c);
    if (err) return err;
    
    for (item = parent_item->children; item; item = item->next) {
        err = out->writec(out, ',');
        if (err) return err;

        err = out->writec(out, '"');
        if (err) return err;
        err = out->write(out, item->name, item->name_size);
        if (err) return err;
        err = out->writec(out, '"');
        if (err) return err;
        err = out->writec(out, ':');
        if (err) return err;
        err = out->writec(out, '"');
        err = out->write(out, item->val, item->val_size);
        if (err) return err;
        err = out->writec(out, '"');
        if (err) return err;
    }

    err = out->writec(out, '}');
    if (err) return err;
    
    return knd_OK;
}

static int ref_item_export_JSON(struct kndConcept *self,
                                struct kndAttrItem *item)
{
    struct glbOutput *out = self->out;
    struct kndConcept *c;
    int err;

    assert(item->conc != NULL);

    c = item->conc;
    c->out = self->out;
    c->task = self->task;
    c->format =  KND_FORMAT_JSON;
    c->depth = self->depth + 1;
    c->max_depth = self->max_depth;
    err = c->export(c);
    if (err) return err;

    return knd_OK;
}

static int attr_item_list_export_JSON(struct kndConcept *self,
                                      struct kndAttrItem *parent_item)
{
    struct glbOutput *out = self->out;
    struct kndAttrItem *item;
    bool in_list = false;
    int err;

    err = out->writec(out, '"');
    if (err) return err;
    err = out->write(out, parent_item->name, parent_item->name_size);
    if (err) return err;
    err = out->write(out, "\":[", strlen("\":["));
    if (err) return err;

    /* first elem: TODO */
    if (parent_item->conc) {
        switch (parent_item->attr->type) {
        case KND_ATTR_AGGR:
            err = aggr_item_export_JSON(self, parent_item);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            err = ref_item_export_JSON(self, parent_item);
            if (err) return err;
            break;
        default:
            err = out->writec(out, '"');
            if (err) return err;
            err = out->write(out, parent_item->val, parent_item->val_size);
            if (err) return err;
            err = out->writec(out, '"');
            if (err) return err;
            break;
        }
        in_list = true;
    }

    for (item = parent_item->list; item; item = item->next) {
        /* TODO */
        if (!item->attr) {
            knd_log("-- no attr: %.*s (%p)",
                    item->name_size, item->name, item);
            continue;
        }

        if (in_list) {
            err = out->writec(out, ',');
            if (err) return err;
        }

        switch (item->attr->type) {
        case KND_ATTR_AGGR:
             err = aggr_item_export_JSON(self, item);
             if (err) return err;
             break;
        case KND_ATTR_REF:
             err = ref_item_export_JSON(self, item);
             if (err) return err;
             break;
        default:
            err = out->writec(out, '"');
            if (err) return err;
            err = out->write(out, item->val, item->val_size);
            if (err) return err;
            err = out->writec(out, '"');
            if (err) return err;
            break;
        }
        in_list = true;
    }
    err = out->writec(out, ']');
    if (err) return err;

    return knd_OK;
}

static int attr_items_export_JSON(struct kndConcept *self,
                                  struct kndAttrItem *items,
                                  size_t depth __attribute__((unused)))
{
    struct kndAttrItem *item;
    struct glbOutput *out;
    struct kndConcept *c;
    bool in_list = false;
    int err;

    out = self->out;
    err = out->write(out, ",\"_attrs\":{", strlen(",\"_attrs\":{"));
    if (err) return err;

    for (item = items; item; item = item->next) {
        if (in_list) {
            err = out->writec(out, ',');
            if (err) return err;
        }

        if (item->attr && item->attr->is_a_set) {
            err = attr_item_list_export_JSON(self, item);
            if (err) return err;
            in_list = true;
            continue;
        }

        err = out->writec(out, '"');
        if (err) return err;
        err = out->write(out, item->name, item->name_size);
        if (err) return err;
        err = out->write(out, "\":", strlen("\":"));
        if (err) return err;

        switch (item->attr->type) {
        case KND_ATTR_NUM:
            err = out->write(out, item->val, item->val_size);
            if (err) return err;
            break;
        case KND_ATTR_AGGR:
            if (!item->conc) {
                err = out->write(out, "{}", strlen("{}"));
                if (err) return err;
            } else {
                c = item->conc;
                c->out = self->out;
                c->task = self->task;
                c->format =  KND_FORMAT_JSON;
                c->depth = self->depth;
                err = c->export(c);
                if (err) return err;
            }
            break;
        default:
            err = out->write(out, "\"", strlen("\""));
            if (err) return err;
            err = out->write(out, item->val, item->val_size);
            if (err) return err;
            err = out->write(out, "\"", strlen("\""));
            if (err) return err;
        }
        in_list = true;
    }
    err = out->writec(out, '}');
    if (err) return err;
  
    return knd_OK;
}

static int export_gloss_JSON(struct kndConcept *self)
{
    struct kndTranslation *tr;
    struct glbOutput *out = self->out;
    int err;
    
    for (tr = self->tr; tr; tr = tr->next) { 
        if (memcmp(self->task->locale, tr->locale, tr->locale_size)) {
            continue;
        }
        err = out->write(out, ",\"_gloss\":\"", strlen(",\"_gloss\":\""));        RET_ERR();
        err = out->write(out, tr->val,  tr->val_size);                            RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();
        break;
    }
    return knd_OK;
}

static int export_concise_JSON(struct kndConcept *self)
{
    struct kndConcItem *item;
    struct kndAttrItem *attr_item;
    struct kndAttr *attr;
    struct glbOutput *out = self->out;
    int err;

    for (item = self->base_items; item; item = item->next) {
        for (attr_item = item->attrs; attr_item; attr_item = attr_item->next) {
            /* TODO assert */
            if (!attr_item->attr) continue;
            attr = attr_item->attr;

            if (attr->is_a_set) continue;
            if (!attr->concise_level) continue;

            /* concise representation */
            err = out->writec(out, ',');                                          RET_ERR();
            err = out->writec(out, '"');                                          RET_ERR();

            err = out->write(out, attr_item->name, attr_item->name_size);
            if (err) return err;
            err = out->write(out, "\":", strlen("\":"));
            if (err) return err;

            switch (attr->type) {
            case KND_ATTR_NUM:
                err = out->write(out, attr_item->val, attr_item->val_size);
                if (err) return err;
                break;
            case KND_ATTR_AGGR:
                //knd_log(".. aggr attr..");
                break;
            default:
                err = out->write(out, "\"", strlen("\""));
                if (err) return err;
                err = out->write(out, attr_item->val, attr_item->val_size);
                if (err) return err;
                err = out->write(out, "\"", strlen("\""));
                if (err) return err;
            }
        }
    }

    return knd_OK;
}

static int export_JSON(struct kndConcept *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndAttr *attr;

    struct kndConcept *c;
    struct kndConcItem *item;
    struct kndConcRef *ref;
    struct kndConcDir *dir;
    struct kndUpdate *update;

    struct tm tm_info;
    struct glbOutput *out;
    size_t item_count;
    int i, err;

    if (DEBUG_CONC_LEVEL_1)
        knd_log(".. JSON export concept: \"%s\"  "
                "locale: %s depth: %lu",
                self->name, self->task->locale,
                (unsigned long)self->depth);

    out = self->out;
    err = out->write(out, "{", 1);                                                RET_ERR();
    err = out->write(out, "\"_name\":\"", strlen("\"_name\":\""));                RET_ERR();
    err = out->write(out, self->dir->name, self->dir->name_size);                 RET_ERR();
    err = out->write(out, "\"", 1);                                               RET_ERR();

    err = out->write(out, ",\"_id\":", strlen(",\"_id\":"));                      RET_ERR();
    buf_size = snprintf(buf, KND_NAME_SIZE, "%zu", self->dir->numid);
    err = out->write(out, buf, buf_size);                                         RET_ERR();

    err = export_gloss_JSON(self);                                                RET_ERR();

    if (self->depth >= self->max_depth) {
        /* any concise fields? */
        err = export_concise_JSON(self);                                          RET_ERR();
        goto final;
    }

    if (self->num_updates) {
        /* latest update */
        //update = self->updates->update;
        //err = out->write(out, ",\"_state\":", strlen(",\"_state\":"));            RET_ERR();
        //err = out->write(out, update->numid, update->id_size);                       RET_ERR();

        //time(&update->timestamp);
        //localtime_r(&update->timestamp, &tm_info);
        //buf_size = strftime(buf, KND_NAME_SIZE,
        //                    ",\"_timestamp\":\"%Y-%m-%d %H:%M:%S\"", &tm_info);
        //err = out->write(out, buf, buf_size);                                     RET_ERR();
    }

    /* display base classes only once */
    if (self->num_base_items) {
        err = out->write(out, ",\"_base\":[", strlen(",\"_base\":["));            RET_ERR();

        item_count = 0;
        for (item = self->base_items; item; item = item->next) {
            if (item->conc && item->conc->ignore_children) continue;
            if (item_count) {
                err = out->write(out, ",", 1);                                    RET_ERR();
            }

            err = out->write(out, "{\"_name\":\"", strlen("{\"_name\":\""));              RET_ERR();
            err = out->write(out, item->conc->name, item->conc->name_size);
            if (err) return err;
            err = out->write(out, "\"", 1);
            if (err) return err;

            err = out->write(out, ",\"_id\":", strlen(",\"_id\":"));
            if (err) return err;
            buf_size = snprintf(buf, KND_NAME_SIZE, "%zu", item->numid);
            err = out->write(out, buf, buf_size);
            if (err) return err;

            if (item->attrs) {
                err = attr_items_export_JSON(self, item->attrs, 0);
                if (err) return err;
            }
            
            err = out->write(out, "}", 1);
            if (err) return err;
            item_count++;
        }
        err = out->write(out, "]", 1);
        if (err) return err;
    }


    if (self->attrs) {
        err = out->write(out, ",\"_attrs\":{",
                         strlen(",\"_attrs\":{"));
        if (err) return err;

        i = 0;
        attr = self->attrs;
        while (attr) {
            if (i) {
                err = out->write(out, ",", 1);
                if (err) return err;
            }
            attr->out = out;
            attr->task = self->task;
            attr->format = KND_FORMAT_JSON;

            err = attr->export(attr);
            if (err) {
                if (DEBUG_CONC_LEVEL_TMP)
                    knd_log("-- failed to export %s attr to JSON: %s\n", attr->name);
                return err;
            }

            i++;
            attr = attr->next;
        }
        err = out->writec(out, '}');
        if (err) return err;
    }

    /* non-terminal classes */
    if (self->dir->num_children) {
        err = out->write(out, ",\"_num_subclasses\":", strlen(",\"_num_subclasses\":"));
        if (err) return err;
        buf_size = sprintf(buf, "%zu", self->dir->num_children);
        err = out->write(out, buf, buf_size);
        if (err) return err;

        if (self->dir->num_terminals) {
            err = out->write(out, ",\"_num_terminals\":", strlen(",\"_num_terminals\":"));
            if (err) return err;
            buf_size = sprintf(buf, "%zu", self->dir->num_terminals);
            err = out->write(out, buf, buf_size);
            if (err) return err;
        }
        
        if (self->dir->num_children) {
            err = out->write(out, ",\"_subclasses\":[", strlen(",\"_subclasses\":["));
            if (err) return err;

            for (size_t i = 0; i < self->dir->num_children; i++) {
                dir = self->dir->children[i];
                if (i) {
                    err = out->write(out, ",", 1);
                    if (err) return err;
                }
                err = out->write(out, "{\"_name\":\"", strlen("{\"_name\":\""));
                if (err) return err;
                err = out->write(out, dir->name, dir->name_size);
                if (err) return err;
                err = out->write(out, "\"", 1);
                if (err) return err;

                err = out->write(out, ",\"_id\":", strlen(",\"_id\":"));
                if (err) return err;
                buf_size = sprintf(buf, "%zu", dir->numid);
                err = out->write(out, buf, buf_size);
                if (err) return err;

                if (dir->num_terminals) {
                    err = out->write(out, ",\"_num_terminals\":",
                                     strlen(",\"_num_terminals\":"));
                    if (err) return err;
                    buf_size = sprintf(buf, "%zu", dir->num_terminals);
                    err = out->write(out, buf, buf_size);
                    if (err) return err;
                }

                /* localized glosses */
                c = dir->conc;
                if (!c) {
                    err = unfreeze_class(self, dir, &c);                          RET_ERR();
                }
                err = export_gloss_JSON(c);                                       RET_ERR();

                err = export_concise_JSON(c);                                     RET_ERR();

                err = out->write(out, "}", 1);
                if (err) return err;
            }
            err = out->write(out, "]", 1);
            if (err) return err;

        }

        err = out->write(out, "}", 1);
        if (err) return err;
        return knd_OK;
    }

    if (self->num_children) {
        err = out->write(out, ",\"_num_subclasses\":", strlen(",\"_num_subclasses\":"));
        if (err) return err;
        buf_size = sprintf(buf, "%zu", self->num_children);
        err = out->write(out, buf, buf_size);
        if (err) return err;

        if (self->depth + 1 < KND_MAX_CLASS_DEPTH) {
            err = out->write(out, ",\"_subclasses\":[", strlen(",\"_subclasses\":["));
            if (err) return err;
            for (size_t i = 0; i < self->num_children; i++) {
                ref = &self->children[i];
                
                if (DEBUG_CONC_LEVEL_2)
                    knd_log(".. export base of --> %s", ref->dir->name);

                if (i) {
                    err = out->write(out, ",", 1);
                    if (err) return err;
                }
                c = ref->dir->conc;
                c->out = self->out;
                c->task = self->task;
                c->format =  KND_FORMAT_JSON;
                c->depth = self->depth + 1;
                err = c->export(c);
                if (err) return err;
            }
            err = out->write(out, "]", 1);
            if (err) return err;
        }
    }

 final:
    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}

static int ref_list_export_GSP(struct kndConcept *self,
                               struct kndAttrItem *parent_item)
{
    struct kndAttrItem *item;
    struct glbOutput *out;
    struct kndConcept *c;
    int err;

    out = self->out;

    err = out->writec(out, '{');
    if (err) return err;
    err = out->write(out, parent_item->name, parent_item->name_size);
    if (err) return err;

    err = out->write(out, "[r", strlen("[r"));
    if (err) return err;

    /* first elem */
    if (parent_item->val_size) {
        c = parent_item->conc;
        if (c) {
            err = out->writec(out, ' ');
            if (err) return err;
            err = out->write(out, c->dir->id, c->dir->id_size);
            if (err) return err;
        }
    }

    for (item = parent_item->list; item; item = item->next) {
        c = item->conc;
        if (c) {
            err = out->writec(out, ' ');
            if (err) return err;
            err = out->write(out, c->dir->id, c->dir->id_size);
            if (err) return err;
        }
    }
    err = out->writec(out, ']');
    if (err) return err;
    
    err = out->writec(out, '}');
    if (err) return err;

    return knd_OK;
}

static int aggr_item_export_GSP(struct kndConcept *self,
                                struct kndAttrItem *parent_item)
{
    struct glbOutput *out = self->out;
    struct kndConcept *c = parent_item->conc;
    struct kndAttrItem *item;
    int err;
    
    if (c) {
        err = out->writec(out, '{');
        if (err) return err;
        err = out->write(out, c->dir->id, c->dir->id_size);
        if (err) return err;
    }

    for (item = parent_item->children; item; item = item->next) {
        err = out->writec(out, '{');
        if (err) return err;
        err = out->write(out, item->name, item->name_size);
        if (err) return err;
        err = out->writec(out, ' ');
        if (err) return err;
        err = out->write(out, item->val, item->val_size);
        if (err) return err;

        if (item->children) {
            err = aggr_item_export_GSP(self, item->children);
            if (err) return err;
        }
        err = out->writec(out, '}');
        if (err) return err;
    }

    if (c) {
        err = out->writec(out, '}');
        if (err) return err;
    }

    return knd_OK;
}

static int aggr_list_export_GSP(struct kndConcept *self,
                                struct kndAttrItem *parent_item)
{
    struct kndAttrItem *item;
    struct glbOutput *out = self->out;
    struct kndConcept *c;
    int err;

    err = out->writec(out, '[');
    if (err) return err;
    err = out->write(out, parent_item->name, parent_item->name_size);
    if (err) return err;

    /* first elem */
    if (parent_item->conc) {
        c = parent_item->conc;
        if (c) {
            err = aggr_item_export_GSP(self, parent_item);
            if (err) return err;
        }
    }

    for (item = parent_item->list; item; item = item->next) {
        c = item->conc;
        // TODO
        if (!c) continue;

        /*err = out->writec(out, '{');
            if (err) return err;
            err = out->write(out, item->val, item->val_size);
            if (err) return err;
            err = out->writec(out, '}');
            if (err) return err;
            continue;
        */

        err = aggr_item_export_GSP(self, item);
        if (err) return err;
    }

    err = out->writec(out, ']');
    if (err) return err;

    return knd_OK;
}

static int attr_items_export_GSP(struct kndConcept *self,
                                 struct kndAttrItem *items,
                                 size_t depth  __attribute__((unused)))
{
    struct kndAttrItem *item;
    struct glbOutput *out;
    int err;

    out = self->out;

    for (item = items; item; item = item->next) {
        if (item->attr && item->attr->is_a_set) {
            switch (item->attr->type) {
            case KND_ATTR_AGGR:
                err = aggr_list_export_GSP(self, item);
                if (err) return err;
                break;
            case KND_ATTR_REF:
                err = ref_list_export_GSP(self, item);
                if (err) return err;
                break;
            default:
                break;
            }
            continue;
        }

        err = out->write(out, "{", 1);
        if (err) return err;
        err = out->write(out, item->name, item->name_size);
        if (err) return err;

        if (item->val_size) {
            err = out->write(out, " ", 1);
            if (err) return err;
            err = out->write(out, item->val, item->val_size);
            if (err) return err;
        }

        if (item->children) {
            err = attr_items_export_GSP(self, item->children, 0);
            if (err) return err;
        }
        err = out->write(out, "}", 1);
        if (err) return err;
    }
    
    return knd_OK;
}

static int export_GSP(struct kndConcept *self)
{
    struct kndAttr *attr;
    struct kndConcItem *item;
    struct kndTranslation *tr;
    struct glbOutput *out = self->out;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. GSP export of \"%.*s\" [%.*s]",
                self->name_size, self->name,
                self->dir->id_size, self->dir->id);

    err = out->writec(out, '{');
    if (err) return err;

    err = out->write(out, self->dir->id, self->dir->id_size);
    if (err) return err;

    err = out->writec(out, ' ');
    if (err) return err;

    err = out->write(out, self->name, self->name_size);
    if (err) return err;

    if (self->tr) {
        err = out->write(out, "[_g", strlen("[_g"));
        if (err) return err;
    
        for (tr = self->tr; tr; tr = tr->next) {
            err = out->write(out, "{", 1);
            if (err) return err;
            err = out->write(out, tr->locale, tr->locale_size);
            if (err) return err;
            err = out->write(out, "{t ", 3);
            if (err) return err;
            err = out->write(out, tr->val, tr->val_size);
            if (err) return err;
            err = out->write(out, "}}", 2);
            if (err) return err;
        }
        err = out->write(out, "]", 1);
        if (err) return err;
    }

    if (self->base_items) {
        err = out->write(out, "[_ci", strlen("[_ci"));
        if (err) return err;

        for (item = self->base_items; item; item = item->next) {
            err = out->writec(out, '{');
            if (err) return err;
            err = out->write(out, item->conc->dir->id, item->conc->dir->id_size);
            if (err) return err;
 
            if (item->attrs) {
              err = attr_items_export_GSP(self, item->attrs, 0);
              if (err) return err;
            }
            err = out->writec(out, '}');
            if (err) return err;

        }
        err = out->writec(out, ']');
        if (err) return err;
    }

    if (self->attrs) {
        for (attr = self->attrs; attr; attr = attr->next) {
            attr->out = self->out;
            attr->format = KND_FORMAT_GSP;
            err = attr->export(attr);
            if (err) return err;
        }
    }

    if (self->dir->descendants) {
        err = kndConcept_export_descendants_GSP(self);                             RET_ERR();
    }

    err = out->writec(out, '}');
    if (err) return err;

    return knd_OK;
}

static int build_class_updates(struct kndConcept *self,
                               struct kndUpdate *update)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size;
    struct glbOutput *out = self->task->update;
    struct kndConcept *c;
    struct kndObject *obj;
    struct kndClassUpdate *class_update;
    int err;

    for (size_t i = 0; i < update->num_classes; i++) {
        class_update = update->classes[i];
        c = class_update->conc;
        c->task = self->task;

        err = out->write(out, "{class ", strlen("{class "));   RET_ERR();
        err = out->write(out, c->name, c->name_size);

        err = out->write(out, "(id ", strlen("(id "));         RET_ERR();
        buf_size = sprintf(buf, "%zu", c->numid);
        err = out->write(out, buf, buf_size);                  RET_ERR();
        
        /* export obj updates */
        for (size_t j = 0; j < class_update->num_objs; j++) {
            obj = class_update->objs[j];

            if (DEBUG_CONC_LEVEL_2)
                knd_log(".. export update of OBJ %.*s", obj->name_size, obj->name);

            err = out->write(out, "{obj ", strlen("{obj "));   RET_ERR();
            err = out->write(out, obj->name, obj->name_size);  RET_ERR();

            if (obj->state->phase == KND_REMOVED) {
                err = out->write(out, "(rm)", strlen("(rm)"));
                if (err) return err;
            }
            err = out->write(out, "}", 1);                     RET_ERR();
        }
        
        /* close class out */
        err = out->write(out, ")}", 2);                        RET_ERR();
    }


    return knd_OK;
}

static int export_updates(struct kndConcept *self,
                          struct kndUpdate *update)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size;
    struct tm tm_info;
    struct glbOutput *out = self->task->update;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. export updates in \"%.*s\"..",
                self->name_size, self->name);

    out->reset(out);
    err = out->write(out, "{task{update", strlen("{task{update"));               RET_ERR();

    localtime_r(&update->timestamp, &tm_info);
    buf_size = strftime(buf, KND_NAME_SIZE,
                        "{_ts %Y-%m-%d %H:%M:%S}", &tm_info);
    err = out->write(out, buf, buf_size);                                        RET_ERR();

    err = out->write(out, "{user", strlen("{user"));                             RET_ERR();

    /* spec body */
    err = out->write(out,
                     self->task->update_spec,
                     self->task->update_spec_size);                              RET_ERR();

    /* state information */
    err = out->write(out, "(state ", strlen("(state "));                         RET_ERR();
    buf_size = sprintf(buf, "%zu", update->numid);
    err = out->write(out, buf, buf_size);                                        RET_ERR();

    if (update->num_classes) {
        err = build_class_updates(self, update);                                 RET_ERR();
    }

    if (self->rel && self->rel->inbox_size) {
        self->rel->out = out;
        err = self->rel->export_updates(self->rel);                              RET_ERR();
    }

    err = out->write(out, ")}}}}", strlen(")}}}}"));                               RET_ERR();
    return knd_OK;
}

static gsl_err_t set_liquid_class_id(void *obj, const char *val, size_t val_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndConcept *c;
    long numval = 0;
    int err;

    if (!val_size) return make_gsl_err(gsl_FORMAT);

    if (!self->curr_class) return make_gsl_err(gsl_FAIL);
    c = self->curr_class;

    err = knd_parse_num((const char*)val, &numval);
    if (err) return make_gsl_err_external(err);

    c->numid = numval;
    if (c->dir) {
        c->dir->numid = numval;
    }

    //self->curr_class->update_id = self->curr_update->id;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. set curr liquid class id: %zu  update id: %zu",
                c->numid, c->numid);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_get_liquid_class(void *obj, const char *name, size_t name_size)
{
    struct kndConcept *self = obj;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    err = get_class(self, name, name_size, &self->curr_class);
    if (err) return make_gsl_err_external(err);
    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_liquid_class_id(void *obj,
                                       const char *rec, size_t *total_size)
{
    struct kndConcept *self = obj;
    struct kndUpdate *update = self->curr_update;
    struct kndConcept *c;
    struct kndClassUpdate *class_update;
    struct kndClassUpdateRef *class_update_ref;
    int err;
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_liquid_class_id,
          .obj = self
        }
    };

    if (!self->curr_class) return make_gsl_err(gsl_FAIL);

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    c = self->curr_class;

    /* register class update */
    err = self->mempool->new_class_update(self->mempool, &class_update);
    if (err) return make_gsl_err_external(err);
    class_update->conc = c;

    update->classes[update->num_classes] = class_update;
    update->num_classes++;

    err = self->mempool->new_class_update_ref(self->mempool, &class_update_ref);
    if (err) return make_gsl_err_external(err);
    class_update_ref->update = update;

    class_update_ref->next = c->updates;
    c->updates =  class_update_ref;
    c->num_updates++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_liquid_class_update(void *obj,
                                           const char *rec, size_t *total_size)
{
    struct kndConcept *self = obj;
    struct kndClassUpdate **class_updates;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log("..  liquid class update REC: \"%.*s\"..", 32, rec); }

    if (!self->curr_update) return make_gsl_err(gsl_FAIL);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_get_liquid_class,
          .obj = self
        },
        { .type = GSL_CHANGE_STATE,
          .name = "id",
          .name_size = strlen("id"),
          .parse = parse_liquid_class_id,
          .obj = self
        }
    };

    /* create index of class updates */
    class_updates = realloc(self->curr_update->classes,
                            (self->inbox_size * sizeof(struct kndClassUpdate*)));
    if (!class_updates) return make_gsl_err_external(knd_NOMEM);
    self->curr_update->classes = class_updates;

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_liquid_rel_update(void *obj,
                                         const char *rec, size_t *total_size)
{
    struct kndConcept *self = obj;
    int err;

    if (!self->curr_update) return make_gsl_err_external(knd_FAIL);

    self->rel->curr_update = self->curr_update;
    err = self->rel->parse_liquid_updates(self->rel, rec, total_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t new_liquid_update(void *obj, const char *val, size_t val_size)
{
    struct kndConcept *self = obj;
    struct kndUpdate *update;
    long numval = 0;
    int err;

    assert(val[val_size] == '\0');

    err = knd_parse_num((const char*)val, &numval);
    if (err) return make_gsl_err_external(err);

    err = self->mempool->new_update(self->mempool, &update);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== new class update: %zu", update->id);

    self->curr_update = update;

    return make_gsl_err(gsl_OK);
}

static int apply_liquid_updates(struct kndConcept *self,
                                const char *rec,
                                size_t *total_size)
{
    struct kndConcept *c;
    struct kndConcDir *dir;
    struct kndRel *rel;
    struct kndStateControl *state_ctrl = self->task->state_ctrl;
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = new_liquid_update,
          .obj = self
        },
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_liquid_class_update,
          .obj = self
        },
        { .name = "rel",
          .name_size = strlen("rel"),
          .parse = parse_liquid_rel_update,
          .obj = self
        }
    };
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CONC_LEVEL_1)
        knd_log("..apply liquid updates..");

    if (self->inbox_size) {
        for (c = self->inbox; c; c = c->next) {
            c->task = self->task;
            c->log = self->log;
            c->frozen_output_file_name = self->frozen_output_file_name;
            c->frozen_output_file_name_size = self->frozen_output_file_name_size;
            c->mempool = self->mempool;

            err = c->resolve(c, NULL);
            if (err) return err;

            err = self->mempool->new_conc_dir(self->mempool, &dir);
            if (err) return err;

            dir->conc = c;
            dir->mempool = self->mempool;

            err = self->class_name_idx->set(self->class_name_idx,
                                       c->name, c->name_size, (void*)dir);
            if (err) return err;
        }
    }

    if (self->rel->inbox_size) {
        for (rel = self->rel->inbox; rel; rel = rel->next) {
            err = rel->resolve(rel);
            if (err) return err;
        }
    }

    parser_err = gsl_parse_task(rec, total_size, specs,
                                sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    if (!self->curr_update) return knd_FAIL;

    err = state_ctrl->confirm(state_ctrl, self->curr_update);
    if (err) return err;

    return knd_OK;
}

static int knd_update_state(struct kndConcept *self)
{
    struct kndConcept *c;
    struct kndStateControl *state_ctrl = self->task->state_ctrl;
    struct kndUpdate *update;
    struct kndClassUpdate *class_update;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("..update state of \"%.*s\"",
                self->name_size, self->name);

    /* new update obj */
    err = self->mempool->new_update(self->mempool, &update);                      RET_ERR();
    update->spec = self->task->spec;
    update->spec_size = self->task->spec_size;

    /* create index of class updates */
    update->classes = calloc(self->inbox_size, sizeof(struct kndClassUpdate*));
    if (!update->classes) return knd_NOMEM;

    /* resolve all refs */
    for (c = self->inbox; c; c = c->next) {
        err = self->mempool->new_class_update(self->mempool, &class_update);      RET_ERR();
        c->task = self->task;
        c->log = self->log;
        c->frozen_output_file_name = self->frozen_output_file_name;
        c->frozen_output_file_name_size = self->frozen_output_file_name_size;
        c->class_idx = self->class_idx;
        c->class_name_idx = self->class_name_idx;

        self->next_numid++;
        c->numid = self->next_numid;
        err = c->resolve(c, class_update);
        if (err) {
            knd_log("-- %.*s class not resolved :(", c->name_size, c->name);
            return err;
        }
        class_update->conc = c;

        if (update->num_classes >= self->inbox_size) {
            knd_log("-- max class updates reached :(");
            return knd_FAIL;
        }

        update->classes[update->num_classes] = class_update;
        update->num_classes++;
        /* stats */
        update->total_objs += class_update->num_objs;
    }

    if (self->rel->inbox_size) {
        err = self->rel->update(self->rel, update);                               RET_ERR();
    }

    if (self->proc->inbox_size) {
        err = self->proc->update(self->proc, update);                             RET_ERR();
    }

    err = state_ctrl->confirm(state_ctrl, update);                                RET_ERR();
    err = export_updates(self, update);                                           RET_ERR();

    return knd_OK;
}

static int restore(struct kndConcept *self)
{
    char state_buf[KND_STATE_SIZE];
    char last_state_buf[KND_STATE_SIZE];
    struct glbOutput *out = self->out;

    const char *inbox_dir = "/schema/inbox";
    size_t inbox_dir_size = strlen(inbox_dir);
    int err;

    memset(state_buf, '0', KND_STATE_SIZE);
    if (DEBUG_CONC_LEVEL_TMP)
        knd_log(".. conc \"%s\" restoring DB state in: %s",
                self->name, self->dbpath, KND_STATE_SIZE);

    out->reset(out);
    err = out->write(out, self->dbpath, self->dbpath_size);
    if (err) return err;
    err = out->write(out, "/schema/class_state.id", strlen("/schema/class_state.id"));
    if (err) return err;

    err = out->write_file_content(out,
                         (const char*)out->buf);
    if (err) {
        knd_log("-- no class_state.id file found, assuming initial state ..");
        return knd_OK;
    }

    /* check if state content is a valid state id */
    err = knd_state_is_valid(out->buf, out->buf_size);
    if (err) {
        knd_log("-- state id is not valid: \"%.*s\"",
                out->buf_size, out->buf);
        return err;
    }

    memcpy(last_state_buf, out->buf, KND_STATE_SIZE);
    if (DEBUG_CONC_LEVEL_TMP)
        knd_log(".. last DB state: \"%.*s\"",
                out->buf_size, out->buf);

    out->rtrim(out, strlen("/schema/class_state.id"));
    err = out->write(out, inbox_dir, inbox_dir_size);
    if (err) return err;
    
    while (1) {
        knd_next_state(state_buf);

        //err = out->write_state_path(out, state_buf);
        //if (err) return err;

        err = out->write(out, "/spec.gsl", strlen("/spec.gsl"));
        if (err) return err;

        
        err = out->write_file_content(out, (const char*)out->buf);
        if (err) {
            knd_log("-- couldn't read GSL spec \"%s\" :(", out->buf);
            return err;
        }

        if (DEBUG_CONC_LEVEL_TMP)
            knd_log(".. state update spec file: \"%.*s\" SPEC: %.*s\n\n",
                    out->buf_size, out->buf, out->buf_size, out->buf);


        /* last update */
        if (!memcmp(state_buf, last_state_buf, KND_STATE_SIZE)) break;

        /* cut the tail */
        out->rtrim(out, strlen("/spec.gsl") + (KND_STATE_SIZE * 2));
    }
    return knd_OK;
}

static int export(struct kndConcept *self)
{
    switch(self->format) {
    case KND_FORMAT_JSON:
        return export_JSON(self);
        /*case KND_FORMAT_HTML:
        return kndConcept_export_HTML(self);
    case KND_FORMAT_GSL:
    return kndConcept_export_GSL(self); */
    default:
        break;
    }

    knd_log("-- format %d not supported :(", self->format);
    return knd_FAIL;
}
 
static int freeze_objs(struct kndConcept *self,
                       size_t *total_frozen_size,
                       char *output,
                       size_t *total_size)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size;
    char *curr_dir = output;
    size_t init_dir_size = 0;
    size_t curr_dir_size = 0;
    struct kndObject *obj;
    struct kndObjEntry *entry;
    struct glbOutput *out;
    const char *key;
    void *val;
    size_t chunk_size;
    size_t num_size;
    size_t obj_block_size = 0;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. freezing objs of class \"%.*s\", total:%zu  valid:%zu",
                self->name_size, self->name,
                self->dir->obj_name_idx->size, self->dir->num_objs);

    out = self->out;
    out->reset(out);
    self->dir_out->reset(self->dir_out);

    err = self->dir_out->write(self->dir_out, "[o", 2);                                    RET_ERR();
    init_dir_size = self->dir_out->buf_size;

    key = NULL;
    self->dir->obj_name_idx->rewind(self->dir->obj_name_idx);
    do {
        self->dir->obj_name_idx->next_item(self->dir->obj_name_idx, &key, &val);
        if (!key) break;
        entry = (struct kndObjEntry*)val;
        obj = entry->obj;

        if (obj->state->phase != KND_CREATED) {
            knd_log("NB: skip freezing \"%.*s\"   phase: %d",
                    obj->name_size, obj->name, obj->state->phase);
            continue;
        }
        obj->out = out;
        obj->format = KND_FORMAT_GSP;
        obj->depth = self->depth + 1;

        if (DEBUG_CONC_LEVEL_2) {
            obj->str(obj);
        }

        err = obj->export(obj);
        if (err) {
            knd_log("-- couldn't export GSP of the \"%.*s\" obj :(",
                    obj->name_size, obj->name);
            return err;
        }

        err = self->dir_out->writec(self->dir_out, ' ');
        if (err) return err;
        
        buf_size = 0;
        knd_num_to_str(obj->frozen_size, buf, &buf_size, KND_RADIX_BASE);
        err = self->dir_out->write(self->dir_out, buf, buf_size);
        if (err) return err;

        if (DEBUG_CONC_LEVEL_2)
            knd_log("OBJ body size: %zu [%.*s]",
                    obj->frozen_size, buf_size, buf);

        /* OBJ persistent write */
        if (out->buf_size > out->threshold) {
            err = knd_append_file(self->frozen_output_file_name,
                                  out->buf, out->buf_size);
            if (err) return err;

            *total_frozen_size += out->buf_size;
            obj_block_size += out->buf_size;
            out->reset(out);
        }
    } while (key);

    /* no objs written? */
    if (self->dir_out->buf_size == init_dir_size) {
        *total_frozen_size = 0;
        *total_size = 0;
        return knd_OK;
    }
    
    /* final chunk to write */
    if (self->out->buf_size) {
        err = knd_append_file(self->frozen_output_file_name,
                              out->buf, out->buf_size);                           RET_ERR();
        *total_frozen_size += out->buf_size;
        obj_block_size += out->buf_size;
        out->reset(out);
    }

    /* close directory */
    err = self->dir_out->write(self->dir_out, "]", 1);                            RET_ERR();

    /* obj directory size */
    buf_size = sprintf(buf, "%lu", (unsigned long)self->dir_out->buf_size);

    err = self->dir_out->write(self->dir_out, "{L ", strlen("{L "));
    if (err) return err;
    err = self->dir_out->write(self->dir_out, buf, buf_size);
    if (err) return err;
    err = self->dir_out->write(self->dir_out, "}", 1);
    if (err) return err;

    /* persistent write of directory */
    err = knd_append_file(self->frozen_output_file_name,
                          self->dir_out->buf, self->dir_out->buf_size);
    if (err) return err;

    *total_frozen_size += self->dir_out->buf_size;
    obj_block_size += self->dir_out->buf_size;

    /* update class dir entry */
    chunk_size = strlen("{O");
    memcpy(curr_dir, "{O", chunk_size); 
    curr_dir += chunk_size;
    curr_dir_size += chunk_size;

    num_size = sprintf(curr_dir, "{size %lu}",
                       (unsigned long)obj_block_size);
    curr_dir +=      num_size;
    curr_dir_size += num_size;

    num_size = sprintf(curr_dir, "{tot %lu}",
                       (unsigned long)self->dir->num_objs);
    curr_dir +=      num_size;
    curr_dir_size += num_size;

    memcpy(curr_dir, "}", 1); 
    curr_dir++;
    curr_dir_size++;

    
    *total_size = curr_dir_size;
    
    return knd_OK;
}

static int freeze_subclasses(struct kndConcept *self,
                             size_t *total_frozen_size,
                             char *output,
                             size_t *total_size)
{
    char buf[KND_SHORT_NAME_SIZE] = {0};
    size_t buf_size;
    struct kndConcept *c;
    struct kndConcRef *ref;
    char *curr_dir = output;
    size_t curr_dir_size = 0;
    size_t chunk_size;
    int err;

    chunk_size = strlen("[c");
    memcpy(curr_dir, "[c", chunk_size); 
    curr_dir += chunk_size;
    curr_dir_size += chunk_size;

    for (size_t i = 0; i < self->num_children; i++) {
        ref = &self->children[i];
        c = ref->dir->conc;
        c->out = self->out;
        c->dir_out = self->dir_out;
        c->frozen_output_file_name = self->frozen_output_file_name;

        err = c->freeze(c);
        if (err) return err;
        if (!c->frozen_size) {
            knd_log("-- empty GSP in %.*s?", c->name_size, c->name);
            continue;
        }
        
        /* terminal class */
        if (c->is_terminal) {
            self->num_terminals++;
        } else {
            self->num_terminals += c->num_terminals;
        }

        if (DEBUG_CONC_LEVEL_2)
            knd_log("     OUT: \"%.*s\" [%zu]\nclass \"%.*s\""
                    " id:%.*s [frozen size: %lu]\n",
                    c->out->buf_size, c->out->buf, c->out->buf_size,
                    c->name_size, c->name, c->dir->id_size, c->dir->id,
                    (unsigned long)c->frozen_size);
        memcpy(curr_dir, " ", 1);
        curr_dir++;
        curr_dir_size++;
        
        buf_size = 0;
        knd_num_to_str(c->frozen_size, buf, &buf_size, KND_RADIX_BASE);
        memcpy(curr_dir, buf, buf_size);
        curr_dir      += buf_size;
        curr_dir_size += buf_size;
        
        *total_frozen_size += c->frozen_size;
    }

    /* close the list of children */
    memcpy(curr_dir, "]", 1);
    curr_dir++;
    curr_dir_size++;

    *total_size = curr_dir_size;
    return knd_OK;
}

static int freeze_rels(struct kndRel *self,
                       size_t *total_frozen_size,
                       char *output,
                       size_t *total_size)
{
    struct kndRel *rel;
    struct kndRelDir *dir;
    const char *key;
    void *val;
    char *curr_dir = output;
    size_t curr_dir_size = 0;
    size_t chunk_size;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. freezing rels..");

    key = NULL;
    self->rel_name_idx->rewind(self->rel_name_idx);
    do {
        self->rel_name_idx->next_item(self->rel_name_idx, &key, &val);
        if (!key) break;

        dir = (struct kndRelDir*)val;
        rel = dir->rel;

        rel->out = self->out;
        rel->dir_out = self->dir_out;
        rel->frozen_output_file_name = self->frozen_output_file_name;

        err = rel->freeze(rel, total_frozen_size, curr_dir, &chunk_size);
        if (err) {
            knd_log("-- couldn't freeze the \"%s\" rel :(", rel->name);
            return err;
        }
        curr_dir +=      chunk_size;
        curr_dir_size += chunk_size;
    } while (key);

    *total_size = curr_dir_size;

    return knd_OK;
}

static int freeze(struct kndConcept *self)
{
    char *curr_dir = self->dir_buf;
    size_t curr_dir_size = 0;
    size_t total_frozen_size = 0;
    size_t num_size;
    size_t chunk_size;
    int err;
 
    self->out->reset(self->out);

    /* class self presentation */
    err = export_GSP(self);
    if (err) {
        knd_log("-- GSP export failed :(");
        return err;
    }

    /* persistent write */
    err = knd_append_file(self->frozen_output_file_name,
                          self->out->buf, self->out->buf_size);                   RET_ERR();

    total_frozen_size = self->out->buf_size;

    /* no dir entry necessary */
    if (!self->num_children) {
        self->is_terminal = true;
        if (!self->dir) {
            self->frozen_size = total_frozen_size;
            return knd_OK;
        }
        if (!self->dir->obj_name_idx) {
            self->frozen_size = total_frozen_size;
            return knd_OK;
        }
    }

    /* class dir entry */
    chunk_size = strlen("{C ");
    memcpy(curr_dir, "{C ", chunk_size); 
    curr_dir += chunk_size;
    curr_dir_size += chunk_size;

    num_size = sprintf(curr_dir, "%zu}", total_frozen_size);
    curr_dir +=      num_size;
    curr_dir_size += num_size;

    /* any instances to freeze? */
    if (self->dir && self->dir->num_objs) {
        err = freeze_objs(self, &total_frozen_size, curr_dir, &chunk_size);       RET_ERR();
        curr_dir +=      chunk_size;
        curr_dir_size += chunk_size;
    }

    if (self->num_children) {
        err = freeze_subclasses(self, &total_frozen_size,
                                curr_dir, &chunk_size);                           RET_ERR();
        curr_dir +=      chunk_size;
        curr_dir_size += chunk_size;
    }

    /* rels */
    if (self->rel && self->rel->rel_name_idx->size) {
        chunk_size = strlen("[R");
        memcpy(curr_dir, "[R", chunk_size); 
        curr_dir += chunk_size;
        curr_dir_size += chunk_size;

        chunk_size = 0;
        self->rel->out = self->out;
        self->rel->dir_out = self->dir_out;
        self->rel->frozen_output_file_name = self->frozen_output_file_name;

        err = freeze_rels(self->rel, &total_frozen_size,
                          curr_dir, &chunk_size);                                  RET_ERR();
        curr_dir +=      chunk_size;
        curr_dir_size += chunk_size;

        memcpy(curr_dir, "]", 1); 
        curr_dir++;
        curr_dir_size++;
    }

    /* procs */
    if (self->proc && self->proc->proc_name_idx->size) {
        self->proc->out = self->out;
        self->proc->dir_out = self->dir_out;
        self->proc->frozen_output_file_name = self->frozen_output_file_name;

        err = self->proc->freeze_procs(self->proc,
                                       &total_frozen_size,
                                       curr_dir, &chunk_size);                    RET_ERR();
        curr_dir +=      chunk_size;
        curr_dir_size += chunk_size;
    }
    
    if (DEBUG_CONC_LEVEL_2)
        knd_log("== %.*s (%.*s)   DIR: \"%.*s\"   [%lu]",
                self->name_size, self->name, self->dir->id_size, self->dir->id,
                curr_dir_size,
                self->dir_buf, (unsigned long)curr_dir_size);

    num_size = sprintf(curr_dir, "{L %lu}",
                       (unsigned long)curr_dir_size);
    curr_dir_size += num_size;

    err = knd_append_file(self->frozen_output_file_name,
                          self->dir_buf, curr_dir_size);
    if (err) return err;

    total_frozen_size += curr_dir_size;
    self->frozen_size = total_frozen_size;

    return knd_OK;
}

/*  Concept initializer */
extern void kndConcept_init(struct kndConcept *self)
{
    self->del = kndConcept_del;
    self->str = str;
    self->open = open_frozen_DB;
    self->load = read_GSL_file;
    self->read = read_GSP;
    self->read_obj_entry = read_obj_entry;
    self->reset_inbox = reset_inbox;
    self->restore = restore;
    self->select_delta = select_delta;
    self->coordinate = coordinate;
    self->resolve = resolve_refs;

    self->import = parse_import_class;
    self->sync = parse_sync_task;
    self->freeze = freeze;
    self->select = parse_select_class;

    self->update_state = knd_update_state;
    self->apply_liquid_updates = apply_liquid_updates;
    self->export = export;
    self->get = get_class;
    self->get_obj = get_obj;
    self->get_attr = get_attr;
}

extern int
kndConcept_new(struct kndConcept **c, struct kndMemPool *mempool)
{
    struct kndConcept *self;

    if (mempool) {
        self = malloc(sizeof(struct kndConcept));
        self->mempool = mempool;
    } else {
        self = malloc(sizeof(struct kndConcept));
        if (!self) return knd_NOMEM;
        memset(self, 0, sizeof(struct kndConcept));
    }

    kndConcept_init(self);
    *c = self;
    return knd_OK;
}
