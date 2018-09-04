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

static int build_attr_name_idx(struct kndClass *self);
static gsl_err_t confirm_class_var(void *obj, const char *name, size_t name_size);
static gsl_err_t confirm_attr_var(void *obj, const char *name, size_t name_size);

static gsl_err_t validate_attr_var_list(void *obj,
                                         const char *name, size_t name_size,
                                         const char *rec, size_t *total_size);
static gsl_err_t attr_var_append(void *accu,
                                  void *item);

static int read_obj_entry(struct kndClass *self,
                          struct kndObjEntry *entry,
                          struct kndClassInst **result);


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

    if (!c) {
        //err = unfreeze_class(self, entry, &c);                                    RET_ERR();
    }

    c->str(c);

    return knd_OK;
}

static void reset_inbox(struct kndClass *self,
                        bool rollback)
{
    struct kndClass *c, *next_c;
    struct kndClassInst *obj, *next_obj;
    struct ooDict *class_name_idx = self->entry->repo->root_class->class_name_idx;
    struct kndSet *class_idx = self->entry->repo->root_class->class_idx;
    int err;

    c = self->inbox;
    while (c) {
        c->reset_inbox(c, rollback);
        next_c = c->next;

        if (rollback) {
            // TODO: remove from class_idx

            err = class_name_idx->remove(class_name_idx, c->name, c->name_size);
            // signal error?
        }

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

    if (entry->inst_name_idx) {
        entry->inst_name_idx->del(entry->inst_name_idx);
        entry->inst_name_idx = NULL;
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
                    item->entry->id_size, item->entry->id, item->numid,
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

        /*if (DEBUG_CLASS_LEVEL_2) {
            err = set->map(set, str_conc_elem, NULL);
            if (err) return;
            }*/
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
        err = knd_inherit_attrs(self, item->entry->class);                        RET_ERR();
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
    err = knd_resolve_classes(self);
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

static int get_inst(struct kndClass *self,
                    const char *name, size_t name_size,
                    struct kndClassInst **result)
{
    struct kndObjEntry *entry;
    struct kndClassInst *obj;
    struct glbOutput *log = self->entry->repo->log;
    struct kndTask *task = self->entry->repo->task;
    int err, e;

    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. \"%.*s\" class to get instance: \"%.*s\"..",
                self->entry->name_size, self->entry->name,
                name_size, name);

    if (!self->entry) {
        knd_log("-- no frozen entry rec in \"%.*s\" :(",
                self->entry->name_size, self->entry->name);
    }

    if (!self->entry->inst_name_idx) {
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

    entry = self->entry->inst_name_idx->get(self->entry->inst_name_idx, name, name_size);
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
        knd_log("++ got obj entry %.*s  size: %zu",
                name_size, name, entry->block_size);

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
    //err = unfreeze_obj_entry(self, entry, result);
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

extern int get_arg_value(struct kndAttrVar *src,
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
    struct kndRepo *repo = self->entry->repo;
    struct kndRel *rel = repo->root_rel;
    struct kndProc *proc = repo->root_proc;
    struct kndUpdate *update;
    struct kndClassUpdate *class_update;
    struct kndMemPool *mempool = repo->mempool;
    struct kndStateControl *state_ctrl = repo->state_ctrl;
    struct kndTask *task = repo->task;
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
        class_update->class = c;
        class_update->update = update;

        err = c->resolve(c, class_update);
        if (err) {
            knd_log("-- %.*s class not resolved :(", c->name_size, c->name);
            return err;
        }

        if (DEBUG_CLASS_LEVEL_2)
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

static int export(struct kndClass *self,
                  knd_format format,
                  struct glbOutput *out)
{
    switch (format) {
    case KND_FORMAT_JSON:
        return knd_class_export_JSON(self, out);
    case KND_FORMAT_GSP:
        return knd_class_export_GSP(self, out);
        break;
    default:
        break;
    }
    knd_log("-- format %d not supported", format);
    return knd_FAIL;
}

static int export_updates(struct kndClass *self,
                          struct kndClassUpdate *update,
                          knd_format format,
                          struct glbOutput *out)
{
    switch (format) {
    case KND_FORMAT_GSP:
        return knd_class_export_updates_GSP(self, update, out);
    default:
        break;
    }
    return knd_FAIL;
}

static int open_DB(struct kndClass *self,
                   const char *filename)
{
    // TODO use kndClassStorage to open a frozen DB

    knd_log(".. opening frozen DB: %s", filename);

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

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. \"%.*s\" class (repo: %.*s) to check attr \"%.*s\"",
                self->entry->name_size, self->entry->name,
                self->entry->repo->name_size, self->entry->repo->name,
                name_size, name);
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
    struct kndClass *c = NULL;
    struct kndRepo *repo = self->entry->repo;
    struct glbOutput *log = repo->log;
    struct ooDict *class_name_idx = repo->root_class->class_name_idx;
    struct kndTask *task = repo->task;
    struct kndState *state;
    int err;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. %.*s to get class: \"%.*s\"..",
                self->entry->name_size, self->entry->name,
                name_size, name);
    }
    
    entry = class_name_idx->get(class_name_idx, name, name_size);
    if (!entry) {
        /* check parent schema */
        if (repo->base) {
            err = knd_get_class(repo->base->root_class, name, name_size, result);
            if (err) return err;
            return knd_OK;
        }
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

    if (repo->base) {
        err = knd_get_class(repo->base->root_class, name, name_size, result);
        if (err) return err;
        return knd_OK;
    }

    if (DEBUG_CLASS_LEVEL_TMP)
        knd_log(".. unfreezing the \"%.*s\" class ..", name_size, name);

    // TODO
    /*err = unfreeze_class(self, entry, &c);
    if (err) {
        knd_log("-- failed to unfreeze class: %.*s",
                entry->name_size, entry->name);
        return err;
        }*/
    //*result = c;

    return knd_FAIL;
}

extern int knd_get_class_by_id(struct kndClass *self,
                               const char *id, size_t id_size,
                               struct kndClass **result)
{
    struct kndClassEntry *entry;
    struct kndClass *c = NULL;
    struct kndRepo *repo = self->entry->repo;
    struct glbOutput *log = repo->log;
    struct kndSet *class_idx = repo->root_class->class_idx;
    struct kndTask *task = repo->task;
    void *elem;
    struct kndState *state;
    int err;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. repo \"%.*s\" to get class by id: \"%.*s\"..",
                repo->name_size, repo->name, id_size, id);
    }

    err = class_idx->get(class_idx, id, id_size, &elem);
    if (err) {
        /* check parent schema */
        if (repo->base) {
            err = knd_get_class_by_id(repo->base->root_class, id, id_size, result);
            if (err) return err;
            return knd_OK;
        }
        knd_log("-- no such class: \"%.*s\":(", id_size, id);
        log->reset(log);
        err = log->write(log, id, id_size);
        if (err) return err;
        err = log->write(log, " class not found",
                               strlen(" class not found"));
        if (err) return err;
        if (task)
            task->http_code = HTTP_NOT_FOUND;
        return knd_NO_MATCH;
    }

    entry = elem;

    if (entry->class) {
        c = entry->class;
        
        if (c->num_states) {
            state = c->states;
            if (state->phase == KND_REMOVED) {
                knd_log("-- \"%s\" class was removed", id);
                log->reset(log);
                err = log->write(log, id, id_size);
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

    if (repo->base) {
        err = knd_get_class_by_id(repo->base->root_class, id, id_size, result);
        if (err) return err;
        return knd_OK;
    }

    if (DEBUG_CLASS_LEVEL_TMP)
        knd_log(".. unfreezing the \"%.*s\" class ..", id_size, id);

    // TODO
    /*err = unfreeze_class(self, entry, &c);
    if (err) {
        knd_log("-- failed to unfreeze class: %.*s",
                entry->name_size, entry->name);
        return err;
        }*/
    //*result = c;

    return knd_FAIL;
}

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

    self->coordinate = coordinate;
    self->resolve = knd_resolve_class;

    self->import = knd_import_class;
    self->select = knd_select_class;

    //self->sync = parse_sync_task;
    //self->freeze = freeze;

    self->update_state = knd_update_state;
    //self->apply_liquid_updates = apply_liquid_updates;
    self->export = export;
    self->export_updates = export_updates;
    self->get = knd_get_class;
    self->get_inst = get_inst;
    self->get_attr = knd_class_get_attr;
}

extern int kndClass_new(struct kndClass **result,
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
    err = mempool->new_class_inst(mempool, &self->curr_inst);                     RET_ERR();
    self->curr_inst->base = self;

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
