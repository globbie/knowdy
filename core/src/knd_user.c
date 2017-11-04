#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "knd_policy.h"
#include "knd_user.h"
#include "knd_repo.h"
#include "knd_output.h"
#include "knd_msg.h"
#include "knd_task.h"
#include "knd_parser.h"
#include "knd_concept.h"
#include "knd_object.h"
#include "knd_proc.h"
#include "knd_rel.h"

#define DEBUG_USER_LEVEL_0 0
#define DEBUG_USER_LEVEL_1 0
#define DEBUG_USER_LEVEL_2 0
#define DEBUG_USER_LEVEL_3 0
#define DEBUG_USER_LEVEL_TMP 1

static int export(struct kndUser *self);
static int run_get_user_by_id(void *obj, struct kndTaskArg *args, size_t num_args);

static void del(struct kndUser *self)
{
    free(self);
}

static void str(struct kndUser *self)
{
    struct kndConcept *dc;
    const char *key = NULL;
    void *val = NULL;

    knd_log("USER: %s [%s]\n", self->name, self->id);

    self->class_idx->rewind(self->class_idx);
    do {
        self->class_idx->next_item(self->class_idx, &key, &val);
        if (!key) break;

        dc = (struct kndConcept*)val;
        
        knd_log("CLASS: %s\n", dc->name);

    } while (key);
}

static int
kndUser_add_user(struct kndUser *self)
{
    char buf[KND_TEMP_BUF_SIZE] = {0};
    //size_t buf_size;
    
    char uid[KND_ID_SIZE + 1];
    int err;

    /* check a human readable name */
    
    /*memcpy(uid, self->last_uid, KND_ID_SIZE);
    uid[KND_ID_SIZE] = '\0';
    knd_inc_id(uid);
    */
    
    if (DEBUG_USER_LEVEL_TMP)
        knd_log(".. create new user: ID \"%s\"", uid);

    memcpy(self->id, uid, KND_ID_SIZE);

    self->name_size = KND_NAME_SIZE;
    
    err = knd_make_id_path(buf, self->path, uid, NULL);
    if (err) return err;

    if (DEBUG_USER_LEVEL_TMP)
        knd_log("==  USER DIR: \"%s\"\n", buf);

    /* TODO: check if DIR already exists */

    err = knd_mkpath(buf, 0755, false);
    if (err) return err;

    err = export(self);
    if (err) return err;
    
    err = knd_write_file((const char*)buf, "user.gsl",
                         self->out->buf, self->out->buf_size);
    if (err) return err;

    /* change last id */
    memcpy(self->last_uid, uid, KND_ID_SIZE);

    return knd_OK;
}


/*
static int 
kndUser_export_GSL(struct kndUser *self)
{
    char buf[KND_TEMP_BUF_SIZE] = {0};
    size_t buf_size = 0;
    struct kndOutput *out;
    int err;

    out = self->out;
    
    buf_size = sprintf(buf, "{ID %s}{N %s}",
                       self->id,
                       self->name);
    out->reset(out);

    err = out->write(out, buf, buf_size);

    return err;
}
*/

static int export_JSON(struct kndUser *self)
{
    //char buf[KND_MED_BUF_SIZE] = {0};
    //size_t buf_size = 0;

    struct kndConcept *c;
    struct kndOutput *out;
    
    const char *key = NULL;
    void *val = NULL;
    int i, err;
    
    if (DEBUG_USER_LEVEL_TMP)
        knd_log("JSON USER: %s [%s]\n",
                self->name, self->id);

    out = self->out;
    out->reset(out);

    err = out->write(out,
                     "{", 1);
    if (err) return err;

    err = out->write(out,
                     "\"n\":\"", strlen("\"n\":\""));
    if (err) return err;

    err = out->write(out,
                     self->name, self->name_size);
    if (err) return err;

    err = out->write(out,
                     "\"", 1);
    if (err) return err;

    err = out->write(out,
                     ",\"c_l\":[", strlen(",\"c_l\":["));
    if (err) return err;

    i = 0;
    self->browse_class_idx->rewind(self->browse_class_idx);
    do {
        self->class_idx->next_item(self->browse_class_idx, &key, &val);
        if (!key) break;

        /* separator */
        if (i) {
            err = out->write(out, ",", 1);
            if (err) return err;
        }

        c = (struct kndConcept*)val;
        err = c->export(c);
        if (err) return err;
        i++;
    } while (key);

    err = out->write(out, "]", 1);
    if (err) return err;

    /* home repo export */
    err = out->write(out,
                     ",\"home\":", strlen(",\"home\":"));
    if (err) return err;

    self->repo->out = out;
    err = self->repo->export(self->repo, KND_FORMAT_JSON);
    if (err) return err;

    err = out->write(out, "}", 1);
    if (err) return err;

    return err;
}


