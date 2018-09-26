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

static int read_GSL_file(struct kndClass *self,
                         struct kndConcFolder *parent_folder,
                         const char *filename,
                         size_t filename_size);

static void reset_inbox(struct kndClass *self,
                        bool rollback)
{
    struct kndClass *c, *next_c;
    struct kndClassInst *obj, *next_obj;

    c = self->inbox;
    while (c) {
        c->reset_inbox(c, rollback);
        next_c = c->next;

        if (rollback) {
            //c->del(c);
            c = next_c;
            continue;
        }
        c->next = NULL;
        c = next_c;
    }

    obj = self->inst_inbox;
    while (obj) {
        next_obj = obj->next;
        obj->next = NULL;
        obj = next_obj;
    }

    self->inst_inbox = NULL;
    self->inst_inbox_size = 0;
    self->inbox = NULL;
    self->inbox_size = 0;
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
    struct kndTranslation *tr, *t;
    struct kndClassVar *item;
    struct kndClassRef *ref;
    struct kndClass *c;
    const char *name;
    size_t name_size;
    char resolved_state = '-';

    knd_log("\n%*s{class %.*s (repo:%.*s)   id:%.*s  numid:%zu",
            self->depth * KND_OFFSET_SIZE, "",
            self->entry->name_size, self->entry->name,
            self->entry->repo->name_size, self->entry->repo->name,
            self->entry->id_size, self->entry->id,
            self->entry->numid);

    if (self->num_states) {
        knd_log("\n%*s_state:%zu",
            self->depth * KND_OFFSET_SIZE, "",
            self->states->update->numid);
    }

    if (self->num_inst_states) {
        knd_log("\n%*snum inst states:%zu",
            self->depth * KND_OFFSET_SIZE, "",
            self->num_inst_states);
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

            /* if (item->attrs) {
                str_attr_vars(item->attrs, self->depth + 1);
                }*/
        }
    }

    for (ref = self->entry->ancestors; ref; ref = ref->next) {
        c = ref->class;
        knd_log("%*s ==> %.*s (repo:%.*s)", self->depth * KND_OFFSET_SIZE, "",
                c->entry->name_size, c->entry->name,
                c->entry->repo->name_size, c->entry->repo->name);
    }

    // print attrs
    /*    for (size_t i = 0; i < self->entry->num_children; i++) {
        c = self->entry->children[i]->class;
        if (!c) continue;
        knd_log("%*sbase of --> %.*s [%zu]",
                (self->depth + 1) * KND_OFFSET_SIZE, "",
                c->name_size, c->name, c->entry->num_terminals);
                } */

    knd_log("%*s the end of %.*s}", self->depth * KND_OFFSET_SIZE, "",
            self->entry->name_size, self->entry->name);
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
    struct kndRepo *repo = self->entry->repo;
    struct glbOutput *out = repo->out;
    struct glbOutput *file_out = repo->file_out;
    struct kndConcFolder *folder, *folders;
    const char *c;
    char *rec;
    char **recs;
    size_t folder_name_size;
    const char *index_folder_name = "index";
    size_t index_folder_name_size = strlen("index");
    const char *file_ext = ".gsl";
    size_t file_ext_size = strlen(".gsl");
    size_t chunk_size = 0;
    int err;

    out->reset(out);
    err = out->write(out, self->entry->repo->schema_path,
                     self->entry->repo->schema_path_size);                        RET_ERR();
    err = out->write(out, "/", 1);                                                RET_ERR();

    if (parent_folder) {
        err = kndClass_write_filepath(out, parent_folder);  RET_ERR();
    }

    err = out->write(out, filename, filename_size);  RET_ERR();
    err = out->write(out, file_ext, file_ext_size);   RET_ERR();

    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. reading GSL file: %.*s", out->buf_size, out->buf);

    file_out->reset(file_out);
    err = file_out->write_file_content(file_out, (const char*)out->buf);
    if (err) {
        knd_log("-- couldn't read GSL class file \"%s\"", out->buf);
        return err;
    }

    // TODO: find another place for storage
    rec = malloc(file_out->buf_size + 1);
    if (!rec) return knd_NOMEM;
    memcpy(rec, file_out->buf, file_out->buf_size);
    rec[file_out->buf_size] = '\0';
    recs = (char**)realloc(repo->source_files,
                           (repo->num_source_files + 1) * sizeof(char*));
    if (!recs) return knd_NOMEM;
    recs[repo->num_source_files] = rec;
    repo->num_source_files++;

    /* actual parsing */
    err = parse_GSL(self, (const char*)rec, &chunk_size);
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

