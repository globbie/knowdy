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
#include "knd_output.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_parser.h"
#include "knd_object.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_utils.h"
#include "knd_http_codes.h"

#define DEBUG_CONC_LEVEL_1 0
#define DEBUG_CONC_LEVEL_2 0
#define DEBUG_CONC_LEVEL_3 0
#define DEBUG_CONC_LEVEL_4 0
#define DEBUG_CONC_LEVEL_5 0
#define DEBUG_CONC_LEVEL_TMP 1

static int run_set_translation_text(void *obj, struct kndTaskArg *args, size_t num_args);

static int read_obj_entry(struct kndConcept *self,
                          struct kndObjEntry *entry,
                          struct kndObject **result);

static int iter_export_JSON(struct kndConcept *self, struct kndConcDir *parent_dir);

static int resolve_attrs(struct kndConcept *self);

static int get_class(struct kndConcept *self,
                     const char *name, size_t name_size,
                     struct kndConcept **result);

static int get_rel_name(struct kndRel *self,
                        struct kndRelDir *dir,
                        int fd);

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
static int run_set_name(void *obj, struct kndTaskArg *args, size_t num_args);
static int run_get_obj(void *obj,
                       struct kndTaskArg *args, size_t num_args);
static int run_select_obj(void *obj,
                          struct kndTaskArg *args, size_t num_args);

static int freeze(struct kndConcept *self);

static int read_GSL_file(struct kndConcept *self,
                         const char *filename,
                         size_t filename_size);

static int get_attr(struct kndConcept *self,
                    const char *name, size_t name_size,
                    struct kndAttr **result);

static int build_diff(struct kndConcept *self,
                      const char *start_state,
                      size_t global_state_count);


static void del_obj_dir(struct kndObjDir *dir)
{
    struct kndObjDir *subdir;

    if (dir->num_dirs) {
        for (size_t i = 0; i < KND_RADIX_BASE; i++) {
            subdir = dir->dirs[i];
            if (!subdir) continue;
            del_obj_dir(subdir);
            dir->dirs[i] = NULL;
        }
        free(dir->dirs);
        dir->dirs = 0;
        dir->num_dirs = 0;
    }

    if (dir->num_objs) {
        free(dir->objs);
        dir->objs = NULL;
        dir->num_objs = 0;
    }
}

static void del_conc_dir(struct kndConcDir *dir)
{
    struct kndConcDir *subdir;
    struct kndObjDir *obj_dir;
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

    if (dir->obj_dirs) {
        for (i = 0; i <  KND_RADIX_BASE; i++) {
            obj_dir = dir->obj_dirs[i];
            if (!obj_dir) continue;
            del_obj_dir(obj_dir);
            dir->obj_dirs[i] = NULL;
        }
        free(dir->obj_dirs);
        dir->obj_dirs = NULL;
    }

    if (dir->objs) {
        free(dir->objs);
        dir->objs = NULL;
    }

    if (dir->obj_idx) {
        dir->obj_idx->del(dir->obj_idx);
        dir->obj_idx = NULL;
    }

    if (dir->rels) {
        free(dir->rels);
        dir->rels = NULL;
    }
}

/*  class destructor */
static void kndConcept_del(struct kndConcept *self)
{
    struct kndConcRef *ref;

    for (size_t i = 0; i < self->num_children; i++) {
        ref = &self->children[i];
        ref->conc->del(ref->conc);
    }

    if (self->attr_idx) self->attr_idx->del(self->attr_idx);
    if (self->summary) self->summary->del(self->summary);
    if (self->dir) del_conc_dir(self->dir);
}

static void str_attr_items(struct kndAttrItem *items, size_t depth)
{
    struct kndAttrItem *item;
    for (item = items; item; item = item->next) {
        knd_log("%*s_attr: \"%s\" => %s", depth * KND_OFFSET_SIZE, "",
                item->name, item->val);
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
    const char *key;
    void *val;

    knd_log("\n%*s{class %.*s    id:%zu",
            self->depth * KND_OFFSET_SIZE, "",
            self->name_size, self->name, self->numid);

    for (tr = self->tr; tr; tr = tr->next) {
        knd_log("%*s~ %s %.*s", (self->depth + 1) * KND_OFFSET_SIZE, "",
                tr->locale, tr->val_size, tr->val);
        if (tr->synt_roles) {
            for (t = tr->synt_roles; t; t = t->next) {
                knd_log("%*s  %d: %.*s", (self->depth + 2) * KND_OFFSET_SIZE, "",
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
            knd_log("%*s_base \"%.*s\"", (self->depth + 1) * KND_OFFSET_SIZE, "",
                    item->name_size, item->name);
            if (item->attrs) {
                str_attr_items(item->attrs, self->depth + 1);
            }
        }
    }

    if (self->attr_idx) {
        key = NULL;
        self->attr_idx->rewind(self->attr_idx);
        do {
            self->attr_idx->next_item(self->attr_idx, &key, &val);
            if (!key) break;
            attr_entry = val;
            attr = attr_entry->attr;
            attr->depth = self->depth + 1;
            attr->str(attr);
        } while (key);
    }

    for (size_t i = 0; i < self->num_children; i++) {
        ref = &self->children[i];
        knd_log("%*sbase of --> %s", (self->depth + 1) * KND_OFFSET_SIZE, "", ref->conc->name);
    }

    knd_log("%*s}", self->depth * KND_OFFSET_SIZE, "");
}

static void obj_str(struct kndObjEntry *self, size_t obj_id,
                    int fd, size_t depth)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    int err;

    knd_log("%*sOBJ %zu   off: %zu   size: %zu", depth * KND_OFFSET_SIZE, "",
            obj_id, self->offset, self->block_size);

    if (lseek(fd, self->offset, SEEK_SET) == -1) {
        return;
    }

    buf_size = self->block_size;
    err = read(fd, buf, buf_size);
    if (err == -1) return;
    buf[buf_size] = '\0';

    knd_log("    %*s%.*s", depth * KND_OFFSET_SIZE, "", buf_size, buf);
}


static void obj_dir_str(struct kndObjDir *self, size_t depth, int fd)
{
    struct kndObjDir *dir;
    struct kndObjEntry *entry;

    if (self->objs) {
        for (size_t i = 0; i <  KND_RADIX_BASE; i++) {
            entry = self->objs[i];
            if (!entry) continue;
            knd_log("    %*s== obj %zu", depth * KND_OFFSET_SIZE, "", i);
        }
    }
    if (self->dirs) {
        for (size_t i = 0; i <  KND_RADIX_BASE; i++) {
            dir = self->dirs[i];
            if (!dir) continue;
            knd_log("    %*sobj dir: %zu", depth * KND_OFFSET_SIZE, "", i);
            obj_dir_str(dir, depth + 1, fd);
        }
    }
}

static void dir_str(struct kndConcDir *self, size_t depth, int fd)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    struct kndConcDir *dir;
    struct kndObjDir *obj_dir;
    struct kndObjEntry *entry;
    int err;

    knd_log("%*s\"%.*s\" => (off: %zu, len: %zu)",
            depth * KND_OFFSET_SIZE, "", self->name_size, self->name,
            self->global_offset, self->block_size);

    if (lseek(fd, self->global_offset, SEEK_SET) == -1) {
        return;
    }

    buf_size = self->block_size;
    err = read(fd, buf, buf_size);
    if (err == -1) return;
    buf[buf_size] = '\0';

    knd_log("      %*s%.*s", depth * KND_OFFSET_SIZE, "", buf_size, buf);

    if (self->num_objs) {
        for (size_t i = 0; i < KND_RADIX_BASE; i++) {
            entry = self->objs[i];
            if (!entry) continue;
            obj_str(entry, i, fd, depth + 1);
        }
        if (self->obj_dirs) {
            for (size_t i = 0; i <  KND_RADIX_BASE; i++) {
                obj_dir = self->obj_dirs[i];
                if (!obj_dir) continue;
                obj_dir_str(obj_dir, depth + 1, fd);
            }
        }
    }

    for (size_t i = 0; i < self->num_children; i++) {
        dir = self->children[i];
        if (!dir) continue;
        dir_str(dir, depth + 1, fd);
    }

    knd_log("%*s}", depth * KND_OFFSET_SIZE, "");
}


static int read_gloss(void *obj,
                      const char *rec,
                      size_t *total_size)
{
    struct kndTranslation *tr = (struct kndTranslation*)obj;
    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_translation_text,
          .obj = tr
        }
    };
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. reading gloss translation: \"%.*s\"",
                tr->locale_size, tr->locale);

    err = knd_parse_task(rec, total_size, specs,
                         sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}

static int gloss_append(void *accu,
                        void *item)
{
    struct kndConcept *self =   accu;
    struct kndTranslation *tr = item;

    tr->next = self->tr;
    self->tr = tr;
   
    return knd_OK;
}

static int gloss_alloc(void *obj,
                       const char *name,
                       size_t name_size,
                       size_t count,
                       void **item)
{
    struct kndConcept *self = obj;
    struct kndTranslation *tr;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. %.*s to create gloss: %.*s count: %zu",
                self->name_size, self->name, name_size, name, count);

    if (name_size > KND_LOCALE_SIZE) return knd_LIMIT;

    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return knd_NOMEM;

    memset(tr, 0, sizeof(struct kndTranslation));
    memcpy(tr->curr_locale, name, name_size);
    tr->curr_locale_size = name_size;

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;
    *item = tr;

    return knd_OK;
}

static int inherit_attrs(struct kndConcept *self, struct kndConcept *base)
{
    struct kndConcDir *dir;
    struct kndAttr *attr;
    struct kndConcept *c;
    struct kndAttrEntry *entry;
    struct kndConcItem *item;
    int err;

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log(".. \"%.*s\" class to inherit attrs from \"%.*s\"..",
                self->name_size, self->name, base->name_size, base->name);

    /* check circled relations */
    for (size_t i = 0; i < self->num_bases; i++) {
        dir = self->bases[i];

	c = dir->conc;
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
        entry = self->attr_idx->get(self->attr_idx, attr->name, attr->name_size);
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

        err = self->attr_idx->set(self->attr_idx,
                                  entry->name, entry->name_size, (void*)entry);
        if (err) return err;
    }
    
    if (self->num_bases >= KND_MAX_BASES) {
        knd_log("-- max bases exceeded for %.*s :(",
                self->name_size, self->name);
        return knd_FAIL;
    }

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log(" .. add %.*s parent to %.*s", 
		base->dir->conc->name_size,
                base->dir->conc->name, 
		self->name_size, self->name);

    self->bases[self->num_bases] = base->dir;
    self->num_bases++;

    /* contact the grandparents */
    for (item = base->base_items; item; item = item->next) {
        knd_log("    == conc item: %.*s", item->name_size, item->name);

        if (item->conc) {
            err = inherit_attrs(self, item->conc);
            if (err) return err;
        }
    }
    
    return knd_OK;
}


static int resolve_attrs(struct kndConcept *self)
{
    struct kndAttr *attr;
    struct kndAttrEntry *entry;
    struct kndConcDir *dir;
    int err;

    err = ooDict_new(&self->attr_idx, KND_SMALL_DICT_SIZE);
    if (err) return err;

    for (attr = self->attrs; attr; attr = attr->next) {
        entry = self->attr_idx->get(self->attr_idx, attr->name, attr->name_size);
        if (entry) {
            knd_log("-- %.*s attr already exists?", attr->name_size, attr->name);
            return knd_FAIL;
        }
        
        entry = malloc(sizeof(struct kndAttrEntry));
        if (!entry) return knd_NOMEM;
        memset(entry, 0, sizeof(struct kndAttrEntry));
        memcpy(entry->name, attr->name, attr->name_size);
        entry->name_size = attr->name_size;
        entry->attr = attr;

        err = self->attr_idx->set(self->attr_idx,
                                  entry->name, entry->name_size, (void*)entry);
        if (err) return err;

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
            dir = self->class_idx->get(self->class_idx,
                                       attr->ref_classname,
                                       attr->ref_classname_size);
            if (!dir) {
                knd_log("-- couldn't resolve the \"%.*s\" attr of %.*s :(",
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

static int resolve_objs(struct kndConcept *self)
{
    struct kndObject *obj;
    int err;

    for (obj = self->obj_inbox; obj; obj = obj->next) {
        obj->task = self->task;
        obj->log = self->log;
        err = obj->resolve(obj);
        if (err) {
            knd_log("-- %.*s obj not resolved :(", obj->name_size, obj->name);
            return err;
        }
    }

    return knd_OK;
}

static int resolve_base_classes(struct kndConcept *self)
{
    struct kndConcItem *item;
    struct kndConcept *c;
    struct kndConcRef *ref;
    int err;

    /* resolve refs to base classes */
    for (item = self->base_items; item; item = item->next) {
        if (item->parent == self) {
            /* TODO */
            if (DEBUG_CONC_LEVEL_2)
                knd_log(".. \"%s\" class to check the update request: \"%s\"..",
                        self->name, item->name);
            continue;
        }

        if (DEBUG_CONC_LEVEL_TMP)
            knd_log("\n.. \"%s\" class to get its base class: \"%s\"..",
                    self->name, item->name);

        err = get_class(self, item->name, item->name_size, &c);
        if (err) return err;

        if (c == self) {
            knd_log("-- self reference detected in \"%.*s\" :(",
                    item->name_size, item->name);
            return knd_FAIL;
        }

        if (DEBUG_CONC_LEVEL_TMP)
            knd_log("++ \"%s\" ref established as a base class for \"%s\"!",
                    item->name, self->name);

        item->conc = c;

        /* should we keep track of our children? */
        /*if (c->ignore_children) continue; */

        /* check item doublets */
        for (size_t i = 0; i < self->num_children; i++) {
            ref = &self->children[i];
            if (ref->conc == self) {
                knd_log("-- doublet conc item found in \"%.*s\" :(",
                        self->name_size, self->name);
                return knd_FAIL;
            }
        }

        if (c->num_children >= KND_MAX_CONC_CHILDREN) {
            knd_log("-- %s as child to %s - max conc children exceeded :(",
                    self->name, item->name);
            return knd_FAIL;
        }

        ref = &c->children[c->num_children];
        ref->conc = self;
        c->num_children++;

	if (DEBUG_CONC_LEVEL_TMP)
	    knd_log("\n\n.. children of class \"%.*s\": %zu",
		    c->name_size, c->name, c->num_children);

        /* ATTRS */
        err = inherit_attrs(self, item->conc);
        if (err) return err;
    }

    return knd_OK;
}

static int resolve_name_refs(struct kndConcept *self)
{
    struct kndConcept *root;
    struct kndConcRef *ref;
    int err;

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log("\n.. resolving class \"%.*s\"",
                self->name_size, self->name);

    if (self->is_resolved) {
        if (self->obj_inbox_size) {
            err = resolve_objs(self);
            if (err) return err;
        }
        return knd_OK;
    }

    /* a child of the root class
     * TODO: refset */
    if (!self->base_items) {
        root = self->root_class;
        ref = &root->children[root->num_children];
        ref->conc = self;
        root->num_children++;
    }

    /* resolve and index the attrs */
    if (!self->attr_idx) {
        err = resolve_attrs(self);
        if (err) return err;
    }

    if (self->base_items) {
        err = resolve_base_classes(self);
        if (err) return err;
    }

    if (self->obj_inbox_size) {
        err = resolve_objs(self);
        if (err) return err;
    }

    self->is_resolved = true;

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

    if (!self->attr_idx) {
        err = ooDict_new(&self->attr_idx, KND_SMALL_DICT_SIZE);
        if (err) return err;

        for (attr = self->attrs; attr; attr = attr->next) {
            entry = malloc(sizeof(struct kndAttrEntry));
            if (!entry) return knd_NOMEM;
            memset(entry, 0, sizeof(struct kndAttrEntry));
            memcpy(entry->name, attr->name, attr->name_size);
            entry->name_size = attr->name_size;
            entry->name[entry->name_size] = '\0';
            entry->attr = attr;

            err = self->attr_idx->set(self->attr_idx,
                                      entry->name, entry->name_size, (void*)entry);
            if (err) return err;
            if (DEBUG_CONC_LEVEL_2)
                knd_log("++ register primary attr: \"%.*s\"",
                        attr->name_size, attr->name);
        }
    }

    entry = self->attr_idx->get(self->attr_idx, name, name_size);
    if (!entry) {
        knd_log("-- attr idx has no entry: %.*s :(", name_size, name);
        return knd_NO_MATCH;
    }

    *result = entry->attr;
    return knd_OK;
}

static int run_set_translation_text(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndTranslation *tr = (struct kndTranslation*)obj;
    struct kndTaskArg *arg;
    const char *val = NULL;
    size_t val_size = 0;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. run set translation text..");

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            val = arg->val;
            val_size = arg->val_size;
        }
    }

    if (!val_size) return knd_FAIL;
    if (val_size >= KND_NAME_SIZE)
        return knd_LIMIT;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. run set translation text: %.*s [%lu]\n", val_size, val,
                (unsigned long)val_size);

    memcpy(tr->val, val, val_size);
    tr->val[val_size] = '\0';
    tr->val_size = val_size;

    return knd_OK;
}

static int parse_synt_role(void *obj,
                           const char *name, size_t name_size,
                           const char *rec, size_t *total_size)
{
    struct kndTranslation *self = obj;
    struct kndTranslation *tr;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing gloss synt role: \"%.*s\"", 16, rec);

    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return knd_NOMEM;
    memset(tr, 0, sizeof(struct kndTranslation));

    if (name_size != KND_SYNT_ROLE_NAME_SIZE) return knd_FORMAT;

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
        return knd_FORMAT;
    }

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_translation_text,
          .obj = tr
        }
    };

    err = knd_parse_task(rec, total_size, specs,
                         sizeof(specs) / sizeof(struct kndTaskSpec));             PARSE_ERR();

    /* assign translation */
    tr->next = self->synt_roles;
    self->synt_roles = tr;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log("== Translation: %.*s (synt role: %d)",
            tr->val_size, tr->val, tr->synt_role);
    }
    return knd_OK;
}

