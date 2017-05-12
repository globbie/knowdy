#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_policy.h"
#include "knd_user.h"
#include "knd_repo.h"
#include "knd_output.h"
#include "knd_msg.h"
#include "knd_spec.h"

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
    
    err = knd_OK;
    *user = curr_user;

 final:    
    return err;
}


static int
kndUser_update(struct kndUser *self, struct kndData *data)
{
    char buf[KND_TEMP_BUF_SIZE] = {0};
    size_t buf_size;
    knd_format format = KND_FORMAT_GSL;
    int err = knd_FAIL;

    /*buf_size = KND_TEMP_BUF_SIZE;
    err = knd_get_attr(data->spec, "tid",
                       data->tid, &buf_size);
    if (err) {
        knd_log(" .. no TID specified :(\n");
        goto final;
    }
    
    buf_size = KND_TEMP_BUF_SIZE;
    err = knd_get_attr(data->spec, "repo",
                       buf, &buf_size);
    */

    if (!err) {
        /* check if repo is a valid one */
    } else {
        if (DEBUG_USER_LEVEL_2)
            knd_log(" .. no repo specified: assuming home directory\n");
    }
    
    /* TODO: check repo policy */

    knd_log("  .. User \"%s\" update... [TID: %s]\n",
            self->id, data->tid);

    /* specific format? */
    /*buf_size = KND_TEMP_BUF_SIZE;
    err = knd_get_attr(data->spec, "format",
                       buf, &buf_size);
    if (!err) {
        knd_log(" .. format specified: %s\n", buf);
    }
    */

    
    /* actual import */
    err = self->repo->update(self->repo, format);
    knd_log("   .. user update status: %d\n", err);

    buf_size = sprintf(buf, "{\"update\": %d}", err);
    self->out->write(self->out, buf, buf_size);

    if (err) goto final;

    
 final:
    return err;
}


static int
kndUser_import(struct kndUser *self, struct kndData *data)
{
    char buf[KND_TEMP_BUF_SIZE] = {0};
    size_t buf_size = 0;
    knd_format format = KND_FORMAT_GSL;
    struct kndRepo *repo = NULL;
    struct kndRepoAccess *acc = NULL;
    int err = knd_FAIL;

    /*buf_size = KND_TEMP_BUF_SIZE;
    err = knd_get_attr(data->spec, "tid",
                       data->tid, &buf_size);
    if (err) return err;

    buf_size = KND_TEMP_BUF_SIZE;
    err = knd_get_attr(data->spec, "repo",
                       buf, &buf_size);
    */
    if (!err) {
        err = kndUser_get_repo(self,
                               (const char*)buf, buf_size, &repo);
        if (err) return err;

        /* TODO: check repo policy */
        if (DEBUG_USER_LEVEL_3)
            knd_log(" .. checking user \"%s\" policy for the repo \"%s\"..\n",
                    self->name, repo->name);


        acc = self->repo_idx->get(self->repo_idx, (const char*)repo->name);
        if (!acc) {
            if (DEBUG_USER_LEVEL_TMP)
                knd_log("   -- repo \"%s\" is not available to user \"%s\" :(\n",
                        repo->name, self->name);
            return knd_ACCESS;
        }
        
        if (!acc->may_import) {
            if (DEBUG_USER_LEVEL_TMP)
                knd_log("   -- import operation on \"%s\" is not granted to user \"%s\" :(\n",
                        repo->name, self->name);

            return knd_ACCESS;
        }

        repo->user = self;
    }
    else {
        if (DEBUG_USER_LEVEL_2)
            knd_log(" .. no repo specified: assuming home directory\n");
        repo = self->repo;
    }

    knd_log("  .. User \"%s\" import... [TID: %s]\n",
            self->id, data->tid);

    /* specific format? */
    /*buf_size = KND_TEMP_BUF_SIZE;
    err = knd_get_attr(data->spec, "format",
                       buf, &buf_size);
    if (!err) {
        knd_log(" .. format specified: %s\n", buf);
    }
    */
    
    /* actual import */
    err = repo->import(repo, data->obj);

    buf_size = sprintf(buf, "{\"import\": %d}", err);
    err = self->out->write(self->out, buf, buf_size);
    if (err) return err;

    return knd_OK;
}


/*static int
kndUser_update_select(struct kndUser *self, struct kndData *data)
{
    int err ;
    
    
    err = self->repo->update_select(self->repo, data);
    
    err = knd_OK;
    
 final:
    return err;
}
*/


