#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "knd_repo.h"
#include "knd_object.h"
#include "knd_attr.h"
#include "knd_refset.h"
#include "knd_user.h"
#include "knd_query.h"
#include "knd_sorttag.h"
#include "knd_task.h"
#include "knd_output.h"
#include "knd_msg.h"
#include "knd_dict.h"
#include "knd_concept.h"

#include <gsl-parser.h>

#define DEBUG_REPO_LEVEL_0 0
#define DEBUG_REPO_LEVEL_1 0
#define DEBUG_REPO_LEVEL_2 0
#define DEBUG_REPO_LEVEL_3 0
#define DEBUG_REPO_LEVEL_TMP 1

static int
kndRepo_linearize_objs(struct kndRepo *self);

static int
kndRepo_get_guid(struct kndRepo *self,
                 struct kndConcept *conc,
                 const char *obj_name,
                 size_t      obj_name_size,
                 char *result);
static int
kndRepo_import_obj(struct kndRepo *self,
                   const char *rec,
                   size_t *total_size)
{
    // Not implemented yet
    return knd_FAIL;
}

static int
kndRepo_get_obj(struct kndRepo *self,
                const char *name,
                size_t name_size)
{
    // Not implemented yet
    return knd_FAIL;
}

static int
kndRepo_del(struct kndRepo *self)
{
    free(self);
    return knd_OK;
}

static int
kndRepo_str(struct kndRepo *self __attribute__((unused)))
{

    knd_log("REPO");

    return knd_OK;
}


static int
kndRepo_parse_config(struct kndRepo *self,
                     const char *rec,
                     size_t *total_size)
{
    size_t buf_size;

    const char *c, *b;
    bool in_body = false;
    bool in_attr = false;
    //bool in_last_id = false;
    bool in_repo_name = false;

    c = rec;
    b = rec;

    while (*c) {
        switch (*c) {
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            if (!in_attr) break;

            buf_size = c - b;
            if (!buf_size) return knd_FAIL;
            if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;

            if (!strncmp(b, "N", 1)) {
                in_repo_name = true;
                b = c + 1;
                break;
            }

            break;
        case '{':
            if (!in_body) {
                in_body = true;
                b = c + 1;
                break;
            }
            if (!in_attr) {
                in_attr = true;
                b = c + 1;
                break;
            }

            break;
        case '}':

            if (in_repo_name) {
                buf_size = c - b;
                if (!buf_size) return knd_FAIL;
                if (buf_size >= KND_NAME_SIZE) return knd_LIMIT;

                memcpy(self->name, b, buf_size);
                self->name_size = buf_size;
                self->name[buf_size] = '\0';

                if (DEBUG_REPO_LEVEL_2)
                    knd_log("== REPO NAME: \"%s\"", self->name);

                in_repo_name = false;
                break;
            }

            if (in_attr) {
                b = c + 1;
                in_attr = false;
                break;
            }

            *total_size = c - rec;
            return knd_OK;
        default:
            break;
        }
        c++;
    }

    return knd_OK;
}


static int
kndRepo_open(struct kndRepo *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    size_t chunk_size;
    const char *repo_dir = "/repos";
    size_t repo_dir_size = strlen(repo_dir);
    int err;

    struct kndOutput *out = self->path_out;

    out->reset(out);
    err = out->write(out, self->user->dbpath, self->user->dbpath_size);
    if (err) return err;

    err = out->write(out, repo_dir, repo_dir_size);
    if (err) return err;

    err = knd_make_id_path(buf, NULL, self->id, NULL);
    if (err) return err;
    buf_size = strlen(buf);

    err = out->write(out, buf, buf_size);
    if (err) return err;

    err = out->write(out, "/", 1);
    if (err) return err;

    if (out->buf_size >= KND_TEMP_BUF_SIZE) return knd_LIMIT;
    memcpy(self->path, out->buf, out->buf_size);
    self->path[out->buf_size] = '\0';
    self->path_size = out->buf_size;

    if (DEBUG_REPO_LEVEL_2)
        knd_log("..opening repo:  ID:\"%s\" REPO PATH:%s",
                self->id, self->path);

    err = out->write(out, "repo.gsl", strlen("repo.gsl"));
    if (err) return err;

    self->out->reset(self->out);
    err = self->out->read_file(self->out,
                               (const char*)out->buf, out->buf_size);
    if (err) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("-- failed to open repo: \"%s\" :(",
                    out->buf);
        return err;
    }

    self->out->file[self->out->file_size] = '\0';

    if (self->restore_mode) {
        err = kndRepo_parse_config(self, self->out->file, &chunk_size);
        if (err) return err;

        if (!self->name_size) {
            knd_log("-- repo %s full name is not set :(",
                    self->id);
            return knd_FAIL;
        }
    }

    if (DEBUG_REPO_LEVEL_2)
        knd_log("++ REPO open success: \"%s\" PATH: \"%s\"",
                self->out->file, self->path);

    return knd_OK;
}