static int kndUser_parse_repo(void *obj,
                              const char *rec,
                              size_t *total_size)
{
    struct kndUser *self = (struct kndUser*)obj;
    int err;

    if (DEBUG_USER_LEVEL_TMP)
        knd_log("   .. parsing the REPO rec: \"%s\"", rec);

    self->repo->task = self->task;
    self->repo->out = self->out;
    self->repo->log = self->log;
    
    err = self->repo->parse_task(self->repo, rec, total_size);
    if (err) return err;

    return knd_OK;
}

static int
kndUser_parse_numid(void *obj,
                    const char *rec,
                    size_t *total_size)
{
    struct kndUser *self = (struct kndUser*)obj;
    int err, e;

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_get_user_by_id,
          .obj = self
        }
    };

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) {
        if (!self->log->buf_size) {
            e = self->log->write(self->log, "user identification failure",
                                   strlen("user identification failure"));
            if (e) return e;
        }
        return err;
    }
    
    return knd_OK;
}

static int
kndUser_parse_auth(void *obj,
                   const char *rec,
                   size_t *total_size)
{
    struct kndUser *self = obj;
    char sid[KND_NAME_SIZE];
    size_t sid_size;
    int err, e;

    struct kndTaskSpec specs[] = {
        { .name = "sid",
          .name_size = strlen("sid"),
          .buf = sid,
          .buf_size = &sid_size,
          .max_buf_size = KND_NAME_SIZE
        }
    };

    if (DEBUG_USER_LEVEL_2)
        knd_log("   .. parsing the AUTH rec: \"%.*s\"", 32, rec);

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) {
        if (!self->log->buf_size) {
            e = self->log->write(self->log, "user authentication failure",
                                   strlen("user authentication failure"));
            if (e) return e;
        }
        return err;
    }

    if (DEBUG_USER_LEVEL_2)
        knd_log("++ got SID: \"%s\"", sid);

    if (!sid_size) {
        knd_log("-- no SID provided :(");
        return knd_FAIL;
    }

    /* TODO: DB check auth token */

    if (strncmp(self->sid, sid, sid_size)) {
        knd_log("-- wrong SID: \"%.*s\"", sid_size, sid);
        err = self->log->write(self->log, "SID authentication failure",
                               strlen("SID authentication failure"));
        if (err) return err;
        
        return knd_FAIL;
    }
    
    return knd_OK;
}

static int parse_proc_import(void *obj,
                             const char *rec,
                             size_t *total_size)
{
    struct kndUser *self = obj;
    int err;

    self->task->type = KND_CHANGE_STATE;
    /*self->root_class->out = self->out;
    self->root_class->log = self->log;
    self->root_class->task = self->task;

    self->root_class->dbpath = self->dbpath;
    self->root_class->dbpath_size = self->dbpath_size;
    self->root_class->frozen_output_file_name = self->frozen_output_file_name;
    self->root_class->frozen_output_file_name_size = self->frozen_output_file_name_size;

    self->root_class->locale = self->locale;
    self->root_class->locale_size = self->locale_size;
    */

    err = self->root_class->proc->parse(self->root_class->proc, rec, total_size);
    if (err) return err;

    return knd_OK;
}

static int parse_rel_import(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndUser *self = obj;
    int err;

    self->task->type = KND_CHANGE_STATE;

    err = self->root_class->rel->import(self->root_class->rel, rec, total_size);
    if (err) return err;

    return knd_OK;
}

static int parse_class_import(void *obj,
                                      const char *rec,
                                      size_t *total_size)
{
    struct kndUser *self = (struct kndUser*)obj;
    int err;

    if (DEBUG_USER_LEVEL_1)
        knd_log(".. parsing the default class import: \"%s\"", rec);

    self->task->type = KND_CHANGE_STATE;

    self->root_class->out = self->out;
    self->root_class->log = self->log;
    self->root_class->task = self->task;

    self->root_class->dbpath = self->dbpath;
    self->root_class->dbpath_size = self->dbpath_size;
    
    err = self->root_class->import(self->root_class, rec, total_size);
    if (err) return err;

    return knd_OK;
}

