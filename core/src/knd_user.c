#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_policy.h"
#include "knd_user.h"
#include "knd_repo.h"
#include "knd_output.h"
#include "knd_msg.h"
#include "knd_task.h"

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
kndUser_get_repo(struct kndUser *self,
                 const char *name, size_t name_size,
                 struct kndRepo **result);
static int 
kndUser_del(struct kndUser *self)
{

    free(self);

    return knd_OK;
}

static int 
kndUser_str(struct kndUser *self)
{
    struct kndDataClass *dc;
    const char *key = NULL;
    void *val = NULL;

    knd_log("USER: %s [%s]\n", self->name, self->id);

    self->class_idx->rewind(self->class_idx);
    do {
        self->class_idx->next_item(self->class_idx, &key, &val);
        if (!key) break;

        dc = (struct kndDataClass*)val;
        
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
    
    /*err = knd_get_attr(data->spec, "name",
                       self->name, &self->name_size);
    if (err) goto final;
    */
    
    err = knd_make_id_path(buf, self->path, uid, NULL);
    if (err) return err;

    if (DEBUG_USER_LEVEL_TMP)
        knd_log("==  USER DIR: \"%s\"\n", buf);

    /* TODO: check if DIR already exists */

    err = knd_mkpath(buf, 0755, false);
    if (err) return err;

    err = kndUser_export(self, KND_FORMAT_GSL);
    if (err) return err;
    
    err = knd_write_file((const char*)buf,
                         "user.gsl",
                         self->out->buf, self->out->buf_size);
    if (err) return err;

    /* change last id */
    memcpy(self->last_uid, uid, KND_ID_SIZE);

    return knd_OK;
}


static int
kndUser_sync(struct kndUser *self)
{
    int err = knd_FAIL;

    /* TODO: check all repos */
    
    err = self->repo->sync(self->repo);
    if (err) return err;
    
    return err;
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
        
        err = kndUser_read(curr_user, (const char*)self->out->file);
        if (err) goto final;

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
kndUser_read_classes(struct kndUser *self, char *rec, size_t rec_size __attribute__((unused)))
{
    struct kndDataClass *dc;
    char *c;
    const char *delim = ";";
    char *last;
    int err;

    for (c = strtok_r(rec, delim, &last);
         c;
         c = strtok_r(NULL, delim, &last)) {

        /* check classname */
        dc = self->root_dc->class_idx->get(self->root_dc->class_idx,
                                           (const char*)c);
        if (!dc) {
            if (DEBUG_USER_LEVEL_TMP)
                knd_log("  .. classname \"%s\" is not valid...\n", c);
            return knd_FAIL;
        }

        /*dc->str(dc, 1);*/
        
        err = self->class_idx->set(self->class_idx,
                                    (const char*)c, (void*)dc);
        if (err) return err;
    }

    return knd_OK;
}


static int
kndUser_read_browse_classes(struct kndUser *self, char *rec, size_t rec_size __attribute__((unused)))
{
    struct kndDataClass *dc;
    char *c;
    const char *delim = ";";
    char *last;
    int err;

    for (c = strtok_r(rec, delim, &last);
         c;
         c = strtok_r(NULL, delim, &last)) {

        /* check classname */
        dc = self->root_dc->class_idx->get(self->root_dc->class_idx,
                                           (const char*)c);
        if (!dc) {
            if (DEBUG_USER_LEVEL_TMP)
                knd_log("   -- classname \"%s\" is not valid...\n", c);
            return knd_FAIL;
        }
        
        err = self->browse_class_idx->set(self->browse_class_idx,
                                          (const char*)c, (void*)dc);
        if (err) return err;
    }

    return knd_OK;
}


static int
kndUser_read_repos(struct kndUser *self, char *rec, size_t rec_size __attribute__((unused)))
{
    char buf[KND_NAME_SIZE] = {0};
    size_t buf_size = 0;

    struct kndRepoAccess *acc;
    char *c;
    char *b;
    const char *delim = ";";
    char *last;
    int err;

    for (c = strtok_r(rec, delim, &last);
         c;
         c = strtok_r(NULL, delim, &last)) {

        b = strchr(c, '/');
        if (b) {
            *b = '\0';
            b++;

            buf_size = strlen(b);
            if (!buf_size) return knd_FAIL;

            if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;

            memcpy(buf, b, buf_size);
            buf[buf_size] = '\0';
        }

        /* check repo's name */
        err = knd_is_valid_id(c, strlen(c));
        if (err) return err;

        acc = malloc(sizeof(struct kndRepoAccess));
        if (!acc) return knd_NOMEM;

        memset(acc, 0, sizeof(struct kndRepoAccess));
        memcpy(acc->repo_id, c, KND_ID_SIZE);

        acc->may_select = true;
        acc->may_get = true;

        if (!strcmp(buf, "U")) {
            acc->may_import = true;
            acc->may_update = true;
            /*knd_log("  ++ import & update granted: %s\n", acc->repo_id);*/
        }

        err = self->repo_idx->set(self->repo_idx,
                                    (const char*)c, (void*)acc);
        if (err) {
            free(acc);
            return err;
        }
    }

    err = knd_OK;

    return err;
}


static int
kndUser_get_repo(struct kndUser *self,
                 const char *name,
                 size_t name_size,
                 struct kndRepo **result)
{
    struct kndRepo *repo = NULL;
    const char *repo_id;
    int err;

    if (DEBUG_USER_LEVEL_TMP)
        knd_log("..  get repo: \"%s\"..", name);

    repo = self->repo_idx->get(self->repo_idx, name);
    if (repo) {
        *result = repo;
        return knd_OK;
    }

    repo_id = self->repo_name_idx->get(self->repo_name_idx, name);
    if (!repo_id) return knd_FAIL;

    err = kndRepo_new(&repo);
    if (err) return knd_NOMEM;

    memcpy(repo->id, repo_id, KND_ID_SIZE);
    repo->user = self;
    repo->out = self->out;

    memcpy(repo->name, name, name_size);
    repo->name_size = name_size;
    repo->name[name_size] = '\0';
    
    err = repo->open(repo);
    if (err) return err;
    
    err = self->repo_idx->set(self->repo_idx, name, (void*)repo);
    if (err) return err;

    *result = repo;
    
    return knd_OK;
}


static int
kndUser_read(struct kndUser *self, const char *rec)
{
    char attr_buf[KND_NAME_SIZE] = {0};
    size_t attr_buf_size;

    char val_buf[KND_TEMP_BUF_SIZE] = {0};
    size_t val_buf_size;

    const char *c;
    const char *b;
    
    //bool in_base = true;
    //bool in_spec = false;
    //long numval;
    int err;
    
    c = rec;
    b = rec;
    
    if (DEBUG_USER_LEVEL_3)
        knd_log("  .. User parsing rec: \"%s\"..\n", rec);
    
    while (*c) {
        switch (*c) {
        case '(':
            b = c + 1;
            break;
        case ')':
            val_buf_size = c - b;
            memcpy(val_buf, b, val_buf_size);
            val_buf[val_buf_size] = '\0';

            /* data classes */
            if (!strcmp(attr_buf, "C")) {
                err = kndUser_read_classes(self, val_buf, val_buf_size);
                if (err) goto final;
            }

            if (!strcmp(attr_buf, "B")) {
                err = kndUser_read_browse_classes(self, val_buf, val_buf_size);
                if (err) goto final;
            }

            if (!strcmp(attr_buf, "R")) {
                err = kndUser_read_repos(self, val_buf, val_buf_size);
                if (err) goto final;
            }

            if (!strcmp(attr_buf, "N")) {
                memcpy(self->name, val_buf, val_buf_size);
                self->name_size = val_buf_size;
                self->name[val_buf_size] = '\0';
            }
            break;
        case '^':
            attr_buf_size = c - b;
            memcpy(attr_buf, b, attr_buf_size);
            attr_buf[attr_buf_size] = '\0';

            /*knd_log("ATTR: %s\n", attr_buf);*/
            b = c + 1;
            break;
            
        default:
            break;
        }
        c++;
    }
    
    /* set default lang */
    if (!self->lang_code_size) {
        memcpy(self->lang_code, "ru_RU", 5);
        self->lang_code_size = 5;
    }
    
    err = knd_OK;
    
 final:
    
    return err;
}


static int
kndUser_read_db_state(struct kndUser *self, char *rec)
{
    char *b, *c;
    
    bool in_body = false;
    //bool in_state = false;
    bool in_field = false;
    bool in_val = false;
    size_t chunk_size = 0;
    int err;
    
    c = rec;
    b = rec;
    
    if (DEBUG_USER_LEVEL_TMP)
        knd_log(".. User \"%s\" parsing DB state config", self->id, rec);
    
    while (*c) {
        switch (*c) {
        case ' ':
        case '\n':
        case '\r':
        case '\t':
            if (in_val) break;
            
            break;
        case '{':
            if (!in_body) {
                in_body = true;
                b = c + 1;
                break;
            }

            *c = '\0';

            if (!in_field) {
                if (!strcmp(b, "STATE")) {
                    in_field = true;
                    b = c + 1;
                    break;
                } else
                    return knd_FAIL;
            }
            
            if (!strcmp(b, "repo")) {
                c++;
                err = self->repo->read_state(self->repo, c, &chunk_size);
                if (err) return err;

                c += chunk_size;

                /*knd_log("CHUNK: %lu continue parsing from \"%s\"",
                  (unsigned long)chunk_size, c); */
                break;
            }
            
            b = c + 1;
            break;
        case '}':
            
            if (in_body)
                return knd_OK;

            break;
        default:
            break;
        }

        c++;
    }

    
    return knd_FAIL;
}


static int 
kndUser_restore(struct kndUser *self)
{
    char buf[KND_TEMP_BUF_SIZE] = {0};
    size_t buf_size = 0;

    char idbuf[KND_ID_SIZE + 1];
    struct kndRepo *repo;
    
    int err;

    if (DEBUG_USER_LEVEL_TMP)
        knd_log("   .. User \"%s\" restoring DB state  DBPATH: %s\n",
                self->id, self->dbpath);

    buf_size = sprintf(buf, "%s/state.gsl", self->dbpath);

    err = self->out->read_file(self->out,
                               (const char*)buf, buf_size);
    if (err) {
        knd_log("-- couldn't read state.gsl");
        return err;
    }

    err = kndUser_read_db_state(self, self->out->file);
    if (err) return err;
    
    memset(idbuf, '0', KND_ID_SIZE);
    idbuf[KND_ID_SIZE] = '\0';

    while ((strncmp(idbuf, self->repo->last_id, KND_ID_SIZE)) < 0) {
        knd_inc_id(idbuf);

        err = kndRepo_new(&repo);
        if (err) return knd_NOMEM;

        memcpy(repo->id, idbuf, KND_ID_SIZE);
        repo->user = self;
        repo->out = self->out;
        repo->restore_mode = true;
        
        err = repo->open(repo);
        if (err) return err;

        err = repo->restore(repo);
        if (err) return err;

        /* update repo full name idx */
        err = self->repo_idx->set(self->repo_idx, repo->name, (void*)repo);
        if (err) return err;
        
        repo->restore_mode = false;
    }


    err = knd_OK;

    return err;
}


static int 
kndUser_export_GSL(struct kndUser *self)
{
    char buf[KND_TEMP_BUF_SIZE] = {0};
    size_t buf_size = 0;
    struct kndOutput *out;
    int err;

    out = self->out;
    
    buf_size = sprintf(buf, "(ID^%s)(N^%s)(C^ooWebResource)",
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

    struct kndDataClass *dc;
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

        dc = (struct kndDataClass*)val;

        memcpy(dc->lang_code, self->lang_code, self->lang_code_size);
        dc->lang_code_size = self->lang_code_size;
        
        err = dc->export(dc, KND_FORMAT_JSON);
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

static int
kndUser_parse_repo(struct kndUser *self,
                   char *rec,
                   size_t *total_size)
{
    char *b, *c, *e;
    size_t buf_size;

    const char *repo_add_tag = "add";
    size_t repo_add_tag_tag_size = strlen(repo_add_tag);

    bool in_field = false;
    
    c = rec;
    b = c;
    e = c;
    
    if (DEBUG_USER_LEVEL_TMP)
        knd_log("   .. parsing REPO rec: \"%s\"", c);
   
    while (*c) {
        switch (*c) {
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (!in_field) break;
            
            break;
        case '{':
            break;
        default:
            break;
        }
        c++;
    }
    
    return knd_FAIL;
}



static int
kndUser_parse_auth(struct kndUser *self,
                   char *rec,
                   size_t *total_size,
                   struct kndUser **user)
{
    char *b, *c, *e;
    size_t buf_size;

    const char *sid_tag = "sid";
    size_t sid_tag_size = strlen(sid_tag);

    bool in_field = false;
    bool in_sid = false;
    bool auth_ok = false;
    
    c = rec;
    b = c;
    e = c;
    
    if (DEBUG_USER_LEVEL_TMP)
        knd_log("   .. parsing  AUTH rec: \"%s\"", c);
   
    while (*c) {
        switch (*c) {
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (!in_field) break;

            if (!strncmp(b, sid_tag, sid_tag_size)) {
                knd_log("++ sid tag: \"%s\"",
                        b);
                in_sid = true;
                b = c + 1;
                break;
            }
            
            break;
        case '{':
            if (!in_field) {
                in_field = true;
                b = c + 1;
                break;
            }
            
            b = c + 1;
            break;
        case '}':
            if (in_sid) {
                buf_size = e - b;
                if (!buf_size) return knd_FAIL;
                if (buf_size >= KND_NAME_SIZE) {
                    knd_log("-- sid too large: %lu bytes",
                            (unsigned long)buf_size);
                    return knd_LIMIT;
                }

                knd_log("SID: \"%s\" [%lu]",
                        b, (unsigned long)buf_size);

                if (!strncmp(self->sid, b, buf_size)) {
                    knd_log("++ AUTH SID OK!");
                    in_sid = false;
                    auth_ok = true;
                    *user = self;
                    break;
                }
                
                return knd_FAIL;
            }

            if (auth_ok) {
                *total_size = c - rec;
                return knd_OK;
            }
            
            break;
        default:
            e = c + 1;
            break;
        }
        c++;
    }
    
    return knd_FAIL;
}



static int
kndUser_parse_task(struct kndUser *self,
                   char *rec, size_t *total_size)
{
    char *b, *c, *e;
    size_t buf_size;

    const char *auth_tag = "auth";
    size_t auth_tag_size = strlen(auth_tag);

    const char *repo_tag = "repo";
    size_t repo_tag_size = strlen(repo_tag);

    struct kndUser *user = NULL;
    size_t chunk_size;
    
    bool in_field = false;
    bool in_auth = false;
    bool in_repo = false;
    int err;
    
    c = rec;
    b = c;
    e = c;
    
    if (DEBUG_USER_LEVEL_2)
        knd_log("   .. parsing USER rec: \"%s\"", rec);
   
    while (*c) {
        switch (*c) {
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            break;
        case '{':
            if (!in_field) {
                in_field = true;
                b = c + 1;
                break;
            }

            buf_size = e - b;
            if (!buf_size) return knd_FAIL;
            if (buf_size >= KND_NAME_SIZE) {
                knd_log("-- field tag too large: %lu bytes",
                        (unsigned long)buf_size);
                return knd_LIMIT;
            }

            if (!in_auth) {
                if (!strncmp(b, auth_tag, auth_tag_size)) {
                    if (DEBUG_USER_LEVEL_2)
                        knd_log("++ auth tag: \"%s\" [%lu]",
                                b, (unsigned long)buf_size);

                    err = kndUser_parse_auth(self, b, &chunk_size, &user);
                    if (err) {
                        knd_log("-- authentication failed :(");
                        return knd_FAIL;
                    }

                    c += chunk_size;
                    b = c + 1;
                    e = b;
                    in_auth = true;
                    in_field = false;
                    
                    knd_log("== remainder: \"%s\" [chunk: %lu]",
                            c, (unsigned long)chunk_size);

                    break;
                }
                
                knd_log("-- no auth given :(");
                return knd_FAIL;
            }

            
            knd_log("== field TAG: \"%s\" [%lu]",
                    b, (unsigned long)buf_size);

            if (!strncmp(b, repo_tag, repo_tag_size)) {
                knd_log("++ repo tag: \"%s\" [%lu]",
                        b, (unsigned long)buf_size);

                if (!user) {
                    knd_log("-- no authenticated user to run the repo task :(");
                    return knd_FAIL;
                }

                err = kndUser_parse_repo(user, b, &chunk_size);
                if (err) return err;
                
                in_repo = true;
            }

            break;
        case '}':

            
            *total_size = c - rec;
            return knd_OK;
        default:
            e = c + 1;
            break;
        }

        c++;
    }
    
    return knd_FAIL;
}

static int 
kndUser_export(struct kndUser *self, knd_format format)
{
    int err = knd_FAIL;
    
    switch(format) {
        case KND_FORMAT_JSON:
        err = kndUser_export_JSON(self);
        if (err) goto final;
        break;
    case KND_FORMAT_GSL:
        err = kndUser_export_GSL(self);
        if (err) goto final;
        break;
    default:
        break;
    }

 final:
    return err;
}


extern int 
kndUser_init(struct kndUser *self)
{
    self->del = kndUser_del;
    self->str = kndUser_str;

    self->parse_task = kndUser_parse_task;

    self->add_user = kndUser_add_user;
    self->get_user = kndUser_get_user;

    self->get_repo = kndUser_get_repo;

    self->read = kndUser_read;
    self->restore = kndUser_restore;

    self->sync = kndUser_sync;
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

    err = kndRepo_new(&self->repo);
    if (err) return knd_NOMEM;
    self->repo->user = self;
    self->repo->name[0] = '~';
    self->repo->name_size = 1;
    
    err = ooDict_new(&self->class_idx, KND_SMALL_DICT_SIZE);
    if (err) return knd_NOMEM;

    err = ooDict_new(&self->browse_class_idx, KND_SMALL_DICT_SIZE);
    if (err) return knd_NOMEM;

    err = ooDict_new(&self->repo_idx, KND_SMALL_DICT_SIZE);
    if (err) return knd_NOMEM;

    err = ooDict_new(&self->repo_name_idx, KND_SMALL_DICT_SIZE);
    if (err) return knd_NOMEM;

    err = kndOutput_new(&self->out, KND_TEMP_BUF_SIZE);
    if (err) return err;
    self->repo->out =  self->out;

    
    kndUser_init(self);

    *user = self;

    return knd_OK;
}
