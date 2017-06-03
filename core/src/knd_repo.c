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

#define DEBUG_REPO_LEVEL_0 0
#define DEBUG_REPO_LEVEL_1 0
#define DEBUG_REPO_LEVEL_2 0
#define DEBUG_REPO_LEVEL_3 0
#define DEBUG_REPO_LEVEL_TMP 1

static int
kndRepo_linearize_objs(struct kndRepo *self);

static int
kndRepo_get_guid(struct kndRepo *self,
                 struct kndDataClass *dc,
                 const char *obj_name,
                 size_t      obj_name_size,
                 char *result);

static int
kndRepo_get_cache(struct kndRepo *self, struct kndDataClass *c,
                  struct kndRepoCache **result);

static int
kndRepo_import_obj(struct kndRepo *self,
                   const char *rec,
                   size_t *total_size);
static int
kndRepo_get_obj(struct kndRepo *self,
                const char *name,
                size_t name_size);

static int 
kndRepo_export(struct kndRepo *self,
                knd_format format);

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

                if (DEBUG_REPO_LEVEL_TMP)
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
    
    memcpy(buf, self->user->dbpath, self->user->dbpath_size);
    buf_size = self->user->dbpath_size;

    if (buf_size + repo_dir_size >= KND_TEMP_BUF_SIZE)
        return knd_LIMIT;

    memcpy(buf + buf_size, repo_dir, repo_dir_size);
    buf_size += repo_dir_size;
    buf[buf_size] = '\0';

    /* TODO: check overflow */
    
    err = knd_make_id_path(self->path, buf, self->id, "repo.gsl");
    if (err) return err;

    self->path_size = strlen(self->path);
    
    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("..opening repo:  ID:\"%s\" REPO FILE:%s",
                self->id, self->path);

    self->out->reset(self->out);
    err = self->out->read_file(self->out,
                               (const char*)self->path, self->path_size);
    if (err) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("-- failed to open repo: \"%s\" :(",
                    self->path);
        return err;
    }

    self->out->file[self->out->file_size] = '\0';

    buf_size = self->path_size - strlen("repo.gsl");
    self->path[buf_size] = '\0';
    self->path_size = buf_size;
    
    if (self->restore_mode) {
        err = kndRepo_parse_config(self, self->out->file, &chunk_size);
        if (err) return err;

        if (!self->name_size) {
            knd_log("-- repo %s full name is not set :(",
                    self->id);
            return knd_FAIL;
        }
    }

    if (DEBUG_REPO_LEVEL_TMP)
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
    memcpy(buf, name, name_size);
    buf[name_size] = '\0';
    repo = self->repo_idx->get(self->repo_idx,
                               buf);
    if (repo) {
        knd_log("-- \"%s\" REPO name already taken?", buf);
        return knd_FAIL;
    }

    err = kndRepo_new(&repo);
    if (err) goto final;
    repo->user = self->user;
    repo->out = self->out;
    
    /* get new repo id */
    memcpy(repo->id, self->last_id, KND_ID_SIZE);
    repo->id[KND_ID_SIZE] = '\0';
    knd_inc_id(repo->id);

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

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("  .. create new repo:  ID:\"%s\"  N:%.*s  PATH:%s",
                repo->id, repo->name_size, repo->name, path);
    
    /* TODO: check if DIR already exists */
    err = knd_mkpath(path, 0755, false);
    if (err) goto final;

    path_size = strlen(path);
    memcpy(self->path, path, path_size);
    self->path_size = path_size;
    self->path[path_size] = '\0';
    
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
 
    err = kndRepo_export(repo, KND_FORMAT_GSL);
    if (err) goto final;

    err = knd_write_file((const char*)path,
                         "repo.gsl",
                         repo->out->buf, repo->out->buf_size);
    if (err) goto final;

    err = self->repo_idx->set(self->repo_idx,
                              repo->name,
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


static int
kndRepo_run_add_repo(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndRepo *repo;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;
    int err;
    
    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "n", strlen("n"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }

    if (!name_size) return knd_FAIL;

    repo = (struct kndRepo*)obj;
    
    err = kndRepo_add_repo(repo, name, name_size);
    if (err) return err;

    return knd_OK;
}



static int
kndRepo_run_get_repo(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndRepo *repo, *curr_repo;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;
    
    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "n", strlen("n"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }

    if (!name_size) return knd_FAIL;

    repo = (struct kndRepo*)obj;
    repo->curr_repo = NULL;
    
    curr_repo = repo->repo_idx->get(repo->repo_idx,
                                    name);
    if (!curr_repo) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("-- no such repo: \"%s\" :(", name);
        return knd_FAIL;
    }

    repo->curr_repo = curr_repo;
    
    return knd_OK;
}