static int kndUser_parse_sync_task(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    struct kndUser *self = (struct kndUser*)obj;
    struct stat st;
    char *s, *n;
    size_t path_size;
    int err;

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. got sync task..");

    s = self->path;
    memcpy(s, self->dbpath, self->dbpath_size);
    s += self->dbpath_size;
    self->path_size += self->dbpath_size;

    path_size =  strlen("/frozen_merge.gsp");
    memcpy(s, "/frozen_merge.gsp", path_size);
    self->path_size += path_size;
    self->path[self->path_size] = '\0';

    /* file exists, remove it */
    if (!stat(self->path, &st)) {
        err = remove(self->path);
        if (err) return err;
        knd_log("-- existing frozen DB file removed..");
    }

    /* name IDX */
    n = buf;
    buf_size = 0;
    memcpy(n, self->dbpath, self->dbpath_size);
    n += self->dbpath_size;
    buf_size += self->dbpath_size;
    path_size =  strlen("/frozen_name.gsi");
    memcpy(n, "/frozen_name.gsi", path_size);
    buf_size += path_size;
    buf[buf_size] = '\0';
    
    self->task->type = KND_SYNC_STATE;
    self->root_class->out = self->out;
    self->root_class->dir_out = self->task->update;
    self->root_class->log = self->log;
    self->root_class->task = self->task;
    self->root_class->frozen_output_file_name = (const char*)self->path;
    self->root_class->frozen_name_idx_path = buf;
    self->root_class->frozen_name_idx_path_size = buf_size;


    err = self->root_class->sync(self->root_class, rec, total_size);
    if (err) return err;

    /* bump frozen count */

    /* temp: simply rename the GSP file */
    self->out->reset(self->out);
    err = self->out->write(self->out, self->dbpath, self->dbpath_size);
    if (err) return err;
    err = self->out->write(self->out, "/frozen.gsp", strlen("/frozen.gsp"));
    if (err) return err;
    err = rename(self->path, self->out->buf);
    if (err) return err;

    /* inform retrievers */

    /* release resources */
    self->root_class->reset(self->root_class);

    if (!stat(self->out->buf, &st)) {
        if (DEBUG_USER_LEVEL_TMP)
            knd_log("++ frozen DB file sync'ed OK, total bytes: %lu",
                    (unsigned long)st.st_size);
    }
    return knd_OK;
}


static int kndUser_parse_class_select(void *obj,
                                      const char *rec,
                                      size_t *total_size)
{
    struct kndUser *self = obj;
    struct kndOutput *out = self->out;
    int err;

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. parsing the default class select: \"%s\"", rec);
    self->root_class->out = self->out;
    self->root_class->log = self->log;
    self->root_class->task = self->task;

    self->root_class->dbpath = self->dbpath;
    self->root_class->dbpath_size = self->dbpath_size;
    self->root_class->frozen_output_file_name = self->frozen_output_file_name;
    self->root_class->frozen_output_file_name_size = self->frozen_output_file_name_size;
    
    err = self->root_class->select(self->root_class, rec, total_size);
    if (err) return err;

    return knd_OK;
}

static int parse_rel_select(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndUser *self = obj;
    struct kndOutput *out = self->out;
    struct kndRel *rel = self->root_class->rel;
    int err;

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. parsing the default Rel select: \"%s\"", rec);
    rel->out = self->out;
    rel->log = self->log;
    rel->task = self->task;

    rel->dbpath = self->dbpath;
    rel->dbpath_size = self->dbpath_size;
    rel->frozen_output_file_name = self->frozen_output_file_name;
    rel->frozen_output_file_name_size = self->frozen_output_file_name_size;
    
    err = rel->select(rel, rec, total_size);
    if (err) return err;

    return knd_OK;
}

static int parse_liquid_updates(void *obj,
                                const char *rec,
                                size_t *total_size)
{
    struct kndUser *self = (struct kndUser*)obj;
    int err;

    if (DEBUG_USER_LEVEL_TMP)
        knd_log(".. parse and apply liquid updates..");

    self->task->type = KND_UPDATE_STATE;
    self->root_class->task = self->task;

    err = self->root_class->apply_liquid_updates(self->root_class,
                                                 rec, total_size);
    if (err) return err;

    if (DEBUG_USER_LEVEL_2)
        knd_log("++ liquid updates OK:  chars parsed: %lu", (unsigned long)*total_size);

    return knd_OK;
}