static int
kndRepo_update_state(struct kndRepo *self)
{
    char new_state_path[KND_TEMP_BUF_SIZE];
    char state_path[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    struct kndOutput *out;
    const char *state_header = "{STATE{repo";
    size_t state_header_size = strlen(state_header);

    const char *state_close = "}}";
    size_t state_close_size = strlen(state_close);
    int err;

    out = self->out;
    out->reset(out);

    err = out->write(out, state_header, state_header_size);
    if (err) return err;

    /* last repo id */
    err = out->write(out, "{last ", strlen("{last "));
    if (err) return err;
    err = out->write(out, self->last_id, KND_ID_SIZE);
    if (err) return err;
    err = out->write(out, "}", 1);
    if (err) return err;

    err = out->write(out, state_close, state_close_size);
    if (err) return err;


    err = knd_write_file((const char*)self->user->dbpath,
                         "state_new.gsl",
                         out->buf, out->buf_size);
    if (err) return err;

    buf_size = self->user->dbpath_size;
    memcpy(state_path, (const char*)self->user->dbpath, buf_size);
    memcpy(state_path + buf_size, "/state.gsl", strlen("/state.gsl"));
    buf_size += strlen("/state.gsl");
    state_path[buf_size] = '\0';

    buf_size = self->user->dbpath_size;
    memcpy(new_state_path, (const char*)self->user->dbpath, buf_size);
    memcpy(new_state_path + buf_size, "/state_new.gsl", strlen("/state_new.gsl"));
    buf_size += strlen("/state_new.gsl");
    new_state_path[buf_size] = '\0';

    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. renaming \"%s\" -> \"%s\"..",
                new_state_path, state_path);

    err = rename(new_state_path, state_path);
    if (err) return err;

    return knd_OK;
}


static int
kndRepo_add_repo(struct kndRepo *self, const char *name, size_t name_size)
{
    char buf[KND_TEMP_BUF_SIZE];
    char path[KND_TEMP_BUF_SIZE];
    size_t path_size;
    struct kndRepo *repo;
    int err;

    /* check if repo's name is unique */
    repo = self->repo_idx->get(self->repo_idx, name, name_size);
    if (repo) {
        knd_log("-- \"%.*s\" REPO name already taken?", name_size, name);
        return knd_FAIL;
    }

    err = kndRepo_new(&repo);
    if (err) goto final;
    repo->user = self->user;
    repo->out = self->out;

    /* get new repo id */
    /*memcpy(repo->id, self->last_id, KND_ID_SIZE);
    repo->id[KND_ID_SIZE] = '\0';
    knd_inc_id(repo->id);
    */

    memcpy(repo->name, name, name_size);
    repo->name_size = name_size;
    repo->name[name_size] = '\0';

    /* check repo's existence: must be an error */
    err = kndRepo_open(repo);
    if (!err) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("-- \"%s\" REPO already exists?", repo->id);
        return knd_FAIL;
    }

    sprintf(buf, "%s/repos", self->user->dbpath);
    err = knd_make_id_path(path, buf, repo->id, NULL);
    if (err) goto final;


    /* TODO: check if DIR already exists */
    err = knd_mkpath(path, 0755, false);
    if (err) goto final;

    path_size = strlen(path);
    memcpy(self->path, path, path_size);
    self->path_size = path_size;
    self->path[path_size] = '\0';

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("  .. create new repo:  ID:\"%s\"  N:%.*s  PATH:%s",
                repo->id, repo->name_size, repo->name, path);

    /* in batch mode:
          ignore incoming tasks
       in non-batch mode (default):
          append incoming tasks to the inbox queue */

    sprintf(buf, "%s/inbox", path);
    err = knd_mkpath(buf, 0755, false);
    if (err) goto final;

    /* reflecting class scheme changes */
    sprintf(buf, "%s/migrations", path);
    err = knd_mkpath(buf, 0755, false);
    if (err) goto final;

    /*    err = kndRepo_export(repo, KND_FORMAT_GSL);
    if (err) goto final;
    */

    err = knd_write_file((const char*)path,
                         "repo.gsl",
                         repo->out->buf, repo->out->buf_size);
    if (err) goto final;

    err = self->repo_idx->set(self->repo_idx,
                              repo->name, repo->name_size,
                              repo);
    if (err) goto final;

    /* increment id */
    memcpy(self->last_id, repo->id, KND_ID_SIZE);

    err = kndRepo_update_state(self);
    if (err) goto final;

    return knd_OK;

 final:
    repo->del(repo);

    return err;
}