static int parse_gloss_translation(void *obj,
                                   const char *name, size_t name_size,
                                   const char *rec, size_t *total_size)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size;
    struct kndTranslation *tr = obj;
    int err;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log("..  gloss translation in \"%s\" REC: \"%.*s\"\n",
                name, 16, rec); }

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_translation_text,
          .obj = tr
        },
        { .name = "synt_role",
          .name_size = strlen("synt_role"),
          .is_validator = true,
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_SHORT_NAME_SIZE,
          .validate = parse_synt_role,
          .obj = tr
        }
    };

    memcpy(tr->curr_locale, name, name_size);
    tr->curr_locale[name_size] = '\0';
    tr->curr_locale_size = name_size;

    tr->locale = tr->curr_locale;
    tr->locale_size = tr->curr_locale_size;

    err = knd_parse_task(rec, total_size, specs,
                         sizeof(specs) / sizeof(struct kndTaskSpec));             RET_ERR();

    return knd_OK;
}


static int parse_gloss(void *obj,
                       const char *rec,
                       size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndTranslation *tr;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing the gloss update: \"%.*s\"", 16, rec);

    tr = malloc(sizeof(struct kndTranslation));
    if (!tr) return knd_NOMEM;
    memset(tr, 0, sizeof(struct kndTranslation));

    struct kndTaskSpec specs[] = {
        { .is_validator = true,
          .buf = tr->curr_locale,
          .buf_size = &tr->curr_locale_size,
          .max_buf_size = KND_LOCALE_SIZE,
          .validate = parse_gloss_translation,
          .obj = tr
        }
    };

    err = knd_parse_task(rec, total_size, specs,
                         sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    /* assign translation */
    tr->next = self->tr;
    self->tr = tr;

    return knd_OK;
}


static int parse_summary(void *obj,
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
        if (err) return err;
        self->summary = text;
    }

    err = text->parse(text, rec, total_size);
    if (err) return err;
    
    return knd_OK;
}


static int parse_aggr(void *obj,
                      const char *rec,
                      size_t *total_size)
{
    struct kndConcept *self = obj;
    struct kndAttr *attr;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing the AGGR attr: \"%.*s\"", 32, rec);

    err = kndAttr_new(&attr);
    if (err) return err;
    attr->parent_conc = self;
    attr->type = KND_ATTR_AGGR;

    err = attr->parse(attr, rec, total_size);
    if (err) {
        if (DEBUG_CONC_LEVEL_TMP)
            knd_log("-- failed to parse the AGGR attr: %d", err);
        return err;
    }

    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    if (DEBUG_CONC_LEVEL_2)
        attr->str(attr);

    /* TODO: resolve attr if read from GSP */
    
    return knd_OK;
}

static int parse_str(void *obj,
                     const char *rec,
                     size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndAttr *attr;
    int err;

    err = kndAttr_new(&attr);
    if (err) return err;
    attr->parent_conc = self;
    attr->type = KND_ATTR_STR;

    err = attr->parse(attr, rec, total_size);
    if (err) {
        knd_log("-- failed to parse the STR attr of \"%.*s\" :(",
                self->name_size, self->name);
        return err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    return knd_OK;
}

static int parse_bin(void *obj,
                     const char *rec,
                     size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndAttr *attr;
    int err;

    err = kndAttr_new(&attr);
    if (err) return err;
    attr->parent_conc = self;
    attr->type = KND_ATTR_BIN;

    err = attr->parse(attr, rec, total_size);
    if (err) {
        knd_log("-- failed to parse the BIN attr: %d", err);
        return err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    return knd_OK;
}

static int parse_num(void *obj,
                     const char *rec,
                     size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndAttr *attr;
    int err;

    err = kndAttr_new(&attr);
    if (err) return err;
    attr->parent_conc = self;
    attr->type = KND_ATTR_NUM;

    err = attr->parse(attr, rec, total_size);
    if (err) {
        knd_log("-- failed to parse the NUM attr: %d", err);
        return err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }
    return knd_OK;
}

static int parse_ref(void *obj,
                     const char *rec,
                     size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndAttr *attr;
    int err;

    err = kndAttr_new(&attr);
    if (err) return err;
    attr->parent_conc = self;
    attr->type = KND_ATTR_REF;

    err = attr->parse(attr, rec, total_size);
    if (err) {
        knd_log("-- failed to parse the REF attr: %d", err);
        return err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }

    return knd_OK;
}

static int parse_text(void *obj,
                      const char *rec,
                      size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndAttr *attr;
    int err;

    err = kndAttr_new(&attr);
    if (err) return err;
    attr->parent_conc = self;
    attr->type = KND_ATTR_TEXT;

    err = attr->parse(attr, rec, total_size);
    if (err) {
        knd_log("-- failed to parse the TEXT attr: %d", err);
        return err;
    }
    if (!self->tail_attr) {
        self->tail_attr = attr;
        self->attrs = attr;
    }
    else {
        self->tail_attr->next = attr;
        self->tail_attr = attr;
    }
    return knd_OK;
}



static int run_set_conc_item(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;
    struct kndConcItem *item;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE) return knd_LIMIT;

    item = malloc(sizeof(struct kndConcItem));
    if (!item) return knd_NOMEM;

    memset(item, 0, sizeof(struct kndConcItem));
    memcpy(item->name, name, name_size);
    item->name_size = name_size;
    item->name[name_size] = '\0';

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== baseclass item name set: \"%.*s\" %p",
                item->name_size, item->name, item);

    item->next = self->base_items;
    self->base_items = item;
    self->num_base_items++;

    return knd_OK;
}




static int set_attr_item_val(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndAttrItem *item = (struct kndAttrItem*)obj;
    struct kndTaskArg *arg;
    const char *val = NULL;
    size_t val_size = 0;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            val = arg->val;
            val_size = arg->val_size;
        }
    }

    if (!val_size) return knd_FAIL;
    if (val_size >= KND_NAME_SIZE) return knd_LIMIT;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. run set attr item val: %s\n", val);

    memcpy(item->val, val, val_size);
    item->val[val_size] = '\0';
    item->val_size = val_size;

    return knd_OK;
}

static int confirm_attr_item(void *obj,
                             struct kndTaskArg *args __attribute__((unused)),
                             size_t num_args __attribute__((unused)))
{
    struct kndAttrItem *self = obj;
    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. confirm attr item: %.*s!",
                self->name_size, self->name);

    return knd_OK;
}

static int parse_item_child(void *obj,
                            const char *name, size_t name_size,
                            const char *rec, size_t *total_size)
{
    struct kndAttrItem *self = obj;
    struct kndAttrItem *item;
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    size_t err;
    
    item = malloc(sizeof(struct kndAttrItem));
    memset(item, 0, sizeof(struct kndAttrItem));
    memcpy(item->name, name, name_size);
    item->name_size = name_size;
    item->name[name_size] = '\0';

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== attr item name set: \"%s\" REC: %.*s",
                item->name, 16, rec);

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_item_val,
          .obj = item
        },
        { .type = KND_CHANGE_STATE,
          .name = "item_child",
          .name_size = strlen("item_child"),
          .is_validator = true,
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_NAME_SIZE,
          .validate = parse_item_child,
          .obj = item
        },
        { .name = "item_child",
          .name_size = strlen("item_child"),
          .is_validator = true,
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_NAME_SIZE,
          .validate = parse_item_child,
          .obj = item
        },
        { .type = KND_CHANGE_STATE,
          .name = "default",
          .name_size = strlen("default"),
          .is_default = true,
          .run = confirm_attr_item,
          .obj = self
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    item->next = self->children;
    self->children = item;
    self->num_children++;
    return knd_OK;
}

static int parse_conc_item(void *obj,
                           const char *name, size_t name_size,
                           const char *rec, size_t *total_size)
{
    struct kndConcept *self = obj;
    struct kndConcItem *conc_item;
    struct kndAttrItem *item;
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    int err;

    if (!self->base_items) return knd_FAIL;
    conc_item = self->base_items;

    item = malloc(sizeof(struct kndAttrItem));
    memset(item, 0, sizeof(struct kndAttrItem));
    memcpy(item->name, name, name_size);
    item->name_size = name_size;
    item->name[name_size] = '\0';

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_item_val,
          .obj = item
        },
        { .type = KND_CHANGE_STATE,
          .name = "item_child",
          .name_size = strlen("item_child"),
          .is_validator = true,
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_NAME_SIZE,
          .validate = parse_item_child,
          .obj = item
        },
        { .name = "item_child",
          .name_size = strlen("item_child"),
          .is_validator = true,
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_NAME_SIZE,
          .validate = parse_item_child,
          .obj = item
        },
        { .type = KND_CHANGE_STATE,
          .name = "default",
          .name_size = strlen("default"),
          .is_default = true,
          .run = confirm_attr_item,
          .obj = item
        }
    };

    err = knd_parse_task(rec, total_size, specs,
                         sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    /*item->next = conc_item->attrs;
      conc_item->attrs = item;*/
    
    if (!conc_item->tail) {
        conc_item->tail = item;
        conc_item->attrs = item;
    }
    else {
        conc_item->tail->next = item;
        conc_item->tail = item;
    }

    conc_item->num_attrs++;

    return knd_OK;
}



static int parse_baseclass(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing the base class: \"%.*s\"", 32, rec);

    struct kndTaskSpec specs[] = {
        { .name = "baseclass item",
          .name_size = strlen("baseclass item"),
          .is_implied = true,
          .run = run_set_conc_item,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "conc_item",
          .name_size = strlen("conc_item"),
          .is_validator = true,
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_NAME_SIZE,
          .validate = parse_conc_item,
          .obj = self
        },
        { .name = "default",
          .name_size = strlen("default"),
          .is_default = true,
          .run = confirm_attr_item,
          .obj = self
        }
    };

    err = knd_parse_task(rec, total_size, specs,
                         sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}


/* TODO: reconsider this */
static int run_set_children_setting(void *obj,
                                    struct kndTaskArg *args, size_t num_args)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndTaskArg *arg;
    const char *val = NULL;
    size_t val_size = 0;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            val = arg->val;
            val_size = arg->val_size;
        }
    }
    if (!val_size) return knd_FAIL;
    if (val_size >= KND_NAME_SIZE) return knd_LIMIT;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. keep track of children option: %s\n", val);

    if (!memcmp("false", val, val_size))
        self->ignore_children = true;
    
    return knd_OK;
}

static int parse_children_settings(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_children_setting,
          .obj = self
        }
    };
    int err;

    err = knd_parse_task(rec, total_size, specs,
                         sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}

static int run_sync_task(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndTaskArg *arg;
    const char *val = NULL;
    size_t val_size = 0;
    int err;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            val = arg->val;
            val_size = arg->val_size;
        }
    }

    /* merge earlier frozen DB with liquid updates */
    err = freeze(self);
    if (err) {
        knd_log("-- freezing failed :(");
        return err;
    }

    err = self->out->write(self->out, "{\"sync\":1}", strlen("{\"sync\":1}"));
    if (err) return err;

    return knd_OK;
}
static int parse_sync_task(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;

    struct kndTaskSpec specs[] = {
        { .name = "default",
          .name_size = strlen("default"),
          .is_default = true,
          .run = run_sync_task,
          .obj = self
        }
    };
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. freezing DB to GSP files..");

    err = knd_parse_task(rec, total_size, specs,
                         sizeof(specs) / sizeof(struct kndTaskSpec));             PARSE_ERR();

    return knd_OK;
}

static int confirm_class_read(void *obj,
                              struct kndTaskArg *args __attribute__((unused)),
                              size_t num_args __attribute__((unused)))
{
    struct kndConcept *self = obj;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== class %.*s read OK!",
                self->name_size, self->name);

    return knd_OK;
}