static int
kndUser_select(struct kndUser *self, struct kndData *data)
{
    char buf[KND_TEMP_BUF_SIZE] = {0};
    size_t buf_size = 0;

    struct kndOutput *out;

    //const char *empty_msg = "None";
    //size_t empty_msg_size = strlen(empty_msg);

    struct kndRepo *repo = NULL;
    struct kndRepoAccess *acc = NULL;

    //void *update_inbox = self->reader->update;
    int err = knd_FAIL;

    /*    sprintf(buf, "%s/users", self->reader->path);
    err = knd_make_id_path(self->path, buf, uid, NULL);
    if (err) goto final;
    */

    /* TODO: get recent updates */
    /* knd_zmq_sendmore(update_inbox, data->spec, data->spec_size);
    knd_zmq_sendmore(update_inbox, data->query, data->query_size);
    knd_zmq_sendmore(update_inbox, empty_msg, empty_msg_size);
    knd_zmq_sendmore(update_inbox, empty_msg, empty_msg_size);
    knd_zmq_send(update_inbox, empty_msg, empty_msg_size);
    */
    
    /*buf_size = KND_TID_SIZE + 1;
    err = knd_get_attr(data->spec, "tid",
                       data->tid, &buf_size);
    if (err) goto final;

    buf_size = KND_NAME_SIZE;
    err = knd_get_attr(data->spec, "l",
                       data->tid, &buf_size);
    
    knd_log("  .. User \"%s\" select... [TID: %s]  LANG: \"%s\"\n",
            self->id, data->tid, self->lang_code);

    buf_size = KND_TEMP_BUF_SIZE;
    err = knd_get_attr(data->spec, "repo",
                       buf, &buf_size);
    */
    if (!err) {

        if (!strncmp(buf, "pub", strlen("pub"))) {
            /*if (!self->reader->default_repo_name_size) return knd_FAIL;

            err = kndUser_get_repo(self,
                                   (const char*)self->reader->default_repo_name,
                                   self->reader->default_repo_name_size, &repo);
            if (err) return err;

            */
            
        }
        else {
            err = kndUser_get_repo(self,
                                   (const char*)buf,
                                   buf_size, &repo);
            if (err) return err;
        }

        if (DEBUG_USER_LEVEL_3)
            knd_log(" .. checking user \"%s\" policy for the repo \"%s\"..\n",
                    self->name, repo->name);

        acc = self->repo_idx->get(self->repo_idx, (const char*)repo->name);
        if (!acc) {
            if (DEBUG_USER_LEVEL_TMP)
                knd_log("   -- repo \"%s\" is not available to user \"%s\" :(\n",
                        repo->name, self->name);
            return knd_ACCESS;
        }
        
        if (!acc->may_select) {
            if (DEBUG_USER_LEVEL_TMP)
                knd_log("   -- select operation on \"%s\" is not granted to user \"%s\" :(\n",
                        repo->name, self->name);

            return knd_ACCESS;
        }

        repo->user = self;
    } else {
        if (DEBUG_USER_LEVEL_3)
            knd_log("\n  .. no repo specified: assuming home directory..\n");
        repo = self->repo;
    }

    err = repo->select(repo, data);
    if (err) goto final;

    /*if (repo == self->repo) {
        err = kndUser_export(self, KND_FORMAT_JSON);
        if (err) return err;
    }
    else {
    */

    /* output format specified? */
    /*buf_size = KND_NAME_SIZE;
    err = knd_get_attr(data->spec, "format",
                       buf, &buf_size);
    if (!err) {
        if (!strcmp(buf, "HTML"))
            data->format = KND_FORMAT_HTML;
    }
    */
    
    out = self->out;
    out->reset(out);

    switch (data->format) {
    case KND_FORMAT_HTML:
        err = out->write(out,
                         "<DIV>", strlen("<DIV>"));
        if (err) return err;
        
        break;
    default:
        err = out->write(out,
                         "{\"repo\":", strlen(",\"repo\":"));
        if (err) return err;
        break;
        
    }
    
    repo->out = out;
    
    err = repo->export(repo, data->format);
    if (err) {
        knd_log("  -- repo export failed :(\n");
        return err;
    }
    
    switch (data->format) {
    case KND_FORMAT_HTML:
        err = out->write(out,
                         "</DIV>", strlen("</DIV>"));
        if (err) return err;
        
        break;
    default:
        err = out->write(out, "}", 1);
        if (err) return err;
        break;
    }

    
    
    err = knd_OK;

 final:

    /*if (self->obj_out->file) {
        free(self->reader->obj_out->file);
        self->reader->obj_out->file = NULL;
        self->reader->obj_out->file_size = 0;
        } */
        
    return err;
}