static gsl_err_t
kndRepo_run_add_repo(void *obj, const char *name, size_t name_size)
{
    struct kndRepo *repo;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);

    repo = (struct kndRepo*)obj;

    err = kndRepo_add_repo(repo, name, name_size);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}



static gsl_err_t
kndRepo_run_get_repo(void *obj, const char *name, size_t name_size)
{
    struct kndRepo *self, *curr_repo;

    if (!name_size) return make_gsl_err(gsl_FORMAT);

    self = (struct kndRepo*)obj;
    self->curr_repo = NULL;

    curr_repo = self->repo_idx->get(self->repo_idx,
                                    name, name_size);
    if (!curr_repo) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("-- no such repo: \"%.*s\" :(", name_size, name);
        return make_gsl_err(gsl_FAIL);
    }

    if (DEBUG_REPO_LEVEL_2) {
        knd_log("++ got repo: \"%s\" PATH: %s",
                name, curr_repo->path);
    }

    /* assign task */
    curr_repo->task = self->task;
    curr_repo->log = self->log;
    self->curr_repo = curr_repo;

    return make_gsl_err(gsl_OK);
}


//static int
//kndRepo_run_get_obj(void *obj, struct kndTaskArg *args, size_t num_args)
//{
//    struct kndRepo *self;
//    struct kndTaskArg *arg;
//    const char *name = NULL;
//    size_t name_size = 0;
//    int err;
//
//    for (size_t i = 0; i < num_args; i++) {
//        arg = &args[i];
//        if (!strncmp(arg->name, "n", strlen("n"))) {
//            name = arg->val;
//            name_size = arg->val_size;
//        }
//    }
//    if (!name_size) return knd_FAIL;
//
//    self = (struct kndRepo*)obj;
//
//    if (DEBUG_REPO_LEVEL_1)
//        knd_log(".. repo %s to get OBJ: \"%s\"", self->name, name);
//
//    err = kndRepo_get_obj(self, name, name_size);
//    if (err) return err;
//
//    return knd_OK;
//}


//static int
//kndRepo_run_import_obj(void *obj, struct kndTaskArg *args, size_t num_args)
//{
//    struct kndRepo *self;
//    struct kndTaskArg *arg;
//    const char *name = NULL;
//    size_t chunk_size = 0;
//    size_t name_size = 0;
//    int err;
//
//    for (size_t i = 0; i < num_args; i++) {
//        arg = &args[i];
//        if (!strncmp(arg->name, "obj", strlen("obj"))) {
//            name = arg->val;
//            name_size = arg->val_size;
//        }
//    }
//    if (!name_size) return knd_FAIL;
//
//    self = (struct kndRepo*)obj;
//
//    /* obj from separate msg */
//    if (!strncmp(name, "_attach", strlen("_attach"))) {
//        err = kndRepo_import_obj(self, self->task->obj, &chunk_size);
//        if (err) return err;
//        return knd_OK;
//    }
//
//    return knd_FAIL;
//}


