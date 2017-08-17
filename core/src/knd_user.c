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

#define DEBUG_USER_LEVEL_0 0
#define DEBUG_USER_LEVEL_1 0
#define DEBUG_USER_LEVEL_2 0
#define DEBUG_USER_LEVEL_3 0
#define DEBUG_USER_LEVEL_TMP 1

static int 
kndUser_export(struct kndUser *self,
                knd_format format);

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
kndUser_get_user(struct kndUser *self, const char *uid,
                 struct kndUser **user)
{
    char buf[KND_TEMP_BUF_SIZE] = {0};
    size_t buf_size = KND_TEMP_BUF_SIZE;

    struct kndUser *curr_user;
    int err;

    if (DEBUG_USER_LEVEL_TMP)
        knd_log("  ?? is \"%s\" a valid user?\n", uid);

    curr_user = self->user_idx->get(self->user_idx, uid);
    if (!curr_user) {
        err = kndUser_new(&curr_user);
        if (err) return err;

        curr_user->out = self->out;
        
        memcpy(curr_user->id, uid, KND_ID_SIZE);

        err = knd_make_id_path(curr_user->path, buf, curr_user->id, NULL);
        if (err) goto final;

        /*curr_user->path_size = strlen(curr_user->path);
        memcpy(curr_user->repo->path, curr_user->path, curr_user->path_size);
        curr_user->repo->path[curr_user->path_size] = '\0';
        curr_user->repo->path_size = curr_user->path_size;

        */
        
        /* open home repo */
        buf_size = sprintf(buf, "%s/user.gsl", curr_user->path);

        err = self->out->read_file(self->out,
                                   (const char*)buf, buf_size);
        if (err) {
            knd_log("   -- couldn't read the user profile in %s\n", curr_user->path);
            return err;
        }

        /*err = kndUser_read(curr_user, (const char*)self->out->file);
        if (err) goto final;
        */
        
        /*err = curr_user->repo->open(curr_user->repo);
        if (err) goto final;
        */
        
        err = self->user_idx->set(self->user_idx,
                                  (const char*)uid, (void*)curr_user);
        if (err) return err;
    }
    
    curr_user->out->reset(curr_user->out);
    curr_user->update_service = self->update_service;
    
    err = knd_OK;
    *user = curr_user;

 final:    
    return err;
}


static int 
kndUser_restore(struct kndUser *self)
{
    char buf[KND_TEMP_BUF_SIZE] = {0};
    size_t buf_size = 0;

    char idbuf[KND_ID_SIZE + 1];
    struct kndRepo *repo;
    
    int err;

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. user \"%s\" restoring DB state  DBPATH: %s",
                self->id, self->dbpath);
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
kndUser_parse_auth(void *obj,
                   const char *rec,
                   size_t *total_size)
{
    struct kndUser *self = (struct kndUser*)obj;
    char sid[KND_NAME_SIZE];
    size_t sid_size;

    struct kndTaskSpec specs[] = {
        { .name = "sid",
          .name_size = strlen("sid"),
          .is_terminal = true,
          .buf = sid,
          .buf_size = &sid_size,
          .max_buf_size = KND_NAME_SIZE
        }
    };
    int err, e;

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

    if (DEBUG_USER_LEVEL_TMP)
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

    if (DEBUG_USER_LEVEL_TMP)
        knd_log(".. parsing liquid updates..");

    if (!self->root_class->inbox_size) 
        return knd_OK;

    err = self->root_class->apply_liquid_updates(self->root_class);
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
        }
    };
    int err, e;
    
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) {
        knd_log("-- user task parse failure: \"%.*s\" :(", self->log->buf_size, self->log->buf);
        if (!self->log->buf_size) {
            e = self->log->write(self->log, "internal server error",
                                   strlen("internal server error"));
            if (e) return e;
        }
        return err;
    }

    if (self->task->type == KND_UPDATE_STATE) {
        knd_log("++ all updates applied!");
        return knd_OK;
    }
    
    /* any transaction to close? */
    if (self->root_class->inbox_size) {
        out = self->task->update;
        err = out->write(out, "{task{update", strlen("{task{update"));
        if (err) return err;
        /* update spec body */
        err = out->write(out, rec, *total_size);
        if (err) return err;

        err = self->root_class->update_state(self->root_class);
        if (err) return err;
    }
    return knd_OK;
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
    self->get_user = kndUser_get_user;

    //self->read = kndUser_read;
    self->restore = kndUser_restore;

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