static int coordinate(struct kndClass *self)
{
    int err;
    if (DEBUG_CLASS_LEVEL_TMP)
        knd_log(".. class coordination in progress ..");

    err = knd_resolve_classes(self);                    RET_ERR();

    if (self->entry->descendants) {
        if (DEBUG_CLASS_LEVEL_TMP)
            knd_log("== TOTAL classes: %zu",
                    self->entry->descendants->num_elems);
    }

    return knd_OK;
}

extern int knd_get_class_inst(struct kndClass *self,
                              const char *name, size_t name_size,
                              struct kndClassInst **result)
{
    struct kndClassInstEntry *entry;
    struct kndClassInst *obj;
    struct ooDict *name_idx;
    struct glbOutput *log = self->entry->repo->log;
    struct kndTask *task = self->entry->repo->task;
    int err, e;

    if (DEBUG_CLASS_LEVEL_2)
        knd_log(".. \"%.*s\" class (%.*s) to get instance: \"%.*s\"..",
                self->entry->name_size, self->entry->name,
                self->entry->repo->name_size, self->entry->repo->name,
                name_size, name);

    if (!self->entry) {
        knd_log("-- no frozen entry rec in \"%.*s\" :(",
                self->entry->name_size, self->entry->name);
    }

    if (!self->entry->inst_idx) {
        knd_log("-- no inst idx in \"%.*s\" :(",
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

    name_idx = self->entry->repo->class_inst_name_idx;
    entry = name_idx->get(name_idx, name, name_size);
    if (!entry) {
        knd_log("-- no such class inst: \"%.*s\"", name_size, name);
        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return err;
        err = log->write(log, " instance not found",
                               strlen(" instance not found"));
        if (err) return err;
        task->http_code = HTTP_NOT_FOUND;
        return knd_NO_MATCH;
    }

    // NB: inst name collisions may occur
    // TODO name -> id + lookup inst_idx

    if (DEBUG_CLASS_LEVEL_2)
        knd_log("++ got obj entry %.*s  size: %zu",
                name_size, name, entry->block_size);

    if (!entry->inst) goto read_entry;

    if (entry->inst->states->phase == KND_REMOVED) {
        knd_log("-- \"%s\" instance was removed", name);
        log->reset(log);
        err = log->write(log, name, name_size);
        if (err) return err;
        err = log->write(log, " instance was removed",
                               strlen(" instance was removed"));
        if (err) return err;
        return knd_NO_MATCH;
    }

    obj = entry->inst;
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
    struct kndAttrRef *attr_ref;
    struct kndAttrVar *child_var;
    struct ooDict *attr_name_idx = src->entry->repo->attr_name_idx;
    int err;

    attr_ref = attr_name_idx->get(attr_name_idx,
                                  query->name, query->name_size);
    if (!attr_ref) {
        knd_log("-- no such attr: %.*s", query->name_size, query->name);
        return knd_FAIL;
    }

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log("++ got attr: %.*s",
                query->name_size, query->name);
    }

    if (!attr_ref->attr_var) return knd_FAIL;

    //str_attr_vars(entry->attr_var, 2);

    /* no more query specs */
    if (!query->num_children) return knd_OK;

    /* check nested attrs */

    // TODO: list item selection
    for (child_var = attr_ref->attr_var->list; child_var; child_var = child_var->next) {
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

static int update_inst_state(struct kndClass *self,
                             struct kndStateRef *children)
{
    struct kndMemPool *mempool = self->entry->repo->mempool;
    struct kndTask *task = self->entry->repo->task;
    struct kndStateRef *state_ref;
    struct kndState *state;
    struct kndSet *inst_idx = self->entry->inst_idx;
    int err;

    assert(inst_idx != NULL);

    err = knd_state_new(mempool, &state);
    if (err) {
        knd_log("-- class inst state alloc failed");
        return err;
    }

    /* check removed objs */
    for (state_ref = children; state_ref; state_ref = state_ref->next) {
        switch (state_ref->state->phase) {
        case KND_REMOVED:
            if (inst_idx->num_valid_elems)
                inst_idx->num_valid_elems--;
            break;
        default:
            break;
        }
    }

    state->phase = KND_UPDATED;
    // set task update
    state->update = task->update;

    state->children = children;
    
    self->num_inst_states++;
    state->numid = self->num_inst_states;
    state->next = self->inst_states;
    self->inst_states = state;

    knd_log("NB: update inst states!");

    return knd_OK;
}

extern int knd_register_inst_states(struct kndClass *self)
{
    struct kndClass *c;
    int err;

    if (DEBUG_CLASS_LEVEL_TMP) {
        knd_log("\n .. \"%.*s\" class to register inst updates..",
                self->name_size, self->name);
    }

    err = update_inst_state(self, self->inst_state_refs);
    if (err) return err;

    /* inform your ancestors */
    for (struct kndClassRef *classref = self->entry->ancestors;
         classref; classref = classref->next) {
        
        c = classref->entry->class;
        /* skip the root class */
        if (!c->entry->ancestors) continue;
        if (c->state_top) continue;
        if (self->entry->repo != classref->entry->repo) {
            err = knd_class_clone(classref->entry->class, self->entry->repo, &c);
            if (err) return err;
            classref->entry = c->entry;
        }

        err = update_inst_state(c, self->inst_state_refs);                        RET_ERR();
    }
    
    self->inst_state_refs = NULL;

    return knd_OK;
}

static int update_state(struct kndClass *self)
{
    struct kndClass *c;
    struct kndRepo *repo = self->entry->repo;
    struct kndRel *rel = repo->root_rel;
    struct kndProc *proc = repo->root_proc;
    struct kndUpdate *update;
    struct kndClassUpdate *class_update;
    struct kndMemPool *mempool = repo->mempool;
    struct kndStateControl *state_ctrl = repo->state_ctrl;
    int err;

    if (DEBUG_CLASS_LEVEL_TMP)
        knd_log(".. update state of class \"%.*s\" (repo:%.*s)",
                self->entry->name_size, self->entry->name,
                self->entry->repo->name_size, self->entry->repo->name);

    /* new update obj */
    err = knd_update_new(mempool, &update);                                  RET_ERR();

    /* resolve all refs */
    for (c = self->inbox; c; c = c->next) {
        err = knd_class_update_new(mempool, &class_update);                  RET_ERR();

        self->entry->repo->num_classes++;
        c->entry->numid = self->entry->repo->num_classes;
        class_update->class = c;
        class_update->update = update;

        err = c->resolve(c, class_update);
        if (err) {
            knd_log("-- \"%.*s\" class not resolved", c->name_size, c->name);
            return err;
        }

        if (DEBUG_CLASS_LEVEL_2)
            c->str(c);

        if (update->num_classes >= self->inbox_size) {
            knd_log("-- max class updates reached :(");
            return knd_FAIL;
        }

        class_update->next = update->classes;
        update->classes = class_update;
        update->num_classes++;
        update->total_class_insts += class_update->num_insts;
    }

    if (rel->inbox_size) {
        //err = rel->update(rel, update);                                           RET_ERR();
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

extern int knd_is_base(struct kndClass *self,
                       struct kndClass *child)
{
    struct kndClassEntry *entry = child->entry;
    struct kndClassRef *ref;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. check inheritance: %.*s (repo:%.*s) [resolved: %d] => "
                " %.*s (repo:%.*s) [resolved:%d]?",
                child->name_size, child->name,
                child->entry->repo->name_size, child->entry->repo->name,
                child->is_resolved,
                self->entry->name_size, self->entry->name,
                self->entry->repo->name_size, self->entry->repo->name,
                self->is_resolved);
    }

    for (ref = entry->ancestors; ref; ref = ref->next) {
        if (ref->class == self) {
            return knd_OK;
        }
    }
    if (DEBUG_CLASS_LEVEL_2)
        knd_log("-- no inheritance from  \"%.*s\" to \"%.*s\" :(",
                self->entry->name_size, self->entry->name,
                child->name_size, child->name);
    return knd_FAIL;
}

extern int knd_class_get_attr(struct kndClass *self,
                              const char *name, size_t name_size,
                              struct kndAttrRef **result)
{
    struct kndAttrRef *ref;
    struct kndClassEntry *class_entry;
    struct ooDict *attr_name_idx = self->entry->repo->attr_name_idx;
    int err;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log("\n.. \"%.*s\" class (repo: %.*s) to select attr \"%.*s\"",
                self->entry->name_size, self->entry->name,
                self->entry->repo->name_size, self->entry->repo->name,
                name_size, name);
    }

    ref = attr_name_idx->get(attr_name_idx, name, name_size);
    if (!ref) {

        if (self->entry->repo->base) {
            attr_name_idx = self->entry->repo->base->attr_name_idx;
            ref = attr_name_idx->get(attr_name_idx, name, name_size);
        }

        if (!ref) {
            knd_log("-- no such attr: \"%.*s\"", name_size, name);
            return knd_NO_MATCH;
        }
    }

    for (; ref; ref = ref->next) {
        class_entry = ref->class_entry;
        if (DEBUG_CLASS_LEVEL_2)
            knd_log("== attr %.*s belongs to class: %.*s (repo:%.*s)",
                    name_size, name,
                    class_entry->name_size, class_entry->name,
                    class_entry->repo->name_size, class_entry->repo->name);
        if (class_entry == self->entry) {
            *result = ref;
            return knd_OK;
        }

        err = knd_is_base(class_entry->class, self);
        if (!err) {
            *result = ref;
            return knd_OK;
        }
    }

    return knd_FAIL;
}

extern int knd_get_class(struct kndRepo *self,
                         const char *name, size_t name_size,
                         struct kndClass **result)
{
    struct kndClassEntry *entry;
    struct kndClass *c = NULL;
    struct glbOutput *log = self->log;
    struct ooDict *class_name_idx = self->class_name_idx;
    struct kndTask *task = self->task;
    struct kndState *state;
    int err;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. %.*s repo to get class: \"%.*s\"..",
                self->name_size, self->name,
                name_size, name);
    }
    
