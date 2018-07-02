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
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_class.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_object.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
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

static int get_arg_value(struct kndAttrVar *src,
                         struct kndAttrVar *query,
                         struct kndProcCallArg *arg);

static int build_attr_name_idx(struct kndClass *self);
static gsl_err_t confirm_class_var(void *obj, const char *name, size_t name_size);
static gsl_err_t confirm_attr_var(void *obj, const char *name, size_t name_size);

static gsl_err_t set_class_var(void *obj, const char *name, size_t name_size);

static gsl_err_t validate_attr_var_list(void *obj,
                                         const char *name, size_t name_size,
                                         const char *rec, size_t *total_size);
static gsl_err_t attr_var_append(void *accu,
                                  void *item);

static int unfreeze_class(struct kndClass *self,
                          struct kndClassEntry *entry,
                          struct kndClass **result);

static int read_obj_entry(struct kndClass *self,
                          struct kndObjEntry *entry,
                          struct kndObject **result);

static int get_class(struct kndClass *self,
                     const char *name, size_t name_size,
                     struct kndClass **result);

static int get_dir_trailer(struct kndClass *self,
                           struct kndClassEntry *parent_entry,
                           int fd,
                           int encode_base);
static int parse_dir_trailer(struct kndClass *self,
                             struct kndClassEntry *parent_entry,
                             int fd,
                             int encode_base);
static int get_obj_dir_trailer(struct kndClass *self,
                               struct kndClassEntry *parent_entry,
                               int fd,
                               int encode_base);
static gsl_err_t set_class_name(void *obj, const char *name, size_t name_size);

static int freeze(struct kndClass *self);

static int read_GSL_file(struct kndClass *self,
                         struct kndConcFolder *parent_folder,
                         const char *filename,
                         size_t filename_size);

static int get_attr(struct kndClass *self,
                    const char *name, size_t name_size,
                    struct kndAttr **result);

static int attr_vars_export_GSP(struct kndClass *self,
                                 struct kndAttrVar *items,
                                 size_t depth);
static gsl_err_t read_nested_attr_var(void *obj,
                                       const char *name, size_t name_size,
                                       const char *rec, size_t *total_size);
static gsl_err_t import_nested_attr_var(void *obj,
                                         const char *name, size_t name_size,
                                         const char *rec, size_t *total_size);
static void append_attr_var(struct kndClassVar *ci,
                             struct kndAttrVar *attr_var);

static int str_conc_elem(void *obj,
                         const char *elem_id,
                         size_t elem_id_size,
                         size_t count,
                         void *elem);

static void reset_inbox(struct kndClass *self)
{
    struct kndClass *c, *next_c;
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

static void del_class_entry(struct kndClassEntry *entry)
{
    struct kndClassEntry *subentry;
    size_t i;

    for (i = 0; i < entry->num_children; i++) {
        subentry = entry->children[i];
        if (!subentry) continue;
        del_class_entry(subentry);
        entry->children[i] = NULL;
    }

    if (entry->children) {
        free(entry->children);
        entry->children = NULL;
    }

    if (entry->obj_name_idx) {
        entry->obj_name_idx->del(entry->obj_name_idx);
        entry->obj_name_idx = NULL;
    }

    if (entry->rels) {
        free(entry->rels);
        entry->rels = NULL;
    }
}

/*  class destructor */
static void kndClass_del(struct kndClass *self)
{
    if (self->attr_name_idx) self->attr_name_idx->del(self->attr_name_idx);
    if (self->entry) del_class_entry(self->entry);
}

static void str_attr_vars(struct kndAttrVar *items, size_t depth)
{
    struct kndAttrVar *item;
    struct kndAttrVar *list_item;
    const char *classname = "None";
    size_t classname_size = strlen("None");
    struct kndClass *c;
    size_t count = 0;

    for (item = items; item; item = item->next) {
        if (item->attr && item->attr->parent_class) {
            c = item->attr->parent_class;
            classname = c->entry->name;
            classname_size = c->entry->name_size;
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
                knd_log("%*s%zu)  val:%.*s",
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

                if (list_item->children) {
                    str_attr_vars(list_item->children, depth + 1);
                }
                
            }
            knd_log("%*s]", depth * KND_OFFSET_SIZE, "");
            continue;
        }

        knd_log("%*s_attr: \"%.*s\" (base: %.*s)  => %.*s",
                depth * KND_OFFSET_SIZE, "",
                item->name_size, item->name,
                classname_size, classname,
                item->val_size, item->val);

        if (item->children) {
            str_attr_vars(item->children, depth + 1);
        }
    }
}

static void str(struct kndClass *self)
{
    struct kndAttr *attr;
    struct kndAttrEntry *attr_entry;
    struct kndTranslation *tr, *t;
    struct kndClassVar *item;
    struct kndClass *c;
    struct ooDict *idx;
    struct kndSet *set;
    const char *name;
    size_t name_size;
    char resolved_state = '-';
    const char *key;
    void *val;
    int err;

    knd_log("\n%*s{class %.*s    id:%.*s numid:%zu  resolved:%d",
            self->depth * KND_OFFSET_SIZE, "",
            self->entry->name_size, self->entry->name,
            self->entry->id_size, self->entry->id,
            self->entry->numid, self->is_resolved);

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

    if (self->num_baseclass_vars) {
        for (item = self->baseclass_vars; item; item = item->next) {
            resolved_state = '-';

            name = item->entry->name;
            name_size = item->entry->name_size;

            knd_log("%*s_base \"%.*s\" id:%.*s numid:%zu [%c]",
                    (self->depth + 1) * KND_OFFSET_SIZE, "",
                    name_size, name,
                    item->id_size, item->id, item->numid,
                    resolved_state);

            if (item->attrs) {
                str_attr_vars(item->attrs, self->depth + 1);
            }
        }
    }

    if (self->implied_attr) {
        knd_log("%*simplied attr: %.*s",
                (self->depth + 1) * KND_OFFSET_SIZE, "",
                self->implied_attr->name_size, self->implied_attr->name);
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

    for (size_t i = 0; i < self->entry->num_children; i++) {
        c = self->entry->children[i]->class;
        if (!c) continue;

        knd_log("%*sbase of --> %.*s [%zu]",
                (self->depth + 1) * KND_OFFSET_SIZE, "",
                c->name_size, c->name, c->entry->num_terminals);
        //c->str(c);
    }

    if (self->entry->descendants) {
        /*struct kndFacet *f;
        set = self->entry->descendants;
        knd_log("%*sdescendants [total: %zu]:",
                (self->depth + 1) * KND_OFFSET_SIZE, "",
                set->num_elems);
        for (size_t i = 0; i < set->num_facets; i++) {
            f = set->facets[i];
            knd_log("%*s  facet: %.*s",
                    (self->depth + 1) * KND_OFFSET_SIZE, "",
                    f->attr->name_size, f->attr->name);
                    } */

        if (DEBUG_CONC_LEVEL_2) {
            err = set->map(set, str_conc_elem, NULL);
            if (err) return;
        }
    }

    if (self->entry->reverse_attr_name_idx) {
        idx = self->entry->reverse_attr_name_idx;
        key = NULL;
        idx->rewind(idx);
        do {
            idx->next_item(idx, &key, &val);
            if (!key) break;
            set = val;
            knd_log("%s total:%zu", key, set->num_elems);
            //set->str(set, self->depth + 1);
        } while (key);
    }

    knd_log("%*s}", self->depth * KND_OFFSET_SIZE, "");
}

static gsl_err_t alloc_gloss_item(void *obj,
                                  const char *name,
                                  size_t name_size,
                                  size_t count,
                                  void **item)
{
    struct kndClass *self = obj;
    struct kndTranslation *tr;

    assert(name == NULL && name_size == 0);

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. %.*s: allocate gloss translation,  count: %zu",
                self->entry->name_size, self->entry->name, count);

    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return make_gsl_err_external(knd_NOMEM);

    memset(tr, 0, sizeof(struct kndTranslation));
    *item = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t append_gloss_item(void *accu,
                                   void *item)
{
    struct kndClass *self =   accu;
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
    struct kndClass *self = obj;

    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .alloc = alloc_gloss_item,
        .append = append_gloss_item,
        .accu = self,
        .parse = parse_gloss_item
    };

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. %.*s: reading gloss",
                self->entry->name_size, self->entry->name);

    return gsl_parse_array(&item_spec, rec, total_size);
}


static gsl_err_t alloc_summary_item(void *obj,
                                    const char *name,
                                    size_t name_size,
                                    size_t count,
                                    void **item)
{
    struct kndClass *self = obj;
    struct kndTranslation *tr;

    assert(name == NULL && name_size == 0);

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. %.*s: allocate summary translation,  count: %zu",
                self->entry->name_size, self->entry->name, count);

    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return make_gsl_err_external(knd_NOMEM);

    memset(tr, 0, sizeof(struct kndTranslation));
    *item = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t append_summary_item(void *accu,
                                   void *item)
{
    struct kndClass *self =   accu;
    struct kndTranslation *tr = item;

    tr->next = self->summary;
    self->summary = tr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_summary_item(void *obj,
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
        knd_log(".. read summary translation: \"%.*s\",  text: \"%.*s\"",
                tr->locale_size, tr->locale, tr->val_size, tr->val);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_summary_array(void *obj,
                                     const char *rec,
                                     size_t *total_size)
{
    struct kndClass *self = obj;

    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .alloc = alloc_summary_item,
        .append = append_summary_item,
        .accu = self,
        .parse = parse_summary_item
    };

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. %.*s: reading summary",
                self->entry->name_size, self->entry->name);

    return gsl_parse_array(&item_spec, rec, total_size);
}

static int inherit_attrs(struct kndClass *self, struct kndClass *base)
{
    struct kndClassEntry *entry;
    struct kndAttr *attr;
    struct kndClass *c;
    struct kndAttrEntry *attr_entry;
    struct kndClassVar *item;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. \"%.*s\" class to inherit attrs from \"%.*s\"..",
                self->entry->name_size, self->entry->name, base->name_size, base->name);

    if (!base->is_resolved) {
        err = base->resolve(base, NULL);                                          RET_ERR();
    }

    /* check circled relations */
    for (size_t i = 0; i < self->num_bases; i++) {
        entry = self->bases[i];
        c = entry->class;
        if (DEBUG_CONC_LEVEL_2)
            knd_log("== (%zu of %zu)  \"%.*s\" is a base of \"%.*s\"",
                    i, self->num_bases, c->name_size, c->name,
                    self->entry->name_size, self->entry->name);
        if (entry->class == base) {
            knd_log("-- circle inheritance detected for \"%.*s\" :(",
                    base->name_size, base->name);
            return knd_FAIL;
        }
    }

    /* get attrs from base */
    for (attr = base->attrs; attr; attr = attr->next) {
        /* compare with exiting attrs */
        attr_entry = self->attr_name_idx->get(self->attr_name_idx,
                                              attr->name, attr->name_size);
        if (attr_entry) {
            knd_log("-- %.*s attr collision between \"%.*s\" and base class \"%.*s\"?",
                    attr_entry->name_size, attr_entry->name,
                    self->entry->name_size, self->entry->name,
                    base->name_size, base->name);
            return knd_FAIL;
        }

        /* register attr entry */
        attr_entry = malloc(sizeof(struct kndAttrEntry));
        if (!attr_entry) return knd_NOMEM;
        memset(attr_entry, 0, sizeof(struct kndAttrEntry));
        memcpy(attr_entry->name, attr->name, attr->name_size);
        attr_entry->name_size = attr->name_size;
        attr_entry->attr = attr;

        err = self->attr_name_idx->set(self->attr_name_idx,
                                       attr_entry->name, attr_entry->name_size,
                                       (void*)attr_entry);
        if (err) return err;

        /* computed attrs */
        if (attr->proc) {
            self->computed_attrs[self->num_computed_attrs] = attr;
            self->num_computed_attrs++;
        }
    }

    if (self->num_bases >= KND_MAX_BASES) {
        knd_log("-- max bases exceeded for %.*s :(",
                self->entry->name_size, self->entry->name);

        return knd_FAIL;
    }
    self->bases[self->num_bases] = base->entry;
    self->num_bases++;

    if (DEBUG_CONC_LEVEL_1)
        knd_log(" .. add %.*s parent to %.*s",
                base->entry->class->name_size,
                base->entry->class->name,
                self->entry->name_size, self->entry->name);

    /* contact the grandparents */
    for (item = base->baseclass_vars; item; item = item->next) {
        err = inherit_attrs(self, item->entry->class);                            RET_ERR();
    }

    return knd_OK;
}

static int is_base(struct kndClass *self,
                   struct kndClass *child)
{
    if (DEBUG_CONC_LEVEL_2) {
        knd_log(".. check inheritance: %.*s [resolved: %d] => %.*s [resolved:%d]?",
                child->name_size, child->name, child->is_resolved,
                self->entry->name_size, self->entry->name, self->is_resolved);
    }

    /* make sure that a child inherits from the base */
    for (size_t i = 0; i < child->num_bases; i++) {
        if (child->bases[i]->class == self) {
            return knd_OK;
        }
    }

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log("-- no inheritance from  \"%.*s\" to \"%.*s\" :(",
                self->entry->name_size, self->entry->name,
                child->name_size, child->name);
    return knd_FAIL;
}

static int index_attr(struct kndClass *self,
                      struct kndAttr *attr,
                      struct kndAttrVar *item)
{
    struct kndClass *base;
    struct kndSet *set;
    struct kndClass *c = NULL;
    int err;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log("\n.. indexing CURR CLASS: \"%.*s\" .. index attr: \"%.*s\" [type:%d]"
                " refclass: \"%.*s\" (name:%.*s val:%.*s)",
                self->entry->name_size, self->entry->name,
                attr->name_size, attr->name, attr->type,
                attr->ref_classname_size, attr->ref_classname,
                item->name_size, item->name, item->val_size, item->val);
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
    err = get_class(self,
                    item->val,
                    item->val_size, &c);                                          RET_ERR();

    item->class = c;

    if (!c->is_resolved) {
        err = c->resolve(c, NULL);                                                RET_ERR();
    }
    err = is_base(base, c);                                                       RET_ERR();

    set = attr->parent_class->entry->descendants;

    /* add curr class to the reverse index */
    err = set->add_ref(set, attr, self->entry, c->entry);
    if (err) return err;

    return knd_OK;
}


static int index_attr_var_list(struct kndClass *self,
                                struct kndAttr *attr,
                                struct kndAttrVar *parent_item)
{
    struct kndClass *base;
    struct kndSet *set;
    struct kndClass *c = NULL;
    struct kndAttrVar *item = parent_item;
    int err;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log("\n.. attr item list indexing.. (class:%.*s) .. index attr: \"%.*s\" [type:%d]"
                " refclass: \"%.*s\" (name:%.*s val:%.*s)",
                self->entry->name_size, self->entry->name,
                attr->name_size, attr->name, attr->type,
                attr->ref_classname_size, attr->ref_classname,
                item->name_size, item->name, item->val_size, item->val);
    }

    if (!attr->ref_classname_size) return knd_OK;
    if (!parent_item->val_size) return knd_OK;

    /* template base class */
    err = get_class(self,
                    attr->ref_classname,
                    attr->ref_classname_size,
                    &base);                                                       RET_ERR();
    if (!base->is_resolved) {
        err = base->resolve(base, NULL);                                          RET_ERR();
    }

    /* specific class */
    err = get_class(self,
                    item->val,
                    item->val_size, &c);                                          RET_ERR();

    item->class = c;

    if (!c->is_resolved) {
        err = c->resolve(c, NULL);                                                RET_ERR();
    }
    err = is_base(base, c);                                                       RET_ERR();

    set = attr->parent_class->entry->descendants;

    /* add curr class to the reverse index */
    err = set->add_ref(set, attr, self->entry, c->entry);
    if (err) return err;

    return knd_OK;
}

static int resolve_class_ref(struct kndClass *self,
                             const char *name, size_t name_size,
                             struct kndClass *base,
                             struct kndClass **result)
{
    struct kndClassEntry *entry;
    struct kndClass *c;
    struct ooDict *class_name_idx = self->entry->repo->root_class->class_name_idx;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. checking class ref:  %.*s", name_size, name);

    entry = class_name_idx->get(class_name_idx, name, name_size);
    if (!entry) {
        knd_log("-- no such class: \"%.*s\"", name_size, name);
        //return knd_OK;
        return knd_FAIL;
    }

    /* class could be frozen */
    if (!entry->class) {
        if (!entry->block_size) return knd_FAIL;
        err = unfreeze_class(self, entry, &entry->class);
        if (err) return err;
    }
    c = entry->class;

    if (!c->is_resolved) {
        err = c->resolve(c, NULL);                                                RET_ERR();
    }

    if (base) {
        err = is_base(base, c);                                                   RET_ERR();
    }

    *result = c;
    return knd_OK;
}

static int resolve_proc_ref(struct kndClass *self,
                             const char *name, size_t name_size,
                             struct kndProc *base __attribute__((unused)),
                             struct kndProc **result)
{
    struct kndProc *root_proc, *proc;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. resolving proc ref:  %.*s", name_size, name);

    root_proc = self->root_class->proc;
    err = root_proc->get_proc(root_proc,
                              name, name_size, &proc);                            RET_ERR();

    /*c = dir->conc;
    if (!c->is_resolved) {
        err = c->resolve(c, NULL);                                                RET_ERR();
    }

    if (base) {
        err = is_base(base, c);                                                   RET_ERR();
    }
    */

    *result = proc;

    return knd_OK;
}

static int resolve_aggr_item(struct kndClass *self,
                             struct kndAttrVar *parent_item)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct kndClass *c;
    struct kndAttrVar *item;
    struct kndAttr *attr;
    const char *classname;
    size_t classname_size;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. resolve aggr item %.*s  attr:%p",
                parent_item->name_size, parent_item->name,
                parent_item->attr);

    if (!parent_item->attr->conc) {
        err = resolve_class_ref(self,
                                parent_item->attr->ref_classname,
                                parent_item->attr->ref_classname_size,
                                NULL, &parent_item->attr->conc);
        if (err) return err;
    }
    c = parent_item->attr->conc;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log(".. resolving aggr item \"%.*s\" (count:%zu)"
                " class:%.*s  is_list_item:%d",
                parent_item->name_size,  parent_item->name,
                parent_item->list_count,
                c->name_size, c->name, parent_item->is_list_item);
    }

    classname = parent_item->val;
    classname_size = parent_item->val_size;
    if (parent_item->is_list_item) {
        classname = parent_item->name;
        classname_size = parent_item->name_size;
    }

    if (DEBUG_CONC_LEVEL_2)
        c->str(c);

    if (c->implied_attr) {
        attr = c->implied_attr;

        if (DEBUG_CONC_LEVEL_2)
            knd_log("== class: \"%.*s\" implied attr: %.*s",
                    classname_size, classname,
                    attr->name_size, attr->name);

        parent_item->implied_attr = attr;

        switch (attr->type) {
        case KND_ATTR_NUM:

            if (DEBUG_CONC_LEVEL_2)
                knd_log(".. resolving implied num attr: %.*s val:%.*s",
                        parent_item->name_size, parent_item->name,
                        parent_item->val_size, parent_item->val);

            if (parent_item->val_size) {
                memcpy(buf, parent_item->val, parent_item->val_size);
                buf_size = parent_item->val_size;
                buf[buf_size] = '\0';

                err = knd_parse_num(buf, &parent_item->numval);
                // TODO: float parsing
            }
            break;
        case KND_ATTR_AGGR:
            break;
        case KND_ATTR_REF:
            err = resolve_class_ref(self,
                                    classname, classname_size,
                                    attr->conc, &parent_item->class);
            if (err) return err;
            break;
        default:
            break;
        }
    }

    /* resolve nested children */
    for (item = parent_item->children; item; item = item->next) {
        if (DEBUG_CONC_LEVEL_2) {
            knd_log(".. check attr \"%.*s\" in class \"%.*s\" "
                    " is_resolved:%d",
                    item->name_size, item->name,
                    c->name_size, c->name, c->is_resolved);
        }

        err = get_attr(c, item->name, item->name_size, &attr);
        if (err) {
            knd_log("-- no attr \"%.*s\" in class \"%.*s\" :(",
                    item->name_size, item->name,
                    c->name_size, c->name);
            return err;
        }
        item->attr = attr;

        switch (attr->type) {
        case KND_ATTR_NUM:
            
            if (DEBUG_CONC_LEVEL_2)
                knd_log(".. resolving default num attr: %.*s val:%.*s",
                        item->name_size, item->name,
                        item->val_size, item->val);

            memcpy(buf, item->val, item->val_size);
            buf_size = item->val_size;
            buf[buf_size] = '\0';
            err = knd_parse_num(buf, &item->numval);

            break;
        case KND_ATTR_AGGR:
            if (DEBUG_CONC_LEVEL_2)
                knd_log("== nested aggr item found: %.*s conc:%p",
                        item->name_size, item->name, attr->conc);
            err = resolve_aggr_item(self, item);
            if (err) return err;
            break;
        case KND_ATTR_REF:

            classname = item->val;
            classname_size = item->val_size;

            err = resolve_class_ref(self,
                                    classname, classname_size,
                                    attr->conc, &item->class);
            if (err) return err;

            break;
        default:
            break;
        }
    }

    return knd_OK;
}