static int parse_import_class(void *obj,
                              const char *rec,
                              size_t *total_size)
{
    struct kndConcept *self = obj;
    struct kndConcept *c;
    struct kndConcDir *dir;
    int err;

    // TODO(ki.stfu): Don't ignore this field
    char time[KND_NAME_SIZE];
    size_t time_size = 0;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. import \"%.*s\" class..", 64, rec);

    err  = self->mempool->new_class(self->mempool, &c);
    if (err) return err;

    c->out = self->out;
    c->log = self->log;
    c->task = self->task;
    c->mempool = self->mempool;
    c->class_idx = self->class_idx;
    c->root_class = self;
    
    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = c
        },
        { .type = KND_CHANGE_STATE,
          .name = "base",
          .name_size = strlen("base"),
          .parse = parse_baseclass,
          .obj = c
        },
        { .type = KND_CHANGE_STATE,
          .name = "children",
          .name_size = strlen("children"),
          .parse = parse_children_settings,
          .obj = c
        },
        { .type = KND_CHANGE_STATE,
          .is_list = true,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .accu = c,
          .alloc = gloss_alloc,
          .append = gloss_append,
          .parse = read_gloss
        },
        { .is_list = true,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .accu = c,
          .alloc = gloss_alloc,
          .append = gloss_append,
          .parse = read_gloss
        },
        { .is_list = true,
          .name = "_g",
          .name_size = strlen("_g"),
          .accu = c,
          .alloc = gloss_alloc,
          .append = gloss_append,
          .parse = read_gloss
        },
        { .type = KND_CHANGE_STATE,
          .name = "summary",
          .name_size = strlen("summary"),
          .parse = parse_summary,
          .obj = c
        },
        { .type = KND_CHANGE_STATE,
          .name = "gloss",
          .name_size = strlen("gloss"),
          .parse = parse_gloss,
          .obj = c
        },
        { .type = KND_CHANGE_STATE,
          .name = "aggr",
          .name_size = strlen("aggr"),
          .parse = parse_aggr,
          .obj = c
        },
        { .type = KND_CHANGE_STATE,
          .name = "str",
          .name_size = strlen("str"),
          .parse = parse_str,
          .obj = c
        },
        { .type = KND_CHANGE_STATE,
          .name = "bin",
          .name_size = strlen("bin"),
          .parse = parse_bin,
          .obj = c
        },
        { .type = KND_CHANGE_STATE,
          .name = "num",
          .name_size = strlen("num"),
          .parse = parse_num,
          .obj = c
        },
        // FIXME(ki.stfu): Temporary spec to ignore the time tag
        { .type = KND_CHANGE_STATE,
          .name = "time",
          .name_size = strlen("time"),
          .buf = time,
          .buf_size = &time_size,
          .max_buf_size = KND_NAME_SIZE
        },
        {  .type = KND_CHANGE_STATE,
           .name = "ref",
          .name_size = strlen("ref"),
          .parse = parse_ref,
          .obj = c
        },
        { .type = KND_CHANGE_STATE,
          .name = "text",
          .name_size = strlen("text"),
          .parse = parse_text,
          .obj = c
        },
        { .name = "default",
          .name_size = strlen("default"),
          .is_default = true,
          .run = confirm_class_read,
          .obj = c
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) goto final;

    if (!c->name_size) {
        err = knd_FAIL;
        goto final;
    }

    dir = self->class_idx->get(self->class_idx,
                               c->name, c->name_size);
    if (dir) {
        knd_log("-- %s class name doublet found :(", c->name);
        self->log->reset(self->log);
        err = self->log->write(self->log,
                               c->name,
                               c->name_size);
        if (err) goto final;
        
        err = self->log->write(self->log,
                               " class name already exists",
                               strlen(" class name already exists"));
        if (err) goto final;
        
        err = knd_FAIL;
        goto final;
    }

    if (!self->batch_mode) {
        c->next = self->inbox;
        self->inbox = c;
        self->inbox_size++;
    }

    err = self->mempool->new_conc_dir(self->mempool, &dir);                       RET_ERR();
    dir->conc = c;
    c->dir = dir;
    dir->mempool = self->mempool;

    err = self->class_idx->set(self->class_idx,
                               c->name, c->name_size, (void*)dir);
    if (err) goto final;

    if (DEBUG_CONC_LEVEL_2)
        c->str(c);
     
    return knd_OK;
 final:
    
    c->del(c);
    return err;
}

static int parse_import_obj(void *data,
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
    self->task->type = KND_CHANGE_STATE;

    if (!self->curr_class) {
        knd_log("-- class not set :(");
        return knd_FAIL;
    }

    err = self->mempool->new_obj(self->mempool, &obj);
    if (err) return err;

    obj->phase = KND_SUBMITTED;
    obj->conc = self->curr_class;
    obj->out = self->out;
    obj->log = self->log;
    obj->task = self->task;
    obj->mempool = self->mempool;

    err = obj->parse(obj, rec, total_size);
    if (err) return err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("++ %.*s obj parse OK!", obj->name_size, obj->name);

    c = obj->conc;
    obj->next = c->obj_inbox;
    c->obj_inbox = obj;
    c->obj_inbox_size++;

    if (!c->dir) {
        if (c->root_class) {
            knd_log("-- no dir in %.*s :(", c->name_size, c->name);
            return knd_FAIL;
        }
        return knd_OK;
    }
    
    if (!c->dir->obj_idx) {
        err = ooDict_new(&c->dir->obj_idx, KND_HUGE_DICT_SIZE);
        if (err) return err;
    }

    err = self->mempool->new_obj_entry(self->mempool, &entry);                    RET_ERR();
    entry->obj = obj;

    err = c->dir->obj_idx->set(c->dir->obj_idx,
                               obj->name, obj->name_size,
                               (void*)entry);
    if (err) return err;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log("\n\nREGISTER OBJ in %.*s IDX:  [total:%zu valid:%zu]",
                c->name_size, c->name, c->dir->obj_idx->size, c->dir->num_objs);
        obj->depth = self->depth + 1;
        obj->str(obj);
    }
   
    return knd_OK;
}


static int parse_select_obj(void *data,
                            const char *rec,
                            size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)data;
    int err, e;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. select \"%.*s\" obj.. task type: %d", 16, rec,
                self->curr_class->task->type);

    if (!self->curr_class) {
        knd_log("-- obj class not set :(");
        /* TODO: log*/
        return knd_FAIL;
    }

    self->curr_class->task->type = KND_GET_STATE;

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_get_obj,
          .obj = self->curr_class
        },/*
        { .type = KND_CHANGE_STATE,
          .name = "set_attr",
          .name_size = strlen("set_attr"),
          .is_validator = true,
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_NAME_SIZE,
          .validate = parse_set_obj_attr,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "rm",
          .name_size = strlen("rm"),
          .run = run_remove_obj,
          .obj = self
          },*/
        { .name = "default",
          .name_size = strlen("default"),
          .is_default = true,
          .run = run_select_obj,
          .obj = self->curr_class
        }
    };

    err = knd_parse_task(rec, total_size, specs,
                         sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) {
        knd_log("-- obj select parse error: \"%.*s\"",
                self->log->buf_size, self->log->buf);
        if (!self->log->buf_size) {
            e = self->log->write(self->log, "obj select parse failure",
                                 strlen("obj select parse failure"));
            if (e) return e;
        }
        return err;
    }

    return knd_OK;
}

static int run_select_baseclass(void *obj,
                                struct kndTaskArg *args, size_t num_args)
{
    struct kndConcept *self = obj;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;
    int err;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE) return knd_LIMIT;

    /* TODO: get current schema */
    knd_log("++ got baseclass: %.*s!", name_size, name);

    err = get_class(self, name, name_size, &self->curr_baseclass);                RET_ERR();

    return knd_OK;
}

static int parse_baseclass_select(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndConcept *self = obj;
    int err;

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log(".. select by baseclass \"%.*s\"..", 16, rec);

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_select_baseclass,
          .obj = self
        }
    };

    err = knd_parse_task(rec, total_size, specs,
                         sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}


static int run_get_schema(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndConcept *self = obj;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE) return knd_LIMIT;

    /* TODO: get current schema */
    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. select schema %.*s from: \"%.*s\"..",
                name_size, name, self->name_size, self->name);

    return knd_OK;
}

static int run_set_name(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE) return knd_LIMIT;

    memcpy(self->name, name, name_size);
    self->name_size = name_size;

    return knd_OK;
}


static int parse_rel_import(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndConcept *self = obj;
    int err;

    err = self->rel->import(self->rel, rec, total_size);
    if (err) return err;

    return knd_OK;
}

static int parse_proc_import(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndConcept *self = obj;
    int err;

    err = self->proc->import(self->proc, rec, total_size);
    if (err) return err;

    return knd_OK;
}

static int run_read_include(void *obj,
                            struct kndTaskArg *args, size_t num_args)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndTaskArg *arg;
    struct kndConcFolder *folder;
    const char *name = NULL;
    size_t name_size = 0;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. running include file func.. num args: %lu",
                (unsigned long)num_args);

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (arg->name_size == strlen("_impl")
            && !memcmp(arg->name, "_impl", arg->name_size)) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }

    if (!name_size) return knd_FAIL;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== got include file name: \"%s\"..", name);

    folder = malloc(sizeof(struct kndConcFolder));
    if (!folder) return knd_NOMEM;
    memset(folder, 0, sizeof(struct kndConcFolder));

    memcpy(folder->name, name, name_size);
    folder->name_size = name_size;
    folder->name[name_size] = '\0';

    folder->next = self->folders;
    self->folders = folder;
    self->num_folders++;

    return knd_OK;
}


static int parse_schema(void *self,
                        const char *rec,
                        size_t *total_size)
{
    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parse schema REC: \"%.*s\"..", 32, rec);

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_get_schema,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = parse_import_class,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "rel",
          .name_size = strlen("rel"),
          .parse = parse_rel_import,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_import,
          .obj = self
        }
    };
    int err;

    err = knd_parse_task(rec, total_size, specs,
                         sizeof(specs) / sizeof(struct kndTaskSpec));             PARSE_ERR();

    return knd_OK;
}

static int parse_include(void *self,
                         const char *rec,
                         size_t *total_size)
{
    int err;
    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parse include REC: \"%s\"..", rec);

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_read_include,
          .obj = self
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}

static int parse_GSL(struct kndConcept *self,
                     const char *rec,
                     size_t *total_size)
{
    struct kndTaskSpec specs[] = {
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
    int err;

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

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
    const char *c, *s;
    int i = 0;

    if (DEBUG_CONC_LEVEL_1)
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
            (errno != 0 && numval == 0))
    {
        return knd_FAIL;
    }

    if (numval <= 0) return knd_FAIL;
    if (numval >= KND_DIR_TRAILER_MAX_SIZE) return knd_LIMIT;
    if (DEBUG_CONC_LEVEL_3)
        knd_log("  == DIR size: %lu    CHUNK SIZE: %lu", (unsigned long)numval, (unsigned long)rec_size - i);

    *dir_size = numval;
    *chunk_size = rec_size - i;

    return knd_OK;
}

static int run_set_dir_size(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndConcDir *self = obj;
    struct kndTaskArg *arg;
    char *val = NULL;
    size_t val_size = 0;
    char *invalid_num_char = NULL;
    long numval;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            val = arg->val;
            val_size = arg->val_size;
        }
    }
    if (!val_size) return knd_FAIL;
    if (val_size >= KND_SHORT_NAME_SIZE) return knd_LIMIT;

    numval = strtol(val, &invalid_num_char, KND_NUM_ENCODE_BASE);
    if (*invalid_num_char) {
        knd_log("-- invalid char: %.*s", 1, invalid_num_char);
        return knd_FAIL;
    }
    
    /* check for various numeric decoding errors */
    if ((errno == ERANGE && (numval == LONG_MAX || numval == LONG_MIN)) ||
            (errno != 0 && numval == 0))
    {
        return knd_FAIL;
    }

    if (numval <= 0) return knd_FAIL;
    if (DEBUG_CONC_LEVEL_2)
        knd_log("== DIR size: %lu", (unsigned long)numval);

    self->block_size = numval;
    
    return knd_OK;
}

static int run_set_reldir_size(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndRelDir *self = obj;
    struct kndTaskArg *arg;
    char *val = NULL;
    size_t val_size = 0;
    char *invalid_num_char = NULL;
    long numval;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            val = arg->val;
            val_size = arg->val_size;
        }
    }
    if (!val_size) return knd_FAIL;
    if (val_size >= KND_SHORT_NAME_SIZE) return knd_LIMIT;

    numval = strtol(val, &invalid_num_char, KND_NUM_ENCODE_BASE);
    if (*invalid_num_char) {
        knd_log("-- invalid char: %.*s", 1, invalid_num_char);
        return knd_FAIL;
    }
    
    /* check for various numeric decoding errors */
    if ((errno == ERANGE && (numval == LONG_MAX || numval == LONG_MIN)) ||
            (errno != 0 && numval == 0))
    {
        return knd_FAIL;
    }

    if (numval <= 0) return knd_FAIL;
    if (DEBUG_CONC_LEVEL_2)
        knd_log("== DIR size: %lu", (unsigned long)numval);

    self->block_size = numval;
    
    return knd_OK;
}

static int parse_dir_entry(void *obj,
                           const char *rec,
                           size_t *total_size)
{
    struct kndConcDir *self = obj;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing dir entry %.*s: \"%.*s\"",
                self->name_size, self->name, 32, rec);

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_dir_size,
          .obj = self
        },
        { .name = "t",
          .name_size = strlen("t"),
          .parse = knd_parse_size_t,
          .obj = &self->num_terminals
        },
        { .name = "o",
          .name_size = strlen("o"),
          .parse = knd_parse_size_t,
          .obj = &self->total_objs
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}

static int parse_parent_dir_size(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct kndConcDir *self = obj;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing parent dir size: \"%.*s\"", 16, rec);

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_dir_size,
          .obj = self
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    self->curr_offset += self->block_size;

    return knd_OK;
}



static int run_set_obj_dir_size(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndConcDir *self = obj;
    struct kndTaskArg *arg;
    char *val = NULL;
    size_t val_size = 0;
    char *invalid_num_char = NULL;
    long numval;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            val = arg->val;
            val_size = arg->val_size;
        }
    }
    if (!val_size) return knd_FAIL;
    if (val_size >= KND_SHORT_NAME_SIZE) return knd_LIMIT;
    
    numval = strtol(val, &invalid_num_char, KND_NUM_ENCODE_BASE);
    if (*invalid_num_char) {
        knd_log("-- invalid char: %.*s", 1, invalid_num_char);
        return knd_FAIL;
    }
    
    /* check for various numeric decoding errors */
    if ((errno == ERANGE && (numval == LONG_MAX || numval == LONG_MIN)) ||
            (errno != 0 && numval == 0))
    {
        return knd_FAIL;
    }

    if (numval <= 0) return knd_FAIL;
    if (DEBUG_CONC_LEVEL_2)
        knd_log("== OBJ DIR size: %lu", (unsigned long)numval);

    self->obj_block_size = numval;
    
    return knd_OK;
}

