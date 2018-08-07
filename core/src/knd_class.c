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
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>
#include <glb-lib/output.h>

#define DEBUG_CLASS_LEVEL_1 0
#define DEBUG_CLASS_LEVEL_2 0
#define DEBUG_CLASS_LEVEL_3 0
#define DEBUG_CLASS_LEVEL_4 0
#define DEBUG_CLASS_LEVEL_5 0
#define DEBUG_CLASS_LEVEL_TMP 1

static int get_arg_value(struct kndAttrVar *src,
                         struct kndAttrVar *query,
                         struct kndProcCallArg *arg);

static int build_attr_name_idx(struct kndClass *self);
static gsl_err_t confirm_class_var(void *obj, const char *name, size_t name_size);
static gsl_err_t confirm_attr_var(void *obj, const char *name, size_t name_size);

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

static int read_GSL_file(struct kndClass *self,
                         struct kndConcFolder *parent_folder,
                         const char *filename,
                         size_t filename_size);

static int str_conc_elem(void *obj,
                         const char *elem_id,
                         size_t elem_id_size,
                         size_t count,
                         void *elem);


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
        //err = unfreeze_class(self, entry, &c);                                    RET_ERR();
    }

    c->str(c);

    return knd_OK;
}

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

    knd_log("\n%*s{class %.*s    id:%.*s numid:%zu",
            self->depth * KND_OFFSET_SIZE, "",
            self->entry->name_size, self->entry->name,
            self->entry->id_size, self->entry->id,
            self->entry->numid);

    if (self->num_states) {
        knd_log("\n%*s_state:%zu",
            self->depth * KND_OFFSET_SIZE, "",
            self->states->update->numid);
    }
    
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

                if (attr_entry->attr_var) {
                    knd_log("%*s  == attr var:",
                            (self->depth + 1) * KND_OFFSET_SIZE, "");
                    str_attr_vars(attr_entry->attr_var, self->depth + 1);
                }

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

        if (DEBUG_CLASS_LEVEL_2) {
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


static int inherit_attrs(struct kndClass *self, struct kndClass *base)
{
    struct kndClassEntry *entry;
    struct kndAttr *attr;
    struct kndClass *c;
    struct kndAttrEntry *attr_entry;
    struct kndClassVar *item;
    int err;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. \"%.*s\" class to inherit attrs from \"%.*s\"..",
                self->entry->name_size, self->entry->name,
                base->name_size, base->name);
    }

    if (!base->is_resolved) {
        err = base->resolve(base, NULL);                                          RET_ERR();
    }

    /* check circled relations */
    for (size_t i = 0; i < self->num_bases; i++) {
        entry = self->bases[i];
        c = entry->class;

        if (DEBUG_CLASS_LEVEL_2)
            knd_log("== (%zu of %zu)  \"%.*s\" is a base of \"%.*s\"",
                    i, self->num_bases, c->name_size, c->name,
                    self->entry->name_size, self->entry->name);

        if (entry->class == base) {
            if (DEBUG_CLASS_LEVEL_2)
                knd_log("NB: class \"%.*s\" is already inherited "
                        "by \"%.*s\"",
                        base->name_size, base->name,
                        self->entry->name_size, self->entry->name);
            return knd_OK;
        }
    }

    /* get attrs from base */
    for (attr = base->attrs; attr; attr = attr->next) {

        /* compare with local set of attrs */
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

    if (DEBUG_CLASS_LEVEL_1)
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

static int index_attr(struct kndClass *self,
                      struct kndAttr *attr,
                      struct kndAttrVar *item)
{
    struct kndClass *base;
    struct kndSet *set;
    struct kndClass *c = NULL;
    int err;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log("\n.. indexing CURR CLASS: \"%.*s\" .. index attr: \"%.*s\" [type:%d]"
                " refclass: \"%.*s\" (name:%.*s val:%.*s)",
                self->entry->name_size, self->entry->name,
                attr->name_size, attr->name, attr->type,
                attr->ref_classname_size, attr->ref_classname,
                item->name_size, item->name, item->val_size, item->val);
    }

    if (!attr->ref_classname_size) return knd_OK;

    /* template base class */
    err = knd_get_class(self,
                        attr->ref_classname,
                        attr->ref_classname_size,
                        &base);                                                       RET_ERR();
    if (!base->is_resolved) {
        err = base->resolve(base, NULL);                                          RET_ERR();
    }

    /* specific class */
    err = knd_get_class(self,
                        item->val,
                        item->val_size, &c);                                          RET_ERR();

    item->class = c;

    if (!c->is_resolved) {
        err = c->resolve(c, NULL);                                                RET_ERR();
    }
    err = knd_is_base(base, c);                                                       RET_ERR();

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

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log("\n.. attr item list indexing.. (class:%.*s) .. index attr: \"%.*s\" [type:%d]"
                " refclass: \"%.*s\" (name:%.*s val:%.*s)",
                self->entry->name_size, self->entry->name,
                attr->name_size, attr->name, attr->type,
                attr->ref_classname_size, attr->ref_classname,
                item->name_size, item->name, item->val_size, item->val);
    }

    if (!attr->ref_classname_size) return knd_OK;

    /* template base class */
    err = knd_get_class(self,
                    attr->ref_classname,
                    attr->ref_classname_size,
                    &base);                                                       RET_ERR();
    if (!base->is_resolved) {
        err = base->resolve(base, NULL);                                          RET_ERR();
    }

    for (item = parent_item->list; item; item = item->next) {

        if (DEBUG_CLASS_LEVEL_3)
            knd_log("== list item name:%.*s", item->name_size, item->name);

        /* specific class */
        err = knd_get_class(self,
                        item->name,
                        item->name_size, &c);                                          RET_ERR();
        item->class = c;

        if (!c->is_resolved) {
            err = c->resolve(c, NULL);                                                RET_ERR();
        }
        err = knd_is_base(base, c);                                                       RET_ERR();

        set = attr->parent_class->entry->descendants;

        /* add curr class to the reverse index */
        err = set->add_ref(set, attr, self->entry, c->entry);
        if (err) return err;
    }

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

    if (DEBUG_CLASS_LEVEL_2)
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
        //err = unfreeze_class(self, entry, &entry->class);
        //if (err) return err;
    }
    c = entry->class;

    if (!c->is_resolved) {
        err = c->resolve(c, NULL);                                                RET_ERR();
    }

    if (base) {
        err = knd_is_base(base, c);                                                   RET_ERR();
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

    if (DEBUG_CLASS_LEVEL_2)
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

    if (DEBUG_CLASS_LEVEL_2)
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

    if (DEBUG_CLASS_LEVEL_2) {
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

    if (DEBUG_CLASS_LEVEL_2)
        c->str(c);

    if (c->implied_attr) {
        attr = c->implied_attr;

        if (DEBUG_CLASS_LEVEL_2)
            knd_log("== class: \"%.*s\" implied attr: %.*s",
                    classname_size, classname,
                    attr->name_size, attr->name);

        parent_item->implied_attr = attr;

        switch (attr->type) {
        case KND_ATTR_NUM:

            if (DEBUG_CLASS_LEVEL_2)
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
        if (DEBUG_CLASS_LEVEL_2) {
            knd_log(".. check attr \"%.*s\" in class \"%.*s\" "
                    " is_resolved:%d",
                    item->name_size, item->name,
                    c->name_size, c->name, c->is_resolved);
        }

        err = knd_class_get_attr(c, item->name, item->name_size, &attr);
        if (err) {
            knd_log("-- no attr \"%.*s\" in class \"%.*s\" :(",
                    item->name_size, item->name,
                    c->name_size, c->name);
            return err;
        }
        item->attr = attr;

        switch (attr->type) {
        case KND_ATTR_NUM:
            if (DEBUG_CLASS_LEVEL_2)
                knd_log(".. resolving default num attr: %.*s val:%.*s",
                        item->name_size, item->name,
                        item->val_size, item->val);

            memcpy(buf, item->val, item->val_size);
            buf_size = item->val_size;
            buf[buf_size] = '\0';
            err = knd_parse_num(buf, &item->numval);

            break;
        case KND_ATTR_AGGR:
            if (DEBUG_CLASS_LEVEL_2)
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

    if (DEBUG_CLASS_LEVEL_2) {
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

    if (DEBUG_CLASS_LEVEL_2)
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
    struct kndAttrVar *attr_var;
    struct kndAttrEntry *entry;
    struct kndClass *c;
    struct kndProc *proc;
    struct ooDict *attr_name_idx = self->attr_name_idx;
    int err;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. resolving attr vars of class %.*s",
                self->entry->name_size, self->entry->name);
    }

    for (attr_var = parent_item->attrs; attr_var; attr_var = attr_var->next) {
        entry = attr_name_idx->get(attr_name_idx,
                                   attr_var->name, attr_var->name_size);
        if (!entry) {
            knd_log("-- no such attr: %.*s", attr_var->name_size, attr_var->name);
            return knd_FAIL;
        }

        /* save attr assignment */
        entry->attr_var = attr_var;

        if (DEBUG_CLASS_LEVEL_2) {
            knd_log("== entry attr: %.*s %d",
                    attr_var->name_size, attr_var->name,
                    entry->attr->is_indexed);
        }

        if (entry->attr->is_a_set) {
            attr_var->attr = entry->attr;

            err = resolve_attr_var_list(self, attr_var);
            if (err) return err;
            if (attr_var->val_size)
                attr_var->num_list_elems++;

            if (entry->attr->is_indexed) {
                err = index_attr_var_list(self, entry->attr, attr_var);
                if (err) return err;
            }
            continue;
        }

        /* single attr */
        switch (entry->attr->type) {
        case KND_ATTR_AGGR:
            /* TODO */
            attr_var->attr = entry->attr;
            err = resolve_aggr_item(self, attr_var);
            if (err) {
                knd_log("-- aggr attr_var not resolved :(");
                return err;
            }

            break;
        case KND_ATTR_REF:
            c = entry->attr->conc;
            if (!c->is_resolved) {
                err = c->resolve(c, NULL);                                        RET_ERR();
            }
            err = resolve_class_ref(self, attr_var->val, attr_var->val_size,
                                    c, &attr_var->class);
            if (err) return err;
            break;
        case KND_ATTR_PROC:
            proc = entry->attr->proc;
            /*if (!c->is_resolved) {
                err = c->resolve(c, NULL);                                        RET_ERR();
                }*/
            err = resolve_proc_ref(self, attr_var->val, attr_var->val_size,
                                   proc, &attr_var->proc);
            if (err) return err;
            break;
        default:
            /* atomic value, call a validation function? */
            break;
        }

        if (entry->attr->is_indexed) {
            err = index_attr(self, entry->attr, attr_var);
            if (err) return err;
        }
        attr_var->attr = entry->attr;
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

    if (DEBUG_CLASS_LEVEL_2)
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

        if (DEBUG_CLASS_LEVEL_2)
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

            if (DEBUG_CLASS_LEVEL_2)
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
    struct kndMemPool *mempool = self->entry->repo->mempool;
    struct kndObject *obj;
    struct kndState *state;
    int err;

    if (DEBUG_CLASS_LEVEL_1)
        knd_log("..resolving objs, num objs: %zu",
                self->obj_inbox_size);

    if (class_update) {
        class_update->insts = calloc(self->obj_inbox_size,
                                     sizeof(struct kndObject*));
        if (!class_update->insts) {
            return knd_NOMEM;
        }

        err = mempool->new_state(mempool, &state);                                MEMPOOL_ERR(ClassInstState);
        state->update = class_update->update;
        state->val = (void*)class_update;
        state->next = self->inst_states;
        self->inst_states = state;
        self->num_inst_states++;
        state->numid = self->num_inst_states;
    }

    for (obj = self->obj_inbox; obj; obj = obj->next) {
        if (obj->states->phase == KND_REMOVED) {
            knd_log("NB: \"%.*s\" obj to be removed", obj->name_size, obj->name);
            goto update;
        }
        err = obj->resolve(obj);
        if (err) {
            knd_log("-- %.*s obj not resolved :(",
                    obj->name_size, obj->name);
            goto final;
        }
        obj->states->phase = KND_CREATED;
    update:
        if (class_update) {
            /* NB: should never happen: mismatch of num objs */
            if (class_update->num_insts >= self->obj_inbox_size) {
                knd_log("-- num objs mismatch in %.*s:  %zu vs %zu:(",
                        self->entry->name_size, self->entry->name,
                        class_update->num_insts, self->obj_inbox_size);
                return knd_FAIL;
            }
            class_update->insts[class_update->num_insts] = obj;
            class_update->num_insts++;
            obj->states->update = class_update->update;
            obj->states->val = (void*)class_update;
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

        if (DEBUG_CLASS_LEVEL_2)
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

    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. resolving baseclasses of \"%.*s\"..",
                self->entry->name_size, self->entry->name);

    /* resolve refs to base classes */
    for (cvar = self->baseclass_vars; cvar; cvar = cvar->next) {

        if (cvar->entry->class == self) {
            /* TODO */
            if (DEBUG_CLASS_LEVEL_2)
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

        if (DEBUG_CLASS_LEVEL_2)
            knd_log("\n.. \"%.*s\" class to get its base class: \"%.*s\"..",
                    self->entry->name_size, self->entry->name,
                    classname_size, classname);

        //c = cvar->entry->class;
        err = knd_get_class(self, classname, classname_size, &c);         RET_ERR();

        if (c == self) {
            knd_log("-- self reference detected in \"%.*s\" :(",
                    cvar->entry->name_size, cvar->entry->name);
            return knd_FAIL;
        }

        if (DEBUG_CLASS_LEVEL_2) {
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

        //if (task->type == KND_UPDATE_STATE) {
        /*if (DEBUG_CLASS_LEVEL_2)
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
                        struct kndClassUpdate *class_update)
{
    struct kndClass *root;
    struct kndClassVar *item;
    struct kndClassEntry *entry;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    struct kndState *state;
    int err;

    if (DEBUG_CLASS_LEVEL_1)
        knd_log(".. resolving class: \"%.*s\"",
                self->name_size, self->name);

    if (self->is_resolved) {
        if (self->obj_inbox_size) {
            err = resolve_objs(self, class_update);                                     RET_ERR();
        }
        return knd_OK;
    } else {
        self->entry->repo->next_class_numid++;
        entry = self->entry;
        entry->numid = self->entry->repo->next_class_numid;
        knd_num_to_str(entry->numid, entry->id, &entry->id_size, KND_RADIX_BASE);
    }

    if (DEBUG_CLASS_LEVEL_1) {
        knd_log(".. resolving class \"%.*s\" id:%.*s entry numid:%zu  batch mode:%d",
                self->entry->name_size, self->entry->name,
                entry->id_size, entry->id, self->entry->numid, self->batch_mode);
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
        err = resolve_baseclasses(self);                                          RET_ERR();
    }

    for (item = self->baseclass_vars; item; item = item->next) {
        if (item->attrs) {
            err = resolve_attr_vars(self, item);                                  RET_ERR();
        }
    }

    if (self->obj_inbox_size) {
        err = resolve_objs(self, class_update);                                   RET_ERR();
    }

    if (class_update) {
        err = mempool->new_state(mempool, &state);                                MEMPOOL_ERR(ClassState);
        state->update = class_update->update;
        state->val = (void*)class_update;
        state->next = self->states;
        self->states = state;
        self->num_states++;
        state->numid = self->num_states;
        state->id_size = 0;
        knd_num_to_str(state->numid, state->id, &state->id_size, KND_RADIX_BASE);
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
                //err = unfreeze_class(self, entry, &entry->class);
                //if (err) return err;
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
        if (DEBUG_CLASS_LEVEL_2)
            knd_log(".. class \"%.*s\" to inherit attrs from baseclass \"%.*s\"..",
                    self->entry->name_size, self->entry->name,
                    item->entry->class->name_size,
                    item->entry->class->name);
        err = inherit_attrs(self, item->entry->class);
    }

    return knd_OK;
}

extern int knd_get_attr_var(struct kndClass *self,
                            const char *name, size_t name_size,
                            struct kndAttrVar **result)
{
    struct kndAttrEntry *entry;
    struct kndAttrVar *attr_var;
    struct kndClassVar *cvar;
    struct kndClass *root_class = self->entry->repo->root_class;
    struct kndClass *c;
    int err;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. \"%.*s\" class to retrieve attr var \"%.*s\"..",
                self->entry->name_size, self->entry->name,
                name_size, name);
    }

    entry = self->attr_name_idx->get(self->attr_name_idx,
                                     name, name_size);
    if (!entry) return knd_NO_MATCH;

    if (entry->attr_var) {
        *result = entry->attr_var;
        return knd_OK;
    }

    /* ask your parents */
    for (cvar = self->baseclass_vars; cvar; cvar = cvar->next) {
        err = knd_get_class(root_class,
                        cvar->entry->name,
                        cvar->entry->name_size,
                        &c);
        if (err) {
            if (err == knd_NO_MATCH) continue;
            return err;
        }

        err = knd_get_attr_var(c, name, name_size, &attr_var);
        if (!err) {
            entry->attr_var = attr_var;
            *result = entry->attr_var;
            return knd_OK;
        }
    }
    
    return knd_FAIL;
}


static gsl_err_t run_sync_task(void *obj, const char *val __attribute__((unused)),
                               size_t val_size __attribute__((unused)))
{
    struct kndClass *self = obj;
    int err;

    /* assign numeric ids as defined by a sorting function */
    //err = assign_ids(self);
    //if (err) return make_gsl_err_external(err);

    /* merge earlier frozen DB with liquid updates */

    // TODO: call storage
    /*err = freeze(self);
    if (err) {
        knd_log("-- freezing failed :(");
        return make_gsl_err_external(err);
        }*/

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

    if (DEBUG_CLASS_LEVEL_1)
        knd_log(".. freezing DB to GSP files..");

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}


static gsl_err_t run_get_schema(void *obj, const char *name, size_t name_size)
{
    struct kndClass *self = obj;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    /* TODO: get current schema */
    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. select schema %.*s from: \"%.*s\"..",
                name_size, name, self->entry->name_size, self->entry->name);

    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_rel_import(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndClass *self = obj;
    struct kndRel *rel = self->entry->repo->root_rel;
    
    return rel->import(rel, rec, total_size);
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

    if (DEBUG_CLASS_LEVEL_1)
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
    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. parse schema REC: \"%.*s\"..", 32, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_get_schema,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = knd_import_class,
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
    if (DEBUG_CLASS_LEVEL_2)
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

    if (DEBUG_CLASS_LEVEL_2)
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

static int resolve_classes(struct kndClass *self)
{
    struct kndClass *c;
    struct kndClassEntry *entry;
    const char *key;
    void *val;
    int err;

    if (DEBUG_CLASS_LEVEL_2)
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
            knd_log("-- couldn't resolve the \"%.*s\" class :(",
                    c->entry->name_size, c->entry->name);
            return err;
        }
        c->is_resolved = true;

        if (DEBUG_CLASS_LEVEL_2)
            c->str(c);

    } while (key);

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

    if (DEBUG_CLASS_LEVEL_TMP)
        knd_log(".. class coordination in progress ..");

    /* resolve names to addresses */
    err = resolve_classes(self);
    if (err) return err;

    calculate_descendants(self);

    if (DEBUG_CLASS_LEVEL_TMP)
        knd_log("== TOTAL classes: %zu", self->entry->num_terminals);

    return knd_OK;
}

static int expand_attr_ref_list(struct kndClass *self,
                                struct kndAttrVar *parent_item)
{
    struct kndAttrVar *item;
    int err;

    for (item = parent_item->list; item; item = item->next) {
        if (!item->class) {
            //err = unfreeze_class(self, item->class_entry, &item->class);
            //if (err) return err;
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


static int get_obj(struct kndClass *self,
                   const char *name, size_t name_size,
                   struct kndObject **result)
{
    struct kndObjEntry *entry;
    struct kndObject *obj;
    struct glbOutput *log = self->entry->repo->log;
    struct kndTask *task = self->entry->repo->task;
    int err, e;

    if (DEBUG_CLASS_LEVEL_2)
        knd_log("\n\n.. \"%.*s\" class to get obj: \"%.*s\"..",
                self->entry->name_size, self->entry->name,
                name_size, name);

    if (!self->entry) {
        knd_log("-- no frozen entry rec in \"%.*s\" :(",
                self->entry->name_size, self->entry->name);
    }

    if (!self->entry->obj_name_idx) {
        knd_log("-- no obj name idx in \"%.*s\" :(",
                self->entry->name_size, self->entry->name);
        log->reset(log);
        e = log->write(log, self->entry->name, self->entry->name_size);
        if (e) return e;
        e = log->write(log, " class has no instances",
                             strlen(" class has no instances"));
        if (e) return e;
        task->http_code = HTTP_NOT_FOUND;
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
        task->http_code = HTTP_NOT_FOUND;
        return knd_NO_MATCH;
    }

    if (DEBUG_CLASS_LEVEL_2)
        knd_log("++ got obj entry %.*s  size: %zu OBJ: %p",
                name_size, name, entry->block_size, entry->obj);

    if (!entry->obj) goto read_entry;

    if (entry->obj->states->phase == KND_REMOVED) {
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
    obj->states->phase = KND_SELECTED;
    *result = obj;
    return knd_OK;

 read_entry:
    // TODO
    //err = read_obj_entry(self, entry, result);
    //if (err) return err;

    return knd_OK;
}



static int get_class_attr_value(struct kndClass *src,
                                struct kndAttrVar *query,
                                struct kndProcCallArg *arg)
{
    struct kndAttrEntry *entry;
    struct kndAttrVar *child_var;
    struct ooDict *attr_name_idx = src->attr_name_idx;
    int err;

    entry = attr_name_idx->get(attr_name_idx,
                               query->name, query->name_size);
    if (!entry) {
        knd_log("-- no such attr: %.*s", query->name_size, query->name);
        return knd_FAIL;
    }

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log("++ got attr: %.*s",
                query->name_size, query->name);
    }

    if (!entry->attr_var) return knd_FAIL;

    //str_attr_vars(entry->attr_var, 2);

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

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. from \"%.*s\" extract field: \"%.*s\"",
                src->name_size, src->name,
                query->name_size, query->name);
        str_attr_vars(src, 2);
    }

    /* check implied attr */
    if (src->implied_attr) {
        attr = src->implied_attr;

        if (!memcmp(attr->name, query->name, query->name_size)) {
            switch (attr->type) {
            case KND_ATTR_NUM:

                if (DEBUG_CLASS_LEVEL_2) {
                    knd_log("== implied NUM attr: %.*s value: %.*s numval:%lu",
                            src->name_size, src->name,
                            src->val_size, src->val, src->numval);
                }
                arg->numval = src->numval;
                return knd_OK;
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

        if (DEBUG_CLASS_LEVEL_2)
            knd_log("== child:%.*s val: %.*s",
                    curr_var->name_size, curr_var->name,
                    curr_var->val_size, curr_var->val);
        
        if (curr_var->implied_attr) {
            attr = curr_var->implied_attr;
        }

        if (curr_var->name_size != query->name_size) continue;

        if (!strncmp(curr_var->name, query->name, query->name_size)) {

            if (DEBUG_CLASS_LEVEL_2)
                knd_log("++ match: %.*s numval:%zu",
                        curr_var->val_size, curr_var->val, curr_var->numval);

            arg->numval = curr_var->numval;

            if (!query->num_children) return knd_OK;
            // TODO check children
        }
    }
                
    return knd_OK;
}


static int knd_update_state(struct kndClass *self)
{
    struct kndClass *c;
    struct kndRel *rel = self->entry->repo->root_rel;
    struct kndProc *proc = self->entry->repo->root_proc;
    struct kndUpdate *update;
    struct kndClassUpdate *class_update;
    struct kndMemPool *mempool = self->entry->repo->mempool;
    struct kndStateControl *state_ctrl = self->entry->repo->state_ctrl;
    struct kndTask *task = self->entry->repo->task;
    int err;

    if (DEBUG_CLASS_LEVEL_2)
        knd_log("..update state of \"%.*s\"",
                self->entry->name_size, self->entry->name);

    /* new update obj */
    err = mempool->new_update(mempool, &update);                      RET_ERR();
    update->spec = task->spec;
    update->spec_size = task->spec_size;

    /* create index of class updates */
    update->classes = calloc(self->inbox_size, sizeof(struct kndClassUpdate*));
    if (!update->classes) return knd_NOMEM;

    /* resolve all refs */
    for (c = self->inbox; c; c = c->next) {
        err = mempool->new_class_update(mempool, &class_update);      RET_ERR();

        self->entry->repo->next_class_numid++;
        c->entry->numid = self->entry->repo->next_class_numid;
        class_update->conc = c;
        class_update->update = update;

        err = c->resolve(c, class_update);
        if (err) {
            knd_log("-- %.*s class not resolved :(", c->name_size, c->name);
            return err;
        }

        if (DEBUG_CLASS_LEVEL_1)
            c->str(c);

        if (update->num_classes >= self->inbox_size) {
            knd_log("-- max class updates reached :(");
            return knd_FAIL;
        }

        update->classes[update->num_classes] = class_update;
        update->num_classes++;
        /* stats */
        update->total_objs += class_update->num_insts;
    }

    if (rel->inbox_size) {
        err = rel->update(rel, update);                                           RET_ERR();
    }

    if (proc->inbox_size) {
        //err = proc->update(proc, update);                                         RET_ERR();
    }

    err = state_ctrl->confirm(state_ctrl, update);                                RET_ERR();

    // TODO: replicas
    //err = export_updates(self, update);                                           RET_ERR();

    return knd_OK;
}

static int export(struct kndClass *self)
{
    struct kndTask *task = self->entry->repo->task;

    switch(task->format) {
    case KND_FORMAT_JSON:
        return knd_class_export_JSON(self,
                                     self->entry->repo->out);
        /*    case KND_FORMAT_GSL:
    return kndClass_export_GSL(self); */
    default:
        break;
    }

    knd_log("-- format %d not supported :(", task->format);
    return knd_FAIL;
}

static int open_DB(struct kndClass *self)
{
    // TODO use kndClassStorage to open a frozen DB
    return knd_NO_MATCH;
}

extern int knd_is_base(struct kndClass *self,
                       struct kndClass *child)
{
    if (DEBUG_CLASS_LEVEL_2) {
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

    if (DEBUG_CLASS_LEVEL_TMP)
        knd_log("-- no inheritance from  \"%.*s\" to \"%.*s\" :(",
                self->entry->name_size, self->entry->name,
                child->name_size, child->name);
    return knd_FAIL;
}

extern int knd_class_get_attr(struct kndClass *self,
                              const char *name, size_t name_size,
                              struct kndAttr **result)
{
    struct kndAttr *attr;
    struct kndAttrEntry *entry;
    int err;

    if (DEBUG_CLASS_LEVEL_1) {
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
            if (DEBUG_CLASS_LEVEL_2)
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

extern int knd_get_class(struct kndClass *self,
                         const char *name, size_t name_size,
                         struct kndClass **result)
{
    struct kndClassEntry *entry;
    struct kndClass *c;
    struct glbOutput *log = self->entry->repo->log;
    struct ooDict *class_name_idx = self->entry->repo->root_class->class_name_idx;
    struct kndTask *task = self->entry->repo->task;
    struct kndState *state;
    int err;

    if (DEBUG_CLASS_LEVEL_2)
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
        if (task)
            task->http_code = HTTP_NOT_FOUND;

        return knd_NO_MATCH;
    }

    if (DEBUG_CLASS_LEVEL_2)
        knd_log("++ got Conc Dir: %.*s from \"%.*s\" block size: %zu conc:%p",
                name_size, name,
                self->entry->repo->frozen_output_file_name_size,
                self->entry->repo->frozen_output_file_name, entry->block_size, entry->class);

    if (entry->class) {
        c = entry->class;

        if (c->num_states) {
            state = c->states;
            if (state->phase == KND_REMOVED) {
                knd_log("-- \"%s\" class was removed", name);
                log->reset(log);
                err = log->write(log, name, name_size);
                if (err) return err;
                err = log->write(log, " class was removed",
                                 strlen(" class was removed"));
                if (err) return err;

                task->http_code = HTTP_GONE;
                return knd_NO_MATCH;
            }
        }
        
        c->next = NULL;
        if (DEBUG_CLASS_LEVEL_2)
            c->str(c);

        *result = c;
        return knd_OK;
    }

    /*err = unfreeze_class(self, entry, &c);
    if (err) {
        knd_log("-- failed to unfreeze class: %.*s",
                entry->name_size, entry->name);
        return err;
        }*/

    *result = c;
    return knd_OK;
}

/*  Concept initializer */
extern void kndClass_init(struct kndClass *self)
{
    self->del = kndClass_del;
    self->str = str;
    self->open = open_DB;
    self->load = read_GSL_file;
    //self->read = read_GSP;
    //self->read_obj_entry = read_obj_entry;
    self->reset_inbox = reset_inbox;
    //self->restore = restore;
    self->select_delta = knd_select_class_delta;
    self->coordinate = coordinate;
    self->resolve = resolve_refs;

    self->import = knd_import_class;
    self->select = knd_select_class;

    //self->sync = parse_sync_task;
    //self->freeze = freeze;

    self->update_state = knd_update_state;
    //self->apply_liquid_updates = apply_liquid_updates;
    self->export = export;
    self->get = knd_get_class;
    self->get_obj = get_obj;
    self->get_attr = knd_class_get_attr;
}

extern int
kndClass_new(struct kndClass **result,
             struct kndMemPool *mempool)
{
    struct kndClass *self = NULL;
    struct kndClassEntry *entry;
    int err;

    self = malloc(sizeof(struct kndClass));
    if (!self) return knd_NOMEM;
    memset(self, 0, sizeof(struct kndClass));

    err = mempool->new_class_entry(mempool, &entry);                              RET_ERR();
    entry->name[0] = '/';
    entry->name_size = 1;
    entry->class = self;
    self->entry = entry;

    /* obj manager */
    err = mempool->new_obj(mempool, &self->curr_obj);                             RET_ERR();
    self->curr_obj->base = self;

    /* specific allocations for the root class */
    err = mempool->new_set(mempool, &self->class_idx);                            RET_ERR();
    self->class_idx->type = KND_SET_CLASS;

    err = ooDict_new(&self->class_name_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    kndClass_init(self);
    *result = self;

    return knd_OK;
 error:
    if (self) kndClass_del(self);
    return err;
}