static int
kndRepo_import_inbox_data(struct kndRepo *self,
                          const char *rec)
{
    struct kndTask *task;
    const char *c;
    size_t chunk_size = 0;
    bool in_task = true;
    int err;

    err = kndTask_new(&task);
    if (err) return err;

    self->task = task;

    c = rec;
    while (*c) {
        switch (*c) {
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            break;
        case '{':
            if (in_task) {
                task->reset(task);
                err = task->parse(task, c, &chunk_size);
                if (err) {
                    knd_log("  -- TASK parse failed: %d", err);
                    goto final;
                }
                in_task = false;
                c += chunk_size;
                break;
            }

            /*err = kndRepo_read_obj(self, c, &chunk_size);
            if (err) return err;
            */

            c += chunk_size;
            in_task = true;
            break;
        default:
            break;
        }

        c++;
    }

    return knd_OK;

 final:
    task->del(task);
    return err;
}


static int kndRepo_restore(struct kndRepo *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    const char *inbox_filename = "inbox/import.data";
    int err;

    if (DEBUG_REPO_LEVEL_2)
        knd_log("  .. restoring repo \"%s\".. PATH: %s [%lu]",
                self->id, self->path, (unsigned long)self->path_size);

    memcpy(buf, self->path, self->path_size);
    memcpy(buf + self->path_size, inbox_filename, strlen(inbox_filename));
    buf_size = self->path_size + strlen(inbox_filename);
    buf[buf_size] = '\0';

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log(".. importing recs from \"%s\"..", buf);

    // fixme: actual restore
    return knd_OK;

    self->out->reset(self->out);
    err = self->out->read_file(self->out,
                               (const char*)buf, buf_size);
    if (err) {
        knd_log("   -- failed to open the inbox \"%s\" :(", buf);
        if (err == knd_IO_FAIL) {
            knd_log("  .. skipping import of \"%s\"..", self->id);
            return knd_OK;
        }

        return err;
    }
    self->out->file[self->out->file_size] = '\0';

    err = kndRepo_import_inbox_data(self, self->out->file);
    if (err) return err;

    return knd_OK;
}

static int
kndRepo_update_inbox(struct kndRepo *self)
{
    struct kndOutput *out = self->path_out;
    int err;

    out->reset(out);

    if (DEBUG_REPO_LEVEL_2)
        knd_log("== repo DB PATH: \"%.*s\"",
                self->path_size, self->path);

    err = out->write(out, self->path, self->path_size);
    if (err) return err;

    err = out->write(out, "inbox/", strlen("inbox/"));
    if (err) return err;

    err = out->write(out, self->db_state, KND_ID_SIZE);
    if (err) return err;

    err = out->write(out, ".spec", strlen(".spec"));
    if (err) return err;

    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. update INBOX \"%.*s\"..  SPEC: %lu OBJ: %lu\n",
                out->buf_size, out->buf,
                (unsigned long)self->task->spec_size,
                (unsigned long)self->task->obj_size);


    /* TRN body */
    err = knd_append_file((const char*)out->buf,
                          (const void*)self->task->spec,
                          self->task->spec_size);
    if (err) return err;

    err = out->rtrim(out, strlen(".spec"));
    if (err) return err;
    err = out->write(out, ".obj", strlen(".obj"));
    if (err) return err;

    err = knd_append_file((const char*)out->buf,
                          (const void*)self->task->obj,
                          self->task->obj_size);
    if (err) return err;

    return knd_OK;
}