    entry = class_name_idx->get(class_name_idx, name, name_size);
    if (!entry) {

        if (DEBUG_CLASS_LEVEL_2)
            knd_log("-- no local class found in: %.*s", self->name_size, self->name);

        /* check parent schema */
        if (self->base) {
            err = knd_get_class(self->base, name, name_size, result);
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

    if (self->base) {
        err = knd_get_class(self->base, name, name_size, result);
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
    struct kndSet *class_idx = repo->class_idx;
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

extern int knd_unregister_class_inst(struct kndClass *self,
                                     struct kndClassInstEntry *entry)
{
    struct kndMemPool *mempool = self->entry->repo->mempool;
    struct kndSet *inst_idx;
    struct kndClass *c;
    struct kndState *state;
    int err;

    /* skip the root class */
    if (!self->entry->ancestors) return knd_OK;

    inst_idx = self->entry->inst_idx;
    if (!inst_idx) return knd_OK;

    // remove
    /* increment state */
    err = knd_state_new(mempool, &state);
    if (err) {
        knd_log("-- state alloc failed :(");
        return err;
    }
    state->val = (void*)entry;
    state->next = self->inst_states;
    self->inst_states = state;
    self->num_inst_states++;
    state->numid = self->num_inst_states;

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. unregister \"%.*s\" inst with class \"%.*s\" (%.*s)  num inst states:%zu",
                entry->inst->name_size, entry->inst->name,
                self->name_size, self->name,
                self->entry->repo->name_size, self->entry->repo->name,
                self->num_inst_states);
    }

    if (entry->inst->base != self) return knd_OK;

    for (struct kndClassRef *ref = self->entry->ancestors; ref; ref = ref->next) {
        c = ref->entry->class;

        if (self->entry->repo != ref->entry->repo) continue;

        err = knd_unregister_class_inst(c, entry);                                         RET_ERR();
    }

    return knd_OK;
}

extern int knd_register_class_inst(struct kndClass *self,
                                   struct kndClassInstEntry *entry)
{
    struct kndMemPool *mempool = self->entry->repo->mempool;
    struct kndSet *inst_idx;
    struct kndClass *c;
    struct kndState *state;
    int err;

    inst_idx = self->entry->inst_idx;
    if (!inst_idx) {
        err = knd_set_new(mempool, &inst_idx);                          RET_ERR();
        inst_idx->type = KND_SET_CLASS_INST;
        self->entry->inst_idx = inst_idx;
    }

    err = inst_idx->add(inst_idx, entry->id, entry->id_size, (void*)entry);
    if (err) {
        knd_log("-- failed to update the class inst idx");
        return err;
    }

    if (DEBUG_CLASS_LEVEL_2) {
        knd_log(".. register \"%.*s\" inst with class \"%.*s\" (%.*s)"
                " num inst states:%zu",
                entry->inst->name_size, entry->inst->name,
                self->name_size, self->name,
                self->entry->repo->name_size, self->entry->repo->name,
                self->num_inst_states);
    }

    if (entry->inst->base != self) return knd_OK;

    for (struct kndClassRef *ref = self->entry->ancestors; ref; ref = ref->next) {
        c = ref->entry->class;
        /* skip the root class */
        if (!c->entry->ancestors) continue;
        if (c->state_top) continue;
       
        if (self->entry->repo != ref->entry->repo) {
            err = knd_class_clone(ref->entry->class, self->entry->repo, &c);
            if (err) return err;
            ref->entry = c->entry;
        }

        err = knd_register_class_inst(c, entry);                                         RET_ERR();
    }

    return knd_OK;
}

extern int knd_class_clone(struct kndClass *self,
                           struct kndRepo *target_repo,
                           struct kndClass **result)
{
    struct kndRepo *repo = self->entry->repo;
    struct kndMemPool *mempool = repo->mempool;
    struct kndClass *c;
    struct kndClassEntry *entry;
    struct ooDict *class_name_idx = target_repo->class_name_idx;
    struct kndSet *class_idx = target_repo->class_idx;
    int err;

    if (DEBUG_CLASS_LEVEL_1)
        knd_log(".. cloning class %.*s (%.*s) to repo %.*s..",
                self->name_size, self->name,
                self->entry->repo->name_size, self->entry->repo->name,
                target_repo->name_size, target_repo->name);

    err = knd_class_new(mempool, &c);                                             RET_ERR();
    err = knd_class_entry_new(mempool, &entry);                                   RET_ERR();
    entry->repo = target_repo;
    entry->orig = self->entry;
    entry->class = c;
    c->entry = entry;
    
    target_repo->num_classes++;
    entry->numid = target_repo->num_classes;
    knd_num_to_str(entry->numid,
                   entry->id, &entry->id_size, KND_RADIX_BASE);

    err = knd_class_copy(self, c);
    if (err) {
        knd_log("-- class copy failed");
        return err;
    }

    /* idx register */
    err = class_name_idx->set(class_name_idx,
                              entry->name, entry->name_size,
                              (void*)entry);                                      RET_ERR();
    err = class_idx->add(class_idx,
                         entry->id, entry->id_size,
                         (void*)entry);                                           RET_ERR();

    if (DEBUG_CLASS_LEVEL_2)
        c->str(c);

    *result = c;
    return knd_OK;
}

extern int knd_class_copy(struct kndClass *self,
                          struct kndClass *c)
{
    struct kndRepo *repo =  c->entry->repo;
    struct kndMemPool *mempool = repo->mempool;
    struct ooDict *class_name_idx = repo->class_name_idx;
    struct kndClassEntry *entry, *src_entry;
    struct kndClassRef   *ref,   *src_ref;
    int err;

    entry = c->entry;
    src_entry = self->entry;

    entry->name = src_entry->name;
    entry->name_size = src_entry->name_size;
    c->name = entry->name;
    c->name_size = entry->name_size;

    entry->class = c;
    c->entry = entry;

    err = class_name_idx->set(class_name_idx,
                              entry->name, entry->name_size,
                              (void*)entry);                                      RET_ERR();

    /* copy the attrs */
    c->attr_idx->mempool = mempool;
    err = self->attr_idx->map(self->attr_idx,
                              knd_register_attr_ref,
                              (void*)c);                                          RET_ERR();

    /* copy the ancestors */
    for (src_ref = self->entry->ancestors; src_ref; src_ref = src_ref->next) {
        err = knd_class_ref_new(mempool, &ref);                                   RET_ERR();
        ref->class = src_ref->class;
        ref->entry = src_ref->entry;

        ref->next = entry->ancestors;
        entry->ancestors = ref;
        entry->num_ancestors++;
    }
   
    return knd_OK;
}

extern void kndClass_init(struct kndClass *self)
{
    self->str = str;
    self->load = read_GSL_file;
    self->reset_inbox = reset_inbox;

    self->coordinate = coordinate;
    self->resolve = knd_resolve_class;

    self->import = knd_import_class;
    self->select = knd_select_class;

    self->update_state = update_state;
    self->export = export;
    self->export_updates = export_updates;
}

/*extern int kndClass_new(struct kndClass **result,
                        struct kndMemPool *mempool)
{
    struct kndClass *self = NULL;
    struct kndClassEntry *entry;
    int err;

    self = malloc(sizeof(struct kndClass));
    if (!self) return knd_NOMEM;
    memset(self, 0, sizeof(struct kndClass));

    err = knd_class_entry_new(mempool, &entry);  RET_ERR();
    entry->class = self;
    self->entry = entry;

    kndClass_init(self);
    *result = self;

    return knd_OK;
}
*/

extern int knd_class_var_new(struct kndMemPool *mempool,
                             struct kndClassVar **result)
{
    void *page;
    int err;
    //knd_log("..class var new [size:%zu]", sizeof(struct kndClassVar));
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL, sizeof(struct kndClassVar), &page);
    if (err) return err;

    // TEST
    mempool->num_class_vars++;

    *result = page;
    return knd_OK;
}

extern int knd_class_ref_new(struct kndMemPool *mempool,
                             struct kndClassRef **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY, sizeof(struct kndClassRef), &page);
    if (err) return err;
    *result = page;
    return knd_OK;
}