static int parse_obj_dir_size(void *obj,
                              const char *rec,
                              size_t *total_size)
{
    struct kndConcDir *self = obj;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing obj dir size: \"%.*s\"", 16, rec);

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_obj_dir_size,
          .obj = self
        }
    };

    err = knd_parse_task(rec, total_size, specs,
                         sizeof(specs) / sizeof(struct kndTaskSpec));             PARSE_ERR();
    if (err) return err;

    return knd_OK;
}

static int dir_entry_append(void *accu,
                            void *item)
{
    struct kndConcDir *parent_dir = accu;
    struct kndConcDir *dir = item;

    if (!parent_dir->children) {
        parent_dir->children = calloc(KND_MAX_CONC_CHILDREN,
                                      sizeof(struct kndConcDir*));
        if (!parent_dir->children) return knd_NOMEM;
    }

    if (parent_dir->num_children + 1 > KND_MAX_CONC_CHILDREN) {
        knd_log("-- warning: num of subclasses of \"%.*s\" exceeded :(",
                parent_dir->name_size, parent_dir->name);
        return knd_OK;
    }

    parent_dir->children[parent_dir->num_children] = dir;
    parent_dir->num_children++;

    dir->global_offset += parent_dir->curr_offset;
    parent_dir->curr_offset += dir->block_size;

    return knd_OK;
}

static int dir_entry_alloc(void *self,
                           const char *name,
                           size_t name_size,
                           size_t count,
                           void **item)
{
    struct kndConcDir *parent_dir = self;
    struct kndConcDir *dir;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. %.*s to add list item: %.*s count: %zu  [total children: %zu]",
                KND_ID_SIZE, parent_dir->id, name_size, name, count, parent_dir->num_children);
    
    if (name_size > KND_ID_SIZE) return knd_LIMIT;

    parent_dir->mempool->new_conc_dir(parent_dir->mempool, &dir);
    memcpy(dir->id, name, KND_ID_SIZE);
    dir->mempool = parent_dir->mempool;

    memset(dir->next_obj_id, '0', KND_ID_SIZE);
    knd_calc_num_id(name, &dir->numid);
    
    *item = dir;
    return knd_OK;
}

static int reldir_entry_alloc(void *self,
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

    if (name_size > KND_ID_SIZE) return knd_LIMIT;

    err = parent_dir->mempool->new_rel_dir(parent_dir->mempool, &dir);            RET_ERR();
    memcpy(dir->id, name, KND_ID_SIZE);

    memset(dir->next_inst_id, '0', KND_ID_SIZE);

    *item = dir;
    return knd_OK;
}

static int reldir_entry_append(void *accu,
                               void *item)
{
    struct kndConcDir *parent_dir = accu;
    struct kndRelDir *dir = item;

    if (!parent_dir->rels) {
        parent_dir->rels = calloc(KND_MAX_RELS,
                                  sizeof(struct kndRelDir*));
        if (!parent_dir->rels) return knd_NOMEM;
    }

    if (parent_dir->num_rels + 1 > KND_MAX_RELS) {
        knd_log("-- warning: max rels of \"%.*s\" exceeded :(",
                parent_dir->name_size, parent_dir->name);
        return knd_OK;
    }

    parent_dir->rels[parent_dir->num_rels] = dir;
    parent_dir->num_rels++;

    dir->global_offset += parent_dir->curr_offset;
    parent_dir->curr_offset += dir->block_size;

    return knd_OK;
}

static int parse_reldir_entry(void *obj,
                              const char *rec,
                              size_t *total_size)
{
    struct kndRelDir *self = obj;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing REL DIR entry %.*s: \"%.*s\"",
                self->name_size, self->name, 32, rec);

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_reldir_size,
          .obj = self
        }
    };

    err = knd_parse_task(rec, total_size,
                         specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}

static int get_conc_name(struct kndConcept *self,
                         struct kndConcDir *dir,
                         int fd)
{
    char buf[KND_NAME_SIZE + 1];
    size_t buf_size;
    char *c, *b, *e;
    off_t offset = 0;
    bool in_name = false;
    bool got_name = false;
    size_t name_size;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("  .. get conc name in DIR: \"%.*s\"   global off:%zu  block size:%zu",
                dir->name_size, dir->name, dir->global_offset, dir->block_size);

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
    c = buf;
    b = buf;
    e = buf;
    for (size_t i = 0; i < buf_size; i++) {
        c = buf + i;
        switch (*c) {
        case ' ':
        case '\n':
        case '\r':
        case '\t':
            break;
        case '[':
        case '{':
            if (!in_name) {
                in_name = true;
                b = c + 1;
                e = b;
                break;
            }
            got_name = true;
            e = c;
            break;
        default:
            e = c;
            break;
        }
        if (got_name) break;
    }

    name_size = e - b;
    if (!name_size) return knd_FAIL;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("\n  .. CONC NAME: \"%.*s\" [%zu]",
                name_size, b, name_size);

    memcpy(dir->name, b, name_size);
    dir->name_size = name_size;

    err = self->class_idx->set(self->class_idx, dir->name, name_size, dir);
    if (err) return err;

    return knd_OK;
}

static int index_obj_name(struct kndConcept *self,
                          struct kndConcDir *conc_dir,
                          struct kndObjEntry *entry,
                          int fd)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = entry->block_size;
    char *c, *b, *e;
    off_t offset = 0;
    bool in_name = false;
    bool got_name = false;
    size_t name_size;
    long numval;
    int err;

    if (entry->block_size > KND_NAME_SIZE)
        buf_size = KND_NAME_SIZE;
    offset = entry->offset;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return knd_IO_FAIL;
    }

    err = read(fd, buf, buf_size);
    if (err == -1) return knd_IO_FAIL;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. OBJ BODY INCIPIT: \"%.*s\"",
                buf_size, buf);
    c = buf;
    b = buf;
    e = buf;
    for (size_t i = 0; i < buf_size; i++) {
        c = buf + i;
        switch (*c) {
        case ' ':
        case '\n':
        case '\r':
        case '\t':
            break;
        case '[':
        case '{':
            if (!in_name) {
                in_name = true;
                b = c + 1;
                break;
            }
            got_name = true;
            e = c;
            c++;
            break;
        default:
            e = c;
            break;
        }
        if (got_name) break;
    }

    name_size = e - b;
    if (!name_size) {
        return knd_FAIL;
    }
    if (name_size > KND_NAME_SIZE)
	return knd_LIMIT;

    memcpy(entry->name, b, name_size);
    entry->name_size = name_size;

    if (DEBUG_CONC_LEVEL_2) {
	if (!strncmp(entry->name, "Username", strlen("Username"))) {
	    knd_log("\n\n!!! index obj name: \"%.*s\"", name_size, entry->name);
	}
    }

    err = conc_dir->obj_idx->set(conc_dir->obj_idx, entry->name, name_size, entry);
    if (err) return err;

    /* HACK: index numeric user ids */
    if (strncmp(conc_dir->name, "User", strlen("User"))) return knd_OK;
    b = strstr(c, "ident{id ");
    if (b) {
        b += strlen("ident{id ");
        e = strchr(b, '{');
        if (e) {
            buf_size = e - b;
            *e = '\0';
            err = knd_parse_num(b, &numval);
            if (err) {
                knd_log("-- failed to parse num val: %.*s :(", buf_size, b);
                return err;
            }
            if (numval > 0 && numval < (long)self->user->max_users) {
                if (DEBUG_CONC_LEVEL_2) {
                    knd_log(".. register User account: %lu",
                            (unsigned long)numval);
                }
                self->user->user_idx[numval] = entry;
            }
        }
    }
    return knd_OK;
}


static int populate_obj_name_idx(struct kndConcept *self,
                                 struct kndConcDir *conc_dir,
                                 struct kndObjDir *dir,
                                 int fd)
{
    struct kndObjEntry *entry;
    struct kndObjDir *subdir;
    int err;

    if (dir->objs) {
        for (size_t i = 0; i < KND_RADIX_BASE; i++) {
            entry = dir->objs[i];
            if (!entry) continue;

            err = index_obj_name(self, conc_dir, entry, fd);
            if (err) return err;
        }
    }

    if (dir->dirs) {
        for (size_t i = 0; i < KND_RADIX_BASE; i++) {
            subdir = dir->dirs[i];
            if (!subdir) continue;
            err = populate_obj_name_idx(self, conc_dir, subdir, fd);
            if (err) return err;
        }
    }
    
    return knd_OK;
}



static int get_rel_name(struct kndRel *self,
                        struct kndRelDir *dir,
                        int fd)
{
    char buf[KND_NAME_SIZE + 1];
    size_t buf_size;
    char *c, *b, *e;
    off_t offset = 0;
    bool in_name = false;
    bool got_name = false;
    size_t name_size;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("  .. get rel name in DIR: \"%.*s\"   global off:%zu  block size:%zu",
                dir->name_size, dir->name, dir->global_offset, dir->block_size);

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
        knd_log("\n  .. REL BODY: %.*s",
                buf_size, buf);
    c = buf;
    b = buf;
    e = buf;
    for (size_t i = 0; i < buf_size; i++) {
        c = buf + i;
        switch (*c) {
        case ' ':
        case '\n':
        case '\r':
        case '\t':
            break;
        case '[':
        case '{':
            if (!in_name) {
                in_name = true;
                b = c + 1;
                e = b;
                break;
            }
            got_name = true;
            e = c;
            break;
        default:
            e = c;
            break;
        }
        if (got_name) break;
    }

    name_size = e - b;
    if (!name_size) return knd_FAIL;

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log(".. set REL NAME: \"%.*s\" [%zu]",
                name_size, b, name_size);

    memcpy(dir->name, b, name_size);
    dir->name_size = name_size;

    err = self->rel_idx->set(self->rel_idx, dir->name, name_size, dir);
    if (err) return err;

    return knd_OK;
}

static int get_dir_trailer(struct kndConcept *self,
                           struct kndConcDir *parent_dir,
                           int fd,
                           int encode_base)
{
    size_t block_size = parent_dir->block_size;
    struct kndOutput *out = self->out;
    off_t offset = 0;
    size_t dir_size = 0;
    size_t chunk_size = 0;
    int err;

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
        knd_log("-- couldn't get dir size in \"%.*s\" :(", out->buf_size, out->buf);
        return err;
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

    if (dir_size >= out->max_size) return knd_LIMIT;

    out->reset(out);
    out->buf_size = dir_size;
    err = read(fd, out->buf, out->buf_size);
    if (err == -1) return knd_IO_FAIL;
    out->buf[out->buf_size] = '\0';

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing DIR: \"%.*s\"",
                out->buf_size, out->buf);

    err = parse_dir_trailer(self, parent_dir, fd, encode_base);
    if (err) {
        knd_log("-- failed to parse dir trailer: \"%.*s\"", out->buf_size, out->buf);
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
    size_t parsed_size = 0;
    int err;

    struct kndTaskSpec specs[] = {
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
          .accu = parent_dir,
          .alloc = dir_entry_alloc,
          .append = dir_entry_append,
          .parse = parse_dir_entry
        },
        { .is_list = true,
          .name = "R",
          .name_size = strlen("R"),
          .accu = parent_dir,
          .alloc = reldir_entry_alloc,
          .append = reldir_entry_append,
          .parse = parse_reldir_entry
        }
    };

    parent_dir->curr_offset = parent_dir->global_offset;
    if (DEBUG_CONC_LEVEL_2)
        knd_log("  .. parsing %.*s  DIR REC: \"%.*s\"  curr offset: %zu   [dir size:%zu]",
                KND_ID_SIZE, parent_dir->id, dir_buf_size, dir_buf,
                parent_dir->curr_offset, dir_buf_size);

    err = knd_parse_task(dir_buf, &parsed_size,
                         specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    /* get conc name */
    if (parent_dir->block_size) {
        err = get_conc_name(self, parent_dir, fd);
        if (err) return err;
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
        knd_log("\nDIR: %.*s   num_children: %zu obj block:%zu",
                KND_ID_SIZE, parent_dir->id, parent_dir->num_children,
                parent_dir->obj_block_size);

    /* try reading each dir */
    for (size_t i = 0; i < parent_dir->num_children; i++) {
        dir = parent_dir->children[i];
        if (!dir) continue;

        dir->mempool = parent_dir->mempool;

        if (DEBUG_CONC_LEVEL_2)
            knd_log("== child DIR %.*s block size: %zu",
                    KND_ID_SIZE, dir->id, dir->block_size);

        if (!dir->num_terminals) {
            dir->is_terminal = true;
            if (!dir->total_objs) {
                err = get_conc_name(self, dir, fd);
                if (err) return err;
                continue;
            }
        }

        if (DEBUG_CONC_LEVEL_2)
            knd_log("\n\n.. read DIR: %.*s   global offset: %zu    block size: %zu  num terminals:%zu",
                    dir->name_size, dir->name,
                    dir->global_offset, dir->block_size, dir->num_terminals);

        err = get_dir_trailer(self, dir, fd, encode_base);
        if (err) {
            knd_log("-- error reading trailer of \"%.*s\" DIR: %d",
                    dir->name_size, dir->name, err);
            return err;
        }
    }

    /* register rel names in rel_idx */
    if (self->rel) {
        for (size_t i = 0; i < parent_dir->num_rels; i++) {
            reldir = parent_dir->rels[i];
            if (reldir->block_size) {
                err = get_rel_name(self->rel, reldir, fd);
                if (err) return err;
            }
        }
    }

    return knd_OK;
}

static int register_obj_entry(struct kndMemPool *mempool,
                              struct kndObjDir **obj_dirs,
                              struct kndObjEntry *entry,
                              const char *obj_id,
                              size_t depth,
                              size_t max_depth)
{
    struct kndObjDir *dir;
    unsigned char c;
    int numval;
    int err;

    c = obj_id[KND_ID_SIZE - depth];
    numval = obj_id_base[(size_t)c];
    if (numval == -1) return knd_LIMIT;

    dir = obj_dirs[numval];
    if (!dir) {
        err = mempool->new_obj_dir(mempool, &dir);                                RET_ERR();
        obj_dirs[numval] = dir;
    }

    if (depth < max_depth) {
        if (!dir->dirs) {
            dir->dirs = calloc(KND_RADIX_BASE, sizeof(struct kndObjDir*));
            if (!dir->dirs) return knd_NOMEM;
        }
        err = register_obj_entry(mempool, dir->dirs, entry,
                                 obj_id, depth + 1, max_depth);                   RET_ERR();
        return knd_OK;
    }

    /* terminal idx */
    if (!dir->objs) {
        dir->objs = calloc(KND_RADIX_BASE, sizeof(struct kndObjEntry*));
        if (!dir->objs) return knd_NOMEM;
    }
    dir->objs[numval] = entry;
    dir->num_objs++;

    if (DEBUG_CONC_LEVEL_3)
        knd_log("== register OBJ %.*s: (depth: %zu) size:%zu",
                KND_ID_SIZE, obj_id, depth, entry->block_size);
    
    return knd_OK;
}

static int obj_atomic_entry_alloc(void *obj,
                                  const char *val,
                                  size_t val_size,
                                  size_t count,
                                  void **item)
{
    char buf[KND_NAME_SIZE];
    struct kndConcDir *parent_dir = obj;
    struct kndObjEntry *entry = NULL;
    char *invalid_num_char = NULL;
    const char *c;
    long numval;
    size_t i, depth;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. create OBJ ENTRY: %.*s  count: %zu  parent_dir:%p",
                val_size, val, count, parent_dir);

    err = parent_dir->mempool->new_obj_entry(parent_dir->mempool, &entry);        RET_ERR();

    if (val_size >= KND_NAME_SIZE) return knd_FAIL;
    memcpy(buf, val, val_size);
    buf[val_size] = '\0';

    /* TODO: remove magic number 16, set custom base  */
    numval = strtol(buf, &invalid_num_char, 16);
    if (*invalid_num_char) {
        knd_log("-- invalid char: %.*s", 1, invalid_num_char);
        return knd_FAIL;
    }

    /* check for various numeric decoding errors */
    if ((errno == ERANGE && (numval == LONG_MAX || numval == LONG_MIN)) ||
            (errno != 0 && numval == 0))
    {
        return knd_FAIL;
    }
    if (numval <= 0) return knd_FAIL;

    entry->block_size = (size_t)numval;
    entry->offset = parent_dir->curr_offset;
    parent_dir->curr_offset += entry->block_size;

    err = knd_next_state(parent_dir->next_obj_id);
    if (err) return err;

    /* assign entry to the terminal idx */
    if (count < KND_RADIX_BASE) {
        if (!parent_dir->objs) {
            parent_dir->objs = calloc(KND_RADIX_BASE, sizeof(struct kndObjEntry*));
            if (!parent_dir->objs) return knd_NOMEM;
        }
        parent_dir->objs[count] = entry;
        parent_dir->num_objs++;
        *item = entry;
        return knd_OK;
    }

    /* calculate base depth */
    i = 0;
    c = parent_dir->next_obj_id;
    for (i = 0; i < KND_ID_SIZE; i++) {
        if (*c != '0') break;
        c++;
    }
    depth = KND_ID_SIZE - i;

    if (!parent_dir->obj_dirs) {
        parent_dir->obj_dirs = calloc(KND_RADIX_BASE, sizeof(struct kndObjDir*));
        if (!parent_dir->obj_dirs) return knd_NOMEM;
    }

    /* assign entry to a subordinate dir */
    err = register_obj_entry(parent_dir->mempool,
                             parent_dir->obj_dirs, entry,
                             parent_dir->next_obj_id, 1, depth);
    if (err) return err;

    *item = entry;

    return knd_OK;
}