static gsl_err_t
kndRepo_parse_class(void *obj,
                    const char *rec,
                    size_t *total_size)
{
    struct kndRepo *self, *repo;
    gsl_err_t err;

    self = (struct kndRepo*)obj;

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("   .. parsing the CLASS rec: \"%s\" REPO: %s\n\n", rec, self->name);

    if (!self->curr_repo) {
        knd_log("-- no repo selected :(");
        return make_gsl_err(gsl_FAIL);
    }

    repo = self->curr_repo;
    repo->out = self->task->out;

    struct gslTaskSpec specs[] = {
        { .name = "obj",
          .name_size = strlen("obj")
        }
    };

    if (DEBUG_REPO_LEVEL_1)
        knd_log("   .. parsing the CLASS rec: \"%s\" CURR REPO: %s",
                rec, repo->name);

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("-- failed to parse the CLASS rec: %d", err);
        return err;
    }

    /* call default action */

    return make_gsl_err(gsl_OK);
}


static int
kndRepo_parse_task(void *self,
                   const char *rec,
                   size_t *total_size)
{
    struct gslTaskSpec add_specs[] = {
        { .name = "n",
          .name_size = strlen("n")
        }
    };

    struct gslTaskSpec specs[] = {
        { .name = "add",
          .name_size = strlen("add"),
          .specs = add_specs,
          .num_specs = sizeof add_specs / sizeof add_specs[0],
          .run = kndRepo_run_add_repo,
          .obj = self
        },
        { .name = "n",
          .name_size = strlen("n"),
          .run = kndRepo_run_get_repo,
          .obj = self
        },
        { .name = "class",
          .name_size = strlen("class"),
          .parse = kndRepo_parse_class,
          .obj = self
        }
    };
    gsl_err_t err;

    err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (err.code) return knd_FAIL;  // FIXME(ki.stfu): convert gsl_err_t to knd_err_codes

    return knd_OK;
}


static int
kndRepo_read_state(struct kndRepo *self,
                   const char *rec, size_t *total_size)
{
    const char *b, *c;
    size_t buf_size;

    bool in_val = false;
    bool in_last_id = false;

    c = rec;
    b = rec;

    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. Repo parsing curr state");

    while (*c) {
        switch (*c) {
        case ' ':
        case '\n':
        case '\r':
        case '\t':
            if (in_val) {
                b = c + 1;
                break;
            }

            if (!strncmp(b, "last", strlen("last"))) {
                in_val = true;
                in_last_id = true;
                b = c + 1;
                break;
            }

            b = c + 1;
            break;
        case '}':

            if (in_last_id) {
                /**c = '\0';*/

                buf_size = c - b;
                if (buf_size != KND_ID_SIZE) return knd_FAIL;

                memcpy(self->last_id, b, buf_size);
                self->last_id[buf_size] = '\0';

                if (DEBUG_REPO_LEVEL_2)
                    knd_log("== LAST REPO ID: %s", self->last_id);

                in_last_id = false;
                in_val = false;
                b = c + 1;
                break;
            }

            *total_size = c - rec;

            return knd_OK;
        default:
            break;
        }

        c++;
    }

    return knd_FAIL;
}


extern int
kndRepo_init(struct kndRepo *self)
{
    self->del = kndRepo_del;
    self->str = kndRepo_str;

    self->read_state = kndRepo_read_state;
    self->parse_task = kndRepo_parse_task;
    self->open = kndRepo_open;
    self->restore = kndRepo_restore;

    return knd_OK;
}

extern int
kndRepo_new(struct kndRepo **repo)
{
    struct kndRepo *self;
    int err = knd_OK;

    self = malloc(sizeof(struct kndRepo));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndRepo));
    memset(self->id, '0', KND_ID_SIZE);

    memset(self->last_id, '0', KND_ID_SIZE);
    memset(self->last_db_state, '0', KND_ID_SIZE);
    memset(self->db_state, '0', KND_ID_SIZE);

    //self->intersect_matrix_size = sizeof(struct kndObject*) * (KND_ID_BASE * KND_ID_BASE * KND_ID_BASE);

    /*err = ooDict_new(&self->repo_idx, KND_SMALL_DICT_SIZE);
    if (err) return knd_NOMEM;

    err = kndOutput_new(&self->path_out, KND_MED_BUF_SIZE);
    if (err) return err;
    */
    kndRepo_init(self);

    *repo = self;

    return knd_OK;
}