static int
kndRepo_run_get_class_cache(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndRepo *self;
    struct ooDict *idx;
    struct kndDataClass *dc;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t name_size = 0;
    int err;

    self = (struct kndRepo *)obj;
    self->curr_cache = NULL;
    
    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "n", strlen("n"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }

    if (!name_size) return knd_FAIL;

    /* check classname */
    idx = self->user->root_dc->class_idx;
    dc = (struct kndDataClass*)idx->get(idx,
                                        name);
    if (!dc) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("   -- classname \"%s\" is not valid :(\n", name);
        return knd_FAIL;
    }
    self->curr_class = dc;

    err = kndRepo_get_cache(self, dc, &self->curr_cache);
    if (err) return err;

    if (DEBUG_REPO_LEVEL_2)
        knd_log("++ got class cache: \"%s\"!", dc->name);

    return knd_OK;
}


static int
kndRepo_run_get_obj(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndRepo *self;
    struct kndTaskArg *arg;
    const char *name = NULL;
    //size_t chunk_size = 0;
    size_t name_size = 0;
    int err;
    
    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "get", strlen("get"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    if (!name_size) return knd_FAIL;

    self = (struct kndRepo*)obj;

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log(".. repo %s to get OBJ: \"%s\"", self->name, name);
    
    err = kndRepo_get_obj(self, name, name_size);
    if (err) return err;
    
    return knd_OK;
}


static int
kndRepo_run_import_obj(void *obj, struct kndTaskArg *args, size_t num_args)
{
    struct kndRepo *self;
    struct kndTaskArg *arg;
    const char *name = NULL;
    size_t chunk_size = 0;
    size_t name_size = 0;
    int err;
    
    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "import", strlen("import"))) {
            name = arg->val;
            name_size = arg->val_size;
        }
    }
    if (!name_size) return knd_FAIL;

    self = (struct kndRepo*)obj;

    /* obj from separate msg */
    if (!strncmp(name, "_obj", strlen("_obj"))) {
        if (DEBUG_REPO_LEVEL_2)
            knd_log("   .. IMPORT OBJ: \"%s\"", self->task->obj);

        err = kndRepo_import_obj(self, self->task->obj, &chunk_size);
        if (err) return err;
        return knd_OK;
    }
    
    return knd_FAIL;
}


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


static int
kndRepo_restore(struct kndRepo *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    const char *inbox_filename = "inbox/import.data";
    int err;
    
    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("  .. restoring repo \"%s\".. PATH: %s [%lu]",
                self->id, self->path, (unsigned long)self->path_size);

    memcpy(buf, self->path, self->path_size);
    memcpy(buf + self->path_size, inbox_filename, strlen(inbox_filename));
    buf_size = self->path_size + strlen(inbox_filename);
    buf[buf_size] = '\0';

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("  .. try importing recs from \"%s\"..",
                buf);

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
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    const char *inbox = "/inbox/import.data";
    size_t inbox_size = strlen(inbox);
    int err;

    memcpy(buf, self->path, self->path_size);
    buf_size = self->path_size;

    memcpy(buf + buf_size, inbox, inbox_size);
    buf_size += inbox_size;
    buf[buf_size] = '\0';
    
    if (DEBUG_REPO_LEVEL_TMP)
        knd_log(".. update INBOX \"%s\" SPEC: %s [%lu]\nOBJ REC: \"%s\"\n",
                buf, self->task->spec, (unsigned long)self->task->spec_size,
                self->task->obj);

    err = knd_append_file((const char*)buf,
                          (const void*)self->task->spec,
                          self->task->spec_size);
    if (err) return err;
    
    err = knd_append_file((const char*)buf,
                          (const void*)self->task->obj,
                          self->task->obj_size);
    if (err) return err;
    
    return knd_OK;
}