static int obj_atomic_append(void *accu,
                             void *item)
{
    struct kndConcDir *parent_dir = accu;
    struct kndObjEntry *entry = item;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. DIR: %.*s append atomic obj entry: %.*s..",
                parent_dir->name_size, parent_dir->name,
                entry->name_size, entry->name);

    return knd_OK;
}

static int obj_atomic_parse(void *obj,
                            const char *rec,
                            size_t *total_size)
{

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parse atomic obj entry..");

    return knd_OK;
}

static int parse_obj_dir_trailer(struct kndConcept *self,
                                 struct kndConcDir *parent_dir,
                                 int fd,
                                 int encode_base)
{
    struct kndTaskSpec specs[] = {
        { .is_list = true,
          .is_atomic = true,
          .name = "o",
          .name_size = strlen("o"),
          .accu = parent_dir,
          .alloc = obj_atomic_entry_alloc,
          .append = obj_atomic_append,
          .parse = obj_atomic_parse
        }
    };
    size_t parsed_size = 0;
    size_t *total_size = &parsed_size;
    char *obj_dir_buf = self->out->buf;
    size_t obj_dir_buf_size = self->out->buf_size;
    struct kndObjEntry *entry;
    struct kndObjDir *dir;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("\n\n.. parsing OBJ DIR REC: %.*s [size %zu]  num base: %d",
                128, obj_dir_buf, obj_dir_buf_size, encode_base);

    err = knd_parse_task(obj_dir_buf, total_size, specs,
                         sizeof(specs) / sizeof(struct kndTaskSpec));             PARSE_ERR();

    /* build obj name idx */
    if (parent_dir->num_objs) {
        /* TODO: calc dict size */

        if (!parent_dir->obj_idx) {
            err = ooDict_new(&parent_dir->obj_idx, KND_MEDIUM_DICT_SIZE);
            if (err) return err;
        }

        for (size_t i = 0; i < KND_RADIX_BASE; i++) {
            entry = parent_dir->objs[i];
            if (!entry) continue;

            err = index_obj_name(self, parent_dir, entry, fd);
            if (err) return err;
        }

        if (parent_dir->obj_dirs) {
            for (size_t i = 0; i < KND_RADIX_BASE; i++) {
                dir = parent_dir->obj_dirs[i];
                if (!dir) continue;

                err = populate_obj_name_idx(self, parent_dir, dir, fd);
                if (err) return err;
            }
        }
    }

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
    struct kndOutput *out = self->out;
    int err;

    offset = parent_dir->global_offset + block_size + parent_dir->obj_block_size - KND_DIR_ENTRY_SIZE;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return knd_IO_FAIL;
    }

    out->reset(out);
    out->buf_size = KND_DIR_ENTRY_SIZE;
    err = read(fd, out->buf, out->buf_size);
    if (err == -1) return knd_IO_FAIL;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("\n  .. OBJ DIR ENTRY SIZE REC: \"%.*s\"",
                out->buf_size, out->buf);

    err =  knd_get_dir_size(self, &dir_size, &chunk_size, encode_base);
    if (err) {
        knd_log("-- failed to read dir size :(");
        return err;
    }
    
    if (DEBUG_CONC_LEVEL_2)
        knd_log("\n  .. OBJ DIR REC SIZE: %lu [size field size: %lu]",
                dir_size, (unsigned long)chunk_size);

    offset = (parent_dir->global_offset + block_size + parent_dir->obj_block_size) - chunk_size - dir_size;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        return knd_IO_FAIL;
    }

    out->reset(out);
    if (dir_size >= out->max_size) {
        return knd_LIMIT;
    }
    out->buf_size = dir_size;
    err = read(fd, out->buf, out->buf_size);
    if (err == -1) return knd_IO_FAIL;
    out->buf[out->buf_size] = '\0';

    if (DEBUG_CONC_LEVEL_2)
        knd_log("  .. OBJ DIR REC: %.*s", out->buf_size, out->buf);

    err = parse_obj_dir_trailer(self, parent_dir, fd, encode_base);
    if (err) return err;

    return knd_OK;
}


static int open_frozen_DB(struct kndConcept *self)
{
    struct kndOutput *out = self->out;
    const char *filename;
    size_t filename_size;
    struct stat st;
    int fd;
    size_t file_size = 0;
    struct stat file_info;
    int err;

    filename = self->frozen_output_file_name;
    filename_size = self->frozen_output_file_name_size;

    if (DEBUG_CONC_LEVEL_TMP)
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

    /* TODO: get encode base from config */
    err = get_dir_trailer(self, self->dir, fd, KND_DIR_SIZE_ENCODE_BASE);
    if (err) {
        knd_log("-- error reading dir trailer in \"%.*s\"", filename_size, filename);
        goto final;
    }

    if (DEBUG_CONC_LEVEL_2)
        dir_str(self->dir, 1, fd);

    err = knd_OK;
    
 final:
    if (err) {
        knd_log("-- failed to open the frozen DB :(");
    }
    close(fd);
    return err;
}


static int read_GSL_file(struct kndConcept *self,
                         const char *filename,
                         size_t filename_size)
{
    struct kndOutput *out = self->out;
    struct kndConcFolder *folder, *folders;
    size_t chunk_size = 0;
    int err;

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log("..reading \"%.*s\"..", filename_size, filename);

    out->reset(out);

    err = out->write(out, self->dbpath, self->dbpath_size);
    if (err) return err;
    err = out->write(out, "/", 1);
    if (err) return err;
    err = out->write(out, filename, filename_size);
    if (err) return err;
    err = out->write(out, ".gsl", strlen(".gsl"));
    if (err) return err;

    out->buf[out->buf_size] = '\0';

    err = out->read_file(out, (const char*)out->buf, out->buf_size);
    if (err) {
        knd_log("-- couldn't read GSL class file \"%s\" :(", out->buf);
        return err;
    }

    out->file[out->file_size] = '\0';
    
    err = parse_GSL(self, (const char*)out->file, &chunk_size);
    if (err) return err;

    /* high time to read our folders */
    folders = self->folders;
    self->folders = NULL;
    self->num_folders = 0;
    
    for (folder = folders; folder; folder = folder->next) {
        /*knd_log(".. should now read the \"%s\" folder..",
                folder->name);
        */
        
        err = read_GSL_file(self, folder->name, folder->name_size);
        if (err) return err;
    }
    return knd_OK;
}



static int conc_item_alloc(void *obj,
                           const char *name,
                           size_t name_size,
                           size_t count,
                           void **item)
{
    struct kndConcept *self = obj;
    struct kndConcItem *ci;

    ci = malloc(sizeof(struct kndConcItem));
    if (!ci) return knd_NOMEM;
    memset(ci, 0, sizeof(struct kndConcItem));

    if (name_size > KND_ID_SIZE) return knd_LIMIT;
    memcpy(ci->id, name, name_size);

    knd_calc_num_id(name, &ci->numid);

    *item = ci;

    return knd_OK;
}

static int conc_item_append(void *accu,
                            void *item)
{
    struct kndConcept *self = accu;
    struct kndConcItem *ci = item;

    ci->next = self->base_items;
    self->base_items = ci;
    self->num_base_items++;

    return knd_OK;
}

static int run_set_conc_item_baseclass(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndConcItem *self = obj;
    struct kndTaskArg  *arg;
    const char *name = NULL;
    size_t name_size = 0;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE) return knd_LIMIT;

    memcpy(self->classname, name, name_size);
    self->classname_size = name_size;
    self->classname[name_size] = '\0';

    return knd_OK;
}


static int conc_item_read(void *obj,
                           const char *name, size_t name_size,
                           const char *rec, size_t *total_size)
{
    struct kndConcItem *conc_item = obj;
    struct kndAttrItem *item;
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    int err;

    item = malloc(sizeof(struct kndAttrItem));
    memset(item, 0, sizeof(struct kndAttrItem));
    memcpy(item->name, name, name_size);
    item->name_size = name_size;
    item->name[name_size] = '\0';

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_item_val,
          .obj = item
        },
        { .name = "item_child",
          .name_size = strlen("item_child"),
          .is_validator = true,
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_NAME_SIZE,
          .validate = parse_item_child,
          .obj = item
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    if (!conc_item->tail) {
        conc_item->tail = item;
        conc_item->attrs = item;
    }
    else {
        conc_item->tail->next = item;
        conc_item->tail = item;
    }

    conc_item->num_attrs++;

    return knd_OK;
}


static int base_conc_item_read(void *obj,
                               const char *rec,
                               size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndConcItem *ci = obj;
    struct kndTaskSpec specs[] = {
        { .name = "base",
          .name_size = strlen("base"),
          .is_implied = true,
          .run = run_set_conc_item_baseclass,
          .obj = ci
        },
        { .name = "conc_item",
          .name_size = strlen("conc_item"),
          .is_validator = true,
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_NAME_SIZE,
          .validate = conc_item_read,
          .obj = ci
        }
    };
    int err;

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}


