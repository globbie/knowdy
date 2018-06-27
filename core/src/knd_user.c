#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gsl-parser.h>
#include <glb-lib/output.h>

//#include "knd_policy.h"
#include "knd_user.h"
#include "knd_shard.h"
#include "knd_repo.h"
#include "knd_class.h"
#include "knd_object.h"
#include "knd_proc.h"
#include "knd_rel.h"
#include "knd_state.h"

#define DEBUG_USER_LEVEL_0 0
#define DEBUG_USER_LEVEL_1 0
#define DEBUG_USER_LEVEL_2 0
#define DEBUG_USER_LEVEL_3 0
#define DEBUG_USER_LEVEL_TMP 1

static int export(struct kndUser *self);
static gsl_err_t get_user_by_id(void *obj, const char *numid, size_t numid_size);

static void del(struct kndUser *self)
{
    if (self->repo)
        self->repo->del(self->repo);
    free(self);
}

static void str(struct kndUser *self)
{
    knd_log("USER: %s", self->name);
}

static int
kndUser_add_user(struct kndUser *self)
{
    char buf[KND_TEMP_BUF_SIZE] = {0};
    size_t buf_size = 0;
    
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

    err = knd_mkpath(buf, buf_size, 0755, false);
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
    struct glbOutput *out;
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

    struct glbOutput *out = self->out;

    //const char *key = NULL;
    //void *val = NULL;
    int err;

    if (DEBUG_USER_LEVEL_TMP)
        knd_log("JSON USER: %s [%s]\n",
                self->name, self->id);

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

    /*i = 0;
    self->browse_class_idx->rewind(self->browse_class_idx);
    do {
        self->class_idx->next_item(self->browse_class_idx, &key, &val);
        if (!key) break;

        if (i) {
            err = out->write(out, ",", 1);
            if (err) return err;
        }

        c = (struct kndClass*)val;
        err = c->export(c);
        if (err) return err;
        i++;
    } while (key);
    */
    
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


static gsl_err_t kndUser_parse_repo(void *obj,
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
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t
kndUser_parse_numid(void *obj,
                    const char *rec,
                    size_t *total_size)
{
    struct kndUser *self = obj;
    int err;
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = get_user_by_id,
          .obj = self
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        if (!self->log->buf_size) {
            err = self->log->write(self->log, "user identification failure",
                                   strlen("user identification failure"));
            if (err) return make_gsl_err_external(err);
        }
        return parser_err;
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_auth(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct kndUser *self = obj;
    char sid[KND_NAME_SIZE];
    size_t sid_size = 0;
    int err;
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .name = "sid",
          .name_size = strlen("sid"),
          .buf = sid,
          .buf_size = &sid_size,
          .max_buf_size = KND_NAME_SIZE
        }
    };

    if (DEBUG_USER_LEVEL_TMP)
        knd_log("   .. parsing the AUTH rec: \"%.*s\"", 32, rec);

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        if (!self->log->buf_size) {
            err = self->log->write(self->log, "user authentication failure",
                                   strlen("user authentication failure"));
            if (err) return make_gsl_err_external(err);
        }
        return parser_err;
    }

    if (DEBUG_USER_LEVEL_TMP)
        knd_log("++ got SID token (JWT): \"%s\"", sid);

    if (!sid_size) {
        knd_log("-- no SID provided :(");
        return make_gsl_err(gsl_FAIL);
    }

    /* TODO: DB check auth token */

    if (strncmp(self->sid, sid, sid_size)) {
        knd_log("-- wrong SID: \"%.*s\"", sid_size, sid);
        err = self->log->write(self->log, "SID authentication failure",
                               strlen("SID authentication failure"));
        if (err) return make_gsl_err_external(err);

        return make_gsl_err(gsl_FAIL);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_proc_import(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndUser *self = obj;
    struct kndProc *proc = self->repo->root_class->proc;
    int err;

    self->task->type = KND_UPDATE_STATE;
    err = proc->import(proc, rec, total_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_proc_select(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct kndUser *self = obj;
    struct kndProc *proc = self->repo->root_class->proc;
    int err;

    err = proc->select(proc, rec, total_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_rel_import(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndUser *self = obj;
    int err;

    err = self->repo->root_class->rel->import(self->repo->root_class->rel, rec, total_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_import(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndUser *self = obj;
    struct kndClass *c;

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. parsing the default class import: \"%.*s\"", 64, rec);
    self->task->type = KND_UPDATE_STATE;
    c = self->repo->root_class;

    return c->import(c, rec, total_size);
}

static gsl_err_t parse_sync_task(void *obj,
                                 const char *rec __attribute__((unused)),
                                 size_t *total_size __attribute__((unused)))
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    struct kndUser *self = obj;
    struct stat st;
    char *s, *n;
    size_t path_size;
    int err;

    if (DEBUG_USER_LEVEL_TMP)
        knd_log(".. got sync task..");

    s = self->path;
    memcpy(s, self->path, self->path_size);
    s += self->path_size;
    self->path_size += self->path_size;

    path_size =  strlen("/frozen_merge.gsp");
    memcpy(s, "/frozen_merge.gsp", path_size);
    self->path_size += path_size;
    self->path[self->path_size] = '\0';

    /* file exists, remove it */
    if (!stat(self->path, &st)) {
        err = remove(self->path);
        if (err) return make_gsl_err_external(err);
        knd_log("-- existing frozen DB file removed..");
    }

    /* name IDX */
    n = buf;
    buf_size = 0;
    memcpy(n, self->path, self->path_size);
    n += self->path_size;
    buf_size += self->path_size;
    path_size =  strlen("/frozen_name.gsi");
    memcpy(n, "/frozen_name.gsi", path_size);
    buf_size += path_size;
    buf[buf_size] = '\0';

    self->task->type = KND_SYNC_STATE;
    //parser_err = self->repo->root_class->sync(self->repo->root_class, rec, total_size);
    //if (parser_err.code) return parser_err;

    /* bump frozen count */

    /* temp: simply rename the GSP file */
    self->out->reset(self->out);
    err = self->out->write(self->out, self->path, self->path_size);
    if (err) return make_gsl_err_external(err);
    err = self->out->write(self->out, "/frozen.gsp", strlen("/frozen.gsp"));
    if (err) return make_gsl_err_external(err);

    /* null-termination is needed to call rename */
    self->out->buf[self->out->buf_size] = '\0';

    err = rename(self->path, self->out->buf);
    if (err) {
        knd_log("-- failed to rename GSP output file: \"%s\" :(", self->out->buf);
        return make_gsl_err_external(err);
    }

    /* TODO: inform retrievers */

    /* release resources */
    if (!stat(self->out->buf, &st)) {
        if (DEBUG_USER_LEVEL_TMP)
            knd_log("++ frozen DB file sync'ed OK, total bytes: %lu",
                    (unsigned long)st.st_size);
    }

    /*self->out->reset(self->out);
    err = self->out->write(self->out,
                           "{\"file_size\":",
                           strlen("{\"file_size\":"));
    if (err) return make_gsl_err_external(err);
    buf_size = sprintf(buf, "%lu", (unsigned long)st.st_size);
    err = self->out->write(self->out, buf, buf_size);
    if (err) return make_gsl_err_external(err);
    err = self->out->write(self->out, "}", 1);
    if (err) return make_gsl_err_external(err);
    */
    return make_gsl_err(gsl_OK);
}


static gsl_err_t parse_class_select(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct kndUser *self = obj;
    struct kndClass *c = self->repo->root_class;
    int err;

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. parsing the default class select: \"%s\"", rec);

    c->reset_inbox(c);

    err = c->select(c, rec, total_size);
    if (err) {
        /* TODO: release resources */
        c->reset_inbox(c);
        return make_gsl_err_external(err);
    }

    return make_gsl_err(gsl_OK);
}

/*static gsl_err_t parse_rel_select(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndUser *self = obj;
    struct kndRel *rel;
    int err;

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. User %p:  parsing default Rel select: \"%s\"", self, rec);

    err = rel->select(rel, rec, total_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}
*/

static gsl_err_t parse_liquid_updates(void *obj,
                                      const char *rec,
                                      size_t *total_size)
{
    struct kndUser *self = (struct kndUser*)obj;
    int err;

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. parse and apply liquid updates..");

    self->task->type = KND_LIQUID_STATE;

    err = self->repo->root_class->apply_liquid_updates(self->repo->root_class, rec, total_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_get_user(void *obj, const char *name, size_t name_size)
{
    struct kndUser *self = obj;
    struct kndClass *c;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. get user: \"%.*s\"..",
                name_size, name);

    err = self->repo->root_class->get(self->repo->root_class,
                                "User", strlen("User"), &c);
    if (err) return make_gsl_err_external(err);

    err = c->get_obj(c, name, name_size, &self->curr_user);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t select_user_rels(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct kndUser *self = obj;
    struct kndObject *user;
    int err;

    if (!self->curr_user) {
        knd_log("-- no user selected :(");
        return make_gsl_err(gsl_FAIL);
    }

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. selecting User rels: \"%.*s\"", 32, rec);

    self->out->reset(self->out);
    user = self->curr_user;
    err = user->select_rels(user, rec, total_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}


static gsl_err_t get_user_by_id(void *data, const char *numid, size_t numid_size)
{
    struct kndUser *self = data;
    struct kndClass *c;
    struct kndObjEntry *entry;
    struct kndObject *obj;
    long numval = 0;
    int err;

    if (!numid_size) return make_gsl_err(gsl_FORMAT);
    if (numid_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    err = knd_parse_num((const char*)numid, &numval);
    if (err) return make_gsl_err_external(err);

    if (numval < 0 || (size_t)numval >= self->max_users) {
        knd_log("-- max user limit exceeded");
        return make_gsl_err(gsl_LIMIT);
    }

    entry = self->user_idx[numval];
    if (!entry) {
        knd_log("-- no such user id: %lu", numval);
        return make_gsl_err(gsl_NO_MATCH);
    }

    err = self->repo->root_class->get(self->repo->root_class, "User", strlen("User"), &c);
    if (err) return make_gsl_err_external(err);

    c->mempool = self->repo->root_class->mempool;

    err = c->read_obj_entry(c, entry, &obj);
    if (err) return make_gsl_err_external(err);

    self->curr_user = obj;

    if (DEBUG_USER_LEVEL_TMP) {
        knd_log("++ got user by num id: %.*s", numid_size, numid);
        self->curr_user->str(self->curr_user);
    }

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_present_user(void *data,
                                  const char *val __attribute__((unused)),
                                  size_t val_size __attribute__((unused)))
{
    struct kndUser *self = data;
    struct kndObject *user;
    int err;

    if (!self->curr_user) {
        knd_log("-- no user selected :(");
        return make_gsl_err(gsl_FAIL);
    }
    self->out->reset(self->out);
    user = self->curr_user;
    user->expand_depth = self->expand_depth;

    err = user->export(user);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t remove_user(void *data,
                             const char *val __attribute__((unused)),
                             size_t val_size __attribute__((unused)))
{
    struct kndUser *self = data;
    struct kndClass *c;
    struct kndClass *root_class;
    struct kndObject *obj;
    int err;

    if (!self->curr_user) {
        knd_log("-- remove operation: no user selected :(");
        return make_gsl_err(gsl_FAIL);
    }

    obj = self->curr_user;
    if (DEBUG_USER_LEVEL_2)
        knd_log("== obj to remove: \"%.*s\"", obj->name_size, obj->name);

    /* TODO: add state */

    obj->state->phase = KND_REMOVED;

    self->log->reset(self->log);
    err = self->log->write(self->log, obj->name, obj->name_size);
    if (err) return make_gsl_err_external(err);
    err = self->log->write(self->log, " obj removed", strlen(" obj removed"));
    if (err) return make_gsl_err_external(err);
    c = obj->base;

    self->task->type = KND_UPDATE_STATE;

    obj->next = c->obj_inbox;
    c->obj_inbox = obj;
    c->obj_inbox_size++;

    root_class = c->root_class;

    c->next = root_class->inbox;
    root_class->inbox = c;
    root_class->inbox_size++;

    return make_gsl_err(gsl_OK);
}

static int parse_task(struct kndUser *self,
                      const char *rec,
                      size_t *total_size)
{
    struct kndClass *c;

    if (DEBUG_USER_LEVEL_2)
        knd_log(".. parsing user task: \"%s\" size: %lu..\n\n",
                rec, (unsigned long)strlen(rec));

    /* reset defaults */
    self->expand_depth = 0;
    self->curr_user = NULL;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_user,
          .obj = self
        },
        { .name = "id",
          .name_size = strlen("id"),
          .is_selector = true,
          .parse = kndUser_parse_numid,
          .obj = self
        },
        { .name = "_depth",
          .name_size = strlen("_depth"),
          .is_selector = true,
          .parse = gsl_parse_size_t,
          .obj = &self->expand_depth
        },
        { .name = "auth",
          .name_size = strlen("auth"),
          .parse = parse_auth,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "_rm",
          .name_size = strlen("_rm"),
          .run = remove_user,
          .obj = self
        },
        { .name = "repo",
          .name_size = strlen("repo"),
          .parse = kndUser_parse_repo,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_import,
          .obj = self
        },
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_select,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_import,
          .obj = self
        },
        { .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_select,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "rel",
          .name_size = strlen("rel"),
          .parse = parse_rel_import,
          .obj = self
        }/*,
        { .name = "rel",
          .name_size = strlen("rel"),
          .parse = parse_rel_select,
          .obj = self
          }*/,
        { .name = "_rels",
          .name_size = strlen("_rels"),
          .parse = select_user_rels,
          .obj = self
        },
        { .type = GSL_SET_STATE,
          .name = "state",
          .name_size = strlen("state"),
          .parse = parse_liquid_updates,
          .obj = self
        },
        { .name = "sync",
          .name_size = strlen("sync"),
          .parse = parse_sync_task,
          .obj = self
        },
        { .is_default = true,
          .run = run_present_user,
          .obj = self
        }
    };
    int err, e;
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- user task parse failure: \"%.*s\" :(", self->log->buf_size, self->log->buf);
        if (!self->log->buf_size) {
            e = self->log->write(self->log, "internal server error",
                                 strlen("internal server error"));
            if (e) {
                err = e;
                goto cleanup;
            }
        }
        err = gsl_err_to_knd_err_codes(parser_err);
        goto cleanup;
    }

    if (DEBUG_USER_LEVEL_2)
        knd_log("task type: %d   ++ user parse task OK: total chars: %lu",
                self->task->type, (unsigned long)*total_size);

    switch (self->task->type) {
    case KND_LIQUID_STATE:
        if (DEBUG_USER_LEVEL_2)
            knd_log("++ liquid updates applied!");
        return knd_OK;
    case KND_GET_STATE:
        return knd_OK;
    case KND_UPDATE_STATE:
        self->task->update_spec = rec;
        self->task->update_spec_size = *total_size;
        c = self->repo->root_class;
        err = c->update_state(c);
        if (err) {
            knd_log("-- failed to update state :(");
            goto cleanup;
        }
        break;
    default:
        break;
    }

    err = knd_OK;

 cleanup:

    /* TODO: release resources */
    self->repo->root_class->reset_inbox(self->repo->root_class);

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

static int kndUser_init(struct kndUser *self)
{

    knd_log(".. init User.. ");

    memcpy(self->path, self->shard->path, self->shard->path_size);
    self->path_size = self->shard->path_size;

    self->repo->name[0] = '~';
    self->repo->name_size = 1;

    self->repo->init(self->repo);

    return knd_OK;
}

extern int
kndUser_new(struct kndUser **user, struct kndMemPool *mempool)
{
    struct kndUser *self;
    struct kndRepo *repo;
    int err = knd_OK;

    self = malloc(sizeof(struct kndUser));                                        ALLOC_ERR(self);
    memset(self, 0, sizeof(struct kndUser));
    memset(self->id, '0', KND_ID_SIZE);
    memset(self->last_uid, '0', KND_ID_SIZE);
    memset(self->db_state, '0', KND_ID_SIZE);

    err = kndRepo_new(&repo, mempool);                                            RET_ERR();
    repo->user = self;
    self->repo = repo;


    self->mempool = mempool;

    self->del = del;
    self->str = str;
    self->init = kndUser_init;
    self->parse_task = parse_task;
    self->add_user = kndUser_add_user;

    *user = self;

    return knd_OK;
}