static int
kndRepo_index_obj(struct kndRepo *self,
                  struct kndObject *obj)
{
    struct kndRepoCache *cache = self->curr_cache;
    struct kndRefSet *refset;
    struct kndObjRef *objref;
    struct kndSortTag *tag = NULL;
    struct kndSortAttr *a = NULL;
    int err;

    refset = cache->name_idx;

    /* save as obj id */
    err = kndObjRef_new(&objref);
    if (err) return err;

    objref->obj = obj;
    memcpy(objref->obj_id, obj->id, KND_ID_SIZE);
    objref->obj_id_size = KND_ID_SIZE;
    
    memcpy(objref->name, obj->name, obj->name_size);
    objref->name_size = obj->name_size;
            
    err = refset->term_idx(refset, objref);
    if (err) {
        knd_log("  -- objref %s::%s not added to refset :(\n",
                objref->obj_id, objref->name);
        goto final;
    }

    /* register in the NAME IDX */
    err = kndObjRef_new(&objref);
    if (err) goto final;
        
    memcpy(objref->obj_id, obj->id, KND_ID_SIZE);
    objref->obj_id_size = KND_ID_SIZE;
    memcpy(objref->name, obj->name, obj->name_size);
    objref->name_size = obj->name_size;

    err = kndSortTag_new(&tag);
    if (err) goto final;

    a = malloc(sizeof(struct kndSortAttr));
    if (!a)  {
        err = knd_NOMEM;
        goto final;
    }
    memset(a, 0, sizeof(struct kndSortAttr));
    a->type = KND_FACET_POSITIONAL;
    
    a->name_size = strlen("AZ");
    memcpy(a->name, "AZ", a->name_size);
            
    memcpy(a->val, obj->name, obj->name_size);
    a->val_size = obj->name_size;

    tag->attrs[tag->num_attrs] = a;
    tag->num_attrs++;

    objref->sorttag = tag;

    err = refset->add_ref(refset, objref);
    if (err) {
        knd_log("-- ref %s not added to refset :(\n", objref->obj_id);
        goto final;
    }
            
    /*err = obj->sync(obj);
    if (err) {
        knd_log("  -- sync failure of OBJ %s::%s :(\n",
                obj->id, obj->name);
        goto final;
    }
    */

    return knd_OK;
    
 final:
    if (objref) objref->del(objref);
    if (tag) tag->del(tag);
    if (a) free(a);
    
    return err;
}


static int
kndRepo_import_obj(struct kndRepo *self,
                   const char *rec,
                   size_t *total_size)
{
    struct kndRepoCache *cache = self->curr_cache;
    struct kndObject *obj;
    size_t chunk_size = 0;
    int err;

    err = kndObject_new(&obj);
    if (err) return err;
    obj->cache = cache;
    obj->out = self->out;

    memcpy(obj->id, cache->obj_last_id, KND_ID_SIZE);
    knd_inc_id(obj->id);

    err = obj->import(obj, rec, &chunk_size, KND_FORMAT_GSL);
    if (err) goto final;

    err = cache->db->set(cache->db, obj->id, (void*)obj);
    if (err) goto final;

    /*err = cache->obj_idx->set(cache->obj_idx, obj->name, (void*)obj);
    if (err) goto final;
    */
    
    if (self->restore_mode) {
        knd_log("  ++ Repo %s: restored OBJ %s::%s import OK [total objs: %lu]\n",
                self->name,
                cache->baseclass->name, obj->id,
                (unsigned long)cache->num_objs + 1);

        /* assign new last_id */
        memcpy(cache->obj_last_id, obj->id, KND_ID_SIZE);
        cache->num_objs++;

        *total_size = chunk_size;
        return knd_OK;
    }
    
    /* NB: append rec to the backup file */
    if (!self->batch_mode) {
        err = kndRepo_update_inbox(self);
        if (err) {
            knd_log("-- inbox update failed :(");
            goto final;
        }

        err = kndRepo_index_obj(self, obj);
        if (err) {
            knd_log("-- \"%s\" obj indexing failure :(", obj->name);
            goto final;
        }

        err = kndRepo_linearize_objs(self);
        if (err) goto final;

    }
    
    /* TODO: update repo state */
    
    /* import success */
    knd_log("  ++ Repo %s: OBJ %s::%s import OK [total objs: %lu]\n",
            self->name,
            cache->baseclass->name, obj->id,
            (unsigned long)cache->num_objs + 1);

    /* assign new last_id */
    memcpy(cache->obj_last_id, obj->id, KND_ID_SIZE);
    cache->num_objs++;

    *total_size = chunk_size;
    
    return knd_OK;
    
 final:
    obj->del(obj);
    
    return err;        
}