static int resolve_attr_var_list(struct kndClass *self,
                                  struct kndAttrVar *parent_item)
{
    struct kndAttr *parent_attr = parent_item->attr;
    struct kndAttrVar *item;
    struct kndClass *c;
    int err;

    if (DEBUG_CONC_LEVEL_2) {
        const char *attr_type_name = knd_attr_names[parent_attr->type];
        size_t attr_type_name_size = strlen(attr_type_name);
        knd_log(".. class %.*s to resolve attr item list \"%.*s\" "
                " of CLASS:%.*s   attr type:%.*s",
                self->entry->name_size, self->entry->name,
                parent_item->name_size, parent_item->name,
                parent_attr->ref_classname_size,
                parent_attr->ref_classname,
                attr_type_name_size, attr_type_name);
    }

    if (!parent_attr->conc) {
        err = resolve_class_ref(self,
                                parent_attr->ref_classname,
                                parent_attr->ref_classname_size,
                                NULL, &parent_attr->conc);
        if (err) return err;
    }

    /* base template class */
    c = parent_attr->conc;
    if (!c->is_resolved) {
        err = c->resolve(c, NULL);                                                RET_ERR();
    }

    if (DEBUG_CONC_LEVEL_2)
        c->str(c);

    /* first item */
    if (parent_item->val_size) {
        parent_item->attr = parent_attr;

        switch (parent_attr->type) {
        case KND_ATTR_AGGR:
            err = resolve_aggr_item(self, parent_item);
            if (err) {
                knd_log("-- first aggr item not resolved :(");
                return err;
            }
            break;
        case KND_ATTR_REF:
            err = resolve_class_ref(self,
                                    parent_item->val, parent_item->val_size,
                                    c, &parent_item->class);
            if (err) return err;
            break;
        default:
            break;
        }
    }

    for (item = parent_item->list; item; item = item->next) {
        item->attr = parent_attr;


        switch (parent_attr->type) {
        case KND_ATTR_AGGR:
            err = resolve_aggr_item(self, item);
            if (err) return err;
            break;
        case KND_ATTR_REF:

            if (item->val_size) {
                err = resolve_class_ref(self, item->val, item->val_size,
                                        c, &item->class);
                if (err) return err;
            } else {
                err = resolve_class_ref(self, item->name, item->name_size,
                                        c, &item->class);
                if (err) return err;
            }

            break;
        default:
            break;
        }
    }

    return knd_OK;
}

static int resolve_attr_vars(struct kndClass *self,
                              struct kndClassVar *parent_item)
{
    struct kndAttrVar *cvar;
    struct kndAttrEntry *entry;
    struct kndClass *c;
    struct kndProc *proc;
    struct ooDict *attr_name_idx = self->attr_name_idx;
    int err;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log("\n.. resolving attr vars of class %.*s",
                self->entry->name_size, self->entry->name);
    }

    for (cvar = parent_item->attrs; cvar; cvar = cvar->next) {

        entry = attr_name_idx->get(attr_name_idx,
                                   cvar->name, cvar->name_size);
        if (!entry) {
            knd_log("-- no such attr: %.*s", cvar->name_size, cvar->name);
            return knd_FAIL;
        }

        /* save attr assignment */
        entry->attr_var = cvar;

        if (DEBUG_CONC_LEVEL_2) {
            knd_log("== entry attr: %.*s %d",
                    cvar->name_size, cvar->name,
                    entry->attr->is_indexed);
        }

        if (entry->attr->is_a_set) {
            cvar->attr = entry->attr;

            err = resolve_attr_var_list(self, cvar);
            if (err) return err;
            if (cvar->val_size)
                cvar->num_list_elems++;

            if (entry->attr->is_indexed) {
                err = index_attr_var_list(self, entry->attr, cvar);
                if (err) return err;
            }
            continue;
        }

        /* single attr */
        switch (entry->attr->type) {
        case KND_ATTR_AGGR:

            /* TODO */
            cvar->attr = entry->attr;
            err = resolve_aggr_item(self, cvar);
            if (err) {
                knd_log("-- aggr cvar not resolved :(");
                return err;
            }

            break;
        case KND_ATTR_REF:
            c = entry->attr->conc;
            if (!c->is_resolved) {
                err = c->resolve(c, NULL);                                        RET_ERR();
            }
            err = resolve_class_ref(self, cvar->val, cvar->val_size,
                                    c, &cvar->class);
            if (err) return err;
            break;
        case KND_ATTR_PROC:
            proc = entry->attr->proc;
            /*if (!c->is_resolved) {
                err = c->resolve(c, NULL);                                        RET_ERR();
                }*/
            err = resolve_proc_ref(self, cvar->val, cvar->val_size,
                                   proc, &cvar->proc);
            if (err) return err;
            break;
        default:
            /* atomic value, call a validation function? */
            break;
        }

        if (entry->attr->is_indexed) {
            err = index_attr(self, entry->attr, cvar);
            if (err) return err;
        }
        cvar->attr = entry->attr;
    }

    return knd_OK;
}

static int resolve_attrs(struct kndClass *self)
{
    struct kndAttr *attr;
    struct kndAttrEntry *attr_entry;
    struct kndClassEntry *entry;
    struct kndProc *root_proc;
    struct kndProcEntry *proc_entry;
    struct ooDict *class_name_idx = self->entry->repo->root_class->class_name_idx;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. resolving attrs..");

    // TODO: mempool
    err = ooDict_new(&self->attr_name_idx, KND_SMALL_DICT_SIZE);                       RET_ERR();

    for (attr = self->attrs; attr; attr = attr->next) {
        attr_entry = self->attr_name_idx->get(self->attr_name_idx, attr->name, attr->name_size);
        if (attr_entry) {
            knd_log("-- %.*s attr already exists?", attr->name_size, attr->name);
            return knd_FAIL;
        }

        /* TODO: mempool */
        attr_entry = malloc(sizeof(struct kndAttrEntry));
        if (!attr_entry) return knd_NOMEM;

        memset(attr_entry, 0, sizeof(struct kndAttrEntry));
        memcpy(attr_entry->name, attr->name, attr->name_size);
        attr_entry->name_size = attr->name_size;
        attr_entry->attr = attr;

        err = self->attr_name_idx->set(self->attr_name_idx,
                                       attr_entry->name, attr_entry->name_size,
                                       (void*)attr_entry);                        RET_ERR();

        /* computed attr idx */
        if (attr->proc) {
            self->computed_attrs[self->num_computed_attrs] = attr;
            self->num_computed_attrs++;
        }

        if (DEBUG_CONC_LEVEL_2)
            knd_log("++ register primary attr: \"%.*s\"",
                    attr->name_size, attr->name);

        switch (attr->type) {
        case KND_ATTR_AGGR:
        case KND_ATTR_REF:
            if (!attr->ref_classname_size) {
                knd_log("-- no classname specified for attr \"%s\"",
                        attr->name);
                return knd_FAIL;
            }

            entry = class_name_idx->get(class_name_idx,
                                        attr->ref_classname,
                                        attr->ref_classname_size);
            if (!entry) {
                knd_log("-- no such class: \"%.*s\" .."
                        "couldn't resolve the \"%.*s\" attr of %.*s :(",
                        attr->ref_classname_size,
                        attr->ref_classname,
                        attr->name_size, attr->name,
                        self->entry->name_size, self->entry->name);
                return knd_FAIL;
            }
            attr->conc = entry->class;
            break;
        case KND_ATTR_PROC:
            if (!attr->ref_procname_size) {
                knd_log("-- no proc name specified for attr \"%s\"",
                        attr->name);
                return knd_FAIL;
            }

            root_proc = self->entry->repo->root_proc;
            proc_entry = root_proc->proc_name_idx->get(root_proc->proc_name_idx,
                                                       attr->ref_procname,
                                                       attr->ref_procname_size);
            if (!proc_entry) {
                knd_log("-- no such proc: \"%.*s\" .."
                        "couldn't resolve the \"%.*s\" attr of %.*s :(",
                        attr->ref_procname_size,
                        attr->ref_procname,
                        attr->name_size, attr->name,
                        self->entry->name_size, self->entry->name);
                return knd_FAIL;
            }
            // TODO
            //attr->proc = proc_entry->proc;

            if (DEBUG_CONC_LEVEL_2)
                knd_log("++ proc ref resolved: %.*s!",
                        proc_entry->name_size, proc_entry->name);
            break;
        default:
            break;
        }

        if (attr->is_implied)
            self->implied_attr = attr;
    }

    return knd_OK;
}

static int resolve_objs(struct kndClass     *self,
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
                        self->entry->name_size, self->entry->name,
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


static int register_descendants(struct kndClass *self)
{
    struct kndClassEntry *entry;
    struct kndSet *set;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    int err;

    for (size_t i = 0; i < self->num_bases; i++) {
        entry = self->bases[i];

        set = entry->descendants;
        if (!set) {
            err = mempool->new_set(mempool, &set);                    RET_ERR();
            set->type = KND_SET_CLASS;
            set->base = entry;
            entry->descendants = set;
        }

        if (DEBUG_CONC_LEVEL_2)
            knd_log(".. add \"%.*s\" as a descendant of \"%.*s\"..",
                    self->entry->name_size, self->entry->name,
                    entry->name_size, entry->name);

        err = set->add(set, self->entry->id, self->entry->id_size,
                       (void*)self->entry);                                       RET_ERR();
    }
    return knd_OK;
}

static int resolve_baseclasses(struct kndClass *self)
{
    struct kndClassVar *cvar;
    void *result;
    struct kndClassEntry *entry;
    struct kndClass *c;
    const char *classname;
    size_t classname_size;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. resolving baseclasses of \"%.*s\"..",
                self->entry->name_size, self->entry->name);

    /* resolve refs to base classes */
    for (cvar = self->baseclass_vars; cvar; cvar = cvar->next) {

        if (cvar->entry->class == self) {
            /* TODO */
            if (DEBUG_CONC_LEVEL_2)
                knd_log(".. \"%.*s\" class to check the update request: \"%s\"..",
                        self->entry->name_size, self->entry->name,
                        cvar->entry->name_size, cvar->entry->name);
            continue;
        }

        classname = cvar->entry->name;
        classname_size = cvar->entry->name_size;

        if (cvar->id_size) {
            err = self->class_idx->get(self->class_idx,
                                       cvar->id, cvar->id_size, &result);
            if (err) {
                knd_log("-- no such class: %.*s :(",
                        cvar->id_size, cvar->id);
                return knd_FAIL;
            }
            entry = result;
            cvar->entry = entry;

            //memcpy(cvar->entry->name, entry->name, entry->name_size);
            //cvar->classname_size = entry->name_size;

            classname = entry->name;
            classname_size = entry->name_size;
        }

        if (!classname_size) {
            knd_log("-- no base class specified in class cvar \"%.*s\" :(",
                    self->entry->name_size, self->entry->name);
            return knd_FAIL;
        }

        if (DEBUG_CONC_LEVEL_2)
            knd_log("\n.. \"%.*s\" class to get its base class: \"%.*s\"..",
                    self->entry->name_size, self->entry->name,
                    classname_size, classname);

        //c = cvar->entry->class;
        err = get_class(self, classname, classname_size, &c);         RET_ERR();

        if (c == self) {
            knd_log("-- self reference detected in \"%.*s\" :(",
                    cvar->entry->name_size, cvar->entry->name);
            return knd_FAIL;
        }

        if (DEBUG_CONC_LEVEL_2) {
            knd_log("++ \"%.*s\" ref established as a base class for \"%.*s\"!",
                    cvar->entry->name_size, cvar->entry->name,
                    self->entry->name_size, self->entry->name);
        }

        cvar->entry->class = c;

        /* check cvar doublets */
        for (size_t i = 0; i < self->entry->num_children; i++) {
            if (self->entry->children[i] == self->entry) {
                knd_log("-- doublet conc cvar found in \"%.*s\" :(",
                        self->entry->name_size, self->entry->name);
                return knd_FAIL;
            }
        }

        if (c->entry->num_children >= KND_MAX_CONC_CHILDREN) {
            knd_log("-- %.*s as child to %.*s - max conc children exceeded :(",
                    self->entry->name_size, self->entry->name, cvar->entry->name_size, cvar->entry->name);
            return knd_FAIL;
        }

        //if (self->entry->repo->task->type == KND_UPDATE_STATE) {
        /*if (DEBUG_CONC_LEVEL_2)
            knd_log(".. update task to add class \"%.*s\" as a child of "
                    " \"%.*s\" (total children:%zu)",
                    self->entry->name_size, self->entry->name,
                    c->name_size, c->name, c->entry->num_children);
        */

        if (!c->entry->children) {
            c->entry->children = calloc(KND_MAX_CONC_CHILDREN,
                                        sizeof(struct kndClassEntry*));
            if (!c->entry->children) {
                knd_log("-- no memory :(");
                return knd_NOMEM;
            }
        }

        if (c->entry->num_children >= KND_MAX_CONC_CHILDREN) {
            knd_log("-- warning: num of subclasses of \"%.*s\" exceeded :(",
                    c->entry->name_size, c->entry->name);
            return knd_OK;
        }
        c->entry->children[c->entry->num_children] = self->entry;
        c->entry->num_children++;
        //}

        err = inherit_attrs(self, cvar->entry->class);                            RET_ERR();
    }

    /* now that we know all our base classes
       let's add a descendant to each of these */
    err = register_descendants(self);                                             RET_ERR();


    return knd_OK;
}

static int resolve_refs(struct kndClass *self,
                        struct kndClassUpdate *update)
{
    struct kndClass *root;
    struct kndClassVar *item;
    struct kndClassEntry *entry;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. resolving class: \"%.*s\"", self->name_size, self->name);

    if (self->is_resolved) {
        if (self->obj_inbox_size) {
            err = resolve_objs(self, update);                                     RET_ERR();
        }
        return knd_OK;
    } else {
        self->entry->repo->next_class_numid++;
        entry = self->entry;
        entry->numid = self->entry->repo->next_class_numid;
        knd_num_to_str(entry->numid, entry->id, &entry->id_size, KND_RADIX_BASE);
    }

    if (DEBUG_CONC_LEVEL_2) {
        knd_log(".. resolving class \"%.*s\" id:%.*s entry numid:%zu",
                self->entry->name_size, self->entry->name,
                entry->id_size, entry->id, self->entry->numid);
    }

    /* a child of the root class
     * TODO: refset */
    if (!self->baseclass_vars) {
        root = self->root_class;
        entry = root->entry;

        if (!entry->children) {
            entry->children = calloc(KND_MAX_CONC_CHILDREN,
                                   sizeof(struct kndClassEntry*));
            if (!entry->children) {
                knd_log("-- no memory :(");
                return knd_NOMEM;
            }
        }
        if (entry->num_children >= KND_MAX_CONC_CHILDREN) {
            knd_log("-- warning: num of subclasses of \"%.*s\" exceeded :(",
                    entry->name_size, entry->name);
            return knd_OK;
        }
        entry->children[entry->num_children] = self->entry;
        entry->num_children++;
    }

    /* resolve and index the attrs */
    if (!self->attr_name_idx) {
        err = resolve_attrs(self);                                                RET_ERR();
    }

    if (self->baseclass_vars) {
        err = resolve_baseclasses(self);                                         RET_ERR();
    }

    for (item = self->baseclass_vars; item; item = item->next) {
        if (item->attrs) {
            err = resolve_attr_vars(self, item);                                 RET_ERR();
        }
    }

    if (self->obj_inbox_size) {
        err = resolve_objs(self, update);                                         RET_ERR();
    }

    self->is_resolved = true;
    return knd_OK;
}

static int build_attr_name_idx(struct kndClass *self)
{
    struct kndClassVar *item;
    struct kndAttr *attr;
    struct kndAttrEntry *attr_entry;
    struct kndClassEntry *entry;
    int err;

    if (!self->attr_name_idx) {
        err = ooDict_new(&self->attr_name_idx, KND_SMALL_DICT_SIZE);              RET_ERR();
    }

    for (attr = self->attrs; attr; attr = attr->next) {
        /* TODO: mempool */
        attr_entry = malloc(sizeof(struct kndAttrEntry));
        if (!attr_entry) return knd_NOMEM;
        memset(attr_entry, 0, sizeof(struct kndAttrEntry));
        memcpy(attr_entry->name, attr->name, attr->name_size);
        attr_entry->name_size = attr->name_size;
        attr_entry->attr = attr;

        err = self->attr_name_idx->set(self->attr_name_idx,
                                       attr_entry->name, attr_entry->name_size,
                                       (void*)attr_entry);                              RET_ERR();

        /* resolve attr */
        switch (attr->type) {
        case KND_ATTR_AGGR:
        case KND_ATTR_REF:
            if (!attr->ref_classname_size) {
                knd_log("-- no classname specified for attr \"%s\"",
                        attr->name);
                return knd_FAIL;
            }
            entry = self->class_name_idx->get(self->class_name_idx,
                                            attr->ref_classname,
                                            attr->ref_classname_size);
            if (!entry) {
                knd_log("-- no such class: \"%.*s\" .."
                        "couldn't resolve the \"%.*s\" attr of %.*s :(",
                        attr->ref_classname_size,
                        attr->ref_classname,
                        attr->name_size, attr->name,
                        self->entry->name_size, self->entry->name);
                return knd_FAIL;
            }

            if (!entry->class) {
                if (!entry->block_size) return knd_FAIL;
                err = unfreeze_class(self, entry, &entry->class);
                if (err) return err;
            }

            attr->conc = entry->class;
            break;
        default:
            break;
        }

        if (attr->is_implied)
            self->implied_attr = attr;

    }

    for (item = self->baseclass_vars; item; item = item->next) {
        if (DEBUG_CONC_LEVEL_2)
            knd_log(".. class \"%.*s\" to inherit attrs from baseclass \"%.*s\"..",
                    self->entry->name_size, self->entry->name,
                    item->entry->class->name_size,
                    item->entry->class->name);
        err = inherit_attrs(self, item->entry->class);
    }

    return knd_OK;
}

static int get_attr(struct kndClass *self,
                    const char *name, size_t name_size,
                    struct kndAttr **result)
{
    struct kndAttr *attr;
    struct kndAttrEntry *entry;
    int err;

    if (DEBUG_CONC_LEVEL_1) {
        knd_log(".. \"%.*s\" class to check attr \"%.*s\"",
                self->entry->name_size, self->entry->name, name_size, name);
    }

    // TODO: no allocation in select tasks
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

static gsl_err_t atomic_elem_alloc(void *obj,
                                   const char *val,
                                   size_t val_size,
                                   size_t count  __attribute__((unused)),
                                   void **item __attribute__((unused)))
{
    struct kndClass *self = obj;
    struct kndSet *class_idx, *set;
    struct kndClassEntry *entry;
    void *elem;
    int err;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log("Conc %.*s: atomic elem alloc: \"%.*s\"",
                self->entry->name_size, self->entry->name,
                val_size, val);
    }
    class_idx = self->class_idx;

    err = class_idx->get(class_idx, val, val_size, &elem);
    if (err) {
        knd_log("-- IDX:%p couldn't resolve class id: \"%.*s\" [size:%zu] :(",
                class_idx, val_size, val, val_size);
        return make_gsl_err_external(knd_NO_MATCH);
    }

    entry = elem;