static int run_get_user(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndUser *self = (struct kndUser*)obj;
    struct kndTaskArg *arg;
    struct kndConcept *conc;
    const char *name = NULL;
    size_t name_size = 0;
    int err;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    if (!name_size) return knd_FAIL;
    if (name_size >= KND_NAME_SIZE) return knd_LIMIT;

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. get user: \"%.*s\".. %p", name_size, name, self->root_class);

    err = self->root_class->get(self->root_class, "User", strlen("User"), &conc);
    if (err) return err;

    err = conc->get_obj(conc, name, name_size, &self->curr_user);
    if (err) return err;

    self->curr_user->out = self->out;
    self->curr_user->log = self->log;

    err = self->curr_user->export(self->curr_user);
    if (err) return err;
    
    return knd_OK;
}

static int run_get_user_by_id(void *data, struct kndTaskArg *args, size_t num_args)
{
    struct kndUser *self = data;
    struct kndTaskArg *arg;
    struct kndConcept *conc;
    struct kndObjEntry *entry;
    struct kndObject *obj;
    const char *numid = NULL;
    size_t numid_size = 0;
    long numval = 0;
    int err;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "_impl", strlen("_impl"))) {
            numid = arg->val;
            numid_size = arg->val_size;
        }
    }
    if (!numid_size) return knd_FAIL;
    if (numid_size >= KND_SHORT_NAME_SIZE) return knd_LIMIT;

    err = knd_parse_num((const char*)numid, &numval);
    if (err) return err;

    if (numval < 0 || (size_t)numval >= self->max_users) {
        return knd_LIMIT;
    }

    entry = self->user_idx[numval];
    if (!entry) {
        knd_log("-- no such user id: %lu", numval);
        return knd_NO_MATCH;
    }

    self->root_class->out = self->out;
    self->root_class->log = self->log;
    self->root_class->task = self->task;
    self->root_class->dbpath = self->dbpath;
    self->root_class->dbpath_size = self->dbpath_size;
    self->root_class->frozen_output_file_name = self->frozen_output_file_name;
    self->root_class->frozen_output_file_name_size = self->frozen_output_file_name_size;
    
    err = self->root_class->get(self->root_class, "User", strlen("User"), &conc);
    if (err) return err;

    err = conc->read_obj_entry(conc, entry, &obj);
    if (err) return err;

    self->curr_user = obj;

    if (DEBUG_USER_LEVEL_2) {
        knd_log("++ got user by num id: %.*s", numid_size, numid);
        self->curr_user->str(self->curr_user);
    }

    /* TODO */
    self->curr_user->out = self->out;
    self->curr_user->log = self->log;

    /*err = self->curr_user->export(self->curr_user);
    if (err) return err;
    */

    return knd_OK;
}

static int run_present_user(void *data,
                            struct kndTaskArg *args __attribute__((unused)),
                            size_t num_args __attribute__((unused)))
{
    struct kndUser *self = (struct kndUser*)data;
    int err;

    knd_log("..present user..");
    if (!self->curr_user) {
        knd_log("-- no user selected :(");
        return knd_FAIL;
    }
    self->curr_user->out = self->out;
    self->curr_user->log = self->log;

    err = self->curr_user->export(self->curr_user);
    if (err) return err;

    return knd_OK;
}