static int read_GSP(struct kndConcept *self,
                    const char *rec,
                    size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. reading GSP: \"%.*s\"..", 32, rec);

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = self
        },
        { .is_list = true,
          .name = "_g",
          .name_size = strlen("_g"),
          .accu = self,
          .alloc = gloss_alloc,
          .append = gloss_append,
          .parse = read_gloss
        },
        { .is_list = true,
          .name = "_ci",
          .name_size = strlen("_ci"),
          .accu = self,
          .alloc = conc_item_alloc,
          .append = conc_item_append,
          .parse = base_conc_item_read
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
        { .name = "default",
          .name_size = strlen("default"),
          .is_default = true,
          .run = confirm_class_read,
          .obj = self
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}


static int resolve_class_refs(struct kndConcept *self)
{
    struct kndConcept *c;
    struct kndConcDir *dir;
    const char *key;
    void *val;
    int err;

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log(".. resolving class refs by \"%.*s\"",
                self->name_size, self->name);

    key = NULL;
    self->class_idx->rewind(self->class_idx);
    do {
        self->class_idx->next_item(self->class_idx, &key, &val);
        if (!key) break;

        dir = (struct kndConcDir*)val;
        c = dir->conc;
        if (c->is_resolved) continue;

        err = c->resolve(c);
	knd_log("== %.*s resolve result: %d", c->name_size, c->name, err);

        if (err) {
            knd_log("-- couldn't resolve the \"%s\" class :(", c->name);
            return err;
        }
	c->is_resolved = true;

    } while (key);

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log("++ classes resolved!\n\n");

    return knd_OK;
}

static int coordinate(struct kndConcept *self)
{
    struct kndConcept *c;
    struct kndConcDir *dir;
    struct kndConcItem *item;
    const char *key;
    void *val;
    int err;

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log(".. class coordination in progress ..");

    /* names to refs */
    err = resolve_class_refs(self);
    if (err) return err;

    /* build attr indices, detect circles, assign ids */
    /*key = NULL;
    self->class_idx->rewind(self->class_idx);
    do {
        self->class_idx->next_item(self->class_idx, &key, &val);
        if (!key) break;
        dir = (struct kndConcDir*)val;
        c = dir->conc;

        for (item = c->base_items; item; item = item->next) {
            err = inherit_attrs(c, item->conc);
            if (err) return err;
    */

            /* TODO: validate attr items */
            /*if (item->attrs) {
              err = validate_attr_items(c, item->attrs);
              if (err) return err;
            }*/

//        }

        /* assign id */
/*        err = knd_next_state(self->next_id);
        if (err) return err;
        
        memcpy(c->id, self->next_id, KND_ID_SIZE);
    } while (key);
*/

    /* display all classes */
    if (DEBUG_CONC_LEVEL_TMP) {
        key = NULL;
        self->class_idx->rewind(self->class_idx);
        do {
            self->class_idx->next_item(self->class_idx, &key, &val);
            if (!key) break;
            dir = (struct kndConcDir*)val;
            c = dir->conc;
            c->depth = self->depth + 1;
            c->str(c);
        } while (key);
    }

    err = self->proc->coordinate(self->proc);
    if (err) return err;

    err = self->rel->coordinate(self->rel);
    if (err) return err;
    
    return knd_OK;
}


static int unfreeze_class(struct kndConcept *self,
                          struct kndConcDir *dir,
                          struct kndConcept **result)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
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
        knd_log(".. unfreezing class: \"%.*s\"..",
                dir->name_size, dir->name);

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
    if (buf_size >= KND_TEMP_BUF_SIZE) return knd_NOMEM;

    err = read(fd, buf, buf_size);
    if (err == -1) {
        err = knd_IO_FAIL;
        goto final;
    }
    buf[buf_size] = '\0';

    if (DEBUG_CONC_LEVEL_2)
        knd_log("\n== frozen Conc REC: \"%.*s\"", buf_size, buf);

    /* done reading */
    close(fd);

    self->mempool->new_class(self->mempool, &c);                                  RET_ERR();
    memcpy(c->name, dir->name, dir->name_size);
    c->name_size = dir->name_size;
    c->out = self->out;
    c->log = self->log;
    c->task = self->task;
    c->root_class = self->root_class ? self->root_class : self;
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
        knd_log("-- conc name not found in %.*s :(", buf_size, buf);
        c->del(c);
        return knd_FAIL;
    }

    err = c->read(c, b, &chunk_size);
    if (err) {
        c->del(c);
        goto final;
    }

    if (DEBUG_CONC_LEVEL_2)
        c->str(c);

    /* TODO: read base classes */
    /* inherit attrs */

    
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

    dir = self->class_idx->get(self->class_idx, name, name_size);
    if (!dir) {
        knd_log("-- no such class: \"%.*s\"  task: %p :(", name_size, name, self->task);
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
        knd_log("++ got Conc Dir: %.*s from %.*s conc: %p", name_size, name,
                self->frozen_output_file_name_size,
                self->frozen_output_file_name, dir->conc);

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
        //c->phase = KND_SELECTED;
        c->task = self->task;
        *result = c;
        return knd_OK;
    }

    err = unfreeze_class(self, dir, &c);
    if (err) return err;

    //c->phase = KND_SELECTED;
    c->task = self->task;

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

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log("\n\n.. \"%.*s\" class to get obj: \"%.*s\"..",
                self->name_size, self->name,
                name_size, name);

    if (!self->dir) {
        knd_log("-- no frozen dir rec in \"%.*s\" :(",
                self->name_size, self->name);
    }
    
    if (!self->dir->obj_idx) {
        knd_log("-- no obj idx in \"%.*s\" :(", self->name_size, self->name);

        self->log->reset(self->log);
        e = self->log->write(self->log, self->name, self->name_size);
        if (e) return e;
        e = self->log->write(self->log, " class has no instances",
                             strlen(" class has no instances"));
        if (e) return e;

        return knd_FAIL;
    }

    entry = self->dir->obj_idx->get(self->dir->obj_idx, name, name_size);
    if (!entry) {
        knd_log("-- no such obj: \"%s\" :(", name);
        self->log->reset(self->log);
        err = self->log->write(self->log, name, name_size);
        if (err) return err;
        err = self->log->write(self->log, " obj name not found",
                               strlen(" obj name not found"));
        if (err) return err;
        self->task->http_code = HTTP_NOT_FOUND;
        return knd_NO_MATCH;
    }

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log("++ got obj entry %.*s  size: %zu OBJ: %p",
                name_size, name, entry->block_size, entry->obj);

    /*if (obj->phase == KND_REMOVED) {
        knd_log("-- \"%s\" obj was removed", name);
        self->log->reset(self->log);
        err = self->log->write(self->log, name, name_size);
        if (err) return err;
        err = self->log->write(self->log, " obj was removed",
                               strlen(" obj was removed"));
        if (err) return err;
        return knd_NO_MATCH;
    }
   */

    if (entry->obj) {
        obj = entry->obj;
        obj->phase = KND_SELECTED;
        obj->task = self->task;
        *result = obj;
        return knd_OK;
    }

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
    // current parser expects a null terminated string
    entry->block[entry->block_size] = '\0';

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log("   == OBJ REC: \"%.*s\"", entry->block_size, entry->block);

    /* done reading */
    close(fd);

    err = self->mempool->new_obj(self->mempool, &obj);
    if (err) return err;

    obj->phase = KND_FROZEN;
    obj->out = self->out;
    obj->log = self->log;
    obj->task = self->task;
    obj->entry = entry;
    obj->conc = self;
    obj->mempool = self->mempool;
    entry->obj = obj;

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
        knd_log("-- obj name not found in \"%.*s\" :(", entry->block_size, entry->block);
        obj->del(obj);
        return knd_FAIL;
    }

    obj->name_size = e - b;
    if (obj->name_size >= KND_NAME_SIZE) return knd_LIMIT;
    obj->name = b;

    err = obj->read(obj, c, &chunk_size);
    if (err) {
        obj->del(obj);
        return err;
    }

    if (DEBUG_CONC_LEVEL_TMP)
        obj->str(obj);

    *result = obj;
    return knd_OK;
    
 final:
    close(fd);
    return err;
}

static int present_class_selection(void *obj,
                                   struct kndTaskArg *args __attribute__((unused)),
                                   size_t num_args __attribute__((unused)))
{
    struct kndConcept *self = obj;
    struct kndConcept *c;
    struct kndOutput *out = self->out;
    int e, err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. presenting class selection: batch size:%zu  batch from:%zu  total selects:%zu",
                self->task->batch_max, self->task->batch_from,
                self->task->num_class_selects);
    out->reset(out);

    if (self->curr_class) {
        c = self->curr_class;
        c->out = out;
        c->task = self->task;
        c->format = KND_FORMAT_JSON;
        c->depth = 0;

        err = c->export(c);
        if (err) return err;
        return knd_OK;
    }

    if (!self->task->num_class_selects) return knd_FAIL;

    err = out->write(out, "[", 1);
    if (err) return err;

    for (size_t i = 0; i < self->task->num_class_selects; i++) {
        c = self->task->class_selects[i];
        c->out = out;
        c->task = self->task;
        c->format = KND_FORMAT_JSON;
        c->depth = 0;

        /* export iterator */
        if (self->task->batch_max) {
            if (!c->dir) return knd_FAIL;

            if (self->task->start_from > c->dir->num_terminals) {
                e = self->log->write(self->log,
                                     "requested offset exceeds the total number of matches",
                                     strlen("requested offset exceeds the total number of matches"));
                if (e) return e;
                
                self->task->http_code = HTTP_REQUESTED_RANGE_NOT_SATISFIABLE;
                return knd_LIMIT;
            }
            
            c->dir->out = out;
            c->dir->task = self->task;

            out->reset(out);
            err = out->write(out, "{\"_term_class_iter\":[", strlen("{\"_term_class_iter\":["));
            if (err) return err;

            err = iter_export_JSON(c, c->dir);
            if (err) return err;

            err = out->write(out, "]", 1);
            if (err) return err;

            /* TODO: total batch? any updates? */
            
            err = out->write(out, "}", 1);
            if (err) return err;
        
            return knd_OK;
        }

        if (i) {
            err = out->write(out, ",", 1);
            if (err) return err;
        }
        err = c->export(c);
        if (err) return err;
    }

    err = out->write(out, "]", 1);
    if (err) return err;

    if (DEBUG_CONC_LEVEL_TMP) {
        knd_log("JSON: \"%.*s\"",  out->buf_size, out->buf);
    }
    
    return knd_OK;
}

static int run_select_obj(void *data,
                          struct kndTaskArg *args __attribute__((unused)),
                          size_t num_args __attribute__((unused)))
{
    struct kndConcept *self = data;
    struct kndObject *obj;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. run obj select.. conc: %s", self->name);

    /* TODO: log */
    if (!self->curr_obj) {
        knd_log("-- no obj selected :(");
        return knd_FAIL;
    }
    obj = self->curr_obj;
    obj->out = self->out;
    obj->out->reset(obj->out);

    obj->log = self->log;
    obj->task = self->task;

    obj->format = KND_FORMAT_JSON;
    err = obj->export(obj);
    if (err) return err;

    return knd_OK;
}


static int run_get_class(void *obj,
                         struct kndTaskArg *args, size_t num_args)
{
    struct kndConcept *self = obj;
    struct kndConcept *c;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;
    int err;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE) return knd_LIMIT;

    self->curr_class = NULL;
    err = get_class(self, name, name_size, &c);
    if (err) return err;

    self->curr_class = c;

    if (DEBUG_CONC_LEVEL_2) {
        c->str(c);
    }

    return knd_OK;
}

static int run_get_obj(void *obj,
                          struct kndTaskArg *args, size_t num_args)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;
    int err;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE) return knd_LIMIT;

    self->curr_obj = NULL;
    err = get_obj(self, name, name_size, &self->curr_obj);
    if (err) {
        knd_log("-- failed to get obj: %.*s :(", name_size, name);
        return err;
    }
    if (DEBUG_CONC_LEVEL_2)
        knd_log("++ got obj: \"%.*s\"! task type: %d CURR OBJ:%p\n",
                name_size, name, self->task->type, self->curr_obj);

    return knd_OK;
}


static int run_remove_class(void *obj,
                            struct kndTaskArg *args, size_t num_args)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndConcept *c;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;
    int err;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "rm", strlen("rm"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    /*if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE) return knd_LIMIT;
    */
    if (!self->curr_class) {
        knd_log("-- remove operation: class name not specified:(");
        self->log->reset(self->log);
        err = self->log->write(self->log, name, name_size);
        if (err) return err;
        err = self->log->write(self->log, " class name not specified",
                               strlen(" class name not specified"));
        if (err) return err;
        return knd_NO_MATCH;
    }

    c = self->curr_class;

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log("== class to remove: \"%.*s\"\n", c->name_size, c->name);

    //c->phase = KND_REMOVED;

    self->log->reset(self->log);
    err = self->log->write(self->log, name, name_size);
    if (err) return err;
    err = self->log->write(self->log, " class removed",
                           strlen(" class removed"));
    if (err) return err;

    c->next = self->inbox;
    self->inbox = c;
    self->inbox_size++;

    return knd_OK;
}

static int parse_set_attr(void *obj,
                          const char *name, size_t name_size,
                          const char *rec, size_t *total_size)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    struct kndConcept *c;
    struct kndConcItem *conc_item;
    const char *conc_item_name = "__self__";
    size_t conc_item_name_size = strlen(conc_item_name);
    struct kndAttrItem *item;
    int err;

    if (DEBUG_CONC_LEVEL_TMP) {
        knd_log("\n%s", self->task->spec);
        knd_log("\n.. attr to set: \"%.*s\"\n", name_size, name);
    }

    if (!self->curr_class) return knd_FAIL;
    c = self->curr_class;
    
    conc_item = malloc(sizeof(struct kndConcItem));
    memset(conc_item, 0, sizeof(struct kndConcItem));
    memcpy(conc_item->name, conc_item_name, conc_item_name_size);
    conc_item->name_size = conc_item_name_size;
    conc_item->name[conc_item_name_size] = '\0';

    conc_item->parent = c;
    
    conc_item->next = c->base_items;
    c->base_items = conc_item;
    c->num_base_items++;
    
    item = malloc(sizeof(struct kndAttrItem));
    memset(item, 0, sizeof(struct kndAttrItem));
    memcpy(item->name, name, name_size);
    item->name_size = name_size;
    item->name[name_size] = '\0';

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_attr_item_val,
          .obj = item
        },
        { .type = KND_CHANGE_STATE,
          .name = "item_child",
          .name_size = strlen("item_child"),
          .is_validator = true,
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_NAME_SIZE,
          .validate = parse_item_child,
          .obj = item
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    if (!conc_item->tail) {
        conc_item->tail = item;
        conc_item->attrs = item;
    }
    else {
        conc_item->tail->next = item;
        conc_item->tail = item;
    }

    conc_item->num_attrs++;

    c->next = self->inbox;
    self->inbox = c;
    self->inbox_size++;
   
    return knd_OK;
}