    set = self->entry->descendants;
    err = set->add(set, entry->id, entry->id_size, (void*)entry);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t atomic_elem_append(void *accu  __attribute__((unused)),
                                    void *item __attribute__((unused)))
{

    return make_gsl_err(gsl_OK);
}

/* facet parsing */

static gsl_err_t facet_alloc(void *obj,
                             const char *name,
                             size_t name_size,
                             size_t count __attribute__((unused)),
                             void **item)
{
    struct kndSet *set = obj;
    struct kndFacet *f;
    struct kndMemPool *mempool = set->base->repo->mempool;
    int err;

    assert(name == NULL && name_size == 0);

    if (DEBUG_CONC_LEVEL_1)
        knd_log(".. \"%.*s\" set to alloc a facet..",
                set->base->name_size, set->base->name);

    err = mempool->new_facet(mempool, &f);
    if (err) return make_gsl_err_external(err);

    /* TODO: mempool alloc */
    err = ooDict_new(&f->set_name_idx, KND_SMALL_DICT_SIZE);
    if (err) return make_gsl_err_external(err);

    f->parent = set;

    *item = (void*)f;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t facet_append(void *accu,
                              void *item)
{
    struct kndSet *set = accu;
    struct kndFacet *f = item;

    if (set->num_facets >= KND_MAX_ATTRS)
        return make_gsl_err_external(knd_LIMIT);

    set->facets[set->num_facets] = f;
    set->num_facets++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_facet_name(void *obj, const char *name, size_t name_size)
{
    struct kndFacet *f = obj;
    struct kndSet *parent = f->parent;
    struct kndClass *c;
    struct kndAttr *attr;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. set %.*s to add facet \"%.*s\"..",
                parent->base->name_size, parent->base->name, name_size, name);

    c = parent->base->class;
    err = c->get_attr(c, name, name_size, &attr);
    if (err) {
        knd_log("-- no such facet attr: \"%.*s\"",
                name_size, name);
        return make_gsl_err_external(err);
    }

    f->attr = attr;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_alloc(void *obj,
                           const char *name,
                           size_t name_size,
                           size_t count __attribute__((unused)),
                           void **item)
{
    struct kndFacet *self = obj;
    struct kndSet *set;
    struct kndMemPool *mempool = self->attr->parent_class->entry->repo->mempool;
    int err;

    assert(name == NULL && name_size == 0);

    err = mempool->new_set(mempool, &set);
    if (err) return make_gsl_err_external(err);

    set->type = KND_SET_CLASS;
    //set->mempool = self->entry->repo->mempool;
    set->parent_facet = self;

    *item = (void*)set;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_append(void *accu,
                            void *item)
{
    struct kndFacet *self = accu;
    struct kndSet *set = item;
    int err;

    if (!self->set_name_idx) return make_gsl_err(gsl_OK);

    err = self->set_name_idx->set(self->set_name_idx,
                                  set->base->name, set->base->name_size,
                                  (void*)set);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t resolve_set_base(void *obj,
                                  const char *id, size_t id_size)
{
    struct kndSet *set = obj;
    struct kndFacet *parent_facet = set->parent_facet;
    struct kndClass *c;
    void *result;
    int err;

    c = parent_facet->parent->base->class;
    err = c->class_idx->get(c->class_idx,
                            id, id_size, &result);
    if (err) {
        knd_log("-- no such class: \"%.*s\":(", id_size, id);
        return make_gsl_err_external(err);
    }
    set->base = result;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("++ base class: %.*s \"%.*s\"",
                id_size, id, set->base->name_size, set->base->name);

    return make_gsl_err(gsl_OK);
}



static gsl_err_t atomic_classref_alloc(void *obj,
                                       const char *val,
                                       size_t val_size,
                                       size_t count  __attribute__((unused)),
                                       void **item __attribute__((unused)))
{
    struct kndSet *self = obj;
    struct kndSet *class_idx;
    struct kndClassEntry *entry;
    void *elem;
    int err;

    entry = self->base;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log(".. base class \"%.*s\": atomic classref alloc: \"%.*s\"",
                entry->name_size, entry->name,
                val_size, val);
    }

    class_idx = entry->class_idx;

    err = class_idx->get(class_idx, val, val_size, &elem);
    if (err) {
        knd_log("-- IDX:%p couldn't resolve class id: \"%.*s\" [size:%zu] :(",
                class_idx, val_size, val, val_size);
        return make_gsl_err_external(knd_NO_MATCH);
    }
    entry = elem;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("++ classref resolved: %.*s \"%.*s\"",
                val_size, val, entry->name_size, entry->name);

    err = self->add(self, val, val_size, (void*)entry);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_read(void *obj,
                          const char *rec,
                          size_t *total_size)
{
    struct kndSet *set = obj;

    struct gslTaskSpec classref_spec = {
        .is_list_item = true,
        .alloc = atomic_classref_alloc,
        .append = atomic_elem_append,
        .accu = set
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = resolve_set_base,
          .obj = set
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "c",
          .name_size = strlen("c"),
          .parse = gsl_parse_array,
          .obj = &classref_spec
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (!set->base) {
        knd_log("-- no base class provided :(");
        return make_gsl_err(gsl_FORMAT);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t facet_read(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndFacet *f = obj;

    struct gslTaskSpec set_item_spec = {
        .is_list_item = true,
        .alloc = set_alloc,
        .append = set_append,
        .accu = f,
        .parse = set_read
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_facet_name,
          .obj = f
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "set",
          .name_size = strlen("set"),
          .parse = gsl_parse_array,
          .obj = &set_item_spec
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (f->attr == NULL) {
        knd_log("-- no facet name provided :(");
        return make_gsl_err(gsl_FORMAT);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_descendants(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndSet *set;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    size_t total_elems = 0;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing a set of descendants: \"%.*s\"", 300, rec);

    err = mempool->new_set(mempool, &set);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    set->type = KND_SET_CLASS;
    set->base = self->entry;
    self->entry->descendants = set;

    struct gslTaskSpec c_item_spec = {
        .is_list_item = true,
        .alloc = atomic_elem_alloc,
        .append = atomic_elem_append,
        .accu = self
    };

    struct gslTaskSpec facet_spec = {
        .is_list_item = true,
        .alloc = facet_alloc,
        .append = facet_append,
        .parse = facet_read,
        .accu = set
    };

    struct gslTaskSpec specs[] = {
        {  .name = "tot",
           .name_size = strlen("tot"),
           .parse = gsl_parse_size_t,
           .obj = &total_elems
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "c",
          .name_size = strlen("c"),
          .parse = gsl_parse_array,
          .obj = &c_item_spec
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "fc",
          .name_size = strlen("fc"),
          .parse = gsl_parse_array,
          .obj = &facet_spec
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (total_elems != set->num_elems) {
        knd_log("-- set total elems mismatch: %zu vs actual %zu",
                total_elems, set->num_elems);
        return make_gsl_err(gsl_FAIL);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_aggr(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndAttr *attr;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing the AGGR attr: \"%.*s\"", 32, rec);

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_AGGR;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        if (DEBUG_CONC_LEVEL_TMP)
            knd_log("-- failed to parse the AGGR attr: %d", parser_err.code);
        return parser_err;
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

    if (attr->is_implied)
        self->implied_attr = attr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_str(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndClass *self = (struct kndClass*)obj;
    struct kndAttr *attr;
    int err;
    gsl_err_t parser_err;

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_STR;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        knd_log("-- failed to parse the STR attr of \"%.*s\" :(",
                self->entry->name_size, self->entry->name);
        return parser_err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    if (attr->is_implied)
        self->implied_attr = attr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_bin(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndClass *self = (struct kndClass*)obj;
    struct kndAttr *attr;
    gsl_err_t parser_err;
    int  err;

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_BIN;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        knd_log("-- failed to parse the BIN attr: %d", parser_err.code);
        return parser_err;
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
    struct kndClass *self = (struct kndClass*)obj;
    struct kndAttr *attr;
    int err;
    gsl_err_t parser_err;

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_NUM;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        knd_log("-- failed to parse the NUM attr: %d", parser_err.code);
        return parser_err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    if (attr->is_implied)
        self->implied_attr = attr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_ref(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndAttr *attr;
    int err;
    gsl_err_t parser_err;

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_REF;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        knd_log("-- failed to parse the REF attr: %d", parser_err.code);
        return parser_err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    if (attr->is_implied)
        self->implied_attr = attr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_proc(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndAttr *attr;
    int err;
    gsl_err_t parser_err;

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_PROC;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        knd_log("-- failed to parse the PROC attr: %d", parser_err.code);
        return parser_err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    if (attr->is_implied)
        self->implied_attr = attr;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_text(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndClass *self = (struct kndClass*)obj;
    struct kndAttr *attr;
    int err;
    gsl_err_t parser_err;

    err = kndAttr_new(&attr);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr->parent_class = self;
    attr->type = KND_ATTR_TEXT;
    attr->is_a_set = true;

    parser_err = attr->parse(attr, rec, total_size);
    if (parser_err.code) {
        knd_log("-- failed to parse the TEXT attr: %d", parser_err.code);
        return parser_err;
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

extern gsl_err_t import_attr_var(void *obj,
                                  const char *name, size_t name_size,
                                  const char *rec, size_t *total_size)
{
    struct kndClassVar *self = obj;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool;
    gsl_err_t parser_err;
    int err;

    /* class var not resolved */
    if (!self->entry) {
        knd_log("-- class var not resolved?");
        //return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }

    mempool = self->root_class->entry->repo->mempool;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== import attr var: \"%.*s\" REC: %.*s",
                name_size, name, 64, rec);

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) {
        knd_log("-- attr item mempool exhausted");
        return *total_size = 0, make_gsl_err_external(err);
    }
    attr_var->class_var = self;

    memcpy(attr_var->name, name, name_size);
    attr_var->name_size = name_size;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = attr_var->val,
          .buf_size = &attr_var->val_size,
          .max_buf_size = sizeof attr_var->val
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = import_nested_attr_var,
          .obj = attr_var
        },
        { .is_validator = true,
          .validate = import_nested_attr_var,
          .obj = attr_var
        },
        { .is_default = true,
          .run = confirm_class_var,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    append_attr_var(self, attr_var);

    if (DEBUG_CONC_LEVEL_2)
        knd_log("++ attr var import OK!");

    return make_gsl_err(gsl_OK);
}


static gsl_err_t import_attr_var_alloc(void *obj,
                                        const char *name __attribute__((unused)),
                                        size_t name_size __attribute__((unused)),
                                        size_t count  __attribute__((unused)),
                                        void **result)
{
    struct kndAttrVar *self = obj;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = self->class_var->entry->repo->mempool;
    int err;

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) return make_gsl_err_external(err);
    attr_var->class_var = self->class_var;

    attr_var->is_list_item = true;

    *result = attr_var;
    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_nested_attr_var(void *obj,
                                        const char *rec,
                                        size_t *total_size)
{
    struct kndAttrVar *item = obj;

    if (DEBUG_CONC_LEVEL_1)
        knd_log(".. parse import attr item..");

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = item->name,
          .buf_size = &item->name_size,
          .max_buf_size = sizeof item->name
        },
        { .is_validator = true,
          .validate = import_nested_attr_var,
          .obj = item
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_attr_var_list(void *obj,
                                       const char *name, size_t name_size,
                                       const char *rec, size_t *total_size)
{
    struct kndClassVar *self = obj;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CONC_LEVEL_1)
        knd_log("== import attr attr_var list: \"%.*s\" REC: %.*s",
                name_size, name, 32, rec);

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->class_var = self;

    memcpy(attr_var->name, name, name_size);
    attr_var->name_size = name_size;

    append_attr_var(self, attr_var);

    struct gslTaskSpec import_attr_var_spec = {
        .is_list_item = true,
        .accu =   attr_var,
        .alloc =  import_attr_var_alloc,
        .append = attr_var_append,
        .parse =  parse_nested_attr_var
    };

    parser_err = gsl_parse_array(&import_attr_var_spec, rec, total_size);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_nested_attr_var(void *obj,
                                      const char *name, size_t name_size,
                                      const char *rec, size_t *total_size)
{
    struct kndAttrVar *self = obj;
    struct kndAttrVar *attr_var;
    struct kndAttr *attr;
    struct kndClass *conc = NULL;
    struct ooDict *class_name_idx;
    struct kndClassEntry *entry;
    struct kndMemPool *mempool = self->attr->parent_class->entry->repo->mempool;
    gsl_err_t parser_err;
    int err;

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->parent = self;
    attr_var->class_var = self->class_var;

    memcpy(attr_var->name, name, name_size);
    attr_var->name_size = name_size;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log("== reading nested attr item: \"%.*s\" REC: %.*s attr:%p",
                name_size, name, 16, rec, self->attr);
    }

    if (!self->attr->conc) {
        knd_log("-- no conc in attr: \"%.*s\"",
                self->attr->name_size, self->attr->name);
        return *total_size = 0, make_gsl_err_external(knd_FAIL);
    }

    conc = self->attr->conc;

    err = get_attr(conc, name, name_size, &attr);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\" :(",
                name_size, name,
                conc->entry->name_size, conc->entry->name);
        if (err) return *total_size = 0, make_gsl_err_external(err);
    }

    switch (attr->type) {
    case KND_ATTR_AGGR:
        if (attr->conc) break;

        class_name_idx = conc->class_name_idx;
        entry = class_name_idx->get(class_name_idx,
                                  attr->ref_classname,
                                  attr->ref_classname_size);
        if (!entry) {
            knd_log("-- aggr ref not resolved :( no such class: %.*s",
                    attr->ref_classname_size,
                    attr->ref_classname);
            return *total_size = 0, make_gsl_err(gsl_FAIL);
        }

        if (!entry->class) {
            err = unfreeze_class(conc, entry, &entry->class);
            if (err) return *total_size = 0, make_gsl_err_external(err);
        }
        attr->conc = entry->class;
        break;
    default:
        break;
    }

    attr_var->attr = attr;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = attr_var->val,
          .buf_size = &attr_var->val_size,
          .max_buf_size = sizeof attr_var->val
        },
        { .is_validator = true,
          .validate = read_nested_attr_var,
          .obj = attr_var
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    attr_var->next = self->children;
    self->children = attr_var;
    self->num_children++;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_nested_attr_var(void *obj,
                                         const char *name, size_t name_size,
                                         const char *rec, size_t *total_size)
{
    struct kndAttrVar *self = obj;
    struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = self->class_var->root_class->entry->repo->mempool;
    gsl_err_t parser_err;
    int err;

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) {
        knd_log("-- mempool exhausted: attr item");
        return *total_size = 0, make_gsl_err_external(err);
    }
    attr_var->class_var = self->class_var;

    memcpy(attr_var->name, name, name_size);
    attr_var->name_size = name_size;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. import nested attr: \"%.*s\" REC: %.*s",
                attr_var->name_size, attr_var->name, 16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = attr_var->val,
          .buf_size = &attr_var->val_size,
          .max_buf_size = sizeof attr_var->val
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = import_nested_attr_var,
          .obj = attr_var
        },
        { .is_validator = true,
          .validate = import_nested_attr_var,
          .obj = attr_var
        },
        { .is_default = true,
          .run = confirm_attr_var,
          .obj = attr_var
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("++ attr var: \"%.*s\" val:%.*s",
                attr_var->name_size, attr_var->name,
                attr_var->val_size, attr_var->val);

    attr_var->next = self->children;
    self->children = attr_var;
    self->num_children++;

    return make_gsl_err(gsl_OK);
}

static void append_attr_var(struct kndClassVar *ci,
                             struct kndAttrVar *attr_var)
{
    struct kndAttrVar *curr_var;

    for (curr_var = ci->attrs; curr_var; curr_var = curr_var->next) {
        if (curr_var->name_size != attr_var->name_size) continue;
        if (!memcmp(curr_var->name, attr_var->name, attr_var->name_size)) {
            if (!curr_var->list_tail) {
                curr_var->list_tail = attr_var;
                curr_var->list = attr_var;
            }
            else {
                curr_var->list_tail->next = attr_var;
                curr_var->list_tail = attr_var;
            }
            curr_var->num_list_elems++;
            return;
        }
    }

    if (!ci->tail) {
        ci->tail  = attr_var;
        ci->attrs = attr_var;
    }
    else {
        ci->tail->next = attr_var;
        ci->tail = attr_var;
    }
    ci->num_attrs++;
}


static gsl_err_t attr_var_alloc(void *obj,
                                 const char *name,
                                 size_t name_size,
                                 size_t count  __attribute__((unused)),
                                 void **result)
{
    struct kndAttrVar *self = obj;
    struct kndAttrVar *attr_var;
    struct kndSet *class_idx;
    struct kndClassEntry *entry;
    struct kndMemPool *mempool = self->attr->parent_class->entry->repo->mempool;
    void *elem;
    int err;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log(".. alloc attr item conc id: \"%.*s\" attr:%p",
                name_size, name, self->attr);
    }

    class_idx = self->attr->parent_class->class_idx;

    err = class_idx->get(class_idx, name, name_size, &elem);
    if (err) {
        knd_log("-- couldn't resolve class id: \"%.*s\" [size:%zu] :(",
                name_size, name, name_size);
        return make_gsl_err_external(knd_NO_MATCH);
    }

    entry = elem;

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) return make_gsl_err_external(err);

    attr_var->class_entry = entry;
    attr_var->attr = self->attr;

    *result = attr_var;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t attr_var_append(void *accu,
                                 void *obj)
{
    struct kndAttrVar *self = accu;
    struct kndAttrVar *attr_var = obj;

    if (!self->list_tail) {
        self->list_tail = attr_var;
        self->list = attr_var;
    }
    else {
        self->list_tail->next = attr_var;
        self->list_tail = attr_var;
    }
    self->num_list_elems++;
    attr_var->list_count = self->num_list_elems;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t aggr_item_set_baseclass(void *obj,
                                         const char *id, size_t id_size)
{
    struct kndAttrVar *item = obj;
    struct kndSet *class_idx;
    struct kndClassEntry *entry;
    struct kndClass *conc = item->attr->parent_class;
    void *result;
    int err;

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE)
        return make_gsl_err(gsl_LIMIT);

    memcpy(item->id, id, id_size);
    item->id_size = id_size;

    class_idx = conc->class_idx;

    err = class_idx->get(class_idx, id, id_size, &result);
    if (err) {
        return make_gsl_err(gsl_FAIL);
    }
    entry = result;

    if (!entry->class) {
        err = unfreeze_class(conc, entry, &entry->class);
        if (err) return make_gsl_err_external(err);
    }

    item->class = entry->class;
    item->class_entry = entry;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t aggr_item_parse(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct kndAttrVar *item = obj;
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = aggr_item_set_baseclass,
          .obj = item
        },
        { .is_validator = true,
          .validate = read_nested_attr_var,
          .obj = item
        }
    };

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing the aggr item: \"%.*s\"", 64, rec);

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t aggr_item_alloc(void *obj,
                                 const char *name,
                                 size_t name_size,
                                 size_t count,
                                 void **result)
{
    struct kndAttrVar *self = obj;
    struct kndAttrVar *item;
    struct kndMemPool *mempool = self->attr->parent_class->entry->repo->mempool;
    int err;

    if (DEBUG_CONC_LEVEL_1) {
        knd_log(".. alloc AGGR attr item..  conc id: \"%.*s\" attr:%p  parent:%p",
                name_size, name, self->attr,  self->attr->parent_class);
    }

    err = mempool->new_attr_var(mempool, &item);
    if (err) return make_gsl_err_external(err);

    item->name_size = sprintf(item->name, "%lu",
                              (unsigned long)count);

    item->attr = self->attr;
    item->parent = self;

    *result = item;
    return make_gsl_err(gsl_OK);
}


static gsl_err_t validate_attr_var(void *obj,
                                    const char *name, size_t name_size,
                                    const char *rec, size_t *total_size)
{
    struct kndClassVar *class_var = obj;
    struct kndAttrVar *attr_var;
    struct kndAttr *attr;
    struct kndProc *root_proc;
    struct kndMemPool *mempool = class_var->entry->repo->mempool;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. class var \"%.*s\" to validate attr var: %.*s.. mem:%p",
                class_var->entry->name_size, class_var->entry->name,
                name_size, name, mempool);

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    attr_var->class_var = class_var;

    err = get_attr(class_var->entry->class, name, name_size, &attr);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name,
                class_var->entry->name_size, class_var->entry->name);
        return *total_size = 0, make_gsl_err_external(err);
    }

    attr_var->attr = attr;
    memcpy(attr_var->name, name, name_size);
    attr_var->name_size = name_size;

    struct gslTaskSpec attr_var_spec = {
        .is_list_item = true,
        .accu = attr_var,
        .alloc = attr_var_alloc,
        .append = attr_var_append
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .buf = attr_var->val,
          .buf_size = &attr_var->val_size,
          .max_buf_size = sizeof attr_var->val
        },
        { .is_validator = true,
          .validate = read_nested_attr_var,
          .obj = attr_var
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "r",
          .name_size = strlen("r"),
          .parse = gsl_parse_array,
          .obj = &attr_var_spec
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    switch (attr->type) {
    case KND_ATTR_PROC:

        if (DEBUG_CONC_LEVEL_2)
            knd_log("== proc attr: %.*s => %.*s",
                    attr_var->name_size, attr_var->name,
                    attr_var->val_size, attr_var->val);

        root_proc = class_var->root_class->proc;
        err = root_proc->get_proc(root_proc,
                                  attr_var->val,
                                  attr_var->val_size, &attr_var->proc);
        if (err) return make_gsl_err_external(err);
        break;
    default:
        break;
    }

    append_attr_var(class_var, attr_var);
    return make_gsl_err(gsl_OK);
}

extern gsl_err_t parse_class_var(struct kndClassVar *self,
                                 const char *rec,
                                 size_t *total_size)
{
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_var,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = import_attr_var,
          .obj = self
        },
        { .is_validator = true,
          .type = GSL_SET_ARRAY_STATE,
          .validate = import_attr_var_list,
          .obj = self
        },
        { .is_validator = true,
          .validate = validate_attr_var,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_class_var(void *obj, const char *name, size_t name_size)
{
    struct kndClassVar *self = obj;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. confirm class var: \"%.*s\".. %p",
                name_size, name, self);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_attr_var(void *obj __attribute__((unused)),
                                  const char *name __attribute__((unused)),
                                  size_t name_size __attribute__((unused)))
{
    return make_gsl_err(gsl_OK);
}

extern gsl_err_t import_class_var(struct kndClassVar *self,
                                  const char *rec,
                                  size_t *total_size)
{
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_var,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = import_attr_var,
          .obj = self
        },
        { .is_validator = true,
          .validate = import_attr_var,
          .obj = self
        },
        { .is_validator = true,
          .type = GSL_SET_ARRAY_STATE,
          .validate = import_attr_var_list,
          .obj = self
        },
        { .is_default = true,
          .run = confirm_class_var,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_baseclass(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndClassVar *classvar;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    gsl_err_t parser_err;
    int err;

    if (DEBUG_CONC_LEVEL_1)
        knd_log(".. parsing the base class: \"%.*s\"", 32, rec);

    err = mempool->new_class_var(mempool, &classvar);
    if (err) {
        knd_log("-- conc item alloc failed :(");
        return *total_size = 0, make_gsl_err_external(err);
    }
    classvar->root_class = self->root_class;

    parser_err = parse_class_var(classvar, rec, total_size);
    if (parser_err.code) return parser_err;

    classvar->next = self->baseclass_vars;
    self->baseclass_vars = classvar;
    self->num_baseclass_vars++;

    return make_gsl_err(gsl_OK);
}

static int assign_ids(struct kndClass *self)
{
    struct kndClassEntry *entry;
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
        entry = (struct kndClassEntry*)val;
        count++;

        entry->id_size = 0;
        knd_num_to_str(count, entry->id, &entry->id_size, KND_RADIX_BASE);

        if (DEBUG_CONC_LEVEL_2)
            knd_log("ID: %zu => \"%.*s\" [size: %zu]",
                    count, entry->id_size, entry->id, entry->id_size);

        /* TODO: assign obj ids */
    } while (key);

    return knd_OK;
}

static gsl_err_t run_sync_task(void *obj, const char *val __attribute__((unused)),
                               size_t val_size __attribute__((unused)))
{
    struct kndClass *self = obj;
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
    struct kndClass *self = (struct kndClass*)obj;

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
    struct kndClass *self = obj;
    struct kndClass *c;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CONC_LEVEL_1)
        knd_log(".. import \"%.*s\" class..", 64, rec);

    err = mempool->new_class(mempool, &c);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    c->root_class = self;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_name,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "base",
          .name_size = strlen("base"),
          .parse = parse_baseclass,
          .obj = c
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = parse_gloss_array,
          .obj = c
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = parse_gloss_array,
          .obj = c
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_summary",
          .name_size = strlen("_summary"),
          .parse = parse_summary_array,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "aggr",
          .name_size = strlen("aggr"),
          .parse = parse_aggr,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "str",
          .name_size = strlen("str"),
          .parse = parse_str,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "bin",
          .name_size = strlen("bin"),
          .parse = parse_bin,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "num",
          .name_size = strlen("num"),
          .parse = parse_num,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "ref",
          .name_size = strlen("ref"),
          .parse = parse_ref,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc,
          .obj = c
        },
        { .type = GSL_SET_STATE,
          .name = "text",
          .name_size = strlen("text"),
          .parse = parse_text,
          .obj = c
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- class parse failed: %d", parser_err.code);
        goto final;
    }

    if (!c->name_size) {
        parser_err = make_gsl_err(gsl_FAIL);
        goto final;
    }

    if (DEBUG_CONC_LEVEL_2)
        knd_log("++  \"%.*s\" class import completed!",
                c->name_size, c->name);

    if (!self->batch_mode) {
        c->next = self->inbox;
        self->inbox = c;
        self->inbox_size++;
    }

    if (DEBUG_CONC_LEVEL_2)
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
    struct kndClass *self = data;
    struct kndClass *c;
    struct kndObject *obj;
    struct kndObjEntry *entry;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log(".. import \"%.*s\" obj.. conc: %p", 128, rec, self->curr_class);
    }

    if (!self->curr_class) {
        knd_log("-- class not set :(");
        return *total_size = 0, make_gsl_err(gsl_FAIL);
    }

    err = self->entry->repo->mempool->new_obj(self->entry->repo->mempool, &obj);
    if (err) {
        return *total_size = 0, make_gsl_err_external(err);
    }
    err = self->entry->repo->mempool->new_state(self->entry->repo->mempool, &obj->state);
    if (err) {
        return *total_size = 0, make_gsl_err_external(err);
    }

    obj->state->phase = KND_SUBMITTED;
    obj->base = self->curr_class;

    parser_err = obj->parse(obj, rec, total_size);
    if (parser_err.code) return parser_err;

    c = obj->base;
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

    if (!c->entry) {
        if (c->root_class) {
            knd_log("-- no entry in %.*s :(", c->name_size, c->name);
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

    if (!c->entry->obj_name_idx) {
        err = ooDict_new(&c->entry->obj_name_idx, KND_HUGE_DICT_SIZE);
        if (err) return make_gsl_err_external(err);
    }

    err = self->entry->repo->mempool->new_obj_entry(self->entry->repo->mempool, &entry);
    if (err) return make_gsl_err_external(err);

    memcpy(entry->id, obj->id, obj->id_size);
    entry->id_size = obj->id_size;

    entry->obj = obj;
    obj->entry = entry;

    err = c->entry->obj_name_idx->set(c->entry->obj_name_idx,
                                      obj->name, obj->name_size,
                                      (void*)entry);
    if (err) return make_gsl_err_external(err);
    c->entry->num_objs++;

    if (DEBUG_CONC_LEVEL_1) {
        knd_log("++ OBJ registered in \"%.*s\" IDX:  [total:%zu valid:%zu]",
                c->name_size, c->name, c->entry->obj_name_idx->size, c->entry->num_objs);
        obj->depth = self->depth + 1;
        obj->str(obj);
    }

    self->entry->repo->task->type = KND_UPDATE_STATE;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_select_obj(void *data,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndClass *self = data;
    struct kndObject *obj = self->curr_obj;
    gsl_err_t err;

    if (!self->curr_class) {
        knd_log("-- base class not set :(");
        /* TODO: log*/
        return *total_size = 0, make_gsl_err(gsl_FAIL);
    }

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. select \"%.*s\" obj.. task type: %d", 16, rec,
                self->curr_class->entry->repo->task->type);

    self->curr_class->entry->repo->task->type = KND_GET_STATE;
    obj->base = self->curr_class;

    err = obj->select(obj, rec, total_size);
    if (err.code) return err;

    if (DEBUG_CONC_LEVEL_2) {
        if (obj->curr_obj)
            obj->curr_obj->str(obj->curr_obj);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t select_by_baseclass(void *obj,
                                     const char *name, size_t name_size)
{
    struct kndClass *self = obj;
    struct kndClass *c;
    struct kndSet *set;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    err = get_class(self, name, name_size, &c);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_CONC_LEVEL_2)
        c->str(c);

    c->entry->class = c;
    if (!c->entry->descendants) {
        knd_log("-- no set of descendants found :(");
        return make_gsl_err(gsl_OK);
    }

    set = c->entry->descendants;
    if (self->entry->repo->task->num_sets + 1 > KND_MAX_CLAUSES)
        return make_gsl_err(gsl_LIMIT);
    self->entry->repo->task->sets[self->entry->repo->task->num_sets] = set;
    self->entry->repo->task->num_sets++;

    self->curr_baseclass = c;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t select_by_attr(void *obj,
                                const char *name, size_t name_size)
{
    struct kndClass *self = obj;
    struct kndClass *c;
    struct kndSet *set;
    struct kndFacet *facet;
    struct glbOutput *log = self->entry->repo->log;
    int err, e;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    c = self->curr_attr->parent_class;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log("== select by attr value: \"%.*s\" of \"%.*s\" resolved:%d",
                name_size, name, c->name_size, c->name, c->is_resolved);
    }

    if (!c->entry->descendants) {
        knd_log("-- no descendants idx in \"%.*s\" :(",
                c->name_size, c->name);
        return make_gsl_err(gsl_FAIL);
    }

    set = c->entry->descendants;

    err = set->get_facet(set, self->curr_attr, &facet);
    if (err) {
        knd_log("-- no such facet: \"%.*s\" :(",
                self->curr_attr->name_size, self->curr_attr->name);
        log->reset(log);
        e = log->write(log, name, name_size);
        if (e) return make_gsl_err_external(e);

        e = log->write(log, " no such facet: ",
                               strlen(" no such facet: "));
        if (e) return make_gsl_err_external(e);
        self->entry->repo->task->http_code = HTTP_NOT_FOUND;
        return make_gsl_err_external(knd_NO_MATCH);
    }

    set = facet->set_name_idx->get(facet->set_name_idx,
                                   name, name_size);
    if (!set) return make_gsl_err(gsl_FAIL);

    err = get_class(self, name, name_size, &set->base->class);
    if (err) {
        knd_log("-- no such class: %.*s", name_size, name);
        return make_gsl_err_external(err);
    }

    if (DEBUG_CONC_LEVEL_2)
        set->str(set, 1);

    if (self->entry->repo->task->num_sets + 1 > KND_MAX_CLAUSES)
        return make_gsl_err(gsl_LIMIT);

    self->entry->repo->task->sets[self->entry->repo->task->num_sets] = set;
    self->entry->repo->task->num_sets++;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_attr_select(void *obj,
                                   const char *name, size_t name_size,
                                   const char *rec, size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndAttr *attr;
    struct glbOutput *log = self->entry->repo->log;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = select_by_attr,
          .obj = self
        }
    };
    int err, e;

    if (!self->curr_baseclass) return *total_size = 0, make_gsl_err_external(knd_FAIL);

    err = get_attr(self->curr_baseclass, name, name_size, &attr);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name,
                self->curr_baseclass->name_size,
                self->curr_baseclass->name);
        log->reset(log);
        e = log->write(log, name, name_size);
        if (e) return *total_size = 0, make_gsl_err_external(e);

        e = log->write(log, ": no such attribute",
                               strlen(": no such attribute"));
        if (e) return *total_size = 0, make_gsl_err_external(e);
        self->entry->repo->task->http_code = HTTP_NOT_FOUND;

        return *total_size = 0, make_gsl_err_external(err);
    }

    if (DEBUG_CONC_LEVEL_2) {
        knd_log(".. select by attr \"%.*s\"..", name_size, name);
        knd_log(".. attr parent: %.*s conc: %.*s",
                attr->parent_class->name_size,
                attr->parent_class->name,
                attr->conc->name_size,
                attr->conc->name);
    }

    self->curr_attr = attr;

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_baseclass_select(void *obj,
                                        const char *rec,
                                        size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndTask *task = self->entry->repo->task;
    gsl_err_t err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. select by baseclass \"%.*s\"..", 16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = select_by_baseclass,
          .obj = self
        },
        { .name = "_batch",
          .name_size = strlen("_batch"),
          .parse = gsl_parse_size_t,
          .obj = &task->batch_max
        },
        { .name = "_from",
          .name_size = strlen("_from"),
          .parse = gsl_parse_size_t,
          .obj = &self->entry->repo->task->batch_from
        },
        {  .name = "_depth",
           .name_size = strlen("_depth"),
           .parse = gsl_parse_size_t,
           .obj = &self->max_depth
        },
        { .is_validator = true,
          .validate = parse_attr_select,
          .obj = self
        }
    };

   task->type = KND_SELECT_STATE;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return err;

    if (task->batch_max > KND_RESULT_MAX_BATCH_SIZE) {
        knd_log("-- batch size exceeded: %zu (max limit: %d) :(",
                task->batch_max, KND_RESULT_MAX_BATCH_SIZE);
        return make_gsl_err(gsl_LIMIT);
    }

   task->start_from = task->batch_max *task->batch_from;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t run_get_schema(void *obj, const char *name, size_t name_size)
{
    struct kndClass *self = obj;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    /* TODO: get current schema */
    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. select schema %.*s from: \"%.*s\"..",
                name_size, name, self->entry->name_size, self->entry->name);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_class_name(void *obj, const char *name, size_t name_size)
{
    struct kndClass *self = obj;
    struct kndRepo *repo = self->root_class->entry->repo;
    struct glbOutput *log = repo->log;
    struct ooDict *class_name_idx = self->root_class->class_name_idx;
    struct kndMemPool *mempool = repo->mempool;
    struct kndClassEntry *entry;
    int err;

    if (DEBUG_CONC_LEVEL_1)
        knd_log(".. set class name: %.*s", name_size, name);

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= sizeof self->entry->name) return make_gsl_err(gsl_LIMIT);

    entry = class_name_idx->get(class_name_idx, name, name_size);
    if (!entry) {
        err = mempool->new_class_entry(mempool, &entry);
        if (err) return make_gsl_err_external(err);
        entry->repo = repo;
        entry->class = self;
        self->entry = entry;

        memcpy(entry->name, name, name_size);
        entry->name_size = name_size;
        self->name = self->entry->name;
        self->name_size = name_size;

        err = class_name_idx->set(class_name_idx,
                                  entry->name, name_size,
                                  (void*)entry);
        if (err) return make_gsl_err_external(err);

        return make_gsl_err(gsl_OK);
    }

    /* class entry already exists */

    if (!entry->class) {
        entry->class =    self;
        self->entry =     entry;
        self->name =      entry->name;
        self->name_size = name_size;
        return make_gsl_err(gsl_OK);
    }

    if (entry->phase == KND_REMOVED) {
        entry->class = self;
        self->entry =  entry;

        if (DEBUG_CONC_LEVEL_2)
            knd_log("== class was removed recently");

        self->name =      entry->name;
        self->name_size = name_size;
        return make_gsl_err(gsl_OK);
    }

    knd_log("-- \"%.*s\" class doublet found :(", name_size, name);

    log->reset(log);
    err = log->write(log, name, name_size);
    if (err) return make_gsl_err_external(err);

    err = log->write(log,   " class name already exists",
                     strlen(" class name already exists"));
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_FAIL);
}

static gsl_err_t set_class_var(void *obj, const char *name, size_t name_size)
{
    struct kndClassVar *self      = obj;
    struct kndClass *root_class   = self->root_class;
    struct ooDict *class_name_idx = root_class->class_name_idx;
    struct kndMemPool *mempool    = root_class->entry->repo->mempool;
    struct kndClassEntry *entry;
    void *result;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. root class:\"%.*s\" to check class var name: %.*s",
                root_class->entry->name_size,
                root_class->entry->name,
                name_size, name);

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    result = class_name_idx->get(class_name_idx, name, name_size);
    if (result) {
        self->entry = result;
        return make_gsl_err(gsl_OK);
    }

    /* register new class entry */
    err = mempool->new_class_entry(mempool, &entry);
    if (err) return make_gsl_err_external(err);

    memcpy(entry->name, name, name_size);
    entry->name_size = name_size;

    err = class_name_idx->set(class_name_idx,
                              entry->name, name_size,
                              (void*)entry);
    if (err) return make_gsl_err_external(err);

    entry->repo = root_class->entry->repo;
    self->entry = entry;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_rel_import(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndClass *self = obj;
    int err;

    err = self->rel->import(self->rel, rec, total_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_proc_import(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndProc *proc = self->entry->repo->root_proc;

    return proc->import(proc, rec, total_size);
}

static gsl_err_t run_read_include(void *obj, const char *name, size_t name_size)
{
    struct kndClass *self = obj;
    struct kndConcFolder *folder;

    if (DEBUG_CONC_LEVEL_1)
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
        { .type = GSL_SET_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = parse_import_class,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "rel",
          .name_size = strlen("rel"),
          .parse = parse_rel_import,
          .obj = self
        },
        { .type = GSL_SET_STATE,
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

static int parse_GSL(struct kndClass *self,
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

static int knd_get_dir_size(struct kndClass *self,
                            size_t *dir_size,
                            size_t *chunk_size,
                            unsigned int encode_base)
{
    char buf[KND_DIR_ENTRY_SIZE + 1] = {0};
    size_t buf_size = 0;
    const char *rec = self->entry->repo->out->buf;
    size_t rec_size = self->entry->repo->out->buf_size;
    char *invalid_num_char = NULL;

    bool in_field = false;
    bool got_separ = false;
    bool got_tag = false;
    bool got_size = false;
    long numval;
    const char *c, *s = NULL;
    int i = 0;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. get size of DIR in %.*s", self->entry->name_size, self->entry->name);

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
    struct kndClassEntry *self = obj;
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
    struct kndClassEntry *self = obj;
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
    struct kndClassEntry *self = obj;

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
    struct kndClassEntry *parent_entry = accu;
    struct kndClassEntry *entry = item;
    struct kndMemPool *mempool = parent_entry->repo->mempool;
    int err;

    if (!parent_entry->child_idx) {
        err = mempool->new_set(mempool,
                               &parent_entry->child_idx);
        if (err) return make_gsl_err_external(err);
    }

    if (!parent_entry->children) {
        parent_entry->children = calloc(KND_MAX_CONC_CHILDREN,
                                      sizeof(struct kndClassEntry*));
        if (!parent_entry->children) {
            knd_log("-- no memory :(");
            return make_gsl_err_external(knd_NOMEM);
        }
    }

    if (parent_entry->num_children >= KND_MAX_CONC_CHILDREN) {
        knd_log("-- warning: num of subclasses of \"%.*s\" exceeded :(",
                parent_entry->name_size, parent_entry->name);
        return make_gsl_err(gsl_OK);
    }

    parent_entry->children[parent_entry->num_children] = entry;
    parent_entry->num_children++;

    entry->global_offset += parent_entry->curr_offset;
    parent_entry->curr_offset += entry->block_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t dir_entry_alloc(void *self,
                                 const char *name,
                                 size_t name_size,
                                 size_t count,
                                 void **item)
{
    struct kndClassEntry *parent_entry = self;
    struct kndClassEntry *entry;
    struct kndMemPool *mempool = parent_entry->repo->mempool;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. %.*s to add list item: %.*s count: %zu"
                " [total children: %zu]",
                parent_entry->id_size, parent_entry->id, name_size, name,
                count, parent_entry->num_children);

    if (name_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    err = mempool->new_class_entry(mempool, &entry);
    if (err) return make_gsl_err_external(err);

    knd_calc_num_id(name, name_size, &entry->block_size);

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== block size: %zu", entry->block_size);

    *item = entry;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t reldir_entry_alloc(void *self,
                                    const char *name,
                                    size_t name_size,
                                    size_t count,
                                    void **item)
{
    struct kndClassEntry *parent_entry = self;
    struct kndRelEntry *entry;
    struct kndMemPool *mempool = parent_entry->repo->mempool;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. create REL DIR ENTRY: %.*s count: %zu",
                name_size, name, count);

    if (name_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    err = mempool->new_rel_entry(mempool, &entry);
    if (err) return make_gsl_err_external(err);
    knd_calc_num_id(name, name_size, &entry->block_size);

    *item = entry;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t reldir_entry_append(void *accu,
                                     void *item)
{
    struct kndClassEntry *parent_entry = accu;
    struct kndRelEntry *entry = item;

    if (!parent_entry->rels) {
        parent_entry->rels = calloc(KND_MAX_RELS,
                                  sizeof(struct kndRelEntry*));
        if (!parent_entry->rels) return make_gsl_err_external(knd_NOMEM);
    }

    if (parent_entry->num_rels + 1 > KND_MAX_RELS) {
        knd_log("-- warning: max rels of \"%.*s\" exceeded :(",
                parent_entry->name_size, parent_entry->name);
        return make_gsl_err(gsl_OK);
    }

    parent_entry->rels[parent_entry->num_rels] = entry;
    parent_entry->num_rels++;

    entry->global_offset += parent_entry->curr_offset;
    parent_entry->curr_offset += entry->block_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t procdir_entry_alloc(void *self,
                                     const char *name,
                                     size_t name_size,
                                     size_t count,
                                     void **item)
{
    struct kndClassEntry *parent_entry = self;
    struct kndProcEntry *entry;
    struct kndMemPool *mempool = parent_entry->repo->mempool;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. create PROC DIR ENTRY: %.*s count: %zu",
                name_size, name, count);

    if (name_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    err = mempool->new_proc_entry(mempool, &entry);
    if (err) return make_gsl_err_external(err);

    knd_calc_num_id(name, name_size, &entry->block_size);

    *item = entry;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t procdir_entry_append(void *accu,
                                      void *item)
{
    struct kndClassEntry *parent_entry = accu;
    struct kndProcEntry *entry = item;

    if (!parent_entry->procs) {
        parent_entry->procs = calloc(KND_MAX_PROCS,
                                   sizeof(struct kndProcEntry*));
        if (!parent_entry->procs) return make_gsl_err_external(knd_NOMEM);
    }

    if (parent_entry->num_procs + 1 > KND_MAX_PROCS) {
        knd_log("-- warning: max procs of \"%.*s\" exceeded :(",
                parent_entry->name_size, parent_entry->name);
        return make_gsl_err(gsl_OK);
    }

    parent_entry->procs[parent_entry->num_procs] = entry;
    parent_entry->num_procs++;

    entry->global_offset += parent_entry->curr_offset;
    parent_entry->curr_offset += entry->block_size;

    return make_gsl_err(gsl_OK);
}

static int idx_class_name(struct kndClass *self,
                          struct kndClassEntry *entry,
                          int fd)
{
    char buf[KND_NAME_SIZE + 1];
    size_t buf_size;
    off_t offset = 0;
    void *result;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("  .. get conc name in DIR: \"%.*s\""
                " global off:%zu  block size:%zu",
                entry->name_size, entry->name,
                entry->global_offset, entry->block_size);

    buf_size = entry->block_size;
    if (entry->block_size > KND_NAME_SIZE)
        buf_size = KND_NAME_SIZE;

    offset = entry->global_offset;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return knd_IO_FAIL;
    }
    err = read(fd, buf, buf_size);
    if (err == -1) return knd_IO_FAIL;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("\n  .. CONC BODY: %.*s",
                buf_size, buf);
    buf[buf_size] = '\0';

    entry->id_size = KND_ID_SIZE;
    entry->name_size = KND_NAME_SIZE;
    err = knd_parse_incipit(buf, buf_size,
                            entry->id, &entry->id_size,
                            entry->name, &entry->name_size);
    if (err) return err;

    knd_calc_num_id(entry->id, entry->id_size, &entry->numid);

    err = self->class_idx->add(self->class_idx,
                               entry->id, entry->id_size, (void*)entry);                RET_ERR();

    err = self->class_name_idx->set(self->class_name_idx,
                                    entry->name, entry->name_size, entry);              RET_ERR();


    err = self->class_idx->get(self->class_idx,
                               entry->id, entry->id_size, &result);                   RET_ERR();
    entry = result;

    return knd_OK;
}

static int get_dir_trailer(struct kndClass *self,
                           struct kndClassEntry *parent_entry,
                           int fd,
                           int encode_base)
{
    size_t block_size = parent_entry->block_size;
    struct glbOutput *out = self->entry->repo->out;
    off_t offset = 0;
    size_t dir_size = 0;
    size_t chunk_size = 0;
    int err;

    if (block_size <= KND_DIR_ENTRY_SIZE)
        return knd_NO_MATCH;

    offset = (parent_entry->global_offset + block_size) - KND_DIR_ENTRY_SIZE;
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
        if (DEBUG_CONC_LEVEL_2)
            knd_log("-- couldn't find the ConcDir size field in \"%.*s\" :(",
                    out->buf_size, out->buf);
        return knd_NO_MATCH;
    }

    parent_entry->body_size = block_size - dir_size - chunk_size;
    parent_entry->dir_size = dir_size;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("  .. DIR: offset: %lu  block: %lu  body: %lu  dir: %lu",
                (unsigned long)parent_entry->global_offset,
                (unsigned long)parent_entry->block_size,
                (unsigned long)parent_entry->body_size,
                (unsigned long)parent_entry->dir_size);

    offset = (parent_entry->global_offset + block_size) - chunk_size - dir_size;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return knd_IO_FAIL;
    }

    if (dir_size >= out->capacity) return knd_LIMIT;

    out->reset(out);
    out->buf_size = dir_size;
    err = read(fd, out->buf, out->buf_size);
    if (err == -1) return knd_IO_FAIL;
    out->buf[out->buf_size] = '\0';

    if (DEBUG_CONC_LEVEL_2) {
        chunk_size = out->buf_size > KND_MAX_DEBUG_CHUNK_SIZE ?\
            KND_MAX_DEBUG_CHUNK_SIZE :  out->buf_size;
        knd_log(".. parsing DIR: \"%.*s\" [size:%zu]",
                chunk_size, out->buf, out->buf_size);
    }

    err = parse_dir_trailer(self, parent_entry, fd, encode_base);
    if (err) {
        chunk_size =  out->buf_size > KND_MAX_DEBUG_CHUNK_SIZE ?\
                KND_MAX_DEBUG_CHUNK_SIZE :  out->buf_size;

        knd_log("-- failed to parse dir trailer: \"%.*s\" [size:%zu]",
                chunk_size, out->buf, out->buf_size);
        return err;
    }

    return knd_OK;
}

static int parse_dir_trailer(struct kndClass *self,
                             struct kndClassEntry *parent_entry,
                             int fd,
                             int encode_base)
{
    char *dir_buf = self->entry->repo->out->buf;
    size_t dir_buf_size = self->entry->repo->out->buf_size;
    struct kndClassEntry *entry;
    struct kndRelEntry *rel_entry;
    struct kndProcEntry *proc_entry;
    size_t parsed_size = 0;
    int err;
    gsl_err_t parser_err;

    struct gslTaskSpec class_dir_spec = {
        .is_list_item = true,
        .accu = parent_entry,
        .alloc = dir_entry_alloc,
        .append = dir_entry_append,
    };

    struct gslTaskSpec rel_dir_spec = {
        .is_list_item = true,
        .accu = parent_entry,
        .alloc = reldir_entry_alloc,
        .append = reldir_entry_append,
    };

    struct gslTaskSpec proc_dir_spec = {
        .is_list_item = true,
        .accu = parent_entry,
        .alloc = procdir_entry_alloc,
        .append = procdir_entry_append
    };

    struct gslTaskSpec specs[] = {
        { .name = "C",
          .name_size = strlen("C"),
          .parse = parse_parent_dir_size,
          .obj = parent_entry
        },
        {  .name = "tot",
           .name_size = strlen("tot"),
           .parse = gsl_parse_size_t,
           .obj = &self->entry->repo->next_class_numid
        },
        { .name = "O",
          .name_size = strlen("O"),
          .parse = parse_obj_dir_size,
          .obj = parent_entry
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "c",
          .name_size = strlen("c"),
          .parse = gsl_parse_array,
          .obj = &class_dir_spec
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "R",
          .name_size = strlen("R"),
          .parse = gsl_parse_array,
          .obj = &rel_dir_spec
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "P",
          .name_size = strlen("P"),
          .parse = gsl_parse_array,
          .obj = &proc_dir_spec
        }
    };

    parent_entry->curr_offset = parent_entry->global_offset;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("  .. parsing \"%.*s\" DIR REC: \"%.*s\"  curr offset: %zu   [dir size:%zu]",
                KND_ID_SIZE, parent_entry->id, dir_buf_size, dir_buf,
                parent_entry->curr_offset, dir_buf_size);

    parser_err = gsl_parse_task(dir_buf, &parsed_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    /* get conc name */
    if (parent_entry->block_size) {
        if (!parent_entry->is_indexed) {
            err = idx_class_name(self, parent_entry, fd);
            if (err) return err;
            parent_entry->is_indexed = true;
        }
    }

    /* try reading the objs */
    if (parent_entry->obj_block_size) {
        err = get_obj_dir_trailer(self, parent_entry, fd, encode_base);
        if (err) {
            knd_log("-- no obj dir trailer loaded :(");
            return err;
        }
    }

    if (DEBUG_CONC_LEVEL_2)
        knd_log("DIR: %.*s   num children: %zu obj block:%zu",
                parent_entry->id_size, parent_entry->id, parent_entry->num_children,
                parent_entry->obj_block_size);

    /* try reading each dir */
    for (size_t i = 0; i < parent_entry->num_children; i++) {
        entry = parent_entry->children[i];
        if (!entry) continue;

        err = idx_class_name(self, entry, fd);
        if (err) return err;

        if (DEBUG_CONC_LEVEL_2)
            knd_log(".. read DIR  ID:%.*s NAME:%.*s"
                    " block size: %zu  num terminals:%zu",
                    entry->id_size, entry->id,
                    entry->name_size, entry->name,
                    entry->block_size, entry->num_terminals);

        err = get_dir_trailer(self, entry, fd, encode_base);
        if (err) {
            if (err != knd_NO_MATCH) {
                knd_log("-- error reading trailer of \"%.*s\" DIR: %d",
                        entry->name_size, entry->name, err);
                return err;
            } else {
                if (DEBUG_CONC_LEVEL_2)
                    knd_log(".. terminal class: %.*s", entry->name_size, entry->name);
                parent_entry->num_terminals++;
            }
        } else {
            parent_entry->num_terminals += entry->num_terminals;

            if (DEBUG_CONC_LEVEL_2)
                knd_log(".. class:%.*s num_terminals:%zu",
                        parent_entry->name_size,
                        parent_entry->name, parent_entry->num_terminals);

        }
    }

    /* read rels */
    for (size_t i = 0; i < parent_entry->num_rels; i++) {
        rel_entry = parent_entry->rels[i];
        rel_entry->mempool = self->entry->repo->mempool;
        self->rel->fd = fd;
        err = self->rel->read_rel(self->rel, rel_entry);
    }

    /* read procs */
    for (size_t i = 0; i < parent_entry->num_procs; i++) {
        proc_entry = parent_entry->procs[i];
        //proc_entry->mempool = self->entry->repo->mempool;
        //self->proc->fd = fd;
        err = self->proc->read_proc(self->proc, proc_entry, fd);
    }

    return knd_OK;
}

static gsl_err_t obj_entry_alloc(void *obj,
                                 const char *val,
                                 size_t val_size,
                                 size_t count,
                                 void **item)
{
    struct kndClassEntry *parent_entry = obj;
    struct kndObjEntry *entry = NULL;
    struct kndMemPool *mempool = parent_entry->repo->mempool;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. create OBJ ENTRY: %.*s  count: %zu",
                val_size, val, count);

    err = mempool->new_obj_entry(mempool, &entry);
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
    struct kndClassEntry *parent_entry = accu;
    struct kndObjEntry *entry = item;
    struct kndSet *set;
    off_t offset = 0;
    int fd = parent_entry->fd;
    int err;

    entry->offset = parent_entry->curr_offset;

    if (DEBUG_CONC_LEVEL_1)
        knd_log("\n.. ConcDir: %.*s to append atomic obj entry"
                " (block size: %zu) offset:%zu",
                parent_entry->name_size, parent_entry->name,
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
    err = knd_parse_incipit(buf, buf_size,
                                   entry->id, &entry->id_size,
                                   entry->name, &entry->name_size);
    if (err)  return make_gsl_err_external(err);

    parent_entry->curr_offset += entry->block_size;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== OBJ id:%.*s name:%.*s",
                entry->id_size, entry->id,
                entry->name_size, entry->name);

    set = parent_entry->obj_idx;
    err = set->add(set, entry->id, entry->id_size, (void*)entry);
    if (err) {
        knd_log("-- failed to update the obj idx :(");
        return make_gsl_err_external(err);
    }
    /* update name idx */
    err = parent_entry->obj_name_idx->set(parent_entry->obj_name_idx,
                                        entry->name, entry->name_size,
                                        entry);
    if (err) {
        knd_log("-- failed to update the obj name idx entry :(");
        return make_gsl_err_external(err);
    }

    return make_gsl_err(gsl_OK);
}

static int parse_obj_dir_trailer(struct kndClass *self,
                                 struct kndClassEntry *parent_entry,
                                 int fd)
{
    struct gslTaskSpec obj_dir_spec = {
        .is_list_item = true,
        .accu = parent_entry,
        .alloc = obj_entry_alloc,
        .append = obj_entry_append
    };

    struct gslTaskSpec specs[] = {
        { .type = GSL_SET_ARRAY_STATE,
          .name = "o",
          .name_size = strlen("o"),
          .parse = gsl_parse_array,
          .obj = &obj_dir_spec
        }
    };
    size_t parsed_size = 0;
    size_t *total_size = &parsed_size;
    char *obj_dir_buf = self->entry->repo->out->buf;
    size_t obj_dir_buf_size = self->entry->repo->out->buf_size;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_CONC_LEVEL_1)
        knd_log(".. parsing OBJ DIR REC: %.*s [size %zu]",
                128, obj_dir_buf, obj_dir_buf_size);

    if (!parent_entry->obj_dir) {
        err = self->entry->repo->mempool->new_obj_dir(self->entry->repo->mempool, &parent_entry->obj_dir);
        if (err) return err;
    }
    parent_entry->fd = fd;

    if (!parent_entry->obj_name_idx) {
        err = ooDict_new(&parent_entry->obj_name_idx, parent_entry->num_objs);            RET_ERR();

        err = self->entry->repo->mempool->new_set(self->entry->repo->mempool, &parent_entry->obj_idx);            RET_ERR();
        parent_entry->obj_idx->type = KND_SET_CLASS;
    }

    parser_err = gsl_parse_task(obj_dir_buf, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== \"%.*s\" total objs: %zu",
                parent_entry->name_size, parent_entry->name,
                parent_entry->num_objs);

    return knd_OK;
}

static int get_obj_dir_trailer(struct kndClass *self,
                                struct kndClassEntry *parent_entry,
                                int fd,
                                int encode_base)
{
    off_t offset = 0;
    size_t dir_size = 0;
    size_t chunk_size = 0;
    size_t block_size = parent_entry->block_size;
    struct glbOutput *out = self->entry->repo->out;
    int err;

    offset = parent_entry->global_offset + block_size +\
        parent_entry->obj_block_size - KND_DIR_ENTRY_SIZE;
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

    offset = (parent_entry->global_offset + block_size +\
              parent_entry->obj_block_size) - chunk_size - dir_size;
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

    err = parse_obj_dir_trailer(self, parent_entry, fd);
    if (err) return err;

    return knd_OK;
}


static int open_frozen_DB(struct kndClass *self)
{
    const char *filename;
    size_t filename_size;
    struct stat st;
    int fd;
    size_t file_size = 0;
    struct stat file_info;
    int err;

    filename = self->entry->repo->frozen_output_file_name;
    filename_size = self->entry->repo->frozen_output_file_name_size;

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

    self->entry->block_size = file_size;
    self->entry->id[0] = '/';

    /* TODO: get encode base from config */
    err = get_dir_trailer(self, self->entry, fd, KND_DIR_SIZE_ENCODE_BASE);
    if (err) {
        knd_log("-- error reading dir trailer in \"%.*s\"", filename_size, filename);
        goto final;
    }

    err = knd_OK;

    knd_log("++ frozen DB opened! total classes: %zu", self->entry->repo->next_class_numid);

 final:
    if (err) {
        knd_log("-- failed to open the frozen DB :(");
    }
    close(fd);
    return err;
}

static int
kndClass_write_filepath(struct glbOutput *out,
                        struct kndConcFolder *folder)
{
    int err;

    if (folder->parent) {
        err = kndClass_write_filepath(out, folder->parent);
        if (err) return err;
    }

    err = out->write(out, folder->name, folder->name_size);
    if (err) return err;

    return knd_OK;
}

static int read_GSL_file(struct kndClass *self,
                         struct kndConcFolder *parent_folder,
                         const char *filename,
                         size_t filename_size)
{
    struct glbOutput *out = self->entry->repo->out;
    struct glbOutput *file_out = self->entry->repo->file_out;
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
    err = out->write(out, self->entry->repo->schema_path, self->entry->repo->schema_path_size);
    if (err) return err;
    err = out->write(out, "/", 1);
    if (err) return err;

    if (parent_folder) {
        err = kndClass_write_filepath(out, parent_folder);
        if (err) return err;
    }

    err = out->write(out, filename, filename_size);
    if (err) return err;
    err = out->write(out, file_ext, file_ext_size);
    if (err) return err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. reading GSL file: %.*s", out->buf_size, out->buf);

    file_out->reset(file_out);
    err = file_out->write_file_content(file_out, (const char*)out->buf);
    if (err) {
        knd_log("-- couldn't read GSL class file \"%s\" :(", out->buf);
        return err;
    }

    err = parse_GSL(self, (const char*)file_out->buf, &chunk_size);
    if (err) {
        knd_log("-- parsing of \"%.*s\" failed",
                out->buf_size, out->buf);
        return err;
    }

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

static gsl_err_t class_var_alloc(void *obj,
                                 const char *name __attribute__((unused)),
                                 size_t name_size __attribute__((unused)),
                                 size_t count  __attribute__((unused)),
                                 void **item)
{
    struct kndClass *self = obj;
    struct kndClassVar *class_var;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    int err;

    err = mempool->new_class_var(mempool, &class_var);
    if (err) return make_gsl_err_external(err);

    class_var->root_class = self;

    *item = class_var;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t class_var_append(void *accu,
                                  void *item)
{
    struct kndClass *self = accu;
    struct kndClassVar *ci = item;

    ci->next = self->baseclass_vars;
    self->baseclass_vars = ci;
    self->num_baseclass_vars++;

    return make_gsl_err(gsl_OK);
}


static gsl_err_t set_class_var_baseclass(void *obj,
                                         const char *id, size_t id_size)
{
    struct kndClassVar *class_var = obj;
    struct kndSet *class_idx;
    struct kndClassEntry *entry;
    void *result;
    int err;

    if (!id_size) return make_gsl_err(gsl_FORMAT);
    if (id_size > KND_ID_SIZE) return make_gsl_err(gsl_LIMIT);

    memcpy(class_var->id, id, id_size);
    class_var->id_size = id_size;

    class_idx = class_var->root_class->class_idx;
    err = class_idx->get(class_idx, id, id_size, &result);
    if (err) return make_gsl_err(gsl_FAIL);
    entry = result;

    if (!entry->class) {
        err = unfreeze_class(class_var->root_class, entry, &entry->class);
        if (err) return make_gsl_err_external(err);
    }

    class_var->entry = entry;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== conc item baseclass: %.*s (id:%.*s) CONC:%p",
                entry->name_size, entry->name, id_size, id, entry->class);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t validate_attr_var_list(void *obj,
                                         const char *name, size_t name_size,
                                         const char *rec, size_t *total_size)
{
    struct kndClassVar *class_var = obj;
    struct kndAttrVar *attr_var;
    struct kndAttr *attr;
    struct ooDict *class_name_idx;
    struct kndClassEntry *entry;
    struct kndMemPool *mempool = class_var->entry->repo->mempool;
    int err;

    if (DEBUG_CONC_LEVEL_1)
        knd_log("\n.. ARRAY: conc item \"%.*s\" to validate list item: %.*s..",
                class_var->entry->name_size, class_var->entry->name,
                name_size, name);

    err = mempool->new_attr_var(mempool, &attr_var);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    err = get_attr(class_var->root_class, name, name_size, &attr);
    if (err) {
        knd_log("-- no attr \"%.*s\" in class \"%.*s\"",
                name_size, name,
                class_var->entry->name_size, class_var->entry->name);
        return *total_size = 0, make_gsl_err_external(err);
    }

    attr_var->attr = attr;
    attr_var->class_var = class_var;

    memcpy(attr_var->name, name, name_size);
    attr_var->name_size = name_size;

    switch (attr->type) {
    case KND_ATTR_AGGR:
        if (attr->conc) break;

        class_name_idx = class_var->root_class->class_name_idx;
        entry = class_name_idx->get(class_name_idx,
                                    attr->ref_classname,
                                    attr->ref_classname_size);
        if (!entry) {
            knd_log("-- aggr ref not resolved :( no such class: %.*s",
                    attr->ref_classname_size,
                    attr->ref_classname);
            return *total_size = 0, make_gsl_err(gsl_FAIL);
        }

        if (!entry->class) {
            err = unfreeze_class(class_var->root_class, entry, &entry->class);
            if (err) return *total_size = 0, make_gsl_err_external(err);
        }
        attr->conc = entry->class;
        break;
    default:
        break;
    }

    struct gslTaskSpec aggr_item_spec = {
        .is_list_item = true,
        .accu = attr_var,
        .alloc = aggr_item_alloc,
        .append = attr_var_append,
        .parse = aggr_item_parse
    };

    append_attr_var(class_var, attr_var);

    return gsl_parse_array(&aggr_item_spec, rec, total_size);
}

static gsl_err_t class_var_read(void *obj,
                                const char *rec, size_t *total_size)
{
    struct kndClassVar *ci = obj;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_var_baseclass,
          .obj = ci
        },
        { .is_validator = true,
          .type = GSL_SET_ARRAY_STATE,
          .validate = validate_attr_var_list,
          .obj = ci
        },
        { .is_validator = true,
          .validate = validate_attr_var,
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

static gsl_err_t parse_class_var_array(void *obj,
                                       const char *rec,
                                       size_t *total_size)
{
    struct kndClass *self = obj;

    struct gslTaskSpec ci_spec = {
        .is_list_item = true,
        .accu = self,
        .alloc = class_var_alloc,
        .append = class_var_append,
        .parse = class_var_read
    };

    return gsl_parse_array(&ci_spec, rec, total_size);
}

static int read_GSP(struct kndClass *self,
                    const char *rec,
                    size_t *total_size)
{
    gsl_err_t parser_err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. reading GSP: \"%.*s\"..", 256, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_class_name,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_g",
          .name_size = strlen("_g"),
          .parse = parse_gloss_array,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_summary",
          .name_size = strlen("_summary"),
          .parse = parse_summary_array,
          .obj = self
        },
        { .type = GSL_SET_ARRAY_STATE,
          .name = "_ci",
          .name_size = strlen("_ci"),
          .parse = parse_class_var_array,
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
        { .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc,
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


static int resolve_classes(struct kndClass *self)
{
    struct kndClass *c;
    struct kndClassEntry *entry;
    const char *key;
    void *val;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. resolving class refs in \"%.*s\"",
                self->entry->name_size, self->entry->name);

    key = NULL;
    self->class_name_idx->rewind(self->class_name_idx);
    do {
        self->class_name_idx->next_item(self->class_name_idx, &key, &val);
        if (!key) break;

        entry = (struct kndClassEntry*)val;

        if (!entry->class) {
            knd_log("-- unresolved class entry: %.*s",
                    entry->name_size, entry->name);
            return knd_FAIL;
        }

        c = entry->class;
        if (c->is_resolved) continue;

        err = c->resolve(c, NULL);
        if (err) {
            knd_log("-- couldn't resolve the \"%s\" class :(", c->name);
            return err;
        }
        c->is_resolved = true;

        if (DEBUG_CONC_LEVEL_2)
            c->str(c);

    } while (key);

    if (DEBUG_CONC_LEVEL_1)
        knd_log("++ all classes resolved!");

    return knd_OK;
}


static void calculate_descendants(struct kndClass *self)
{
    struct kndClassEntry *parent_entry = NULL;
    struct kndClassEntry *entry = self->entry;
    struct kndClassEntry *subentry = NULL;
    size_t i = 0;

    entry->child_count = 0;
    entry->prev = NULL;

    while (1) {
        /*knd_log(".. entry: %.*s",
          entry->name_size, entry->name); */

        if (!entry->num_children) {
            //knd_log("== terminal class!");

            parent_entry = entry->prev;
            if (!parent_entry) break;

            parent_entry->num_terminals++;

            entry = parent_entry;
            parent_entry = parent_entry->prev;
            continue;
        }

        i = entry->child_count;

        /* all children explored */
        if (i >= entry->num_children) {

            /* accumulate terminals */
            parent_entry->num_terminals += entry->num_terminals;

            entry = parent_entry;
            parent_entry = entry->prev;

            if (!parent_entry) {
                //knd_log("++ top reached!");
                break;
            }
            continue;
        }

        /* explore current child */
        subentry = entry->children[i];
        entry->child_count++;

        subentry->prev = entry;
        parent_entry = entry;
        entry = subentry;
    }
}

static int coordinate(struct kndClass *self)
{
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. class coordination in progress ..");

    /* resolve names to addresses */
    err = resolve_classes(self);
    if (err) return err;

    calculate_descendants(self);

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log("\n  == TOTAL classes: %zu", self->entry->num_terminals);

    //err = self->proc->coordinate(self->proc);                                     RET_ERR();
    //err = self->rel->coordinate(self->rel);                                       RET_ERR();

    knd_log("++ coordination OK!");

    return knd_OK;
}

static int expand_attr_ref_list(struct kndClass *self,
                                struct kndAttrVar *parent_item)
{
    struct kndAttrVar *item;
    int err;

    for (item = parent_item->list; item; item = item->next) {
        if (!item->class) {
            err = unfreeze_class(self, item->class_entry, &item->class);
            if (err) return err;
        }
    }

    return knd_OK;
}

static int expand_attrs(struct kndClass *self,
                        struct kndAttrVar *parent_item)
{
    struct kndAttrVar *item;
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

static int expand_refs(struct kndClass *self)
{
    struct kndClassVar *item;
    int err;

    for (item = self->baseclass_vars; item; item = item->next) {
        if (!item->attrs) continue;
        err = expand_attrs(self, item->attrs);
    }

    return knd_OK;
}

static int unfreeze_class(struct kndClass *self,
                          struct kndClassEntry *entry,
                          struct kndClass **result)
{
    char buf[KND_MED_BUF_SIZE];
    size_t buf_size = 0;
    struct kndClass *c;
    struct kndMemPool *mempool = self->entry->repo->mempool;
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
        knd_log(".. unfreezing class: \"%.*s\".. global offset:%zu  block size:%zu",
                entry->name_size, entry->name, entry->global_offset, entry->block_size);

    /* parse DB rec */
    filename = self->entry->repo->frozen_output_file_name;
    filename_size = self->entry->repo->frozen_output_file_name_size;
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

    if (lseek(fd, entry->global_offset, SEEK_SET) == -1) {
        err = knd_IO_FAIL;
        goto final;
    }

    buf_size = entry->block_size;
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

    if (DEBUG_CONC_LEVEL_2)
        knd_log("\n== frozen Conc REC: \"%.*s\"",
                buf_size, buf);
    /* done reading */
    close(fd);

    err = mempool->new_class(mempool, &c);
    if (err) goto final;
    c->name = entry->name;
    c->name_size = entry->name_size;
    c->entry = entry;
    entry->class = c;

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

static int get_class(struct kndClass *self,
                     const char *name, size_t name_size,
                     struct kndClass **result)
{
    struct kndClassEntry *entry;
    struct kndClass *c;
    struct glbOutput *log = self->entry->repo->log;
    struct ooDict *class_name_idx = self->entry->repo->root_class->class_name_idx;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. %.*s to get class: \"%.*s\".. idx:%p",
                self->entry->name_size, self->entry->name,
                name_size, name,  class_name_idx);

    entry = class_name_idx->get(class_name_idx, name, name_size);
    if (!entry) {
        knd_log("-- no such class: \"%.*s\":(", name_size, name);
        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return err;
        err = log->write(log, " class name not found",
                               strlen(" class name not found"));
        if (err) return err;
        if (self->entry->repo->task)
            self->entry->repo->task->http_code = HTTP_NOT_FOUND;

        return knd_NO_MATCH;
    }

    if (DEBUG_CONC_LEVEL_2)
        knd_log("++ got Conc Dir: %.*s from \"%.*s\" block size: %zu conc:%p",
                name_size, name,
                self->entry->repo->frozen_output_file_name_size,
                self->entry->repo->frozen_output_file_name, entry->block_size, entry->class);

    if (entry->phase == KND_REMOVED) {
        knd_log("-- \"%s\" class was removed", name);
        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return err;
        err = log->write(log, " class was removed",
                               strlen(" class was removed"));
        if (err) return err;

        self->entry->repo->task->http_code = HTTP_GONE;
        return knd_NO_MATCH;
    }

    if (entry->class) {
        c = entry->class;
        c->next = NULL;
        if (DEBUG_CONC_LEVEL_2)
            c->str(c);

        *result = c;
        return knd_OK;
    }

    err = unfreeze_class(self, entry, &c);
    if (err) {
        knd_log("-- failed to unfreeze class: %.*s",
                entry->name_size, entry->name);
        return err;
    }

    *result = c;
    return knd_OK;
}

static int get_obj(struct kndClass *self,
                   const char *name, size_t name_size,
                   struct kndObject **result)
{
    struct kndObjEntry *entry;
    struct kndObject *obj;
    struct glbOutput *log = self->entry->repo->log;
    int err, e;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("\n\n.. \"%.*s\" class to get obj: \"%.*s\"..",
                self->entry->name_size, self->entry->name,
                name_size, name);

    if (!self->entry) {
        knd_log("-- no frozen entry rec in \"%.*s\" :(",
                self->entry->name_size, self->entry->name);
    }

    if (!self->entry->obj_name_idx) {
        knd_log("-- no obj name idx in \"%.*s\" :(", self->entry->name_size, self->entry->name);

        log->reset(log);
        e = log->write(log, self->entry->name, self->entry->name_size);
        if (e) return e;
        e = log->write(log, " class has no instances",
                             strlen(" class has no instances"));
        if (e) return e;

        return knd_FAIL;
    }

    entry = self->entry->obj_name_idx->get(self->entry->obj_name_idx, name, name_size);
    if (!entry) {
        knd_log("-- no such obj: \"%.*s\" :(", name_size, name);
        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return err;
        err = log->write(log, " obj name not found",
                               strlen(" obj name not found"));
        if (err) return err;
        self->entry->repo->task->http_code = HTTP_NOT_FOUND;
        return knd_NO_MATCH;
    }

    if (DEBUG_CONC_LEVEL_2)
        knd_log("++ got obj entry %.*s  size: %zu OBJ: %p",
                name_size, name, entry->block_size, entry->obj);

    if (!entry->obj) goto read_entry;

    if (entry->obj->state->phase == KND_REMOVED) {
        knd_log("-- \"%s\" obj was removed", name);
        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return err;
        err = log->write(log, " obj was removed",
                               strlen(" obj was removed"));
        if (err) return err;
        return knd_NO_MATCH;
    }

    obj = entry->obj;
    obj->state->phase = KND_SELECTED;
    *result = obj;
    return knd_OK;

 read_entry:
    err = read_obj_entry(self, entry, result);
    if (err) return err;

    return knd_OK;
}

static int read_obj_entry(struct kndClass *self,
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
    gsl_err_t parser_err;

    /* parse DB rec */
    filename = self->entry->repo->frozen_output_file_name;
    filename_size = self->entry->repo->frozen_output_file_name_size;
    if (!filename_size) {
        knd_log("-- no file name to read in conc %.*s :(",
                self->entry->name_size, self->entry->name);
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

    err = self->entry->repo->mempool->new_obj(self->entry->repo->mempool, &obj);                           RET_ERR();
    err = self->entry->repo->mempool->new_state(self->entry->repo->mempool, &obj->state);                  RET_ERR();

    obj->state->phase = KND_FROZEN;

    obj->base = self;
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

    parser_err = obj->read(obj, c, &chunk_size);
    if (parser_err.code) {
        knd_log("-- failed to parse obj %.*s :(",
                obj->name_size, obj->name);
        obj->del(obj);
        return gsl_err_to_knd_err_codes(parser_err);
    }

    if (DEBUG_CONC_LEVEL_2)
        obj->str(obj);

    *result = obj;
    return knd_OK;

 final:
    close(fd);
    return err;
}

static int str_conc_elem(void *obj,
                         const char *elem_id __attribute__((unused)),
                         size_t elem_id_size __attribute__((unused)),
                         size_t count __attribute__((unused)),
                         void *elem)
{
    struct kndClass *self = obj;
    struct kndClassEntry *entry = elem;
    struct kndClass *c = entry->class;
    int err;

    if (!c) {
        err = unfreeze_class(self, entry, &c);                                    RET_ERR();
    }

    c->str(c);

    return knd_OK;
}

static int export_conc_elem_JSON(void *obj,
                                 const char *elem_id,
                                 size_t elem_id_size,
                                 size_t count,
                                 void *elem)
{
    struct kndClass *self = obj;

    if (count < self->entry->repo->task->start_from) return knd_OK;
    if (self->entry->repo->task->batch_size >= self->entry->repo->task->batch_max) return knd_RANGE;

    struct glbOutput *out = self->entry->repo->out;
    struct kndClassEntry *entry = elem;
    struct kndClass *c = entry->class;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("..export elem: %.*s  conc:%p entry:%p",
                elem_id_size, elem_id, c, entry);

    if (!c) {
        err = unfreeze_class(self, entry, &c);                                      RET_ERR();
    }

    /* separator */
    if (self->entry->repo->task->batch_size) {
        err = out->writec(out, ',');                                              RET_ERR();
    }

    c->depth = 0;
    c->max_depth = 0;
    if (self->max_depth) {
        c->max_depth = self->max_depth;
    }

    err = c->export(c);
    if (err) return err;

    self->entry->repo->task->batch_size++;

    return knd_OK;
}

static int export_set_JSON(struct kndClass *self,
                           struct kndSet *set)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct glbOutput *out = self->entry->repo->out;
    int err;

    err = out->write(out, "{\"_set\":{",
                     strlen("{\"_set\":{"));                                      RET_ERR();

    /* TODO: present child clauses */

    if (set->base) {
        err = out->write(out, "\"_base\":\"",
                         strlen("\"_base\":\""));                                 RET_ERR();
        err = out->write(out, set->base->name,  set->base->name_size);            RET_ERR();
        err = out->writec(out, '"');                                              RET_ERR();
    }
    err = out->writec(out, '}');                                                  RET_ERR();

    buf_size = sprintf(buf, ",\"total\":%lu",
                       (unsigned long)set->num_elems);
    err = out->write(out, buf, buf_size);                                         RET_ERR();


    err = out->write(out, ",\"batch\":[",
                     strlen(",\"batch\":["));                                     RET_ERR();

    err = set->map(set, export_conc_elem_JSON, (void*)self);
    if (err && err != knd_RANGE) return err;

    err = out->writec(out, ']');                                                  RET_ERR();

    buf_size = sprintf(buf, ",\"batch_max\":%lu",
                       (unsigned long)self->entry->repo->task->batch_max);
    err = out->write(out, buf, buf_size);                                        RET_ERR();
    buf_size = sprintf(buf, ",\"batch_size\":%lu",
                       (unsigned long)self->entry->repo->task->batch_size);
    err = out->write(out, buf, buf_size);                                     RET_ERR();
    err = out->write(out,
                     ",\"batch_from\":", strlen(",\"batch_from\":"));         RET_ERR();
    buf_size = sprintf(buf, "%lu",
                       (unsigned long)self->entry->repo->task->batch_from);
    err = out->write(out, buf, buf_size);                                     RET_ERR();

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
    struct kndClassEntry *entry = elem;
    int err;
    err = out->writec(out, ' ');                                                  RET_ERR();
    err = out->write(out, entry->id, entry->id_size);                                 RET_ERR();
    return knd_OK;
}


static int export_facets_GSP(struct kndClass *self, struct kndSet *set)
{
    struct glbOutput *out = self->entry->repo->out;
    struct kndSet *subset;
    struct kndFacet *facet;
    struct ooDict *set_name_idx;
    const char *key;
    void *val;
    int err;

    err = out->write(out,  "[fc ", strlen("[fc "));                               RET_ERR();
    for (size_t i = 0; i < set->num_facets; i++) {
        facet = set->facets[i];
        err = out->write(out,  "{", 1);                                           RET_ERR();
        err = out->write(out, facet->attr->name, facet->attr->name_size);
        err = out->write(out,  " ", 1);                                           RET_ERR();

        if (facet->set_name_idx) {
            err = out->write(out,  "[set", strlen("[set"));                       RET_ERR();
            set_name_idx = facet->set_name_idx;
            key = NULL;
            set_name_idx->rewind(set_name_idx);
            do {
                set_name_idx->next_item(set_name_idx, &key, &val);
                if (!key) break;
                subset = (struct kndSet*)val;

                err = out->writec(out, '{');                                      RET_ERR();
                err = out->write(out, subset->base->id,
                                 subset->base->id_size);                          RET_ERR();

                err = out->write(out, "[c", strlen("[c"));                        RET_ERR();
                err = subset->map(subset, export_conc_id_GSP, (void*)out);
                if (err) return err;
                err = out->writec(out, ']');                                      RET_ERR();
                err = out->writec(out, '}');                                      RET_ERR();
            } while (key);
            err = out->write(out,  "]", 1);                                       RET_ERR();
        }
        err = out->write(out,  "}", 1);                                           RET_ERR();
    }
    err = out->write(out,  "]", 1);                                               RET_ERR();

    return knd_OK;
}

static int export_descendants_GSP(struct kndClass *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct glbOutput *out = self->entry->repo->out;
    struct kndSet *set;
    int err;

    set = self->entry->descendants;

    err = out->write(out, "{_desc", strlen("{_desc"));                            RET_ERR();
    buf_size = sprintf(buf, "{tot %zu}", set->num_elems);
    err = out->write(out, buf, buf_size);                                         RET_ERR();

    err = out->write(out, "[c", strlen("[c"));                                    RET_ERR();
    err = set->map(set, export_conc_id_GSP, (void*)out);
    if (err) return err;
    err = out->writec(out, ']');                                                  RET_ERR();

    if (set->num_facets) {
        err = export_facets_GSP(self, set);                                       RET_ERR();
    }

    err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

static gsl_err_t present_class_selection(void *obj,
                                         const char *val __attribute__((unused)),
                                         size_t val_size __attribute__((unused)))
{
    struct kndClass *self = obj;
    struct kndClass *c;
    struct kndSet *set;
    struct glbOutput *out = self->entry->repo->out;
    int err;

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log(".. presenting %.*s.. task:%p",
                self->entry->name_size, self->entry->name,
                self->entry->repo->task);

    out->reset(out);

    if (self->entry->repo->task->type == KND_SELECT_STATE) {

        if (DEBUG_CONC_LEVEL_2)
            knd_log(".. batch selection: batch size: %zu   start from: %zu",
                    self->entry->repo->task->batch_max, self->entry->repo->task->batch_from);

        /* no sets found? */
        if (!self->entry->repo->task->num_sets) {

            if (self->curr_baseclass && self->curr_baseclass->entry->descendants) {
                set = self->curr_baseclass->entry->descendants;

                err = export_set_JSON(self, set);
                if (err) return make_gsl_err_external(err);

                return make_gsl_err(gsl_OK);
            }

            err = out->write(out, "{}", strlen("{}"));
            if (err) return make_gsl_err_external(err);
            return make_gsl_err(gsl_OK);
        }

        /* TODO: intersection cache lookup  */
        set = self->entry->repo->task->sets[0];

        /* intersection required */
        if (self->entry->repo->task->num_sets > 1) {
            err = self->entry->repo->mempool->new_set(self->entry->repo->mempool, &set);
            if (err) return make_gsl_err_external(err);

            set->type = KND_SET_CLASS;
            set->mempool = self->entry->repo->mempool;

            // TODO: compound class
            set->base = self->entry->repo->task->sets[0]->base;

            err = set->intersect(set, self->entry->repo->task->sets, self->entry->repo->task->num_sets);
            if (err) return make_gsl_err_external(err);
        }

        if (!set->num_elems) {
            err = out->write(out, "{}", strlen("{}"));
            if (err) return make_gsl_err_external(err);
            return make_gsl_err(gsl_OK);
        }

        /* final presentation in JSON 
           TODO: choose output format */
        err = export_set_JSON(self, set);
        if (err) return make_gsl_err_external(err);

        return make_gsl_err(gsl_OK);
    }

    if (!self->curr_class) {
        knd_log("-- no class to present :(");
        return make_gsl_err(gsl_FAIL);
    }

    c = self->curr_class;
    c->depth = 0;
    c->max_depth = 1;
    if (self->max_depth) {
        c->max_depth = self->max_depth;
    }

    err = c->export(c);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log("++ class presentation OK!");

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_get_class(void *obj, const char *name, size_t name_size)
{
    struct kndClass *self = obj;
    struct kndClass *c;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    self->curr_class = NULL;
    err = get_class(self, name, name_size, &c);
    if (err) return make_gsl_err_external(err);

    self->curr_class = c;

    if (DEBUG_CONC_LEVEL_2) {
        c->str(c);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_get_class_by_numid(void *obj, const char *id, size_t id_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct kndClass *self = obj;
    struct kndClass *c;
    struct kndClassEntry *entry;
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
    entry = result;

    self->curr_class = NULL;
    err = get_class(self, entry->name, entry->name_size, &c);
    if (err) return make_gsl_err_external(err);

    self->curr_class = c;

    if (DEBUG_CONC_LEVEL_2) {
        c->str(c);
    }

    return make_gsl_err(gsl_OK);
}


static gsl_err_t run_remove_class(void *obj, const char *name, size_t name_size)
{
    struct kndClass *self = obj;
    struct kndClass *c;
    struct glbOutput *log = self->entry->repo->log;
    int err;

    if (DEBUG_CONC_LEVEL_1)
        knd_log(".. remove class..");

    if (!self->curr_class) {
        knd_log("-- remove operation: class name not specified:(");

        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return make_gsl_err_external(err);
        err = log->write(log, " class name not specified",
                               strlen(" class name not specified"));
        if (err) return make_gsl_err_external(err);
        return make_gsl_err(gsl_NO_MATCH);
    }

    c = self->curr_class;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== class to remove: \"%.*s\"\n", c->name_size, c->name);

    c->entry->phase = KND_REMOVED;

    log->reset(log);
    err = log->write(log, name, name_size);
    if (err) return make_gsl_err_external(err);
    err = log->write(log, " class removed",
                           strlen(" class removed"));
    if (err) return make_gsl_err_external(err);

    c->next = self->inbox;
    self->inbox = c;
    self->inbox_size++;

    return make_gsl_err(gsl_OK);
}


static int select_delta(struct kndClass *self,
                        const char *rec,
                        size_t *total_size)
{
    struct kndStateControl *state_ctrl = self->entry->repo->state_ctrl;
    struct kndUpdate *update;
    struct kndClassUpdate *class_update;
    struct kndClass *c;
    int err;
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .name = "eq",
          .name_size = strlen("eq"),
          .parse = gsl_parse_size_t,
          .obj = &self->entry->repo->task->batch_eq
        },
        { .name = "gt",
          .name_size = strlen("gt"),
          .parse = gsl_parse_size_t,
          .obj = &self->entry->repo->task->batch_gt
        },
        { .name = "lt",
          .name_size = strlen("lt"),
          .parse = gsl_parse_size_t,
          .obj = &self->entry->repo->task->batch_lt
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return gsl_err_to_knd_err_codes(parser_err);

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. select delta:  gt %zu  lt %zu ..",
                self->entry->repo->task->batch_gt,
                self->entry->repo->task->batch_lt);

    err = state_ctrl->select(state_ctrl);                                         RET_ERR();

    for (size_t i = 0; i < state_ctrl->num_selected; i++) {
        update = state_ctrl->selected[i];

        for (size_t j = 0; j < update->num_classes; j++) {
            class_update = update->classes[j];
            c = class_update->conc;

            //c->str(c);
            if (!c) return knd_FAIL;

            /*if (!self->curr_baseclass) {
                self->entry->repo->task->class_selects[self->entry->repo->task->num_class_selects] = c;
                self->entry->repo->task->num_class_selects++;
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
    struct kndClass *self = data;
    int err;

    err = select_delta(self, rec, total_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_select_class(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndClass *c;
    struct glbOutput *log = self->entry->repo->log;
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
        { .type = GSL_SET_STATE,
          .is_validator = true,
          .validate = parse_set_attr,
          .obj = self
          }*/,
        { .type = GSL_SET_STATE,
          .name = "_rm",
          .name_size = strlen("_rm"),
          .run = run_remove_class,
          .obj = self
        },
        { .type = GSL_SET_STATE,
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
                    parser_err.code, log->buf_size, log->buf);
        if (!log->buf_size) {
            err = log->write(log, "class parse failure",
                                 strlen("class parse failure"));
            if (err) return make_gsl_err_external(err);
        }

        /* TODO: release resources */
        if (self->curr_class) {
            c = self->curr_class;
            c->reset_inbox(c);
        }
        return parser_err;
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

    return make_gsl_err(gsl_OK);
}

static int get_class_attr_value(struct kndClass *src,
                                struct kndAttrVar *query,
                                struct kndProcCallArg *arg)
{
    struct kndAttrEntry *entry;
    struct kndAttrVar *child_var;
    struct ooDict *attr_name_idx = src->attr_name_idx;
    int err;

    //src->str(src);

    entry = attr_name_idx->get(attr_name_idx,
                               query->name, query->name_size);
    if (!entry) {
        knd_log("-- no such attr: %.*s", query->name_size, query->name);
        return knd_FAIL;
    }

    if (DEBUG_CONC_LEVEL_TMP) {
        knd_log("++ got attr: %.*s  attr var:%p",
                query->name_size, query->name, entry->attr_var);
    }

    if (!entry->attr_var) return knd_FAIL;

    str_attr_vars(entry->attr_var, 2);

    /* no more query specs */
    if (!query->num_children) return knd_OK;

    /* check nested attrs */

    // TODO: list item selection
    for (child_var = entry->attr_var->list; child_var; child_var = child_var->next) {
        err = get_arg_value(child_var, query->children, arg);
        if (err) return err;
    }

    return knd_OK;
}

static int get_arg_value(struct kndAttrVar *src,
                         struct kndAttrVar *query,
                         struct kndProcCallArg *arg)
{
    struct kndAttrVar *curr_var;
    struct kndAttr *attr;

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log(".. from \"%.*s\" extract field: \"%.*s\"",
                src->name_size, src->name,
                query->name_size, query->name);

    str_attr_vars(src, 2);

    /* check implied attr */
    if (src->implied_attr) {
        attr = src->implied_attr;
        //attr->str(attr);
        if (!memcmp(attr->name, query->name, query->name_size)) {
            switch (attr->type) {
            case KND_ATTR_REF:
                //knd_log("++ match ref: %.*s",
                //        src->class->name_size, src->class->name);

                return get_class_attr_value(src->class, query->children, arg);
                break;
            default:
                break;
            }
        }
    }

    /* iterate children */
    for (curr_var = src->children; curr_var; curr_var = curr_var->next) {

        knd_log("== child:%.*s val: %.*s",
                curr_var->name_size, curr_var->name,
                curr_var->val_size, curr_var->val);
        
        if (curr_var->implied_attr) {
            attr = curr_var->implied_attr;
            attr->str(attr);
        }

        if (curr_var->name_size != query->name_size) continue;

        if (!strncmp(curr_var->name, query->name, query->name_size)) {

            knd_log("++ match: %.*s numval:%zu",
                    curr_var->val_size, curr_var->val, curr_var->numval);

            arg->numval = curr_var->numval;

            if (!query->num_children) return knd_OK;
            knd_log(".. checking nested attrs..\n\n");
            
        }
    }
                
    return knd_OK;
}

static int compute_num_value(struct kndClass *self,
                             struct kndAttr *attr,
                             struct kndAttrVar *attr_var,
                             long *result)
{
    struct kndProcCall *proc_call;
    struct kndProcCallArg *arg;
    struct kndClassVar *class_var;
    long numval = 0;
    long times = 0;
    long quant = 0;
    int err;

    proc_call = &attr->proc->proc_call;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("\nPROC CALL: \"%.*s\"",
                proc_call->name_size, proc_call->name);

    for (arg = proc_call->args; arg; arg = arg->next) {
        class_var = arg->class_var;

        if (DEBUG_CONC_LEVEL_TMP)
            knd_log("ARG: %.*s",
                    arg->name_size, arg->name);

        err = get_arg_value(attr_var, class_var->attrs, arg);
        if (err) return err;

        if (!strncmp("times", arg->name, arg->name_size)) {
            times = arg->numval;
            knd_log("TIMES:%lu", arg->numval);
        }

        if (!strncmp("quant", arg->name, arg->name_size)) {
            quant = arg->numval;
            knd_log("QUANT:%lu", arg->numval);
        }
    }

    /* run main proc */
    switch (proc_call->type) {
        /* multiplication */
    case KND_PROC_MULT:
        numval = (times * quant);
        break;
    case KND_PROC_MULT_PERCENT:
        numval = (times * quant) / 100;
        break;
    default:
        break;
    }
    
    *result = numval;

    return knd_OK;
}

static int present_computed_attrs(struct kndClass *self,
                                  struct kndAttrVar *attr_var)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct glbOutput *out = self->entry->repo->out;
    struct kndClass *c = attr_var->attr->conc;
    struct kndAttr *attr;
    struct kndAttrVar *item;
    long numval;
    int err;

    for (item = attr_var->children; item; item = item->next) {
        knd_log("~~ child: \"%.*s\" VAL:%.*s",
                item->name_size, item->name,
                item->val_size, item->val);
    }

    for (size_t i = 0; i < c->num_computed_attrs; i++) {
        attr = c->computed_attrs[i];

        switch (attr->type) {
        case KND_ATTR_NUM:

            err = compute_num_value(self, attr, attr_var, &numval);
            // TODO: signal failure
            if (err) continue;

            // TODO: memoization
            
            err = out->writec(out, ',');
            if (err) return err;
            err = out->writec(out, '"');
            if (err) return err;
            err = out->write(out, attr->name, attr->name_size);
            if (err) return err;
            err = out->writec(out, '"');
            if (err) return err;
            err = out->writec(out, ':');
            if (err) return err;
            
            buf_size = snprintf(buf, KND_NAME_SIZE, "%lu", numval);
            err = out->write(out, buf, buf_size);                                     RET_ERR();
            break;
        default:
            break;
        }
    }

    return knd_OK;
}

static int aggr_item_export_JSON(struct kndClass *self,
                                 struct kndAttrVar *parent_item)
{
    struct glbOutput *out = self->entry->repo->out;
    struct kndAttrVar *item;
    struct kndAttr *attr;
    struct kndClass *c;
    bool in_list = false;
    int err;

    c = parent_item->attr->parent_class;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log(".. JSON export aggr item: %.*s",
                parent_item->name_size, parent_item->name);
        //c = parent_item->attr->parent_class;
        //c->str(c);
        c->str(c);
        knd_log("== comp attrs:%zu",
                c->num_computed_attrs);
    }

    err = out->writec(out, '{');
    if (err) return err;

    if (parent_item->id_size) {
        err = out->write(out, "\"_id\":", strlen("\"_id\":"));
        if (err) return err;
        err = out->writec(out, '"');
        if (err) return err;
        err = out->write(out, parent_item->id, parent_item->id_size);
        if (err) return err;
        err = out->writec(out, '"');
        if (err) return err;
        in_list = true;

        c = parent_item->attr->conc;
        if (c->num_computed_attrs) {

            knd_log("\n..present computed attrs in %.*s (val:%.*s)",
                    parent_item->name_size, parent_item->name,
                    parent_item->val_size, parent_item->val);

            err = present_computed_attrs(self, parent_item);
            if (err) return err;
        }
   }

    
    /* export a class ref */
    if (parent_item->class) {
        attr = parent_item->attr;
        c = parent_item->attr->conc;

        /* TODO: check assignment */
        if (parent_item->implied_attr) {
            attr = parent_item->implied_attr;
        }

        if (c->implied_attr)
            attr = c->implied_attr;

        c = parent_item->class;

        if (in_list) {
            err = out->writec(out, ',');
            if (err) return err;
        }

        err = out->writec(out, '"');
        if (err) return err;
        err = out->write(out, attr->name, attr->name_size);
        if (err) return err;
        err = out->writec(out, '"');
        if (err) return err;
        err = out->writec(out, ':');
        if (err) return err;

        c->depth = self->depth + 1;
        c->max_depth = self->max_depth;

        err = c->export(c);
        if (err) return err;
        in_list = true;
    }

    if (!parent_item->class) {

        /* terminal string value */
        if (parent_item->val_size) {
            c = parent_item->attr->conc;
            attr = parent_item->attr;

            if (c->implied_attr) {
                attr = c->implied_attr;
                err = out->writec(out, '"');
                if (err) return err;
                err = out->write(out, attr->name, attr->name_size);
                if (err) return err;
                err = out->writec(out, '"');
                if (err) return err;
                err = out->writec(out, ':');
                if (err) return err;
            } else {
                err = out->write(out, "\"_val\":", strlen("\"_val\":"));
                if (err) return err;
            }

            /* string or numeric value? */
            switch (attr->type) {
            case KND_ATTR_NUM:
                err = out->write(out, parent_item->val, parent_item->val_size);
                if (err) return err;
                break;
            default:
                err = out->writec(out, '"');
                if (err) return err;
                err = out->write(out, parent_item->val, parent_item->val_size);
                if (err) return err;
                err = out->writec(out, '"');
                if (err) return err;
            }
            in_list = true;
        }
    }

    for (item = parent_item->children; item; item = item->next) {
        if (in_list) {
            err = out->writec(out, ',');
            if (err) return err;
        }

        err = out->writec(out, '"');
        if (err) return err;
        err = out->write(out, item->name, item->name_size);
        if (err) return err;
        err = out->writec(out, '"');
        if (err) return err;
        err = out->writec(out, ':');
        if (err) return err;

        switch (item->attr->type) {
        case KND_ATTR_REF:
            c = item->class;
            c->depth = self->depth;
            c->max_depth = self->max_depth;
            err = c->export(c);
            if (err) return err;
            break;
        case KND_ATTR_AGGR:
            err = aggr_item_export_JSON(self, item);
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

    err = out->writec(out, '}');
    if (err) return err;

    return knd_OK;
}

static int ref_item_export_JSON(struct kndClass *self,
                                struct kndAttrVar *item)
{
    struct kndClass *c;
    int err;

    assert(item->class != NULL);

    c = item->class;
    c->depth = self->depth + 1;
    c->max_depth = self->max_depth;
    err = c->export(c);
    if (err) return err;

    return knd_OK;
}

static int proc_item_export_JSON(struct kndClass *self,
                                 struct kndAttrVar *item)
{
    struct kndProc *proc;
    int err;

    assert(item->proc != NULL);

    proc = item->proc;
    proc->depth = self->depth + 1;
    proc->max_depth = self->max_depth;
    err = proc->export(proc);
    if (err) return err;

    return knd_OK;
}

static int attr_var_list_export_JSON(struct kndClass *self,
                                     struct kndAttrVar *parent_item)
{
    struct glbOutput *out = self->entry->repo->out;
    struct kndAttrVar *item;
    bool in_list = false;
    size_t count = 0;
    int err;

    if (DEBUG_CONC_LEVEL_TMP) {
        knd_log(".. export JSON list: %.*s\n\n",
                parent_item->name_size, parent_item->name);
    }

    err = out->writec(out, '"');
    if (err) return err;
    err = out->write(out, parent_item->name, parent_item->name_size);
    if (err) return err;
    err = out->write(out, "\":[", strlen("\":["));
    if (err) return err;

    /* first elem: TODO */
    if (parent_item->class) {
        switch (parent_item->attr->type) {
        case KND_ATTR_AGGR:

            parent_item->id_size = sprintf(parent_item->id, "%lu",
                                           (unsigned long)count);
            count++;

            err = aggr_item_export_JSON(self, parent_item);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            err = ref_item_export_JSON(self, parent_item);
            if (err) return err;
            break;
        case KND_ATTR_PROC:
            if (parent_item->proc) {
                err = proc_item_export_JSON(self, parent_item);
                if (err) return err;
            }
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

        switch (parent_item->attr->type) {
        case KND_ATTR_AGGR:
            item->id_size = sprintf(item->id, "%lu",
                                    (unsigned long)count);
            count++;

            err = aggr_item_export_JSON(self, item);
            if (err) return err;
            break;
        case KND_ATTR_REF:
            err = ref_item_export_JSON(self, item);
            if (err) return err;
            break;
        case KND_ATTR_PROC:
            if (item->proc) {
                err = proc_item_export_JSON(self, item);
                if (err) return err;
            }
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

static int attr_vars_export_JSON(struct kndClass *self,
                                  struct kndAttrVar *items,
                                  size_t depth __attribute__((unused)))
{
    struct kndAttrVar *item;
    struct glbOutput *out;
    struct kndClass *c;
    int err;

    out = self->entry->repo->out;

    for (item = items; item; item = item->next) {
        err = out->writec(out, ',');
        if (err) return err;

        if (item->attr && item->attr->is_a_set) {
            err = attr_var_list_export_JSON(self, item);
            if (err) return err;
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
        case KND_ATTR_PROC:
            if (item->proc) {
                err = proc_item_export_JSON(self, item);
                if (err) return err;
            } else {
                err = out->write(out, "\"", strlen("\""));
                if (err) return err;
                err = out->write(out, item->val, item->val_size);
                if (err) return err;
                err = out->write(out, "\"", strlen("\""));
                if (err) return err;
            }
            break;
        case KND_ATTR_AGGR:

            if (!item->class) {
                err = aggr_item_export_JSON(self, item);
                if (err) return err;
            } else {
                c = item->class;
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
    }


    return knd_OK;
}

static int export_gloss_JSON(struct kndClass *self)
{
    struct kndTranslation *tr;
    struct glbOutput *out = self->entry->repo->out;
    int err;

    for (tr = self->tr; tr; tr = tr->next) {
        if (memcmp(self->entry->repo->task->locale, tr->locale, tr->locale_size)) {
            continue;
        }

        err = out->write(out, ",\"_gloss\":\"", strlen(",\"_gloss\":\""));        RET_ERR();
        err = out->write_escaped(out, tr->val,  tr->val_size);                    RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();
        break;
    }

    for (tr = self->summary; tr; tr = tr->next) {
        if (memcmp(self->entry->repo->task->locale, tr->locale, tr->locale_size)) {
            continue;
        }
        err = out->write(out, ",\"_summary\":\"", strlen(",\"_summary\":\""));    RET_ERR();
        err = out->write_escaped(out, tr->val,  tr->val_size);                    RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();
        break;
    }

    return knd_OK;
}

static int export_concise_JSON(struct kndClass *self)
{
    struct kndClassVar *item;
    struct kndAttrVar *attr_var;
    struct kndAttr *attr;
    struct kndClass *c;
    struct glbOutput *out = self->entry->repo->out;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. export concise JSON for %.*s..",
                self->entry->name_size, self->entry->name, self->entry->repo->out);

    for (item = self->baseclass_vars; item; item = item->next) {
        for (attr_var = item->attrs; attr_var; attr_var = attr_var->next) {

            /* TODO assert */
            if (!attr_var->attr) continue;

            attr = attr_var->attr;

            if (!attr->concise_level) continue;

            if (attr->is_a_set) {
                err = out->writec(out, ',');                                      RET_ERR();
                err = attr_var_list_export_JSON(self, attr_var);
                if (err) return err;
                continue;
            }

            /* single elem concise representation */
            err = out->writec(out, ',');                                          RET_ERR();
            err = out->writec(out, '"');                                          RET_ERR();

            err = out->write(out, attr_var->name, attr_var->name_size);
            if (err) return err;
            err = out->write(out, "\":", strlen("\":"));
            if (err) return err;

            switch (attr->type) {
            case KND_ATTR_NUM:
                err = out->write(out, attr_var->val, attr_var->val_size);
                if (err) return err;
                break;
            case KND_ATTR_REF:
                c = attr_var->class;
                if (c) {
                    c->depth = self->depth;
                    c->max_depth = self->max_depth;
                    err = c->export(c);
                    if (err) return err;
                } else {
                    err = out->write(out, "\"", strlen("\""));
                    if (err) return err;
                    err = out->write(out, attr_var->val, attr_var->val_size);
                    if (err) return err;
                    err = out->write(out, "\"", strlen("\""));
                    if (err) return err;
                }
                break;
            case KND_ATTR_AGGR:
                err = aggr_item_export_JSON(self, attr_var);
                if (err) return err;
                break;
            default:
                err = out->write(out, "\"", strlen("\""));
                if (err) return err;
                err = out->write(out, attr_var->val, attr_var->val_size);
                if (err) return err;
                err = out->write(out, "\"", strlen("\""));
                if (err) return err;
            }
        }
    }

    return knd_OK;
}

static int export_JSON(struct kndClass *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndAttr *attr;

    struct kndClass *c;
    struct kndClassVar *item;
    struct kndClassEntry *entry;

    //struct kndUpdate *update;
    //struct tm tm_info;

    struct glbOutput *out;
    size_t item_count;
    int i, err;

    out = self->entry->repo->out;

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log(".. JSON export: \"%.*s\"  ",
                self->entry->name_size, self->entry->name);

    err = out->write(out, "{", 1);                                                RET_ERR();
    err = out->write(out, "\"_name\":\"", strlen("\"_name\":\""));                RET_ERR();

    err = out->write_escaped(out, self->entry->name, self->entry->name_size);         RET_ERR();

    err = out->write(out, "\"", 1);                                               RET_ERR();

    err = out->write(out, ",\"_id\":", strlen(",\"_id\":"));                      RET_ERR();

    buf_size = snprintf(buf, KND_NAME_SIZE, "%zu", self->entry->numid);
    err = out->write(out, buf, buf_size);                                         RET_ERR();

    err = export_gloss_JSON(self);                                                RET_ERR();

    if (self->depth >= self->max_depth) {

        //knd_log(".. export concise fields: %p   CURR OUTPUT: %.*\n\n",
        //        self->entry->repo->out, self->entry->repo->out->buf_size, self->entry->repo->out->buf);

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
    if (self->num_baseclass_vars) {
        err = out->write(out, ",\"_base\":[", strlen(",\"_base\":["));            RET_ERR();

        item_count = 0;
        for (item = self->baseclass_vars; item; item = item->next) {
            //if (item->class && item->class->ignore_children) continue;
            if (item_count) {
                err = out->write(out, ",", 1);                                    RET_ERR();
            }

            err = out->write(out, "{\"_name\":\"", strlen("{\"_name\":\""));              RET_ERR();
            err = out->write(out, item->entry->name, item->entry->name_size);
            if (err) return err;
            err = out->write(out, "\"", 1);
            if (err) return err;

            err = out->write(out, ",\"_id\":", strlen(",\"_id\":"));
            if (err) return err;
            buf_size = snprintf(buf, KND_NAME_SIZE, "%zu", item->numid);
            err = out->write(out, buf, buf_size);
            if (err) return err;

            if (item->attrs) {
                err = attr_vars_export_JSON(self, item->attrs, 0);
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
    if (self->entry->num_children) {
        err = out->write(out, ",\"_num_subclasses\":",
                         strlen(",\"_num_subclasses\":"));
        if (err) return err;

        buf_size = sprintf(buf, "%zu", self->entry->num_children);
        err = out->write(out, buf, buf_size);
        if (err) return err;

        if (self->entry->num_terminals) {
            err = out->write(out, ",\"_num_terminals\":",
                             strlen(",\"_num_terminals\":"));
            if (err) return err;
            buf_size = sprintf(buf, "%zu", self->entry->num_terminals);
            err = out->write(out, buf, buf_size);
            if (err) return err;
        }

        if (self->entry->num_children) {
            err = out->write(out, ",\"_subclasses\":[",
                             strlen(",\"_subclasses\":["));
            if (err) return err;

            for (size_t i = 0; i < self->entry->num_children; i++) {
                entry = self->entry->children[i];
                if (i) {
                    err = out->write(out, ",", 1);
                    if (err) return err;
                }
                err = out->write(out, "{\"_name\":\"", strlen("{\"_name\":\""));
                if (err) return err;
                err = out->write(out, entry->name, entry->name_size);
                if (err) return err;
                err = out->write(out, "\"", 1);
                if (err) return err;

                err = out->write(out, ",\"_id\":", strlen(",\"_id\":"));
                if (err) return err;
                buf_size = sprintf(buf, "%zu", entry->numid);
                err = out->write(out, buf, buf_size);
                if (err) return err;

                if (entry->num_terminals) {
                    err = out->write(out, ",\"_num_terminals\":",
                                     strlen(",\"_num_terminals\":"));
                    if (err) return err;
                    buf_size = sprintf(buf, "%zu", entry->num_terminals);
                    err = out->write(out, buf, buf_size);
                    if (err) return err;
                }

                /* localized glosses */
                c = entry->class;
                if (!c) {
                    err = unfreeze_class(self, entry, &c);                          RET_ERR();
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

 final:
    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}

static int ref_list_export_GSP(struct kndClass *self,
                               struct kndAttrVar *parent_item)
{
    struct kndAttrVar *item;
    struct glbOutput *out;
    struct kndClass *c;
    int err;

    out = self->entry->repo->out;

    err = out->writec(out, '{');
    if (err) return err;
    err = out->write(out, parent_item->name, parent_item->name_size);
    if (err) return err;

    err = out->write(out, "[r", strlen("[r"));
    if (err) return err;

    /* first elem */
    if (parent_item->val_size) {
        c = parent_item->class;
        if (c) {
            err = out->writec(out, ' ');
            if (err) return err;
            err = out->write(out, c->entry->id, c->entry->id_size);
            if (err) return err;
        }
    }

    for (item = parent_item->list; item; item = item->next) {
        c = item->class;
        if (!c) continue;

        err = out->writec(out, ' ');
        if (err) return err;
        err = out->write(out, c->entry->id, c->entry->id_size);
        if (err) return err;
    }
    err = out->writec(out, ']');
    if (err) return err;

    err = out->writec(out, '}');
    if (err) return err;

    return knd_OK;
}

static int aggr_item_export_GSP(struct kndClass *self,
                                struct kndAttrVar *parent_item)
{
    struct glbOutput *out = self->entry->repo->out;
    struct kndClass *c = parent_item->class;
    struct kndAttrVar *item;
    struct kndAttr *attr;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== GSP export of aggr item: %.*s val:%.*s",
                parent_item->name_size, parent_item->name,
                parent_item->val_size, parent_item->val);

    err = out->writec(out, '{');
    if (err) return err;

    if (c) {
        err = out->write(out, c->entry->id, c->entry->id_size);
        if (err) return err;
    } else {

        /* terminal value */
        if (parent_item->val_size) {
            c = parent_item->attr->conc;
            if (c && c->implied_attr) {
                attr = c->implied_attr;
                err = out->writec(out, ' ');
                if (err) return err;
                err = out->write(out,
                                 parent_item->val,
                                 parent_item->val_size);
                if (err) return err;
            } else {
                err = out->writec(out, ' ');
                if (err) return err;
                err = out->write(out,
                                 parent_item->val,
                                 parent_item->val_size);
                if (err) return err;
            }
        } else {
            err = out->write(out,
                             parent_item->id,
                             parent_item->id_size);
            if (err) return err;
        }
    }

    c = parent_item->attr->conc;

    for (item = parent_item->children; item; item = item->next) {
        err = out->writec(out, '{');
        if (err) return err;

        err = out->write(out, item->name, item->name_size);
        if (err) return err;

        switch (item->attr->type) {
        case KND_ATTR_REF:
            //knd_log("ref item: %.*s", item->name_size, item->name);
            break;
        case KND_ATTR_AGGR:
            err = aggr_item_export_GSP(self, item);
            if (err) return err;
            break;
        default:
            err = out->writec(out, ' ');
            if (err) return err;
            err = out->write(out, item->val, item->val_size);
            if (err) return err;
            break;
        }

        err = out->writec(out, '}');
        if (err) return err;
    }

    //if (parent_item->class) {
    err = out->writec(out, '}');
    if (err) return err;

    return knd_OK;
}

static int aggr_list_export_GSP(struct kndClass *self,
                                struct kndAttrVar *parent_item)
{
    struct kndAttrVar *item;
    struct glbOutput *out = self->entry->repo->out;
    struct kndClass *c;
    int err;

    if (DEBUG_CONC_LEVEL_1) {
        knd_log(".. export aggr list: %.*s  val:%.*s",
                parent_item->name_size, parent_item->name,
                parent_item->val_size, parent_item->val);
    }

    err = out->writec(out, '[');
    if (err) return err;
    err = out->write(out, parent_item->name, parent_item->name_size);
    if (err) return err;

    /* first elem */
    /*if (parent_item->class) {
        c = parent_item->class;
        if (c) { */
    if (parent_item->class) {
        err = aggr_item_export_GSP(self, parent_item);
        if (err) return err;
    }

    for (item = parent_item->list; item; item = item->next) {
        c = item->class;

        err = aggr_item_export_GSP(self, item);
        if (err) return err;
    }

    err = out->writec(out, ']');
    if (err) return err;

    return knd_OK;
}

static int attr_vars_export_GSP(struct kndClass *self,
                                 struct kndAttrVar *items,
                                 size_t depth  __attribute__((unused)))
{
    struct kndAttrVar *item;
    struct glbOutput *out;
    int err;

    out = self->entry->repo->out;

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
            err = attr_vars_export_GSP(self, item->children, 0);
            if (err) return err;
        }
        err = out->write(out, "}", 1);
        if (err) return err;
    }

    return knd_OK;
}

static int export_GSP(struct kndClass *self)
{
    struct kndAttr *attr;
    struct kndClassVar *item;
    struct kndTranslation *tr;
    struct glbOutput *out = self->entry->repo->out;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. GSP export of \"%.*s\" [%.*s]",
                self->entry->name_size, self->entry->name,
                self->entry->id_size, self->entry->id);

    err = out->writec(out, '{');
    if (err) return err;

    err = out->write(out, self->entry->id, self->entry->id_size);
    if (err) return err;

    err = out->writec(out, ' ');
    if (err) return err;

    err = out->write(out, self->entry->name, self->entry->name_size);
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

    if (self->summary) {
        err = out->write(out, "[_summary", strlen("[_summary"));
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

    if (self->baseclass_vars) {
        err = out->write(out, "[_ci", strlen("[_ci"));
        if (err) return err;

        for (item = self->baseclass_vars; item; item = item->next) {
            err = out->writec(out, '{');
            if (err) return err;
            err = out->write(out, item->entry->id, item->entry->id_size);
            if (err) return err;

            if (item->attrs) {
              err = attr_vars_export_GSP(self, item->attrs, 0);
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
            attr->out = self->entry->repo->out;
            attr->format = KND_FORMAT_GSP;
            err = attr->export(attr);
            if (err) return err;
        }
    }

    if (self->entry->descendants) {
        err = export_descendants_GSP(self);                                      RET_ERR();
    }

    err = out->writec(out, '}');
    if (err) return err;

    return knd_OK;
}

static int build_class_updates(struct kndClass *self,
                               struct kndUpdate *update)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size;
    struct glbOutput *out = self->entry->repo->task->update;
    struct kndClass *c;
    struct kndObject *obj;
    struct kndClassUpdate *class_update;
    int err;

    for (size_t i = 0; i < update->num_classes; i++) {
        class_update = update->classes[i];
        c = class_update->conc;

        err = out->write(out, "{class ", strlen("{class "));   RET_ERR();
        err = out->write(out, c->name, c->name_size);

        err = out->write(out, "(id ", strlen("(id "));         RET_ERR();
        buf_size = sprintf(buf, "%zu", c->entry->numid);
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

static int export_updates(struct kndClass *self,
                          struct kndUpdate *update)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size;
    struct tm tm_info;
    struct glbOutput *out = self->entry->repo->task->update;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. export updates in \"%.*s\"..",
                self->entry->name_size, self->entry->name);

    out->reset(out);
    err = out->write(out, "{task{update", strlen("{task{update"));               RET_ERR();

    localtime_r(&update->timestamp, &tm_info);
    buf_size = strftime(buf, KND_NAME_SIZE,
                        "{_ts %Y-%m-%d %H:%M:%S}", &tm_info);
    err = out->write(out, buf, buf_size);                                        RET_ERR();

    err = out->write(out, "{user", strlen("{user"));                             RET_ERR();

    /* spec body */
    err = out->write(out,
                     self->entry->repo->task->update_spec,
                     self->entry->repo->task->update_spec_size);                  RET_ERR();

    /* state information */
    err = out->write(out, "(state ", strlen("(state "));                          RET_ERR();
    buf_size = sprintf(buf, "%zu", update->numid);
    err = out->write(out, buf, buf_size);                                         RET_ERR();

    if (update->num_classes) {
        err = build_class_updates(self, update);                                  RET_ERR();
    }

    if (self->rel && self->rel->inbox_size) {
        self->rel->out = out;
        err = self->rel->export_updates(self->rel);                               RET_ERR();
    }

    err = out->write(out, ")}}}}", strlen(")}}}}"));                              RET_ERR();
    return knd_OK;
}

static gsl_err_t set_liquid_class_id(void *obj, const char *val, size_t val_size)
{
    struct kndClass *self = (struct kndClass*)obj;
    struct kndClass *c;
    long numval = 0;
    int err;

    if (!val_size) return make_gsl_err(gsl_FORMAT);

    if (!self->curr_class) return make_gsl_err(gsl_FAIL);
    c = self->curr_class;

    err = knd_parse_num((const char*)val, &numval);
    if (err) return make_gsl_err_external(err);

    c->entry->numid = numval;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. set curr liquid class id: %zu  update id: %zu",
                c->entry->numid, c->entry->numid);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_get_liquid_class(void *obj, const char *name, size_t name_size)
{
    struct kndClass *self = obj;
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
    struct kndClass *self = obj;
    struct kndUpdate *update = self->curr_update;
    struct kndClass *c;
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

    if (!self->curr_class) return *total_size = 0, make_gsl_err(gsl_FAIL);

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    c = self->curr_class;

    /* register class update */
    err = self->entry->repo->mempool->new_class_update(self->entry->repo->mempool, &class_update);
    if (err) return make_gsl_err_external(err);
    class_update->conc = c;

    update->classes[update->num_classes] = class_update;
    update->num_classes++;

    err = self->entry->repo->mempool->new_class_update_ref(self->entry->repo->mempool, &class_update_ref);
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
    struct kndClass *self = obj;
    struct kndClassUpdate **class_updates;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log("..  liquid class update REC: \"%.*s\"..", 32, rec); }

    if (!self->curr_update) return *total_size = 0, make_gsl_err(gsl_FAIL);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_get_liquid_class,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "id",
          .name_size = strlen("id"),
          .parse = parse_liquid_class_id,
          .obj = self
        }
    };

    /* create index of class updates */
    class_updates = realloc(self->curr_update->classes,
                            (self->inbox_size * sizeof(struct kndClassUpdate*)));
    if (!class_updates) return *total_size = 0, make_gsl_err_external(knd_NOMEM);
    self->curr_update->classes = class_updates;

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t parse_liquid_rel_update(void *obj,
                                         const char *rec, size_t *total_size)
{
    struct kndClass *self = obj;
    int err;

    if (!self->curr_update) return *total_size = 0, make_gsl_err_external(knd_FAIL);

    self->rel->curr_update = self->curr_update;
    err = self->rel->parse_liquid_updates(self->rel, rec, total_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t new_liquid_update(void *obj, const char *val, size_t val_size)
{
    struct kndClass *self = obj;
    struct kndUpdate *update;
    long numval = 0;
    int err;

    assert(val[val_size] == '\0');

    err = knd_parse_num((const char*)val, &numval);
    if (err) return make_gsl_err_external(err);

    err = self->entry->repo->mempool->new_update(self->entry->repo->mempool, &update);
    if (err) return make_gsl_err_external(err);

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== new class update: %zu", update->id);

    self->curr_update = update;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t apply_liquid_updates(struct kndClass *self,
                                      const char *rec,
                                      size_t *total_size)
{
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct kndRel *rel;
    struct kndStateControl *state_ctrl = self->entry->repo->state_ctrl;
    struct kndMemPool *mempool = self->entry->repo->mempool;

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

            err = c->resolve(c, NULL);
            if (err) return *total_size = 0, make_gsl_err_external(err);

            err = mempool->new_class_entry(mempool, &entry);
            if (err) return *total_size = 0, make_gsl_err_external(err);
            entry->class = c;

            err = self->class_name_idx->set(self->class_name_idx,
                                            c->entry->name, c->name_size,
                                            (void*)entry);
            if (err) return *total_size = 0, make_gsl_err_external(err);
        }
    }

    if (self->rel->inbox_size) {
        for (rel = self->rel->inbox; rel; rel = rel->next) {
            err = rel->resolve(rel);
            if (err) return *total_size = 0, make_gsl_err_external(err);
        }
    }

    parser_err = gsl_parse_task(rec, total_size, specs,
                                sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (!self->curr_update) return make_gsl_err_external(knd_FAIL);

    err = state_ctrl->confirm(state_ctrl, self->curr_update);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static int knd_update_state(struct kndClass *self)
{
    struct kndClass *c;
    struct kndStateControl *state_ctrl = self->entry->repo->state_ctrl;
    struct kndUpdate *update;
    struct kndClassUpdate *class_update;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("..update state of \"%.*s\"",
                self->entry->name_size, self->entry->name);

    /* new update obj */
    err = mempool->new_update(mempool, &update);                      RET_ERR();
    update->spec = self->entry->repo->task->spec;
    update->spec_size = self->entry->repo->task->spec_size;

    /* create index of class updates */
    update->classes = calloc(self->inbox_size, sizeof(struct kndClassUpdate*));
    if (!update->classes) return knd_NOMEM;

    /* resolve all refs */
    for (c = self->inbox; c; c = c->next) {
        err = mempool->new_class_update(mempool, &class_update);      RET_ERR();

        self->entry->repo->next_class_numid++;
        c->entry->numid = self->entry->repo->next_class_numid;

        err = c->resolve(c, class_update);
        if (err) {
            knd_log("-- %.*s class not resolved :(", c->name_size, c->name);
            return err;
        }

        if (DEBUG_CONC_LEVEL_2)
            c->str(c);

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

static int restore(struct kndClass *self)
{
    char state_buf[KND_STATE_SIZE];
    char last_state_buf[KND_STATE_SIZE];
    struct glbOutput *out = self->entry->repo->out;

    const char *inbox_dir = "/schema/inbox";
    size_t inbox_dir_size = strlen(inbox_dir);
    int err;

    memset(state_buf, '0', KND_STATE_SIZE);
    if (DEBUG_CONC_LEVEL_TMP)
        knd_log(".. conc \"%s\" restoring DB state in: %s",
                self->entry->name, self->entry->repo->path, KND_STATE_SIZE);

    out->reset(out);
    err = out->write(out, self->entry->repo->path, self->entry->repo->path_size);
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

static int export(struct kndClass *self)
{
    switch(self->entry->repo->task->format) {
    case KND_FORMAT_JSON:
        return export_JSON(self);
        /*case KND_FORMAT_HTML:
        return kndClass_export_HTML(self);
    case KND_FORMAT_GSL:
    return kndClass_export_GSL(self); */
    default:
        break;
    }

    knd_log("-- format %d not supported :(", self->entry->repo->task->format);
    return knd_FAIL;
}

static int freeze_objs(struct kndClass *self,
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
    struct glbOutput *dir_out;
    const char *key;
    void *val;
    size_t chunk_size;
    size_t num_size;
    size_t obj_block_size = 0;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. freezing objs of class \"%.*s\", total:%zu  valid:%zu",
                self->entry->name_size, self->entry->name,
                self->entry->obj_name_idx->size, self->entry->num_objs);

    out = self->entry->repo->out;
    out->reset(out);
    dir_out = self->entry->repo->dir_out;

    dir_out->reset(dir_out);

    err = dir_out->write(dir_out, "[o", 2);                           RET_ERR();
    init_dir_size = dir_out->buf_size;

    key = NULL;
    self->entry->obj_name_idx->rewind(self->entry->obj_name_idx);
    do {
        self->entry->obj_name_idx->next_item(self->entry->obj_name_idx, &key, &val);
        if (!key) break;
        entry = (struct kndObjEntry*)val;
        obj = entry->obj;

        if (obj->state->phase != KND_CREATED) {
            knd_log("NB: skip freezing \"%.*s\"   phase: %d",
                    obj->name_size, obj->name, obj->state->phase);
            continue;
        }
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

        err = dir_out->writec(dir_out, ' ');
        if (err) return err;

        buf_size = 0;
        knd_num_to_str(obj->frozen_size, buf, &buf_size, KND_RADIX_BASE);
        err = dir_out->write(dir_out, buf, buf_size);
        if (err) return err;

        if (DEBUG_CONC_LEVEL_2)
            knd_log("OBJ body size: %zu [%.*s]",
                    obj->frozen_size, buf_size, buf);

        /* OBJ persistent write */
        if (out->buf_size > out->threshold) {
            err = knd_append_file(self->entry->repo->frozen_output_file_name,
                                  out->buf, out->buf_size);
            if (err) return err;

            *total_frozen_size += out->buf_size;
            obj_block_size += out->buf_size;
            out->reset(out);
        }
    } while (key);

    /* no objs written? */
    if (dir_out->buf_size == init_dir_size) {
        *total_frozen_size = 0;
        *total_size = 0;
        return knd_OK;
    }

    /* final chunk to write */
    if (self->entry->repo->out->buf_size) {
        err = knd_append_file(self->entry->repo->frozen_output_file_name,
                              out->buf, out->buf_size);                           RET_ERR();
        *total_frozen_size += out->buf_size;
        obj_block_size += out->buf_size;
        out->reset(out);
    }

    /* close directory */
    err = dir_out->write(dir_out, "]", 1);                            RET_ERR();

    /* obj directory size */
    buf_size = sprintf(buf, "%lu", (unsigned long)dir_out->buf_size);

    err = dir_out->write(dir_out, "{L ", strlen("{L "));
    if (err) return err;
    err = dir_out->write(dir_out, buf, buf_size);
    if (err) return err;
    err = dir_out->write(dir_out, "}", 1);
    if (err) return err;

    /* persistent write of directory */
    err = knd_append_file(self->entry->repo->frozen_output_file_name,
                          dir_out->buf, dir_out->buf_size);
    if (err) return err;

    *total_frozen_size += dir_out->buf_size;
    obj_block_size += dir_out->buf_size;

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
                       (unsigned long)self->entry->num_objs);
    curr_dir +=      num_size;
    curr_dir_size += num_size;

    memcpy(curr_dir, "}", 1);
    curr_dir++;
    curr_dir_size++;


    *total_size = curr_dir_size;

    return knd_OK;
}

static int freeze_subclasses(struct kndClass *self,
                             size_t *total_frozen_size,
                             char *output,
                             size_t *total_size)
{
    char buf[KND_SHORT_NAME_SIZE] = {0};
    size_t buf_size;
    struct kndClass *c;
    char *curr_dir = output;
    size_t curr_dir_size = 0;
    size_t chunk_size;
    int err;

    chunk_size = strlen("[c");
    memcpy(curr_dir, "[c", chunk_size);
    curr_dir += chunk_size;
    curr_dir_size += chunk_size;

    for (size_t i = 0; i < self->entry->num_children; i++) {
        c = self->entry->children[i]->class;
        err = c->freeze(c);
        if (err) return err;

        if (!c->entry->frozen_size) {
            knd_log("-- empty GSP in %.*s?", c->name_size, c->name);
            continue;
        }

        /* terminal class */
//        if (c->is_terminal) {
//            self->num_terminals++;
//        } else {
//            self->num_terminals += c->num_terminals;
//        }

        memcpy(curr_dir, " ", 1);
        curr_dir++;
        curr_dir_size++;

        buf_size = 0;
        knd_num_to_str(c->entry->frozen_size, buf, &buf_size, KND_RADIX_BASE);
        memcpy(curr_dir, buf, buf_size);
        curr_dir      += buf_size;
        curr_dir_size += buf_size;

        *total_frozen_size += c->entry->frozen_size;
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
    struct kndRelEntry *entry;
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

        entry = (struct kndRelEntry*)val;
        rel = entry->rel;


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

static int freeze(struct kndClass *self)
{
    // TODO
    char dir_buf[KND_MAX_CONC_CHILDREN * KND_DIR_ENTRY_SIZE];// = self->dir_buf;
    char *curr_dir = dir_buf;

    size_t curr_dir_size = 0;
    size_t total_frozen_size = 0;
    size_t num_size;
    size_t chunk_size;
    int err;

    self->entry->repo->out->reset(self->entry->repo->out);

    /* class self presentation */
    err = export_GSP(self);
    if (err) {
        knd_log("-- GSP export failed :(");
        return err;
    }

    /* persistent write */
    err = knd_append_file(self->entry->repo->frozen_output_file_name,
                          self->entry->repo->out->buf, self->entry->repo->out->buf_size);                   RET_ERR();

    total_frozen_size = self->entry->repo->out->buf_size;

    /* TODO: no dir entry necessary */
    /*if (!self->entry->num_children) {
        if (!self->entry) {
            self->frozen_size = total_frozen_size;
            return knd_OK;
        }
        if (!self->entry->obj_name_idx) {
            self->frozen_size = total_frozen_size;
            return knd_OK;
        }
        }*/

    /* class dir entry */
    chunk_size = strlen("{C ");
    memcpy(curr_dir, "{C ", chunk_size);
    curr_dir += chunk_size;
    curr_dir_size += chunk_size;

    num_size = sprintf(curr_dir, "%zu}", total_frozen_size);
    curr_dir +=      num_size;
    curr_dir_size += num_size;

    if (self->entry->repo->next_class_numid) {
        num_size = sprintf(curr_dir, "{tot %zu}", self->entry->repo->next_class_numid);
        curr_dir +=      num_size;
        curr_dir_size += num_size;
    }

    /* any instances to freeze? */
    if (self->entry && self->entry->num_objs) {
        err = freeze_objs(self, &total_frozen_size, curr_dir, &chunk_size);       RET_ERR();
        curr_dir +=      chunk_size;
        curr_dir_size += chunk_size;
    }

    if (self->entry->num_children) {
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
        //self->proc->out = self->entry->repo->out;
        //self->proc->dir_out = dir_out;
        //self->proc->frozen_output_file_name = self->entry->repo->frozen_output_file_name;

        err = self->proc->freeze_procs(self->proc,
                                       &total_frozen_size,
                                       curr_dir, &chunk_size);                    RET_ERR();
        curr_dir +=      chunk_size;
        curr_dir_size += chunk_size;
    }

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== %.*s (%.*s)   DIR: \"%.*s\"   [%lu]",
                self->entry->name_size, self->entry->name, self->entry->id_size, self->entry->id,
                curr_dir_size,
                dir_buf, (unsigned long)curr_dir_size);

    num_size = sprintf(curr_dir, "{L %lu}",
                       (unsigned long)curr_dir_size);
    curr_dir_size += num_size;

    err = knd_append_file(self->entry->repo->frozen_output_file_name,
                          dir_buf, curr_dir_size);
    if (err) return err;

    total_frozen_size += curr_dir_size;

    self->entry->frozen_size = total_frozen_size;

    return knd_OK;
}

/*  Concept initializer */
extern void kndClass_init(struct kndClass *self)
{
    self->del = kndClass_del;
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
kndClass_new(struct kndClass **c)
{
    struct kndClass *self;

    self = malloc(sizeof(struct kndClass));
    if (!self) return knd_NOMEM;
    memset(self, 0, sizeof(struct kndClass));

    kndClass_init(self);
    *c = self;
    return knd_OK;
}
