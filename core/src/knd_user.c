#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_policy.h"
#include "knd_user.h"
#include "knd_repo.h"
#include "knd_output.h"
#include "knd_msg.h"
#include "knd_task.h"
#include "knd_parser.h"
#include "knd_concept.h"
#include "knd_object.h"

#define DEBUG_USER_LEVEL_0 0
#define DEBUG_USER_LEVEL_1 0
#define DEBUG_USER_LEVEL_2 0
#define DEBUG_USER_LEVEL_3 0
#define DEBUG_USER_LEVEL_TMP 1

static int 
kndUser_export(struct kndUser *self,
                knd_format format);
static int run_get_user_by_id(void *obj, struct kndTaskArg *args, size_t num_args);

static int
kndUser_read(struct kndUser *self, const char *rec);

static int 
kndUser_del(struct kndUser *self)
{

    free(self);

    return knd_OK;
}

static int 
kndUser_str(struct kndUser *self)
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
        

    return knd_OK;
}


static int
kndUser_add_user(struct kndUser *self)
{
    char buf[KND_TEMP_BUF_SIZE] = {0};
    //size_t buf_size;
    
    char uid[KND_ID_SIZE + 1];
    int err;

    /* check a human readable name */
    
    memcpy(uid, self->last_uid, KND_ID_SIZE);
    uid[KND_ID_SIZE] = '\0';
    knd_inc_id(uid);

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

    err = kndUser_export(self, KND_FORMAT_GSL);
    if (err) return err;
    
    err = knd_write_file((const char*)buf, "user.gsl",
                         self->out->buf, self->out->buf_size);
    if (err) return err;

    /* change last id */
    memcpy(self->last_uid, uid, KND_ID_SIZE);

    return knd_OK;
}



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


static int
kndUser_export_JSON(struct kndUser *self)
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
    struct kndUser *self = (struct kndUser*)obj;
    char sid[KND_NAME_SIZE];
    size_t sid_size;
    int err, e;

    struct kndTaskSpec specs[] = {
        { .name = "sid",
          .name_size = strlen("sid"),
          .is_terminal = true,
          .buf = sid,
          .buf_size = &sid_size,
          .max_buf_size = KND_NAME_SIZE
        }
    };

    if (DEBUG_USER_LEVEL_2)
        knd_log("   .. parsing the AUTH rec: \"%s\"", rec);

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





static int kndUser_parse_class_import(void *obj,
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

    self->root_class->locale = self->locale;
    self->root_class->locale_size = self->locale_size;
    
    err = self->root_class->import(self->root_class, rec, total_size);
    if (err) return err;

    return knd_OK;
}


static int kndUser_parse_class_select(void *obj,
                                      const char *rec,
                                      size_t *total_size)
{
    struct kndUser *self = (struct kndUser*)obj;
    int err;

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. parsing the default class select: \"%s\"", rec);

    self->root_class->out = self->out;
    self->root_class->log = self->log;
    self->root_class->task = self->task;

    self->root_class->dbpath = self->dbpath;
    self->root_class->dbpath_size = self->dbpath_size;

    self->root_class->locale = self->locale;
    self->root_class->locale_size = self->locale_size;
    
    err = self->root_class->select(self->root_class, rec, total_size);
    if (err) return err;

    return knd_OK;
}


static int kndUser_parse_liquid_updates(void *obj,
                                        const char *rec,
                                        size_t *total_size)
{
    struct kndUser *self = (struct kndUser*)obj;
    struct kndConcept *c;
    int err;

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. parse and apply liquid updates..");

    self->task->type = KND_UPDATE_STATE;
    self->root_class->task = self->task;

    err = self->root_class->apply_liquid_updates(self->root_class);
    if (err) return err;

    /* TODO: state parsing */
    *total_size = 0;
    
    if (DEBUG_USER_LEVEL_2)
        knd_log("++ liquid updates OK!");

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


    if (DEBUG_USER_LEVEL_TMP)
        knd_log(".. get user: \"%.*s\".. %p", name_size, name, self->root_class);

    err = self->root_class->get(self->root_class, "User", strlen("User"));
    if (err) return err;
    conc = self->root_class->curr_class;
    
    err = conc->get_obj(conc, name, name_size);
    if (err) return err;
    
    self->curr_user = conc->curr_obj;

    if (DEBUG_USER_LEVEL_TMP)
        knd_log("++ got user: \"%.*s\"!", name_size, name);

    self->curr_user->out = self->out;
    self->curr_user->log = self->log;

    err = self->curr_user->export(self->curr_user);
    if (err) return err;
    
    return knd_OK;
}