static int select_delta(struct kndConcept *self,
                        const char *rec,
                        size_t *total_size)
{
    struct kndStateControl *state_ctrl = self->task->state_ctrl;
    struct kndUpdate *update;
    struct kndClassUpdate *class_update;
    struct kndConcept *c;
    struct kndConcDir *dir;
    int err;

    struct kndTaskSpec specs[] = {
        { .name = "eq",
          .name_size = strlen("eq"),
          .parse = knd_parse_size_t,
          .obj = &self->task->batch_eq
        },
        { .name = "gt",
          .name_size = strlen("gt"),
          .parse = knd_parse_size_t,
          .obj = &self->task->batch_gt
        },
        { .name = "lt",
          .name_size = strlen("lt"),
          .parse = knd_parse_size_t,
          .obj = &self->task->batch_lt
        }
    };

    err = knd_parse_task(rec, total_size, specs,
                         sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    if (DEBUG_CONC_LEVEL_2)
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

            if (!self->curr_baseclass) {
                self->task->class_selects[self->task->num_class_selects] = c;
                self->task->num_class_selects++;
                continue;
            }

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

static int parse_select_class_delta(void *data,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndConcept *self = data;
    int err;

    err = select_delta(self, rec, total_size);
    if (err) return err;

    return knd_OK;
}

static int parse_select_class(void *obj,
                              const char *rec,
                              size_t *total_size)
{
    struct kndConcept *self = obj;
    char buf[KND_NAME_SIZE];
    size_t buf_size = 0;
    int err = knd_FAIL, e;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. parsing class select rec: \"%.*s\"", 32, rec);

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_class,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "set_attr",
          .name_size = strlen("set_attr"),
          .is_validator = true,
          .buf = buf,
          .buf_size = &buf_size,
          .max_buf_size = KND_NAME_SIZE,
          .validate = parse_set_attr,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "rm",
          .name_size = strlen("rm"),
          .run = run_remove_class,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
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
        { .name = "_delta",
          .name_size = strlen("_delta"),
          .is_selector = true,
          .parse = parse_select_class_delta,
          .obj = self
        },
        { .name = "default",
          .name_size = strlen("default"),
          .is_default = true,
          .run = present_class_selection,
          .obj = self
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) {
        knd_log("-- class parse error: \"%.*s\"", self->log->buf_size, self->log->buf);
        if (!self->log->buf_size) {
            e = self->log->write(self->log, "class parse failure",
                                 strlen("class parse failure"));
            if (e) return e;
        }
        return err;
    }

    return knd_OK;
}

static int attr_items_export_JSON(struct kndConcept *self,
                                  struct kndAttrItem *items, size_t depth)
{
    struct kndAttrItem *item;
    struct kndOutput *out;
    int err;

    out = self->out;

    for (item = items; item; item = item->next) {
        err = out->write(out, "{\"n\":\"", strlen("{\"n\":\""));
        if (err) return err;
        err = out->write(out, item->name, item->name_size);
        if (err) return err;
        err = out->write(out, "\",\"val\":\"", strlen("\",\"val\":\""));
        if (err) return err;
        err = out->write(out, item->val, item->val_size);
        if (err) return err;
        err = out->write(out, "\"", strlen("\""));
        if (err) return err;

        /* TODO: control nesting depth (if depth) */
        if (item->children) {
            err = out->write(out, ",\"items\":[", strlen(",\"items\":["));
            if (err) return err;
            err = attr_items_export_JSON(self, item->children, 0);
            if (err) return err;
            err = out->write(out, "]", 1);
            if (err) return err;
        }
        
        err = out->write(out, "}", 1);
        if (err) return err;

        if (item->next) {
            err = out->write(out, ",", 1);
            if (err) return err;
        }
    }
    
    return knd_OK;
}

static int iter_export_JSON(struct kndConcept *self, struct kndConcDir *parent_dir)
{
    struct kndConcDir *dir;
    struct kndConcept *c;
    struct kndOutput *out = self->out;
    size_t start_from = parent_dir->task->start_from;
    size_t match_count = parent_dir->task->match_count;
    int i, err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. iterate terminal classes of\"%.*s\".. start_from:%zu  match_count:%zu",
                parent_dir->name_size, parent_dir->name, start_from, match_count);

    for (size_t i = 0; i < parent_dir->num_children; i++) {
        dir = parent_dir->children[i];
        if (!dir) continue;

        /* a terminal class to export? */
        if (dir->is_terminal) {
            if (match_count < start_from) {
                match_count++;
                continue;
            }

            if (parent_dir->task->batch_size >= parent_dir->task->batch_max)
                return knd_OK;

            c = dir->conc;
            if (!c) {
                err = unfreeze_class(self, dir, &c);
                if (err) return err;
            }
            c->out = parent_dir->out;

            if (DEBUG_CONC_LEVEL_2) {
                knd_log("    :: export term Conc: \"%.*s\"  match_count:%zu  start_from:%zu",
                        dir->name_size, dir->name, match_count, start_from);
                c->str(c);
            }

            /* separator needed? */
            if (parent_dir->task->batch_size) {
                err = c->out->write(c->out, ",", 1);
                if (err) return err;
            }
            
            err = c->export(c);
            if (err) return err;

            parent_dir->task->batch_size++;
            match_count++;
            continue;
        }
        
        match_count += dir->num_terminals;

        if (match_count < start_from) {
            knd_log("-- class to skip over: \"%.*s\" num terminals:%zu",
                    dir->name_size, dir->name, dir->num_terminals);
            continue;
        }

        if (DEBUG_CONC_LEVEL_2)
            knd_log("++ class to explore: \"%.*s\" num terminals:%zu",
                    dir->name_size, dir->name, dir->num_terminals);

        dir->task = parent_dir->task;
        dir->task->match_count = match_count;
        dir->out = parent_dir->out;

        err = iter_export_JSON(self, dir);
        if (err) return err;

        if (parent_dir->task->batch_size >= parent_dir->task->batch_max) {
            if (DEBUG_CONC_LEVEL_3)
                knd_log("== pagination batch is full!");
            return knd_OK;
        }
    }

    return knd_OK;
}

static int export_JSON(struct kndConcept *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    struct kndTranslation *tr;
    struct kndAttr *attr;

    struct kndConcept *c;
    struct kndConcItem *item;
    struct kndConcRef *ref;
    struct kndConcDir *dir;
    struct kndUpdate *update;

    struct tm tm_info;
    struct kndOutput *out;
    size_t item_count;
    int i, err, e;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. JSON export concept: \"%s\"  "
                "locale: %s depth: %lu num_terminals:%zu\n",
                self->name, self->task->locale,
                (unsigned long)self->depth, self->dir->num_terminals);

    out = self->out;
    err = out->write(out, "{", 1);                                                RET_ERR();
    err = out->write(out, "\"_n\":\"", strlen("\"_n\":\""));                      RET_ERR();
    err = out->write(out, self->name, self->name_size);                           RET_ERR();
    err = out->write(out, "\"", 1);                                               RET_ERR();

    if (self->dir && self->dir->numid) {
        err = out->write(out, ",\"_id\":", strlen(",\"_id\":"));                  RET_ERR();
        buf_size = snprintf(buf, KND_NAME_SIZE, "%zu", self->dir->numid);
        err = out->write(out, buf, buf_size);                                     RET_ERR();
    }

    if (self->num_updates) {
        /* latest update */
        update = self->updates->update;
        err = out->write(out, ",\"_state\":", strlen(",\"_state\":"));            RET_ERR();
        buf_size = snprintf(buf, KND_NAME_SIZE, "%zu", update->id);
        err = out->write(out, buf, buf_size);                                     RET_ERR();

        time(&update->timestamp);
        localtime_r(&update->timestamp, &tm_info);
        buf_size = strftime(buf, KND_NAME_SIZE,
                            ",\"_timestamp\":\"%Y-%m-%d %H:%M:%S\"", &tm_info);
        err = out->write(out, buf, buf_size);                                     RET_ERR();
    }

    /* choose gloss */
    tr = self->tr;
    while (tr) {
        if (memcmp(self->task->locale, tr->locale, tr->locale_size)) {
            goto next_tr;
        }
        err = out->write(out, ",\"gloss\":\"", strlen(",\"gloss\":\""));          RET_ERR();
        err = out->write(out, tr->val,  tr->val_size);                            RET_ERR();
        err = out->write(out, "\"", 1);                                           RET_ERR();
        break;

    next_tr:
        tr = tr->next;
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

            err = out->write(out, "{\"_n\":\"", strlen("{\"_n\":\""));              RET_ERR();
            err = out->write(out, item->name, item->name_size);
            if (err) return err;
            err = out->write(out, "\"", 1);
            if (err) return err;

            err = out->write(out, ",\"_id\":", strlen(",\"_id\":"));
            if (err) return err;
            buf_size = snprintf(buf, KND_NAME_SIZE, "%zu", item->numid);
            err = out->write(out, buf, buf_size);
            if (err) return err;

            if (item->attrs) {
                err = out->write(out, ",\"_attrs\":[", strlen(",\"_attrs\":["));
                if (err) return err;
                err = attr_items_export_JSON(self, item->attrs, 0);
                if (err) return err;
                err = out->write(out, "]", 1);
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
        err = out->write(out, ",\"_attrs\": {",
                         strlen(",\"_attrs\": {"));
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
            
            err = attr->export(attr);
            if (err) {
                if (DEBUG_CONC_LEVEL_TMP)
                    knd_log("-- failed to export %s attr to JSON: %s\n", attr->name);
                return err;
            }

            i++;
            attr = attr->next;
        }
        err = out->write(out, "}", 1);
        if (err) return err;
    }

    /* non-terminal classes */
    if (self->dir && self->dir->num_children) {
        err = out->write(out, ",\"_num_subclasses\":", strlen(",\"_num_subclasses\":"));
        if (err) return err;
        buf_size = sprintf(buf, "%zu", self->dir->num_children);
        err = out->write(out, buf, buf_size);
        if (err) return err;

        if (self->dir->num_terminals) {
            err = out->write(out, ",\"_num_term_classes\":", strlen(",\"_num_term_classes\":"));
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
                err = out->write(out, "{\"_n\":\"", strlen("{\"_n\":\""));
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
                    knd_log("base of --> %s", ref->conc->name);

                if (i) {
                    err = out->write(out, ",", 1);
                    if (err) return err;
                }
                c = ref->conc;
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

    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}

static int attr_items_export_GSP(struct kndConcept *self,
                                 struct kndAttrItem *items, size_t depth)
{
    struct kndAttrItem *item;
    struct kndOutput *out;
    int err;

    out = self->out;

    for (item = items; item; item = item->next) {
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
    struct kndOutput *out = self->out;
    int err;

    if (DEBUG_CONC_LEVEL_1)
        knd_log(".. GSP export of \"%.*s\" [%.*s]",
                self->name_size, self->name, KND_ID_SIZE, self->id);

    err = out->write(out, "{", 1);
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
            err = out->write(out, " ", 1);
            if (err) return err;
            err = out->write(out, tr->val, tr->val_size);
            if (err) return err;
            err = out->write(out, "}", 1);
            if (err) return err;
        }
        err = out->write(out, "]", 1);
        if (err) return err;
    }

    if (self->base_items) {
        err = out->write(out, "[_ci", strlen("[_ci"));
        if (err) return err;

        for (item = self->base_items; item; item = item->next) {
            err = out->write(out, "{", strlen("{"));
            if (err) return err;
            err = out->write(out, item->conc->id, KND_ID_SIZE);
            if (err) return err;
            err = out->write(out, " ", 1);
            if (err) return err;
            err = out->write(out, item->conc->name, item->conc->name_size);
            if (err) return err;
 
            if (item->attrs) {
              err = attr_items_export_GSP(self, item->attrs, 0);
              if (err) return err;
            }
            err = out->write(out, "}", 1);
            if (err) return err;

        }
        err = out->write(out, "]", 1);
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

    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}

static int build_class_updates(struct kndConcept *self)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size;
    struct kndOutput *out = self->task->update;
    struct kndConcept *c;
    int err;

    for (c = self->inbox; c; c = c->next) {
        c->task = self->task;

        err = out->write(out, "{class ", strlen("{class "));   RET_ERR();
        err = out->write(out, c->name, c->name_size);

        err = out->write(out, "(id ", strlen("(id "));         RET_ERR();
        buf_size = sprintf(buf, "%zu", c->numid);
        err = out->write(out, buf, buf_size);                  RET_ERR();

        /* TODO: timestamp */

        /*if (c->phase == KND_REMOVED) {
            err = out->write(out, "removed", strlen("removed"));
            if (err) return err;
            err = out->write(out, "rm", strlen("rm"));
            if (err) return err;
            } else { */

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
    struct kndOutput *out = self->task->update;
    int err;

    out->reset(out);
    err = out->write(out, "{task{update", strlen("{task{update"));               RET_ERR();

    /* spec body */
    err = out->write(out,
                     self->task->update_spec,
                     self->task->update_spec_size);                              RET_ERR();
    
    /* state information */
    err = out->write(out, "(state ", strlen("(state "));                         RET_ERR();
    buf_size = sprintf(buf, "%zu", update->id);
    err = out->write(out, buf, buf_size);                                        RET_ERR();

    if (self->inbox) {
        err = build_class_updates(self);                                         RET_ERR();
    }

    if (self->rel->inbox_size) {
	self->rel->out = out;
        err = self->rel->export_updates(self->rel);                              RET_ERR();
    }
   
    err = out->write(out, ")}}", strlen(")}}"));                                 RET_ERR();
    return knd_OK;
}


static int set_liquid_class_id(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndConcept *self = (struct kndConcept*)obj;
    struct kndConcept *c;
    struct kndTaskArg *arg;
    const char *val = NULL;
    size_t val_size = 0;
    long numval = 0;
    int err;

    if (!self->curr_class) return knd_FAIL;
    c = self->curr_class;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            val = arg->val;
            val_size = arg->val_size;
        }
    }

    err = knd_parse_num((const char*)val, &numval);               RET_ERR();
    c->numid = numval;
    if (c->dir) {
        c->dir->numid = numval;
    }

    //self->curr_class->update_id = self->curr_update->id;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. set curr liquid class id: %zu  update id: %zu",
                c->numid, c->numid);

    return knd_OK;
}


static int run_get_liquid_class(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndConcept *self = obj;
    struct kndTaskArg *arg;
    struct kndConcept *c;
    struct kndObjEntry *entry;
    const char *name = NULL;
    size_t name_size = 0;
    int err;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    if (name_size >= KND_NAME_SIZE) return knd_LIMIT;

    err = get_class(self, name, name_size, &self->curr_class);
    if (err) return err;
    
    return knd_OK;
}

static int parse_liquid_class_id(void *obj,
                               const char *rec, size_t *total_size)
{
    struct kndConcept *self = obj;
    struct kndUpdate *update = self->curr_update;
    struct kndConcept *c;
    struct kndClassUpdate *class_update;
    struct kndClassUpdateRef *class_update_ref;
    int err;

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_liquid_class_id,
          .obj = self
        }
    };

    if (!self->curr_class) return knd_FAIL;

    err = knd_parse_task(rec, total_size, specs,
                         sizeof(specs) / sizeof(struct kndTaskSpec));            PARSE_ERR();

    c = self->curr_class;

    /* register class update */
    err = self->mempool->new_class_update(self->mempool, &class_update);         RET_ERR();
    class_update->conc = c;

    update->classes[update->num_classes] = class_update;
    update->num_classes++;

    err = self->mempool->new_class_update_ref(self->mempool, &class_update_ref); RET_ERR();
    class_update_ref->update = update;

    class_update_ref->next = c->updates;
    c->updates =  class_update_ref;
    c->num_updates++;

    return knd_OK;
}

static int parse_liquid_class_update(void *obj,
                                   const char *rec, size_t *total_size)
{
    struct kndConcept *self = obj;
    struct kndClassUpdate **class_updates;
    int err;

    if (DEBUG_CONC_LEVEL_2) {
        knd_log("..  liquid class update REC: \"%.*s\"..", 32, rec); }

    if (!self->curr_update) return knd_FAIL;

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_get_liquid_class,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "id",
          .name_size = strlen("id"),
          .parse = parse_liquid_class_id,
          .obj = self
        }
    };


    /* create index of class updates */
    class_updates = realloc(self->curr_update->classes,
                            (self->inbox_size * sizeof(struct kndClassUpdate*)));
    if (!class_updates) return knd_NOMEM;
    self->curr_update->classes = class_updates;

    err = knd_parse_task(rec, total_size, specs,
                         sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;

    return knd_OK;
}

static int parse_liquid_rel_update(void *obj,
                                   const char *rec, size_t *total_size)
{
    struct kndConcept *self = obj;
    int err;

    if (!self->curr_update) return knd_FAIL;

    self->rel->curr_update = self->curr_update;
    err = self->rel->parse_liquid_updates(self->rel, rec, total_size);                RET_ERR();

    return knd_OK;
}

static int new_liquid_update(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndConcept *self = obj;
    struct kndTaskArg *arg;
    struct kndUpdate *update;
    const char *val = NULL;
    size_t val_size = 0;
    long numval = 0;
    int err;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!memcmp(arg->name, "_impl", strlen("_impl"))) {
            val = arg->val;
            val_size = arg->val_size;
        }
    }

    err = knd_parse_num((const char*)val, &numval);
    if (err) return err;

    err = self->mempool->new_update(self->mempool, &update);
    if (err) return err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log("== new class update: %zu", update->id);

    self->curr_update = update;

    return knd_OK;
}