static int
kndUser_get_obj(struct kndUser *self, struct kndData *data)
{
    char buf[KND_NAME_SIZE] = {0};
    size_t buf_size = 0;

    struct kndRepoAccess *acc = NULL;
    struct kndRepo *repo = NULL;
    
    //const char *empty_msg = "None";
    //size_t empty_msg_size = strlen(empty_msg);

    //void *update_inbox = self->reader->update;
    int err = knd_FAIL;

    /*knd_log("  .. user get obj...\n");*/
    
    /* TODO: get recent updates */
    /* knd_zmq_sendmore(update_inbox, data->spec, data->spec_size);
    knd_zmq_sendmore(update_inbox, empty_msg, empty_msg_size);
    knd_zmq_sendmore(update_inbox, empty_msg, empty_msg_size);
    knd_zmq_sendmore(update_inbox, empty_msg, empty_msg_size);
    knd_zmq_send(update_inbox, empty_msg, empty_msg_size);
    */
    
    /*buf_size = KND_TID_SIZE + 1;
    err = knd_get_attr(data->spec, "tid",
                       data->tid, &buf_size);
    if (err) goto final;

    buf_size = KND_ID_SIZE + 1;
    err = knd_get_attr(data->spec, "repo",
                       buf, &buf_size);
    */

    if (!err) {

        if (!strncmp(buf, "pub", strlen("pub"))) {
            /*if (!self->reader->default_repo_name_size) return knd_FAIL;

            err = self->reader->get_repo(self->reader,
                                         (const char*)self->reader->default_repo_name,
                                         self->reader->default_repo_name_size, &repo);
                                         if (err) return err; */
            
        }
        else {
            err = kndUser_get_repo(self,
                                   (const char*)buf,
                                   buf_size, &repo);
            if (err) return err;
        }
        
        if (DEBUG_USER_LEVEL_3)
            knd_log("\n   .. checking user \"%s\" policy for the repo \"%s\"..\n",
                    self->name, repo->name);

        acc = self->repo_idx->get(self->repo_idx, (const char*)repo->name);
        if (!acc) {
            if (DEBUG_USER_LEVEL_TMP)
                knd_log("   -- repo \"%s\" is not available to user \"%s\" :(\n",
                        repo->name, self->name);
            return knd_ACCESS;
        }
        
        if (!acc->may_get) {
            if (DEBUG_USER_LEVEL_TMP)
                knd_log("   -- GET operation on \"%s\" is not granted to user \"%s\" :(\n",
                        repo->name, self->name);
            return knd_ACCESS;
        }
        repo->user = self;
    } else {
        if (DEBUG_USER_LEVEL_3)
            knd_log("\n  .. no repo specified for GET: assuming home directory..\n");

        repo = self->repo;
    }
    
    /* get the frozen state of obj */
    err = repo->get_obj(repo, data);
    if (err) goto final;

    
    /* delivery service will decide
       which state is a valid one? */

    
    err = knd_OK;

 final:



    return err;
}

static int
kndUser_update_get_obj(struct kndUser *self, struct kndData *data)
{
    int err;
    knd_log("  .. UPDATE get obj...\n");
    
    /*buf_size = KND_TID_SIZE + 1;
    err = knd_get_attr(data->spec, "tid",
                       data->tid, &buf_size);
    if (err) goto final;

    buf_size = KND_TEMP_BUF_SIZE;
    err = knd_get_attr(data->spec, "repo",
                       buf, &buf_size);
    if (err) {
        

        knd_log(" .. no repo specified: assuming home directory\n");
    }
    */
    /* TODO: check repo policy */
    err = self->repo->get_liquid_obj(self->repo, data);
    return err;
}




static int
kndUser_flatten(struct kndUser *self, struct kndData *data)
{
    const char *empty_msg = "None";
    size_t empty_msg_size = strlen(empty_msg);

    void *update_inbox = self->update_inbox;
    
    /* get recent updates */
    knd_zmq_sendmore(update_inbox, data->spec, data->spec_size);
    knd_zmq_sendmore(update_inbox, empty_msg, empty_msg_size);
    knd_zmq_sendmore(update_inbox, empty_msg, empty_msg_size);
    knd_zmq_sendmore(update_inbox, empty_msg, empty_msg_size);
    knd_zmq_send(update_inbox, empty_msg, empty_msg_size);

    /*buf_size = KND_TID_SIZE + 1;
    err = knd_get_attr(data->spec, "tid",
                       data->tid, &buf_size);
    if (err) goto final;

    buf_size = KND_TEMP_BUF_SIZE;
    err = knd_get_attr(data->spec, "repo",
                       buf, &buf_size);
    if (err) {
        knd_log(" .. no repo specified: assuming home directory\n");
    }
    */

    
    /* get the frozen state of flattened obj */

    /* delivery service will decide
       which state is a valid one */

    
    return knd_OK;
}

static int
kndUser_update_flatten(struct kndUser *self, struct kndData *data)
{
    int err;

    knd_log("  .. UPDATE flatten obj...\n");
    
    /*buf_size = KND_TID_SIZE + 1;
    err = knd_get_attr(data->spec, "tid",
                       data->tid, &buf_size);
    if (err) goto final;

    buf_size = KND_TEMP_BUF_SIZE;
    err = knd_get_attr(data->spec, "repo",
                       buf, &buf_size);
    if (err) {
        knd_log(" .. no repo specified: assuming home directory\n");
    }
    */
    
    /* TODO: check repo policy */

    err = self->repo->update_flatten(self->repo, data);
    return err;
}