static int parse_task(struct kndUser *self,
                      const char *rec,
                      size_t *total_size)
{
    struct kndOutput *out;
    struct kndConcept *conc;
    struct kndObject *obj, *next_obj;
    struct ooDict *idx;

    if (DEBUG_USER_LEVEL_1)
        knd_log(".. parsing user task: \"%s\" size: %lu..\n\n",
                rec, (unsigned long)strlen(rec));

    struct kndTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_get_user,
          .obj = self
        },
        { .name = "id",
          .name_size = strlen("id"),
          .is_selector = true,
          .parse = kndUser_parse_numid,
          .obj = self
        },
        { .name = "auth",
          .name_size = strlen("auth"),
          .parse = kndUser_parse_auth,
          .obj = self
        },
        { .name = "repo",
          .name_size = strlen("repo"),
          .parse = kndUser_parse_repo,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_import,
          .obj = self
        },
        { .name = "class",
          .name_size = strlen("class"),
          .parse = kndUser_parse_class_select,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_import,
          .obj = self
        },
        { .type = KND_CHANGE_STATE,
          .name = "rel",
          .name_size = strlen("rel"),
          .parse = parse_rel_import,
          .obj = self
        },
        { .name = "rel",
          .name_size = strlen("rel"),
          .parse = parse_rel_select,
          .obj = self
        },
        { .name = "state",
          .name_size = strlen("state"),
          .parse = parse_liquid_updates,
          .obj = self
        },
        { .name = "sync",
          .name_size = strlen("sync"),
          .parse = kndUser_parse_sync_task,
          .obj = self
        },
        { .name = "default",
          .name_size = strlen("default"),
          .is_default = true,
          .run = run_present_user,
          .obj = self
        }
    };
    int err, e;

    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) {
        knd_log("-- user task parse failure: \"%.*s\" :(", self->log->buf_size, self->log->buf);
        if (!self->log->buf_size) {
            e = self->log->write(self->log, "internal server error",
                                 strlen("internal server error"));
            if (e) {
                err = e;
                goto cleanup;
            }
        }
        goto cleanup;
    }

    if (DEBUG_USER_LEVEL_2)
        knd_log("user parse task OK: total chars: %lu root class: %p",
                (unsigned long)*total_size, self->root_class);

    switch (self->task->type) {
    case KND_UPDATE_STATE:
        if (DEBUG_USER_LEVEL_2)
            knd_log("++ all updates applied!");
        return knd_OK;
    case KND_GET_STATE:
        if (DEBUG_USER_LEVEL_1)
            knd_log("++ get task complete!");
        return knd_OK;
    default:
        /* any transaction to close? */
        if (self->root_class->inbox_size || self->root_class->obj_inbox_size) {
            out = self->task->update;

            if (DEBUG_USER_LEVEL_2)
                knd_log(".. update state.. total output free space: %lu TOTAL SPEC SIZE: %lu",
                        (unsigned long)out->free_space,
                        (unsigned long)*total_size);

            err = out->write(out, "{task{update", strlen("{task{update"));
            if (err) goto cleanup;

            /* update spec body */
            err = out->write(out, rec, *total_size);
            if (err) {
                knd_log("-- output failed :(");
                goto cleanup;
            }

            err = self->root_class->update_state(self->root_class);
            if (err) {
                knd_log("-- failed to update state :(");
                goto cleanup;
            }
            err = out->write(out, "}}", strlen("}}"));
            if (err) goto cleanup;
        }
    }

    return knd_OK;

 cleanup:

    /* TODO : deallocate resources */
    if (self->root_class->obj_inbox_size) {

        if (DEBUG_USER_LEVEL_1)
            knd_log("\n.. obj inbox cleanup..");
        obj = self->root_class->obj_inbox;
        while (obj) {
            if (obj->conc && obj->conc->dir) {
                idx = obj->conc->dir->obj_idx;
                e = idx->remove(idx, obj->name, obj->name_size);

                if (DEBUG_USER_LEVEL_2)
                    knd_log("!! removed \"%.*s\" from obj idx: %d",
                            obj->name_size, obj->name, e);
            }
            next_obj = obj->next;
            obj->del(obj);
            obj = next_obj;
        }

        self->root_class->obj_inbox = NULL;
        self->root_class->obj_inbox_size = 0;
    }
    
    if (self->root_class->inbox_size) {
        if (DEBUG_USER_LEVEL_2)
            knd_log(".. class inbox cleanup..\n\n");
        self->root_class->inbox = NULL;
        self->root_class->inbox_size = 0;
    }
    
    return err;
}

static int export(struct kndUser *self)
{
    switch(self->format) {
    case KND_FORMAT_JSON:
        return export_JSON(self);
        /*case KND_FORMAT_GSL:
          return kndUser_export_GSL(self); */
    default:
        break;
    }

    return knd_FAIL;
}

extern int 
kndUser_init(struct kndUser *self)
{
    self->del = del;
    self->str = str;

    self->parse_task = parse_task;
    self->add_user = kndUser_add_user;

    return knd_OK;
}

extern int 
kndUser_new(struct kndUser **user)
{
    struct kndUser *self;
    int err = knd_OK;
    
    self = malloc(sizeof(struct kndUser));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndUser));

    memset(self->id, '0', KND_ID_SIZE);
    memset(self->last_uid, '0', KND_ID_SIZE);
    memset(self->db_state, '0', KND_ID_SIZE);

    err = kndRepo_new(&self->repo);
    if (err) return knd_NOMEM;
    self->repo->user = self;
    self->repo->name[0] = '~';
    self->repo->name_size = 1;
    
    err = ooDict_new(&self->class_idx, KND_SMALL_DICT_SIZE);
    if (err) return knd_NOMEM;

    err = ooDict_new(&self->browse_class_idx, KND_SMALL_DICT_SIZE);
    if (err) return knd_NOMEM;
    
    kndUser_init(self);

    *user = self;

    return knd_OK;
}