static int apply_liquid_updates(struct kndConcept *self,
                                const char *rec,
                                size_t *total_size)
{
    struct kndConcept *c;
    struct kndConcDir *dir;
    struct kndObjEntry *entry;
    struct kndObject *obj;
    struct kndRel *rel;
    struct kndStateControl *state_ctrl = self->task->state_ctrl;
    struct kndTaskSpec specs[] = {
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

    if (DEBUG_CONC_LEVEL_TMP)
        knd_log("..apply liquid updates..");

    if (self->inbox_size) {
        for (c = self->inbox; c; c = c->next) {
            c->task = self->task;
            c->log = self->log;
            c->frozen_output_file_name = self->frozen_output_file_name;
            c->frozen_output_file_name_size = self->frozen_output_file_name_size;
            c->mempool = self->mempool;

            err = c->resolve(c);
            if (err) return err;

            dir = malloc(sizeof(struct kndConcDir));
            memset(dir, 0, sizeof(struct kndConcDir));
            dir->conc = c;
            dir->mempool = self->mempool;

            err = self->class_idx->set(self->class_idx,
                                       c->name, c->name_size, (void*)dir);
            if (err) return err;
        }
        self->inbox = NULL;
        self->inbox_size = 0;
    }

    if (self->rel->inbox_size) {
	for (rel = self->rel->inbox; rel; rel = rel->next) {
	    err = rel->resolve(rel);
	    if (err) return err;
	}
    }

    err = knd_parse_task(rec, total_size, specs,
                         sizeof(specs) / sizeof(struct kndTaskSpec));             PARSE_ERR();

    if (!self->curr_update) return knd_FAIL;

    err = state_ctrl->confirm(state_ctrl, self->curr_update);                     RET_ERR();

    if (self->rel->inbox_size)
	self->rel->reset_inbox(self->rel);

    return knd_OK;
}


static void reset_inbox(struct kndConcept *self)
{

    self->inbox = NULL;
    self->inbox_size = 0;
    self->obj_inbox = NULL;
    self->obj_inbox_size = 0; 

    self->rel->reset_inbox(self->rel);
}


static int knd_update_state(struct kndConcept *self)
{
    char pathbuf[KND_TEMP_BUF_SIZE];
    struct kndConcept *c;
    struct kndObject *obj;
    struct kndOutput *out = self->task->spec_out;
    struct kndStateControl *state_ctrl = self->task->state_ctrl;
    struct kndUpdate *update;
    struct kndClassUpdate *class_update;
    struct kndClassUpdate **class_updates;
    const char *inbox_dir = "/schema/inbox";
    size_t inbox_dir_size = strlen(inbox_dir);
    size_t update_id;
    int err;

    if (DEBUG_CONC_LEVEL_2)
	knd_log("..update state..");

    /* new update obj */
    err = self->mempool->new_update(self->mempool, &update);
    if (err) return err;

    update->spec = self->task->spec;
    update->spec_size = self->task->spec_size;

    /* create index of class updates */
    class_updates = realloc(update->classes,
                            (self->inbox_size * sizeof(struct kndClassUpdate*)));
    if (!class_updates) return knd_NOMEM;
    update->classes = class_updates;

    /* resolve all refs */
    for (c = self->inbox; c; c = c->next) {
        c->task = self->task;
        c->log = self->log;
        c->frozen_output_file_name = self->frozen_output_file_name;
        c->frozen_output_file_name_size = self->frozen_output_file_name_size;

        err = c->resolve(c);
        if (err) {
            knd_log("-- %.*s class not resolved :(", c->name_size, c->name);
            return err;
        }

        err = self->mempool->new_class_update(self->mempool, &class_update);      RET_ERR();
        self->next_numid++;
        c->numid = self->next_numid;
        
        class_update->conc = c;
        update->classes[update->num_classes] = class_update;
        update->num_classes++;
    }

    if (self->rel->inbox_size) {
        err = self->rel->update(self->rel, update);                               RET_ERR();
    }
    //err = self->proc->update(self->proc, update);                               RET_ERR();

    err = state_ctrl->confirm(state_ctrl, update);                                RET_ERR();

    err = export_updates(self, update);                                           RET_ERR();

    reset_inbox(self);

    return knd_OK;
}

static int restore(struct kndConcept *self)
{
    char state_buf[KND_STATE_SIZE];
    char last_state_buf[KND_STATE_SIZE];
    struct kndOutput *out = self->out;

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

    err = out->read_file(out,
                         (const char*)out->buf, out->buf_size);
    if (err) {
        knd_log("-- no class_state.id file found, assuming initial state ..");
        return knd_OK;
    }

    /* check if state content is a valid state id */
    err = knd_state_is_valid(out->file, out->file_size);
    if (err) {
        knd_log("-- state id is not valid: \"%.*s\"",
                out->file_size, out->file);
        return err;
    }

    memcpy(last_state_buf, out->file, KND_STATE_SIZE);
    if (DEBUG_CONC_LEVEL_TMP)
        knd_log(".. last DB state: \"%.*s\"",
                out->file_size, out->file);

    out->rtrim(out, strlen("/schema/class_state.id"));
    err = out->write(out, inbox_dir, inbox_dir_size);
    if (err) return err;
    
    while (1) {
        knd_next_state(state_buf);

        err = out->write_state_path(out, state_buf);
        if (err) return err;

        err = out->write(out, "/spec.gsl", strlen("/spec.gsl"));
        if (err) return err;

        
        err = out->read_file(out, (const char*)out->buf, out->buf_size);
        if (err) {
            knd_log("-- couldn't read GSL spec \"%s\" :(", out->buf);
            return err;
        }

        if (DEBUG_CONC_LEVEL_TMP)
            knd_log(".. state update spec file: \"%.*s\" SPEC: %.*s\n\n",
                    out->buf_size, out->buf, out->file_size, out->file);


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
    size_t curr_dir_size = 0;
    struct kndObject *obj;
    struct kndObjEntry *entry;
    struct kndOutput *out;
    const char *key;
    void *val;
    size_t chunk_size;
    size_t obj_block_size = 0;
    size_t num_size;
    int err;

    if (DEBUG_CONC_LEVEL_2)
        knd_log(".. freezing objs of class \"%.*s\", total:%zu  valid:%zu",
                self->name_size, self->name,
                self->dir->obj_idx->size, self->dir->num_objs);

    out = self->out;
    out->reset(out);
    self->dir_out->reset(self->dir_out);

    err = self->dir_out->write(self->dir_out, "[o", 2);
    if (err) return err;
    key = NULL;
    self->dir->obj_idx->rewind(self->dir->obj_idx);
    do {
        self->dir->obj_idx->next_item(self->dir->obj_idx, &key, &val);
        if (!key) break;
        entry = (struct kndObjEntry*)val;
        obj = entry->obj;

        if (obj->phase != KND_CREATED) {
            knd_log("NB: skip freezing \"%.*s\"   phase: %d",
                    obj->name_size, obj->name, obj->phase);
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

        err = self->dir_out->write(self->dir_out, " ", 1);
        if (err) return err;
        
        buf_size = sprintf(buf, "%zx", obj->frozen_size);
        err = self->dir_out->write(self->dir_out, buf, buf_size);
        if (err) return err;

        /* OBJ persistent write */
        if (out->buf_size > out->threshold_size) {
            err = knd_append_file(self->frozen_output_file_name,
                                  out->buf, out->buf_size);
            if (err) return err;

            *total_frozen_size += out->buf_size;
            obj_block_size += out->buf_size;
            out->reset(out);
        }
    } while (key);

    /* final chunk to write */
    if (self->out->buf_size) {
        err = knd_append_file(self->frozen_output_file_name,
                              out->buf, out->buf_size);
        if (err) return err;

        *total_frozen_size += out->buf_size;
        obj_block_size += out->buf_size;
        out->reset(out);
    }

    /* close directory */
    err = self->dir_out->write(self->dir_out, "]", 1);
    if (err) return err;

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

    num_size = sprintf(curr_dir, " %lu}",
                       (unsigned long)obj_block_size);
    curr_dir +=      num_size;
    curr_dir_size += num_size;

    *total_size = curr_dir_size;
    
    return knd_OK;
}


static int freeze_subclasses(struct kndConcept *self,
                             size_t *total_frozen_size,
                             char *output,
                             size_t *total_size)
{
    char buf[KND_SHORT_NAME_SIZE];
    size_t buf_size;
    struct kndConcept *c;
    struct kndConcRef *ref;
    char *curr_dir = output;
    size_t curr_dir_size = 0;
    size_t chunk_size;
    size_t num_size;
    int err;

    chunk_size = strlen("[c");
    memcpy(curr_dir, "[c", chunk_size); 
    curr_dir += chunk_size;
    curr_dir_size += chunk_size;

    for (size_t i = 0; i < self->num_children; i++) {
        ref = &self->children[i];
        c = ref->conc;
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
            knd_log("     OUT: \"%.*s\" [%zu]\nclass \"%.*s\" id:%.*s [frozen size: %lu]\n",
                    c->out->buf_size, c->out->buf, c->out->buf_size,
                    c->name_size, c->name, KND_ID_SIZE, c->id,
                    (unsigned long)c->frozen_size);

        memcpy(curr_dir, "{", 1); 
        curr_dir++;
        curr_dir_size++;
        memcpy(curr_dir, c->id, KND_ID_SIZE);
        curr_dir      += KND_ID_SIZE;
        curr_dir_size += KND_ID_SIZE;
        
        num_size = sprintf(curr_dir, " %lu",
                           (unsigned long)c->frozen_size);
        curr_dir +=      num_size;
        curr_dir_size += num_size;
        *total_frozen_size += c->frozen_size;

        if (c->num_terminals) {
            chunk_size = strlen("{t ");
            memcpy(curr_dir, "{t ", chunk_size); 
            curr_dir += chunk_size;
            curr_dir_size += chunk_size;

            num_size = sprintf(curr_dir, "%lu",
                               (unsigned long)c->num_terminals);
            curr_dir +=      num_size;
            curr_dir_size += num_size;

            memcpy(curr_dir, "}", 1);
            curr_dir++;
            curr_dir_size++;
        }

        if (c->dir && c->dir->num_objs) {
            chunk_size = strlen("{o ");
            memcpy(curr_dir, "{o ", chunk_size); 
            curr_dir += chunk_size;
            curr_dir_size += chunk_size;

            num_size = sprintf(curr_dir, "%lu",
                               (unsigned long)c->dir->num_objs);
            curr_dir +=      num_size;
            curr_dir_size += num_size;

            memcpy(curr_dir, "}", 1);
            curr_dir++;
            curr_dir_size++;
        }

        memcpy(curr_dir, "}", 1);
        curr_dir++;
        curr_dir_size++;
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
    size_t num_size;
    int err;

    if (DEBUG_CONC_LEVEL_1)
        knd_log(".. freezing rels..");

    key = NULL;
    self->rel_idx->rewind(self->rel_idx);
    do {
        self->rel_idx->next_item(self->rel_idx, &key, &val);
        if (!key) break;

        dir = (struct kndRelDir*)val;
        rel = dir->rel;

        rel->out = self->out;
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
    struct kndRel *rel;
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
                          self->out->buf, self->out->buf_size);
    if (err) return err;

    total_frozen_size = self->out->buf_size;

    /* no dir entry necessary */
    if (!self->num_children) {
        self->is_terminal = true;
        if (!self->dir) {
            self->frozen_size = total_frozen_size;
            return knd_OK;
        }
        if (!self->dir->obj_idx) {
            self->frozen_size = total_frozen_size;
            return knd_OK;
        }
    }

    /* class dir entry */
    chunk_size = strlen("{C ");
    memcpy(curr_dir, "{C ", chunk_size); 
    curr_dir += chunk_size;
    curr_dir_size += chunk_size;

    num_size = sprintf(curr_dir, "%lu}",
                       (unsigned long)total_frozen_size);
    curr_dir +=      num_size;
    curr_dir_size += num_size;

    /* any instances to freeze? */
    if (self->dir && self->dir->num_objs) {
        err = freeze_objs(self, &total_frozen_size, curr_dir, &chunk_size);
        if (err) return err;
        curr_dir +=      chunk_size;
        curr_dir_size += chunk_size;
    }

    if (self->num_children) {
        err = freeze_subclasses(self, &total_frozen_size, curr_dir, &chunk_size);
        if (err) return err;
        curr_dir +=      chunk_size;
        curr_dir_size += chunk_size;
    }

    /* rels */
    if (self->rel && self->rel->rel_idx->size) {
        chunk_size = strlen("[R ");
        memcpy(curr_dir, "[R ", chunk_size); 
        curr_dir += chunk_size;
        curr_dir_size += chunk_size;

        chunk_size = 0;
        self->rel->out = self->out;
        self->rel->frozen_output_file_name = self->frozen_output_file_name;

        err = freeze_rels(self->rel, &total_frozen_size, curr_dir, &chunk_size);
        if (err) return err;

        curr_dir +=      chunk_size;
        curr_dir_size += chunk_size;

        memcpy(curr_dir, "]", 1); 
        curr_dir++;
        curr_dir_size++;
    }

    /* procs */
    if (self->proc) {
        /*chunk_size = strlen("[P ");
        memcpy(curr_dir, "[P ", chunk_size); 
        curr_dir += chunk_size;
        curr_dir_size += chunk_size;

        chunk_size = 0;
        self->proc->out = self->out;
        self->proc->frozen_output_file_name = self->frozen_output_file_name;

        err = freeze_procs(self->proc, &total_frozen_size, curr_dir, &chunk_size);
        if (err) return err;

        curr_dir +=      chunk_size;
        curr_dir_size += chunk_size;

        memcpy(curr_dir, "]", 1); 
        curr_dir++;
        curr_dir_size++;*/
    }
    
    if (DEBUG_CONC_LEVEL_2)
        knd_log("== %.*s (%.*s)   DIR: \"%.*s\"   [%lu]",
                self->name_size, self->name, KND_ID_SIZE, self->id,
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

static void reset(struct kndConcept *self)
{
    struct kndConcRef *ref;
    struct kndConcept *c;
    struct kndObject *obj;

    /* reset children */
    for (size_t i = 0; i < self->num_children; i++) {
        ref = &self->children[i];
        c = ref->conc;
        c->reset(c);
    }
    self->num_children = 0;
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
    self->restore = restore;
    self->reset = reset;
    self->select_delta = select_delta;
    self->coordinate = coordinate;
    self->resolve = resolve_name_refs;

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
kndConcept_new(struct kndConcept **c)
{
    struct kndConcept *self;

    self = malloc(sizeof(struct kndConcept));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndConcept));
    memset(self->id,      '0', KND_ID_SIZE);
    memset(self->next_id, '0', KND_ID_SIZE);
    memset(self->next_obj_id, '0', KND_ID_SIZE);

    kndConcept_init(self);
    *c = self;
    return knd_OK;
}