static int
kndRepo_read_db_chunk(struct kndRepo *self,
                      struct kndRepoCache *cache,
                      const char *rec,
                      size_t rec_size,
                      const char *obj_id,
                      struct kndObject **result)
{
    char buf[KND_MED_BUF_SIZE];
    size_t buf_size;

    unsigned char dir[KND_ID_BASE * KND_MAX_INT_SIZE];
    size_t dir_size = KND_ID_BASE * KND_MAX_INT_SIZE;

    struct kndObject *obj;
    
    unsigned long start_offset = 0;
    unsigned long offset = 0;
    int numval;
    
    const char *s = NULL;

    unsigned char *c;
    int err;

    memset(dir, 0, dir_size);
    memcpy(dir, rec + (rec_size - dir_size), dir_size);

    numval = obj_id_base[(size_t)*obj_id];

    if (DEBUG_REPO_LEVEL_3)
        knd_log(".. ID char: \"%c\" NUM VAL %d\n", (*obj_id), numval);
    
    if (numval == -1) {
        knd_log("-- wrong obj id value :(\n");
        return knd_FAIL;
    }

    c = dir + (numval * KND_MAX_INT_SIZE);

    start_offset = knd_unpack_int(c);
    /*if (pref_size)
      start_offset += pref_size; */

    if (numval >= KND_ID_BASE) return knd_LIMIT;

    /* get the next field */
    c = dir + ((numval + 1) * KND_MAX_INT_SIZE);
    offset = knd_unpack_int(c);
    if (!offset) {
        
        offset = rec_size - dir_size;
    }

    if (start_offset >= offset) {
        knd_log("  -- start offset is greater or equal..\n");
        return knd_FAIL;
    }

    if (DEBUG_REPO_LEVEL_2)
        knd_log("   == DB CHUNK SIZE:  %lu\n", (unsigned long)rec_size);

    if (DEBUG_REPO_LEVEL_2)
        knd_log("  == start offset: %lu next offset: %lu\n",
                (unsigned long)start_offset, (unsigned long)offset);

    
    obj_id++;

    /* read nested dir */
    if (*obj_id) {
        return kndRepo_read_db_chunk(self,
                                     cache,
                                     rec + start_offset,
                                     offset - start_offset,
                                     obj_id, result);
    }

    
    if (DEBUG_REPO_LEVEL_3)
        knd_log("  .. read terminal chunk from %lu to %lu..\n",
                (unsigned long)start_offset, (unsigned long)offset);

    if (start_offset >= rec_size) {
        knd_log("  -- offset overflow? :(\n");
        return knd_FAIL;
    }
    
    buf_size = offset - start_offset;
    if (buf_size >= KND_MED_BUF_SIZE) return knd_LIMIT;

    s = rec + start_offset;
    
    memcpy(buf, s, buf_size);
    buf[buf_size] = '\0';
    
    if (DEBUG_REPO_LEVEL_3)
        knd_log("  ++ OBJ REC: \"%s\" [SIZE: %lu]\n\n",
                buf, (unsigned long)buf_size);

    err = kndObject_new(&obj);
    if (err) return err;

    obj->cache = cache;
    obj->out = self->out;
    obj->out->reset(obj->out);
   
    err = obj->parse(obj, buf, buf_size, KND_FORMAT_GSC);
    if (err) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("    -- %s obj parse error: %d\n",
                    obj->name, err);
        return err;
    }
    
    /*obj->str(obj, 0);*/
    
    *result = obj;
    
    return knd_OK;
}