extern int knd_class_entry_new(struct kndMemPool *mempool,
                               struct kndClassEntry **result)
{
    void *page;
    int err;

    //knd_log("..class entry new [size:%zu]", sizeof(struct kndClassEntry));
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                            sizeof(struct kndClassEntry), &page);
    if (err) return err;

    *result = page;
    return knd_OK;
}

extern void knd_class_free(struct kndMemPool *mempool,
                           struct kndClass *self)
{
    knd_mempool_free(mempool, KND_MEMPAGE_SMALL_X4, (void*)self);
}

extern int knd_class_update_new(struct kndMemPool *mempool,
                                struct kndClassUpdate **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                            sizeof(struct kndClassUpdate), &page);  RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_inner_class_new(struct kndMemPool *mempool,
                               struct kndClass **self)
{
    void *page;
    int err;

    //knd_log("..class new [size:%zu]", sizeof(struct kndClass));
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X4,
                            sizeof(struct kndClass), &page);                      RET_ERR();
    if (err) return err;
    *self = page;
    kndClass_init(*self);
    return knd_OK;
}

extern int knd_class_new(struct kndMemPool *mempool,
                         struct kndClass **self)
{
    struct kndSet *attr_idx;
    void *page;
    int err;

    //knd_log("..class new [size:%zu]", sizeof(struct kndClass));

    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X4,
                            sizeof(struct kndClass), &page);                      RET_ERR();
    if (err) return err;
    *self = page;

    err = knd_set_new(mempool, &attr_idx);                                        RET_ERR();
    (*self)->attr_idx = attr_idx;

    // TEST
    mempool->num_classes++;

    kndClass_init(*self);
    return knd_OK;
}