static int run_get_user_by_id(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndUser *self = (struct kndUser*)obj;
    struct kndTaskArg *arg;
    struct kndConcept *conc;
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

    if (numval < 0 || numval >= self->max_users) {
        return knd_LIMIT;
    }

    self->curr_user = self->user_idx[numval];

    if (!self->curr_user) return knd_NO_MATCH;
    
    /*if (DEBUG_USER_LEVEL_TMP)
        knd_log(".. get user by num id: \"%.*s\"..", numid_size, numid);

    err = self->root_class->get(self->root_class, "User", strlen("User"));
    if (err) return err;
    conc = self->root_class->curr_class;
    
    err = conc->get_obj(conc, numid, numid_size);
    if (err) return err;
    
    self->curr_user = conc->curr_obj;
    */

    
    if (DEBUG_USER_LEVEL_TMP) {
        knd_log("++ got user by num id: \"%.*s\"!", numid_size, numid);
        self->curr_user->str(self->curr_user, 1);
    }


    /* TODO */
    self->curr_user->out = self->out;
    self->curr_user->log = self->log;

    err = self->curr_user->export(self->curr_user);
    if (err) return err;

    return knd_OK;
}

static int run_present_user(void *data,
                            struct kndTaskArg *args, size_t num_args)
{
    struct kndUser *self = (struct User*)data;
    struct kndTaskArg *arg;
    struct kndObject *obj;
    int err;

    knd_log(".. present user..");

    if (!self->curr_user) return knd_FAIL;

    self->curr_user->out = self->out;
    self->curr_user->log = self->log;

    err = self->curr_user->export(self->curr_user);
    if (err) return err;

    return knd_OK;
}

static int
kndUser_parse_task(struct kndUser *self,
                   const char *rec,
                   size_t *total_size)
{
    struct kndOutput *out;

    if (DEBUG_USER_LEVEL_1)
        knd_log(".. parsing user task: \"%s\"..", rec);

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
          .parse = kndUser_parse_class_import,
          .obj = self
        },
        { .name = "class",
          .name_size = strlen("class"),
          .parse = kndUser_parse_class_select,
          .obj = self
        },
        { .name = "state",
          .name_size = strlen("state"),
          .parse = kndUser_parse_liquid_updates,
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
        knd_log("user parse task OK!");

    switch (self->task->type) {
    case KND_UPDATE_STATE:
        if (DEBUG_USER_LEVEL_TMP)
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
                knd_log(".. update state.. OUT SIZE: %lu TOTAL SPEC SIZE: %lu",
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
        }
    }

    return knd_OK;

 cleanup:

    /* TODO */
    if (self->root_class->obj_inbox_size) {
        if (DEBUG_USER_LEVEL_TMP)
            knd_log(".. obj inbox cleanup..\n\n");
        self->root_class->obj_inbox = NULL;
        self->root_class->obj_inbox_size = 0;
    }
    
    if (self->root_class->inbox_size) {
        if (DEBUG_USER_LEVEL_TMP)
            knd_log(".. class inbox cleanup..\n\n");
        self->root_class->inbox = NULL;
        self->root_class->inbox_size = 0;
    }
    
    return err;
}

static int 
kndUser_export(struct kndUser *self, knd_format format)
{
    switch(format) {
        case KND_FORMAT_JSON:
        return kndUser_export_JSON(self);
    case KND_FORMAT_GSL:
        return kndUser_export_GSL(self);
    default:
        break;
    }

    return knd_FAIL;
}

extern int 
kndUser_init(struct kndUser *self)
{
    self->del = kndUser_del;
    self->str = kndUser_str;

    self->parse_task = kndUser_parse_task;

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