static int
kndRepo_read_obj_db(struct kndRepo *self,
                    const char *guid,
                    struct kndRepoCache *cache,
                    struct kndObject **result)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    struct kndObject *obj = NULL;
    int err;

    err = knd_FAIL;

    buf_size = sprintf(buf, "%s/%s/objs.gsc",
                       self->path, cache->baseclass->name);

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("  .. open OBJ db file: \"%s\" ..\n", buf);
    
    /* TODO: large file support */
    err = self->out->read_file(self->out,
                               (const char*)buf, buf_size);
    if (err) {
        knd_log("   -- no DB read for \"%s\" :(\n",
                cache->baseclass->name);
        return knd_FAIL;
    }

    if (DEBUG_REPO_LEVEL_3)
        knd_log("  == DB file size: %lu\n",
                (unsigned long)self->out->file_size);
    
    err = kndRepo_read_db_chunk(self,
                                cache,
                                self->out->file,
                                self->out->file_size,
                                guid, &obj);
    if (err) return err;

    obj->cache = cache;
    memcpy(obj->id, guid, KND_ID_SIZE);
    
    *result = obj;

    return knd_OK;
}


static int
kndRepo_get_obj(struct kndRepo *self,
                const char *name,
                size_t name_size)
{
    struct kndRepoCache *cache = self->curr_cache;
    struct kndDataClass *dc = cache->baseclass;
    
    char guid[KND_ID_SIZE + 1] = {0};
    struct kndObject *obj = NULL;

    knd_format format;
    size_t curr_depth = 0;
    int err = knd_FAIL;

    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. repo GET in progress..");

    err = kndRepo_get_guid(self, dc,
                           name, name_size,
                           guid);
    if (err) {
        knd_log("-- \"%s\" name not recognized :(", name);
        return knd_FAIL;
    }
    guid[KND_ID_SIZE] = '\0';

    /* get obj by name */
    if (DEBUG_REPO_LEVEL_2)
        knd_log("   ?? get browser state obj \"%s\" (depth: %lu)\n",
                name, (unsigned long)curr_depth);

    obj = (struct kndObject*)cache->db->get(cache->db,
                                            (const char*)guid);
    if (!obj) {
        if (DEBUG_REPO_LEVEL_2)
            knd_log("   -- no obj in cache..\n");
        err = kndRepo_read_obj_db(self,
                                  (const char*)guid,
                                  cache,
                                  &obj);
        if (err) return err;
    }

    if (DEBUG_REPO_LEVEL_TMP)
        obj->str(obj, 1);
    
    if (curr_depth) {
        obj->export_depth = curr_depth;

        if (DEBUG_REPO_LEVEL_TMP)
            knd_log(" .. expanding obj %s [%s]..\n", obj->name, obj->id);

        if (!obj->is_expanded) {
            err = obj->expand(obj, 0);
            if (err) return err;
        }
    }
    
    self->out = self->out;
    self->out->reset(self->out);
    
    
    /* export obj */
    format = KND_FORMAT_JSON;
    obj->out = self->out;
    obj->export_depth = 0;
    
    err = obj->export(obj, format, 0);
    if (err) return err;
    
    return knd_OK;
}






static int
kndRepo_linearize_objs(struct kndRepo *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    struct kndRepoCache *cache = self->curr_cache;
    struct kndRefSet *refset = cache->name_idx;
    int err;
    
    /* TODO: update state */

    /* write OBJS to filesystem */
    buf_size = sprintf(buf, "%s/%s/", self->path, cache->baseclass->name);
    err = knd_mkpath(buf, 0755, false);
    if (err) {
        return err;
    }

    /* task provides space for the output */
    /* TODO: write chunks directly to disk */
    
    refset->out = self->task->out;
    refset->out->reset(refset->out);

    knd_log("== refset output buf size: %lu total: %lu",
            (unsigned long)refset->out->buf_size, refset->out->max_size);

    err = refset->sync_objs(refset, (const char*)buf);
    if (err) {
        knd_log("-- refset failed to sync objs :(");
        return err;
    }

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("  ++ sync objs of \"%s\" OK!\n", cache->baseclass->name);

    return knd_OK;
}