static int
kndUser_match(struct kndUser *self, struct kndData *data)
{
    const char *empty_msg = "None";
    size_t empty_msg_size = strlen(empty_msg);

    void *update_inbox = NULL; /*self->reader->update;*/
    int err;

    knd_log("  .. match obj...\n");
    
    /* get recent updates */
    knd_zmq_sendmore(update_inbox, data->spec, data->spec_size);
    knd_zmq_sendmore(update_inbox, data->obj, data->obj_size);
    knd_zmq_sendmore(update_inbox, empty_msg, empty_msg_size);
    knd_zmq_sendmore(update_inbox, empty_msg, empty_msg_size);
    knd_zmq_send(update_inbox, empty_msg, empty_msg_size);

    /*buf_size = KND_TID_SIZE + 1;
    err = knd_get_attr(data->spec, "tid",
                       data->tid, &buf_size);
    if (err) goto final;

    buf_size = KND_TEMP_BUF_SIZE;
    err = knd_get_attr(data->spec, "repo",
                       buf, &buf_size);
    if (err) {
        knd_log(" .. no repo specified: assuming home directory\n");
    }
    */
    
    /* get the frozen state of matched obj */

    /*sleep(0);*/  // fixme! was sleep(0.1). why sleep?

    err = self->repo->match(self->repo, data);
    if (err) return err;

    return knd_OK;
}

static int
kndUser_update_match(struct kndUser *self, struct kndData *data)
{
    int err;

    knd_log("  .. UPDATE match obj...\n");

    
    /*buf_size = KND_TID_SIZE + 1;
    err = knd_get_attr(data->spec, "tid",
                       data->tid, &buf_size);
    if (err) goto final;

    buf_size = KND_TEMP_BUF_SIZE;
    err = knd_get_attr(data->spec, "repo",
                       buf, &buf_size);
    if (err) {
        knd_log(" .. no repo specified: assuming home directory\n");
    }
    */
    
    /* TODO: check repo policy */

    err = self->repo->liquid_match(self->repo, data);
    
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
                 const char *name, size_t name_size,
                 struct kndRepo **result)
{
    //char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size = KND_TEMP_BUF_SIZE;
    struct kndRepo *repo = NULL;
    int err;

    repo = self->repo_idx->get(self->repo_idx, name);
    if (repo) {
        *result = repo;
        return knd_OK;
    }

    err = knd_is_valid_id(name, name_size);
    if (err) return err;

    err = kndRepo_new(&repo);
    if (err) return knd_NOMEM;

    memcpy(repo->id, name, KND_ID_SIZE);
    repo->user = self;
    
    repo->out = self->out;
    
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
    //char buf[KND_TEMP_BUF_SIZE] = {0};
    //size_t buf_size = 0;

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
    int err;

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

    /*if (self->user_idx) {
        self->user_idx->rewind(self->user_idx);
        do {
            self->user_idx->next_item(self->user_idx, &key, &val);
            if (!key) break;

            user = (struct kndUser*)val;

            knd_log("USER: %s\n", user->id);

        } while (key);
        }*/



    err = knd_OK;

    return err;
}



static int 
kndUser_run(struct kndUser *self)
{
    struct kndSpecArg *arg;
    const char *val;
    int err;
    
    if (DEBUG_USER_LEVEL_TMP)
        knd_log(".. USER task to run: %s",
                self->instruct->proc_name);

    if (!strcmp(self->instruct->proc_name, "add")) {
        for (size_t i = 0; i < self->instruct->num_args; i++) {
            arg = &self->instruct->args[i];

            knd_log("== ARG NAME: %s", arg->name);
        }
        
        err = kndUser_add_user(self);
        if (err) return err;
        
    }

    
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

    self->run = kndUser_run;

    self->add_user = kndUser_add_user;
    self->get_user = kndUser_get_user;

    self->read = kndUser_read;
    self->import = kndUser_import;
    self->update = kndUser_update;

    self->select = kndUser_select;
    /*   self->update_select = kndUser_update_select;*/

    self->get_obj = kndUser_get_obj;
    self->update_get_obj = kndUser_update_get_obj;
    self->restore = kndUser_restore;

    self->flatten = kndUser_flatten;
    self->update_flatten = kndUser_update_flatten;

    self->match = kndUser_match;
    self->update_match = kndUser_update_match;

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

    err = kndOutput_new(&self->out, KND_TEMP_BUF_SIZE);
    if (err) return err;
    self->repo->out =  self->out;

    
    kndUser_init(self);

    *user = self;

    return knd_OK;
}