static int 
kndRepo_export_class_JSON(struct kndRepo *self, struct kndRepoCache *cache)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    struct kndDataClass *dc;
    struct kndOutput *out;
    struct kndObject *obj;

    int err;

    out = self->out;

    dc = cache->baseclass;

    err = out->write(out, "{", 1);
    if (err) return err;

    buf_size = sprintf(buf, "\"n\":\"%s\"", dc->name);
    err = out->write(out, buf, buf_size);
    if (err) return err;

    if (cache->select_result) {
        cache->select_result->out = out;
        cache->select_result->export_depth = 1;

        err = out->write(out, ",\"browser\":", strlen(",\"browser\":"));
        if (err) return err;

        /* TODO: define depth elsewhere */
        cache->select_result->export_depth = 1;

        err = cache->select_result->export(cache->select_result, KND_FORMAT_JSON, 0);
        if (err) return err;

        if (cache->num_matches) {

            if (DEBUG_REPO_LEVEL_3) 
                knd_log("\n\n .. export NUM MATCHES: %lu\n",
                        (unsigned long)cache->num_matches);

            err = out->write(out, ",\"objs\":[", strlen(",\"objs\":["));
            if (err) return err;

            for (size_t i = 0; i < cache->num_matches; i++) {
                obj = cache->matches[i];
                if (!obj) break;

                obj->out = self->out;

                if (i) {
                    err = out->write(out, ",", 1);
                    if (err) return err;
                }

                err = obj->export(obj, KND_FORMAT_JSON, 1);
                if (err) return err;
            }

            err = out->write(out, "]", 1);
            if (err) return err;

        }

    } else {
        if (cache->browser->num_refs) {
            err = out->write(out, ",\"browser\":", strlen(",\"browser\":"));
            if (err) return err;

            cache->browser->out = out;

            /* TODO: define depth elsewhere */
            cache->browser->export_depth = 2;

            err = cache->browser->export(cache->browser, KND_FORMAT_JSON, 0);
            if (err) return err;
        }

    }

    err = out->write(out, "}", 1);
    if (err) return err;

    /*knd_log("\n  == FINAL OUTPUT: %s\n\n", out->buf);*/

    return knd_OK;
}

static int 
kndRepo_export_JSON(struct kndRepo *self)
{
    //char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size;
    
    struct kndRepoCache *cache;
    //struct kndDataClass *dc;
    struct kndOutput *out;
    //struct kndObject *obj;
    
    int err;

    out = self->out;

    err = out->write(out, "{", 1);
    if (err) return err;

    err = out->write(out, "\"n\":\"", strlen("\"n\":\""));
    if (err) return err;
    
    err = out->write(out,
                     self->name, self->name_size);
    if (err) return err;

    err = out->write(out, "\"", 1);
    if (err) return err;

    err = out->write(out, ",\"c_l\":[", strlen(",\"c_l\":["));
    if (err) return err;
    
    /* export ALL classes */
    if (!self->curr_class) {
        cache = self->cache;
        while (cache) {
            err = kndRepo_export_class_JSON(self, cache);
            if (err) return err;
            
            /* separator */
            if (cache->next) {
                err = out->write(out, ",", 1);
                if (err) return err;
            }
            cache = cache->next;
        }
    }
    else {
        /* export just the selected class */
        err = kndRepo_get_cache(self, self->curr_class, &cache);
        if (err) return err;

        err = kndRepo_export_class_JSON(self, cache);
        if (err) return err;
    }

    
    err = out->write(out, "]", 1);
    if (err) return err;

    err = out->write(out, "}", 1);
    if (err) return err;
    
    return err;
}



static int 
kndRepo_export_class_HTML(struct kndRepo *self, struct kndRepoCache *cache)
{
    //char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size;
    
    struct kndDataClass *dc;
    struct kndOutput *out;
    struct kndObject *obj;
    
    int err;

    out = self->out;
    dc = cache->baseclass;

    if (cache->select_result) {

        cache->select_result->out = out;

        /* TODO: define depth elsewhere */
        cache->select_result->export_depth = 1;

        err = cache->select_result->export(cache->select_result, KND_FORMAT_HTML, 0);
        if (err) return err;

        if (cache->num_matches) {
            err = out->write(out, "<UL>", strlen("<UL>"));
            if (err) return err;

            for (size_t i = 0; i < cache->num_matches; i++) {
                obj = cache->matches[i];
                if (!obj) break;

                obj->out = self->out;

                err = out->write(out, "<LI>", strlen("<LI>"));
                if (err) return err;

                err = obj->export(obj, KND_FORMAT_HTML, 1);
                if (err) return err;

                err = out->write(out, "</LI>", strlen("</LI>"));
                if (err) return err;
            }

            err = out->write(out, "</UL>", strlen("</UL>"));
            if (err) return err;
        }


        /* add JS */

        err = self->out->write(self->out, "<script type=\"text/javascript\">", strlen("<script type=\"text/javascript\">"));
        if (err) return err;

        err = self->out->write(self->out, "oo_init.select = ", strlen("oo_init.select = "));
        if (err) return err;

        /*err = self->out->write(self->out,
                               "{\"baseclass\":",
                               strlen("{\"baseclass\":"));
        if (err) return err;
        */

        err = kndRepo_export_class_JSON(self, cache);
        if (err) return err;

        err = self->out->write(self->out, "</script>", strlen("</script>"));

    }
    else {
        if (cache->browser->num_refs) {

            cache->browser->out = out;
            /* TODO: define depth elsewhere */
            cache->browser->export_depth = 1;

            err = cache->browser->export(cache->browser, KND_FORMAT_HTML, 0);
            if (err) return err;
        }

    }


    return knd_OK;
}


static int 
kndRepo_export_HTML(struct kndRepo *self)
{
    //char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size;
    
    struct kndRepoCache *cache;
    //struct kndDataClass *dc;
    struct kndOutput *out;
    //struct kndObject *obj;
    
    int err;

    out = self->out;
    
    /* export ALL classes */
    if (!self->curr_class) {

        cache = self->cache;
        while (cache) {
            err = kndRepo_export_class_HTML(self, cache);
            if (err) return err;
            
            cache = cache->next;
        }
    }
    else {
        /* export just the selected class */
        err = kndRepo_get_cache(self, self->curr_class, &cache);
        if (err) return err;

        err = kndRepo_export_class_HTML(self, cache);
        if (err) return err;
    }
    
    return err;
}

static int 
kndRepo_export_GSL(struct kndRepo *self)
{
    struct kndOutput *out = self->out;
    int err;

    err = out->write(out, "{", 1);
    if (err) return err;

    err = out->write(out, "{ID ", strlen("{ID "));
    if (err) return err;

    err = out->write(out, self->id, KND_ID_SIZE);
    if (err) return err;
    
    err = out->write(out, "}", 1);
    if (err) return err;

    err = out->write(out, "{N ", strlen("{N "));
    if (err) return err;

    err = out->write(out, self->name, self->name_size);
    if (err) return err;
    
    err = out->write(out, "}", 1);
    if (err) return err;

    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}


static int
kndRepo_get_cache(struct kndRepo *self,
                  struct kndDataClass *dc,
                  struct kndRepoCache **result)
{
    struct kndRepoCache *cache;
    int err;
    
    cache = self->cache;
    while (cache) {
        if (cache->baseclass == dc) {
            *result = cache;
            return knd_OK;
        }
        cache = cache->next;
    }

    /* no cache found */
    cache  = malloc(sizeof(struct kndRepoCache));
    if (!cache) return knd_NOMEM;
    memset(cache, 0, sizeof(struct kndRepoCache));

    cache->baseclass = dc;
    cache->repo = self;

    err = ooDict_new(&cache->db, KND_MEDIUM_DICT_SIZE);
    if (err) goto final;

    /*err = ooDict_new(&cache->obj_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto final;
    */
    
    err = kndRefSet_new(&cache->browser);
    if (err) goto final;
    cache->browser->name[0] = '/';
    cache->browser->name_size = 1;
    cache->browser->cache = cache;
    cache->browser->out = self->out;

    err = kndRefSet_new(&cache->name_idx);
    if (err) goto final;
    memcpy(cache->name_idx->name, "AZ", strlen("AZ"));
    cache->name_idx->name_size = strlen("AZ");
    cache->name_idx->cache = cache;
    cache->name_idx->out = self->out;

    /* root query */
    err = kndQuery_new(&cache->query);
    if (err) return err;
    memcpy(cache->query->facet_name, "/", 1);
    cache->query->facet_name_size = 1;
    cache->query->cache = cache;
    
    memset(cache->obj_last_id, '0', KND_ID_SIZE);

    cache->next = self->cache;
    self->cache = cache;

    *result = cache;
    return knd_OK;
    
 final:
    return err;
}


static int
kndRepo_get_guid(struct kndRepo *self,
                 struct kndDataClass *dc,
                 const char *name,
                 size_t      name_size,
                 char *result)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    struct kndRepoCache *cache;
    struct kndRefSet *refset;
    int err;
    
    if (DEBUG_REPO_LEVEL_TMP)
        knd_log(".. get guid of %s::%s..",
                dc->name, name);
    
    err = kndRepo_get_cache(self, dc, &cache);
    if (err) return err;

    /* read name IDX */
    if (!cache->name_idx) {
        buf_size = sprintf(buf, "%s/%s/AZ.idx",
                           self->path,
                           dc->name);

        if (DEBUG_REPO_LEVEL_TMP)
            knd_log(".. reading name IDX file: \"%s\" ..",
                    buf);

        self->out = self->out;
        self->out->reset(self->out);
    
        err = self->out->read_file(self->out,
                                   (const char*)buf, buf_size);
        if (err) {
            if (DEBUG_REPO_LEVEL_TMP)
                knd_log("-- no IDX DB found: \"%s\" :(",
                        buf);
            return knd_FAIL;
        }
    
        if (DEBUG_REPO_LEVEL_3)
            knd_log("\n\n   ++  atom IDX DB rec size: %lu\n",
                    (unsigned long)self->out->file_size);

        err = kndRefSet_new(&refset);
        if (err) return err;
        refset->export_depth = 0;
    
        err = refset->read(refset,
                           self->out->file,
                           self->out->file_size);
        if (err) {
            if (DEBUG_REPO_LEVEL_TMP)
                knd_log("    -- name IDX refset not parsed :(\n");
            return err;
        }
        refset->cache = cache;
        cache->name_idx = refset;
    }

    err = cache->name_idx->lookup_name(cache->name_idx,
                                       name, name_size,
                                       name, name_size, result);
    if (err) {
        knd_log("-- obj name \"%s\" not recognized :(", name);
        return knd_NO_MATCH;
    }
    
    return knd_OK;
}


static int 
kndRepo_export(struct kndRepo *self, knd_format format)
{
    int err = knd_FAIL;
    
    switch (format) {
    case KND_FORMAT_JSON:
        err = kndRepo_export_JSON(self);
        if (err) goto final;
        break;
    case KND_FORMAT_HTML:
        err = kndRepo_export_HTML(self);
        if (err) goto final;
        break;
    case KND_FORMAT_GSL:
        err = kndRepo_export_GSL(self);
        if (err) goto final;
        break;
    default:
        break;
    }
 final:
    return err;
}


static int
kndRepo_parse_add_repo(void *self,
                       const char *rec,
                       size_t *total_size)
{
    struct kndTaskSpec specs[] = {
        { .name = "n",
          .name_size = strlen("n"),
          .run = kndRepo_run_add_repo,
          .obj = self
        }
    };
    int err;
    
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}

static int
kndRepo_parse_class(void *obj,
                    const char *rec,
                    size_t *total_size)
{
    struct kndTaskSpec specs[] = {
        { .name = "n",
          .name_size = strlen("n"),
          .run = kndRepo_run_get_class_cache,
          .obj = obj
        },
        { .name = "import",
          .name_size = strlen("import"),
          .run = kndRepo_run_import_obj,
          .obj = obj
        },
        { .name = "get",
          .name_size = strlen("get"),
          .run = kndRepo_run_get_obj,
          .obj = obj
        }
    };
    int err;

    if (DEBUG_REPO_LEVEL_2)
        knd_log("   .. parsing the CLASS rec: \"%s\"", rec);
    
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
    return knd_OK;
}


static int
kndRepo_parse_task(void *self,
                   const char *rec,
                   size_t *total_size)
{
    struct kndTaskSpec specs[] = {
        { .name = "add",
          .name_size = strlen("add"),
          .parse = kndRepo_parse_add_repo,
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
    int err;
    
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) return err;
    
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
                
                if (DEBUG_REPO_LEVEL_TMP)
                    knd_log("LAST REPO ID: %s", self->last_id);

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
    
    self->export = kndRepo_export;
    
    self->restore = kndRepo_restore;
    //self->sync = kndRepo_batch_sync;
    self->get_cache = kndRepo_get_cache;
    self->get_guid = kndRepo_get_guid;
    
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

    self->intersect_matrix_size = sizeof(struct kndObject*) * (KND_ID_BASE * KND_ID_BASE * KND_ID_BASE);

    err = ooDict_new(&self->repo_idx, KND_SMALL_DICT_SIZE);
    if (err) return knd_NOMEM;

    kndRepo_init(self);

    *repo = self;

    return knd_OK;
}
