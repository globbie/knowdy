#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_repo.h"
#include "knd_policy.h"
#include "knd_attr.h"
#include "knd_refset.h"
#include "knd_user.h"
#include "knd_query.h"
#include "knd_sorttag.h"
#include "knd_spec.h"
#include "knd_output.h"
#include "knd_msg.h"
#include "knd_dict.h"

#include "knd_data_writer.h"
#include "knd_data_reader.h"

#define DEBUG_REPO_LEVEL_0 0
#define DEBUG_REPO_LEVEL_1 0
#define DEBUG_REPO_LEVEL_2 0
#define DEBUG_REPO_LEVEL_3 0
#define DEBUG_REPO_LEVEL_TMP 1

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
kndRepo_open(struct kndRepo *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size;

    char path[KND_TEMP_BUF_SIZE];
    //size_t path_size;
    int err;
    
    sprintf(buf, "%s/repos", self->user->path);
    err = knd_make_id_path(path, buf, self->id, "repo.gsl");
    if (err) return err;

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("..opening repo:  ID:\"%s\" PATH:%s",
                self->id, path);

    self->out->reset(self->out);
    err = self->out->read_file(self->out,
                               (const char*)path, strlen(path));
    if (err) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("   -- failed to open repo: \"%s\" :(",
                    path);
        return err;
    }

    self->out->file[self->out->file_size] = '\0';
    
    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("++ REPO open success: \"%s\"",
                self->out->file);
    
    return knd_OK;
}

static int
kndRepo_add_repo(struct kndRepo *self, struct kndSpecArg *args, size_t num_args)
{
    char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size;

    char path[KND_TEMP_BUF_SIZE];
    //size_t path_size;

    struct kndSpecArg *arg;
    int err;

    memcpy(self->id, self->last_id, KND_ID_SIZE);
    self->id[KND_ID_SIZE] = '\0';
    knd_inc_id(self->id);

    self->name_size = 0;

    self->out = self->user->writer->out;
    self->out->reset(self->out);

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strcmp(arg->name, "n")) {

            if (DEBUG_REPO_LEVEL_TMP)
                knd_log("== ARG VAL:\"%s\" %lu",
                        arg->val,
                        (unsigned long)arg->val_size);

            memcpy(self->name, arg->val, arg->val_size);
            self->name_size = arg->val_size;
            self->name[self->name_size] = '\0';
        }
    }

    if (!self->name_size) return knd_FAIL;

    /* just in case: check Repo's existence */
    err = kndRepo_open(self);
    if (!err) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("== %s REPO already exists?", self->id);
        return knd_FAIL;
    }

    sprintf(buf, "%s/repos", self->user->path);
    err = knd_make_id_path(path, buf, self->id, NULL);
    if (err) return err;

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("  .. create new repo:  ID:\"%s\"  N:%.*s  PATH:%s",
                self->id, self->name_size, self->name, path);
    
    /* TODO: check if DIR already exists */
    err = knd_mkpath(self->path, 0755, false);
    if (err) return err;
    
    /* in batch mode:
          ignore incoming tasks
       in non-batch mode:
          appends incoming tasks to the inbox queue */

    sprintf(buf, "%s/inbox", path);
    err = knd_mkpath(buf, 0755, false);
    if (err) return err;

    /* reflecting class scheme changes */
    sprintf(buf, "%s/migrations", path);
    err = knd_mkpath(buf, 0755, false);
    if (err) return err;
 
    err = kndRepo_export(self, KND_FORMAT_GSL);
    if (err) return err;

    err = knd_write_file((const char*)path,
                         "repo.gsl",
                         self->out->buf, self->out->buf_size);
    if (err) return err;

    memcpy(self->last_id, self->id, KND_ID_SIZE);


    self->open(self);

    return knd_OK;
}



static int
kndRepo_read_idx(struct kndRepo *self,
                 struct kndRepoCache *cache,
                 const char *idx_name,
                 size_t idx_name_size __attribute__((unused)))
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    struct kndDataClass *dc;
    //struct kndRefSet *refset;
    //struct kndFacet *f;
    int err;
    
    dc = cache->baseclass;

    buf_size = sprintf(buf, "%s/%s/atom_%s.idx",
                       self->path,
                       dc->name,
                       idx_name);

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("  .. reading atom IDX file: \"%s\" ..\n",
                buf);

    self->out = self->user->reader->out;
    self->out->reset(self->out);
    
    err = self->out->read_file(self->out,
                               (const char*)buf, buf_size);
    if (err) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("   -- failed to load IDX: \"%s\"  :(\n",
                    buf);
        return knd_OK;
    }
    
    if (DEBUG_REPO_LEVEL_3)
        knd_log("\n\n   ++  atom IDX DB rec size: %lu\n",
                (unsigned long)self->out->file_size);
    
    cache->browser->export_depth = 0;
    cache->browser->out = self->out;
    
    err = cache->browser->read(cache->browser,
                               self->out->file,
                               self->out->file_size);
    if (err) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("-- IDX DB not read: %s :(\n", buf);
        return err;
    }

    return knd_OK;
}




static int
kndRepo_build_idx_path(struct kndRepo *self,
                       struct kndDataClass *dc,
                       const char *pref,
                       size_t pref_size,
                       struct kndRepoCache *cache)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size = 0;

    char path[KND_TEMP_BUF_SIZE];
    size_t path_size = 0;

    struct kndDataElem *de = NULL;
    char *b;
    int err;

    if (pref_size)
        memcpy(buf, pref, pref_size);

    
    dc->rewind(dc);
    do {
        err = dc->next_elem(dc, &de);
        if (!de) break;

        b = buf + pref_size;
        memcpy(b, de->name, de->name_size);
        buf_size = pref_size + de->name_size;
        
        buf[buf_size] = '\0';


        if (de->is_recursive) continue;

        if (de->dc) {
            b = buf + buf_size;
            memcpy(b, "_", 1);
            buf_size++;
            err = kndRepo_build_idx_path(self, de->dc, (const char*)buf, buf_size, cache);
            if (err) return err;
            continue;
        }

        if (!buf_size) continue;

        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("   == ATOMIC ELEM \"%s\"\n",
                    buf);

        if (!de->attr) continue;
        
        switch (de->attr->type) {
        case  KND_ELEM_REF:
            path_size = sprintf(path, "%s_%s",
                               de->attr->dataclass->name,
                               de->attr->idx_name);
            
            if (DEBUG_REPO_LEVEL_TMP)
                knd_log("  == read dependent IDX: \"%s\"?\n", buf);

            err = kndRepo_read_idx(self, cache, path, path_size);
            if (err) return err;

            break;
        case  KND_ELEM_ATOM:

            if (DEBUG_REPO_LEVEL_TMP)
                knd_log("  .. atomic IDX for \"%s\"?\n", de->attr_name);

            err = kndRepo_read_idx(self, cache, buf, buf_size);
            if (err) return err;
            
            break;
        default:
            break;
        }

            
        
    } while (de);

    
    return knd_OK;
}


static int
kndRepo_read_indices(struct kndRepo *self,
                     struct kndRepoCache *cache)
{
    int err;

    /* default alphabetic AZ idx */
    err = kndRepo_read_idx(self, cache, "AZ", strlen("AZ"));
    if (err) return err;
    
    err = kndRepo_build_idx_path(self, cache->baseclass, "", 0, cache);
    if (err) return err;

    return knd_OK;
}
            





static int
kndRepo_select(struct kndRepo *self, struct kndData *data)
{
    //char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size;

    struct kndDataClass *dc;
    //struct kndObject *obj;
    struct kndRepoCache *cache;
    //struct kndRefSet *browser;
    //struct kndRefSet *refset;
    //struct ooDict *idx;
    
    //void *delivery;
    //char *rec = NULL;
    //size_t rec_size;
    //char *header = NULL;
    //size_t header_size;

    //size_t num_objs;
    
    //const char *key = NULL;
    //void *val = NULL;

    int err;

    self->curr_class = NULL;
    
    /* check class */
    /*data->classname_size = KND_NAME_SIZE;
    err = knd_get_attr(data->spec, "class",
                       data->classname, &data->classname_size);
    if (err)
        return kndRepo_default_select(self, data);
    */
    
    /* check classname */
    dc = (struct kndDataClass*)self->user->class_idx->get(self->user->class_idx,
                                    (const char*)data->classname);
    if (!dc) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("   -- classname \"%s\" is not valid :(\n", data->classname);
        return knd_FAIL;
    }

    if (DEBUG_REPO_LEVEL_TMP) 
        knd_log("\n\n    .. REPO:%s    selecting class \"%s\"..\n",
                self->name, data->classname);

    err = kndRepo_get_cache(self, dc, &cache);
    if (err) return err;

    self->curr_class = dc;

    if (!cache->browser->num_refs) {
        err = kndRepo_read_indices(self, cache);
        if (err) return err;
    }

    /* reset */
    cache->select_result = NULL;
    memset(cache->matches, 0, sizeof(struct kndObject*) * KND_MAX_MATCHES);
    cache->num_matches = 0;
    
    cache->query->reset(cache->query);
    err = cache->query->parse(cache->query,
                              data->query, data->query_size);
    if (err) return err;

    err = cache->query->exec(cache->query);
    if (err) return err;

    return knd_OK;
}


static int
kndRepo_read_db_chunk(struct kndRepo *self,
                      struct kndRepoCache *cache,
                      const char *rec,
                      size_t rec_size,
                      size_t pref_size __attribute__((unused)),
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
                                     0,
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
    obj->out = self->user->reader->obj_out;
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

    self->out = self->user->reader->out;

    if (DEBUG_REPO_LEVEL_3)
        knd_log("  .. open OBJ db file: \"%s\" ..\n", buf);
    
    /*self->out->reset(self->out);*/
    
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
                                0,
                                guid, &obj);
    if (err) return err;

    obj->cache = cache;
    memcpy(obj->id, guid, KND_ID_SIZE);
    
    *result = obj;

    return knd_OK;
}

static int
kndRepo_update_select_class(struct kndRepo *self, struct kndData *data,
                            struct kndRepoCache *cache)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    struct kndRefSet *browser = NULL;
    void *delivery;
    char *header = NULL;
    size_t header_size;
    char *confirm = NULL;
    size_t confirm_size;
    int err;

    delivery = self->user->writer->delivery;

    knd_log("   .. UPDATE select in CLASS  \"%s\"..\n",
            cache->baseclass->name);

    browser = cache->browser;

    /* got some fresh updates? */
        /*if (!browser->is_updated) {
            knd_log("   -- no new updates in RefSet for \"%s\"\n",
                    cache->baseclass->name);
            goto next_cache;
            }*/
        
    browser->out = self->user->writer->out;
    browser->out->reset(browser->out);
        
    err = browser->sync(browser);
    if (err) return err;
        
    /* save refset in Delivery */
    buf_size = sprintf(buf, "<spec action=\"update_select\" uid=\"%s\" "
                       "    class=\"%s\" sid=\"AUTH_SERVER_SID\"/>",
                       self->user->id,  cache->baseclass->name);

    err = knd_zmq_sendmore(delivery, (const char*)buf, buf_size);
    err = knd_zmq_sendmore(delivery, data->obj, data->obj_size);
    err = knd_zmq_sendmore(delivery, "None", 4);
    err = knd_zmq_send(delivery, browser->out->buf, browser->out->buf_size);
        
    /* get reply from delivery */
    header = knd_zmq_recv(delivery, &header_size);
    confirm = knd_zmq_recv(delivery, &confirm_size);
        
    if (DEBUG_REPO_LEVEL_3)
        knd_log("  SELECT UPDATE SPEC: %s\n\n  == Delivery Service reply: %s\n",
                buf, confirm);

    /* TODO: check delivery reply */
    
    free(header);
    free(confirm);

    header = NULL;
    confirm = NULL;
    
    browser->out->reset(browser->out);

    err = browser->out->write(browser->out,  "\"objs\":[", strlen("\"objs\":["));
    if (err) return err;
   
    err = browser->export_summaries(browser, KND_FORMAT_JSON, 0, 0);
    if (err) return err;

    err = browser->out->write(browser->out, "]", 1);
    if (err) return err;

    if (DEBUG_REPO_LEVEL_3)
        knd_log("\n\n OBJ SUMMARIES:\n%s\n\n", browser->out->buf);
        
    /* save obj summaries in Delivery */
    buf_size = sprintf(buf, "<spec action=\"update_summaries\" uid=\"%s\" "
                       "    class=\"%s\" format=\"JSON\" sid=\"AUTH_SERVER_SID\"/>",
                       self->user->id,  cache->baseclass->name);

    err = knd_zmq_sendmore(delivery, (const char*)buf, buf_size);
    err = knd_zmq_sendmore(delivery, "/", 1);
    err = knd_zmq_sendmore(delivery, "None", 4);
    err = knd_zmq_send(delivery, browser->out->buf, browser->out->buf_size);
        
    /* get reply from delivery */
    header = knd_zmq_recv(delivery, &header_size);
    confirm = knd_zmq_recv(delivery, &confirm_size);
    
    if (DEBUG_REPO_LEVEL_3)
        knd_log("    EXPORT SUMMARIES SPEC: %s\n\n  == Delivery Service reply: %s\n",
                buf, confirm);
    
    if (!strcmp(confirm, "OK")) 
        browser->is_updated = false;

    err = knd_OK;
    
    if (header)
        free(header);
    if (confirm)
        free(confirm);

    return err;
}

static int
kndRepo_update_select(struct kndRepo *self, struct kndData *data)
{
    struct kndDataClass *dc = NULL;
    struct kndRepoCache *cache;

    int err = knd_FAIL;

    knd_log("  .. repo SELECT in progress: %s\n", data->spec);
    
    /* select specific class */
    /*data->classname_size = KND_NAME_SIZE;
    err = knd_get_attr(data->spec, "class",
                       data->classname, &data->classname_size);
    */

    if (!err) {
        dc = self->user->class_idx->get(self->user->class_idx,
                                        (const char*)data->classname);
        if (!dc) {
            if (DEBUG_REPO_LEVEL_TMP)
                knd_log("  .. classname \"%s\" is not valid...\n", data->classname);
            return knd_FAIL;
        }

        err = kndRepo_get_cache(self, dc, &cache);
        if (err) return err;

        err = kndRepo_update_select_class(self, data, cache);
        if (err) return err;
    }
    
    /* set language */
    /*self->lang_code_size = KND_NAME_SIZE;
    err = knd_get_attr(data->spec, "lang",
                       self->lang_code, &self->lang_code_size);
    */
    if (err)
        self->lang_code_size = 0;

    cache = self->cache;
    if (!cache) {
        knd_log("   -- no cached data in Repo?\n");
        return err;
    }

    /* select from all classes */
    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("  .. selecting from all available classes ..\n");

    while (cache) {
        err = kndRepo_update_select_class(self, data, cache);
        if (err) return err;
        
        cache = cache->next;
    }
    
    return knd_OK;
}


static int
kndRepo_update_get_obj(struct kndRepo *self, struct kndData *data)
{
    struct kndDataClass *c = NULL;
    struct kndRepoCache *cache;
    struct kndObject *obj = NULL;
    struct ooDict *idx = NULL;
    int err;

    if (DEBUG_REPO_LEVEL_3)
        knd_log("  .. repo GET obj in progress..\n");

    /*data->name_size = KND_NAME_SIZE;
    err = knd_get_attr(data->spec, "name",
                       data->name, &data->name_size);
    if (err) {
        knd_log("  -- no name provided :(\n");
        return knd_FAIL;
    }
    */
    
    /* check class */
    /*data->classname_size = KND_NAME_SIZE;
    err = knd_get_attr(data->spec, "class",
                       data->classname, &data->classname_size);
    if (err) return err;
    */
    
    /* check classname */
    idx = self->user->writer->dc->class_idx;
    if (!idx) return knd_FAIL;
    
    c = idx->get(idx,
                 (const char*)data->classname);
    if (!c) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("  .. classname \"%s\" is not valid...\n", data->classname);
        return knd_FAIL;
    }
    
    err = kndRepo_get_cache(self, c, &cache);
    if (err) goto final;

    /* set language */
    /*self->lang_code_size = KND_NAME_SIZE;
    err = knd_get_attr(data->spec, "lang",
                       self->lang_code, &self->lang_code_size);
    if (err)
        self->lang_code_size = 0;
    */
    
    /* get obj by name */
    obj = (struct kndObject*)cache->obj_idx->get(cache->obj_idx,
                                                 (const char*)data->name);
    if (!obj) {
        knd_log("  -- obj %s not found :(\n",
            data->name);
        err = knd_NO_MATCH;
        goto final;
    }

    obj->out = self->user->writer->out;

    /* export obj */
    err = obj->export(obj, KND_FORMAT_JSON, 0);
    if (err) {
        knd_log("  -- obj %s export failure :(\n",
            data->name);
        goto final;
    }

    /*if (DEBUG_REPO_LEVEL_TMP) {
        knd_log("\n    ++ CONTAINER refset:");
        cache->contain_idx->str(cache->contain_idx, 1, 5);
        }*/

    
    /* OBJ file attachment */
    if (obj->filepath) {

        knd_log("\n\n  == OBJ DATA filepath: \"%s\"\n", obj->filepath);
        
        data->filepath = strdup(obj->filepath);
        if (!data->filepath) {
            err = knd_NOMEM;
            goto final;
        }
        data->filepath_size = obj->filepath_size;
        
        data->filesize = obj->filesize;
        
        if (obj->mimetype_size) {
            strcpy(data->mimetype, obj->mimetype);
            data->mimetype_size = obj->mimetype_size;
        }
    }

    knd_log("\nOBJ AFTER export: %s [%lu]\n\n",
            obj->out->buf, (unsigned long)obj->out->buf_size);

    
 final:



    
    
    return err;
}


static int
kndRepo_get_obj(struct kndRepo *self,
                struct kndData *data)
{
    struct kndDataClass *dc = NULL;
    struct kndRepoCache *cache;
    struct kndObject *obj = NULL;
    struct ooDict *idx = NULL;
    size_t curr_depth = 0;
    int err = knd_FAIL;

    if (DEBUG_REPO_LEVEL_2)
        knd_log("  .. repo GET in progress..\n");

    /* check class */
    /*data->classname_size = KND_NAME_SIZE;
    err = knd_get_attr(data->spec, "class",
                       data->classname, &data->classname_size);
    if (err) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("  -- no class name specified :(\n");
        return err;
    }
    */
    
    /* check classname */
    idx = self->user->reader->dc->class_idx;
    if (!idx) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("    -- no class name IDX :(\n");
    
        return knd_FAIL;
    }
    
    dc = idx->get(idx,
                 (const char*)data->classname);
    if (!dc) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("    -- classname \"%s\" is not valid :(\n", data->classname);
        return knd_FAIL;
    }
    
    err = kndRepo_get_cache(self, dc, &cache);
    if (err) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("    -- no cache found for class \"%s\" :(\n", data->classname);
        return err;
    }
    
    /*data->name_size = KND_NAME_SIZE;
    err = knd_get_attr(data->spec, "name",
                       data->name, &data->name_size);
    if (err) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("  -- no obj name provided :(\n");
        return knd_FAIL;
    }

    idbuf_size = KND_ID_SIZE + 1;
    err = knd_get_attr(data->spec, "guid",
                       data->guid, &idbuf_size);
    */
    if (err) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("  -- no obj GUID provided..   trying to resolve by name..\n");

        err = kndRepo_get_guid(self, dc,
                               data->name, data->name_size,
                               data->guid);
        if (err) {
            if (DEBUG_REPO_LEVEL_TMP)
                knd_log("   -- \"%s\" name not recognized :(\n", data->name);
            return knd_FAIL;
        }
        data->guid[KND_ID_SIZE] = '\0';
    }

    /* set language */
    /*self->lang_code_size = KND_NAME_SIZE;
    err = knd_get_attr(data->spec, "lang",
                       self->lang_code, &self->lang_code_size);
    if (err)
        self->lang_code_size = 0;
    */
    
    /* output format specified? */
    /*buf_size = KND_NAME_SIZE;
    err = knd_get_attr(data->spec, "format",
                       buf, &buf_size);
    if (!err) {
        if (!strcmp(buf, "HTML"))
            data->format = KND_FORMAT_HTML;
        else if (!strcmp(buf, "JS"))
            data->format = KND_FORMAT_JS;
    }
    */
    
    /* set nesting depth */
    /*curr_depth = KND_DEFAULT_OBJ_DEPTH;
    buf_size = KND_NAME_SIZE;
    err = knd_get_attr(data->spec, "depth",
                       buf, &buf_size);
    if (!err) {
        err = knd_parse_num((const char*)buf, &numval);
        if (!err) {
            if (numval >= 0 && numval < KND_MAX_OBJ_DEPTH)
                curr_depth = numval;
        }
    }
    */
    
    /* get obj by name */
    if (DEBUG_REPO_LEVEL_2)
        knd_log("   ?? get browser state obj \"%s\" (depth: %lu)\n",
                data->name, (unsigned long)curr_depth);

    obj = (struct kndObject*)cache->db->get(cache->db,
                                            (const char*)data->guid);
    if (!obj) {
        if (DEBUG_REPO_LEVEL_2)
            knd_log("   -- no obj in cache..\n");

        err = kndRepo_read_obj_db(self,
                                  data->guid,
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

    
    self->out = self->user->reader->out;
    self->out->reset(self->out);
    
    /*self->user->reader->obj_out->reset(self->user->reader->obj_out);*/

    if (data->format == KND_FORMAT_JSON) {
        err = self->out->write(self->out,
                               "{\"baseclass\":",
                               strlen("{\"baseclass\":"));
        if (err) return err;
    }
    
    err = cache->baseclass->export(cache->baseclass, data->format);
    if (err) return err;

    if (data->format == KND_FORMAT_JSON) {
        err = self->out->write(self->out,
                               ",\"obj\":",
                               strlen(",\"obj\":"));
        if (err) return err;
    }
    
    /*obj->str(obj, 1);*/
    
    /* export obj */
    obj->out = self->out;
    obj->export_depth = 0;
    
    err = obj->export(obj, data->format, 0);
    if (err) return err;
    
    if (data->format == KND_FORMAT_JSON) {
        err = self->out->write(self->out, "}", 1);
    }

    if (data->format == KND_FORMAT_HTML) {

        err = self->out->write(self->out, "<script type=\"text/javascript\">", strlen("<script type=\"text/javascript\">"));

        err = self->out->write(self->out, "oo_init.get_obj = ", strlen("oo_init.get_obj = "));

        err = self->out->write(self->out,
                               "{\"baseclass\":",
                               strlen("{\"baseclass\":"));
        if (err) return err;

        err = cache->baseclass->export(cache->baseclass, KND_FORMAT_JSON);
        if (err) return err;

        err = self->out->write(self->out,
                               ",\"obj\":",
                               strlen(",\"obj\":"));
        if (err) return err;

        err = obj->export(obj, KND_FORMAT_JSON, 0);
        if (err) return err;

        err = self->out->write(self->out, "}", 1);
        err = self->out->write(self->out, "</script>", strlen("</script>"));
    }


    /* OBJ file attachment */
    /*if (obj->filepath) {
        data->filename = strdup(obj->filepath);
        if (!data->filename) {
            err = knd_NOMEM;
            goto final;
        }

        data->filesize = obj->filesize;
        
        if (obj->mimetype_size) {
            strcpy(data->mimetype, obj->mimetype);
            data->mimetype_size = obj->mimetype_size;
        }
    }
    */

    /*self->out = self->user->writer->out;*/

    /*err = self->out->write(self->out, "{", 1);
    if (err) goto final;
    */


    if (DEBUG_REPO_LEVEL_1)
        knd_log("\nEXPORT RESULT: %s [%lu]\n\nMETA: %s\n",
                self->out->buf, (unsigned long)obj->out->buf_size,
                self->user->reader->obj_out->buf);

    
    /*err = self->out->write(self->out,
                           obj->out->buf,
                           obj->out->buf_size);
    if (err) goto final;
    */
    
    /* export refs */
    /*rel_entry = (struct kndRelEntry*)idx->get(idx,
                                              data->name);
    if (rel_entry) {
        knd_log("   .. export refs..\n");
        
        err = self->out->write(self->out,
                               ",\"reltypes\":[",
                               strlen(",\"reltypes\":["));

        cache->maze->rel_idx->out = self->out;
        
        err = cache->maze->rel_idx->export(cache->maze->rel_idx,
                                           rel_entry, obj);
        knd_log("   == REL IDX export result: %d\n", err);
        if (err) goto final;

        err = self->out->write(self->out, "]", 1);
        if (err) goto final;
    }

    err = self->out->write(self->out, "}", 1);
    if (err) goto final;
    */

    
    err = knd_OK;
    
    return err;
}









static int
kndRepo_update_flatten(struct kndRepo *self, struct kndData *data)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    struct kndDataClass *c = NULL;
    struct kndRepoCache *cache;
    struct kndObject *obj = NULL;
    struct ooDict *idx = NULL;
    
    struct kndFlatTable *table = NULL;
    struct kndFlatRow *row;
    struct kndFlatCell *cell;

    unsigned long span = 0;
    long total = 0;
    struct kndOutput *out;
    int err;

    knd_log("  .. repo FLATTEN obj in progress..\n");

    /*data->name_size = KND_NAME_SIZE;
    err = knd_get_attr(data->spec, "name",
                       data->name, &data->name_size);
    if (err) {
        knd_log("  -- no name provided :(\n");
        return knd_FAIL;
    }
    */
    
    /* check class */
    /*data->classname_size = KND_NAME_SIZE;
    err = knd_get_attr(data->spec, "class",
                       data->classname, &data->classname_size);
    if (err) return err;
    */
    
    /* check classname */
    idx = self->user->writer->dc->class_idx;
    if (!idx) return knd_FAIL;
    
    c = idx->get(idx,
                 (const char*)data->classname);
    if (!c) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("  .. classname \"%s\" is not valid...\n", data->classname);
        return knd_FAIL;
    }
    
    err = kndRepo_get_cache(self, c, &cache);
    if (err) goto final;

    /* set language */
    /*self->lang_code_size = KND_NAME_SIZE;
    err = knd_get_attr(data->spec, "lang",
                       self->lang_code, &self->lang_code_size);
    if (err)
        self->lang_code_size = 0;
    */
    
    /* get obj by name */
    obj = (struct kndObject*)cache->obj_idx->get(cache->obj_idx,
                                                 (const char*)data->name);
    if (!obj) {
        knd_log("  -- obj %s not found :(\n",
            data->name);
        err = knd_NO_MATCH;
        goto final;
    }


    table  = malloc(sizeof(struct kndFlatTable));
    if (!table) return knd_NOMEM;
    memset(table, 0, sizeof(struct kndFlatTable));
    
    err = obj->flatten(obj, table, &span);
    if (err) goto final;

    knd_log("   FINAL FLATTENED TABLE:  span: %lu  num rows: %lu\n",
            (unsigned long)span, (unsigned long)table->num_rows);

    out = self->user->writer->out;

    buf_size = sprintf(buf, "{\"name\":\"%s\",\"span\":%lu,\"header\":[",
                       obj->name,
                       (unsigned long)span);
    err = out->write(out, buf, buf_size);
    if (err) goto final;
    
    for (size_t i = 0; i < span; i++) {
        total = table->totals[i];
        if (i) {
            out->write(out, ",", 1);
            if (err) goto final;
        }
        buf_size = sprintf(buf, "{\"tot\":%lu}",
                           (unsigned long)total);
        err = out->write(out, buf, buf_size);
        if (err) goto final;
    }

    out->write(out, "]", 1);
    if (err) goto final;

    buf_size = sprintf(buf, ",\"table\":[");
    err = out->write(out, buf, buf_size);
    if (err) goto final;
    

    for (size_t i = 0; i < table->num_rows; i++) {
        if (i) {
            out->write(out, ",", 1);
            if (err) goto final;
        }

        out->write(out, "[", 1);
        if (err) goto final;
        
        row = &table->rows[i];
        
        for (size_t j = 0; j < row->num_cols; j++) {
            cell = &row->cols[j];

            if (j) {
                out->write(out, ",", 1);
                if (err) goto final;
            }
        
            buf_size = sprintf(buf, "{\"name\":\"%s\",\"span\":%lu,\"estim\":%lu}",
                               cell->obj->name,
                               (unsigned long)cell->span,
                               (unsigned long)cell->estim);

            err = out->write(out, buf, buf_size);
            if (err) goto final;
        }

        out->write(out, "]", 1);
        if (err) goto final;

    }

    out->write(out, "]}", 2);
    if (err) goto final;

    knd_log("\nFLAT JSON TABLE: %s [%lu]\n\n",
            out->buf, (unsigned long)out->buf_size);
    
 final:

    if (table)
        free(table);

    return err;
}


static int
kndRepo_flatten(struct kndRepo *self __attribute__((unused)), struct kndData *data __attribute__((unused)))
{
    //struct kndDataClass *c = NULL;
    //struct kndRepoCache *cache;
    //struct kndObject *obj = NULL;
    //struct ooDict *idx = NULL;

    int err;

    knd_log("  .. repo FLATTEN obj in progress..\n");

    err = knd_OK;

    return err;
}



static int
kndRepo_update_match(struct kndRepo *self, struct kndData *data)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    struct kndDataClass *c = NULL;
    struct kndRepoCache *cache;
    struct kndObject *obj = NULL;
    struct ooDict *idx = NULL;
    //long span = 0;
    //long total = 0;

    void *delivery;

    char *header = NULL;
    size_t header_size;
    char *confirm = NULL;
    size_t confirm_size;

    //struct kndOutput *out;
    int err;

    knd_log("  .. repo MATCH obj in progress..\n");

    /* check class */
    /*data->classname_size = KND_NAME_SIZE;
    err = knd_get_attr(data->spec, "class",
                       data->classname, &data->classname_size);
    if (err) return err;
    */
    
    /* check classname */
    idx = self->user->writer->dc->class_idx;
    if (!idx) return knd_FAIL;
    
    c = idx->get(idx,
                 (const char*)data->classname);
    if (!c) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("  .. classname \"%s\" is not valid...\n", data->classname);
        return knd_FAIL;
    }
    
    err = kndRepo_get_cache(self, c, &cache);
    if (err) goto final;

    /* set language */
    /*self->lang_code_size = KND_NAME_SIZE;
    err = knd_get_attr(data->spec, "lang",
                       self->lang_code, &self->lang_code_size);
    if (err)
        self->lang_code_size = 0;

    */
    
    knd_log("\n   .. MATCH OBJ of class \"%s\": \"%s\"..\n\n",
            c->name, data->body);

    /*memset(cache->intersect_matrix, 0, cache->intersect_matrix_size);*/

    self->match_state++;
    
    err = kndObject_new(&obj);
    if (err) return err;
    obj->cache = cache;
    obj->out = self->user->writer->out;
    obj->out->reset(obj->out);

    err = obj->match(obj, data->body, data->body_size);
    if (err) goto final;

    /* save matching results in Delivery */
    buf_size = sprintf(buf, "<spec action=\"update_match\" uid=\"%s\" "
                       "    class=\"%s\" sid=\"AUTH_SERVER_SID\"/>",
                       self->user->id,  cache->baseclass->name);

    delivery = self->user->writer->delivery;

    err = knd_zmq_sendmore(delivery, (const char*)buf, buf_size);
    err = knd_zmq_sendmore(delivery, data->body, data->body_size);
    err = knd_zmq_sendmore(delivery, "None", 4);
    err = knd_zmq_send(delivery, obj->out->buf, obj->out->buf_size);
        
    /* get reply from delivery */
    header = knd_zmq_recv(delivery, &header_size);
    confirm = knd_zmq_recv(delivery, &confirm_size);
        
    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("  MATCH UPDATE SPEC: %s\n\n  == Delivery Service reply: %s\n",
                buf, confirm);

    free(header);
    free(confirm);
        
    err = knd_OK;
    
 final:


    return err;
}


static int
kndRepo_match(struct kndRepo *self __attribute__((unused)), struct kndData *data __attribute__((unused)))
{
    //struct kndDataClass *c = NULL;
    //struct kndRepoCache *cache;
    //struct kndObject *obj = NULL;
    //struct ooDict *idx = NULL;
    int err;

    knd_log("  .. repo MATCH obj in progress..\n");

    /*   knd_log("   .. get updates for the CLASS: %s\n", dc->name);

        buf_size = sprintf(buf, "<spec action=\"get_updates\" uid=\"%s\" "
                           "    class=\"%s\" sid=\"AUTH_SERVER_SID\"/>",
                           self->user->id, dc->name);

        err = knd_zmq_sendmore(delivery, (const char*)buf, buf_size);

        if (!strcmp(data->query, "None"))
            err = knd_zmq_sendmore(delivery, "/", 1);
        else
            err = knd_zmq_sendmore(delivery, data->query, data->query_size);

        err = knd_zmq_sendmore(delivery, "None", 4);
        err = knd_zmq_send(delivery, "None", 4);

        header = knd_zmq_recv(delivery, &header_size);
        rec = knd_zmq_recv(delivery, &rec_size);

        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("  UPDATES SPEC: %s\n\n  == Delivery Service header: \"%s\" [rec size:%lu]\n",
                    buf, header, (unsigned long)rec_size);
    */

    err = knd_OK;

    return err;
}

/*
static int
kndRepo_save_import_rec(struct kndRepo *self, struct kndData *data)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    int err;
    
    buf_size = sprintf(buf, "%s/import.db", self->path);

    err = knd_append_file((const char*)buf, data->spec, data->spec_size);
    if (err) goto final;

    err = knd_append_file((const char*)buf, data->body, data->body_size);
    if (err) goto final;
    
 final:
    return err;
}
*/

static int
kndRepo_update_obj(struct kndRepo *self,
                   const char *rec,
                   size_t *total_size,
                   struct kndRepoCache *cache)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    
    //struct kndDataClass *dc;
    struct kndObject *obj;
   
    const char *c;
    const char *b;
    size_t chunk_size = 0;
    
    bool in_body = false;
    //bool in_obj_list = false;
    int err = knd_FAIL;
    
    c = rec;
    b = c;
    
    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            /* whitespace */
            b = c + 1;
            break;
        case '{':
            if (!in_body) {
                in_body = true;
                b = c + 1;
                break;
            }

            buf_size = c - b;
            memcpy(buf, b, buf_size);
            buf[buf_size] = '\0';
            
            knd_log("  ==  obj \"%s\" exists?\n", buf);

            obj = (struct kndObject*)cache->obj_idx->get(cache->obj_idx,
                                                 (const char*)buf);
            if (!obj) {
                knd_log("  -- obj \"%s\" not found :(\n",
                        buf);
                err = knd_NO_MATCH;
                goto final;
            }
    
            err = obj->update(obj, c, &chunk_size);
            if (err) goto final;
            c += (chunk_size - 1);

            knd_log("\n  ++ Repo: OBJ update OK! [curr state: %lu] chunk: %lu remainder: %s\n",
                    (unsigned long)self->state, (unsigned long)chunk_size, c);


            /*obj->str(obj, 1);*/

            
            
            break;
        case '}':

            knd_log("close remainder: %s chunk: %lu\n",
                    c, (unsigned long)(c - rec));
            
            *total_size = c - rec;
            return knd_OK;
        }
        
        c++;
    }

    err = knd_FAIL;
    
 final:

    knd_log(" obj update return: %d\n", err);
    
    return err;
}


static int
kndRepo_update_class(struct kndRepo *self,
                     const char *rec,
                     size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size;
    
    struct kndDataClass *dc;
    //struct kndObject *obj;
    struct kndRepoCache *cache = NULL;
    const char *c;
    const char *b;
    size_t chunk_size;
    
    bool in_class = false;
    bool in_obj_list = false;
    
    int err;
    
    c = rec;
    b = c;
    
    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            /* whitespace */
            b = c + 1;
            break;
        case '{':
            if (!in_class) {
                in_class = true;
                b = c + 1;
                break;
            }

            if (in_obj_list) {
                b = c + 1;
                err = kndRepo_update_obj(self, c, &chunk_size, cache);
                if (err) goto final;

                knd_log("chunk size: %lu\n", (unsigned long)chunk_size);
                
                c += chunk_size;

                knd_log("class update: %d\n  .. remainder: %s\n", err, c);
                

                break;
            }
            
            break;
        case '[':
            in_obj_list = true;

            buf_size = c - b;
            memcpy(buf, b, buf_size);
            buf[buf_size] = '\0';

            knd_log("  == update class \"%s\"..\n", buf);
            
            /* check classname */
            dc = self->user->class_idx->get(self->user->class_idx,
                                           (const char*)buf);
            if (!dc) {
                if (DEBUG_REPO_LEVEL_TMP)
                    knd_log("  .. classname \"%s\" is not valid...\n", buf);
                return knd_FAIL;
            }

            err = kndRepo_get_cache(self,
                                    dc,
                                    &cache);
            if (err) goto final;
            

            knd_log(" ++ cache OK!\n");
            

            break;
        case '}':
            knd_log("  class end?\n");
            in_class = false;
            
            break;
        case ']':
            c++;
            *total_size = c - rec;
            
            return knd_OK;
        }
        c++;
    }

    
 final:
    return err;
}


static int
kndRepo_update(struct kndRepo *self,
               struct kndData *data,
               knd_format format __attribute__((unused)))
{
    //char buf[KND_NAME_SIZE];
    //size_t buf_size;
    
    //struct kndDataClass *dc;
    //struct kndObject *obj;
    //struct kndRepoCache *cache;
    char *c;
    char *b;
    bool in_class_list = false;
    size_t chunk_size;
    int err = knd_FAIL;

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("  .. repo \"%s\" update..\n",
                self->path);

    
    /* foreach class */
    c = data->body;
    b = c;
    
    while (*c) {
        switch (*c) {
            /* non-whitespace char */
        default:
            break;
        case '\n':
        case '\r':
        case '\t':
        case ' ':
            /* whitespace */
            b = c + 1;
            break;
        case '{':
            if (in_class_list) {
                b = c + 1;
                err = kndRepo_update_class(self, c, &chunk_size);
                if (err) goto final;
                c += chunk_size;
                break;
            }
            break;
        case '[':
            if (!in_class_list) {
                in_class_list = true;
                break;
            }
            break;
        case ']':
            goto final;
        }
        c++;
    }

    err = knd_OK;

    self->state++;

 final:
    knd_log("   .. user update status: %d\n", err);

    return err;
}



static int
kndRepo_import(struct kndRepo *self,
               struct kndData *data,
               knd_format format)
{
    struct kndDataClass *c;
    struct kndObject *obj = NULL;
    struct kndRepoCache *cache;
    int err;

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("  .. repo \"%s\" import..\n",
                self->path);

    /* batch mode? */
    /*err = knd_get_attr(data->spec,
                       "bm",
                       buf, &buf_size);
    if (!err) self->batch_mode = true;
    */
    
    /* file attached? */
    /*data->filename_size = KND_NAME_SIZE;
    err = knd_get_attr(data->spec,
                       "file",
                       data->filename, &data->filename_size);
    if (err) {
        data->filename_size = 0;
    }
    */
    
    /* check class */
    /*data->classname_size = KND_NAME_SIZE;
    err = knd_get_attr(data->spec,
                       "class",
                       data->classname, &data->classname_size);
    if (err) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("  .. no classname specified...\n");
        return err;
    }

    */

    
    /* check classname */
    c = self->user->class_idx->get(self->user->class_idx,
                                   (const char*)data->classname);
    if (!c) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("  .. classname \"%s\" is not valid...\n", data->classname);
        return knd_FAIL;
    }

    err = kndRepo_get_cache(self,
                            c,
                            &cache);
    if (err) goto final;

    err = kndObject_new(&obj);
    if (err) return err;
    obj->cache = cache;
    obj->out = self->user->writer->obj_out;

    /* assign local id */
    memcpy(obj->id, cache->obj_last_id, KND_ID_SIZE);
    knd_inc_id(obj->id);

    err = obj->import(obj, data, format);
    if (err) goto final;

    err = cache->db->set(cache->db, obj->id, (void*)obj);
    if (err) goto final;

    err = cache->obj_idx->set(cache->obj_idx, obj->name, (void*)obj);
    if (err) goto final;



    /* append rec to the backup file */
    /*if (!self->batch_mode) {
        err = kndRepo_save_import_rec(self, data);
        if (err) goto final;
        }*/

    
    knd_log("\n  ++ Repo %s: OBJ %s::%s import OK [total objs: %lu]\n",
            self->name, cache->baseclass->name, obj->id, (unsigned long)cache->num_objs + 1);
    
    /* assign new last_id */
    memcpy(cache->obj_last_id, obj->id, KND_ID_SIZE);
    cache->num_objs++;

    obj = NULL;

 final:

    if (obj) {
        obj->del(obj);
    }
    
    return err;
}


static int
kndRepo_sync(struct kndRepo *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    char filename_buf[KND_TEMP_BUF_SIZE];
    size_t filename_buf_size;

    struct kndRepoCache *cache;
    struct kndRefSet *refset;
    struct kndRefSet *idx;
    struct kndObject *obj;
    struct kndObjRef *objref;
    struct kndSortTag *tag;
    struct kndSortAttr *a;
  
    const char *key = NULL;
    void *val = NULL;
    int err = knd_FAIL;

    cache = self->cache;
    while (cache) {

        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("   .. syncing objs of class \"%s\"..\n",
                    cache->baseclass->name);

        err = kndRefSet_new(&refset);
        if (err) return err;

        cache->db->rewind(cache->db);
        do {
            cache->db->next_item(cache->db, &key, &val);
            if (!key) break;

            obj = (struct kndObject*)val;

            /* add ref */
            err = kndObjRef_new(&objref);
            if (err) goto final;
        
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

            /* default NAME IDX */
            err = kndObjRef_new(&objref);
            if (err) goto final;
        
            /*objref->obj = obj;*/
            
            memcpy(objref->obj_id, obj->id, KND_ID_SIZE);
            objref->obj_id_size = KND_ID_SIZE;
            memcpy(objref->name, obj->name, obj->name_size);
            objref->name_size = obj->name_size;

            err = kndSortTag_new(&tag);
            if (err) goto final;
            objref->sorttag = tag;

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

            err = refset->add_ref(refset, objref);
            if (err) {
                knd_log("  -- ref %s not added to refset :(\n", objref->obj_id);
                goto final;
            }

            err = obj->sync(obj);
            if (err) {
                knd_log("  -- sync failure of OBJ %s::%s :(\n",
                        obj->id, obj->name);

                goto final;
            }
        }
        while (key);

        if (!refset->idx) {
            refset->del(refset);
            refset = NULL;
            goto sync_indices;
        }

        /* write OBJS to filesystem */
        buf_size = sprintf(buf, "%s/%s/", self->path, cache->baseclass->name);
        err = knd_mkpath(buf, 0755, false);
        if (err) return err;

        refset->out = self->user->writer->out;
        refset->out->reset(refset->out);


        err = refset->sync_objs(refset, (const char*)buf);
        if (err) goto final;
        
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("\n    ++ sync objs of %s OK!\n", cache->baseclass->name);

        refset->out->reset(refset->out);
        err = refset->sync(refset);
        if (err) return err;

        filename_buf_size = sprintf(filename_buf, "AZ.idx");
        err = knd_write_file((const char*)buf,
                             (const char*)filename_buf,
                             refset->out->buf, refset->out->buf_size);
        if (err) return err;

        
        refset->del(refset);
        refset = NULL;

    sync_indices:
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("\n    .. syncing indices.. \n");

        /* check idxs */
        idx = cache->idxs;
        while (idx) {
            idx->out = self->user->writer->out;
            idx->out->reset(idx->out);

            /*idx->str(idx, 0, 5);*/
            
            err = idx->sync(idx);
            if (err) return err;

            filename_buf_size = sprintf(filename_buf, "atom_%s.idx", idx->name);

            knd_log("  IDX filename: %s\n", filename_buf);
            
            err = knd_write_file((const char*)buf,
                                 (const char*)filename_buf,
                                 idx->out->buf, idx->out->buf_size);
            if (err) return err;

            idx = idx->next;
        }
        
        cache = cache->next;
    }

    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("\n    ++ DB SYNC SUCCESS!\n");

    return knd_OK;
    
 final:
    if (refset)
        refset->del(refset);

    return err;
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
    //char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size;
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
kndRepo_get_cache(struct kndRepo *self, struct kndDataClass *c,
                  struct kndRepoCache **result)
{
    struct kndRepoCache *cache;
    int err;
    
    /* set repo DB cache for writing */
    cache = self->cache;
    while (cache) {
        if (cache->baseclass == c) {
            *result = cache;
            return knd_OK;
        }
        cache = cache->next;
    }

    /* no cache found */
    cache  = malloc(sizeof(struct kndRepoCache));
    if (!cache) return knd_NOMEM;
    memset(cache, 0, sizeof(struct kndRepoCache));

    cache->baseclass = c;
    cache->repo = self;

    err = ooDict_new(&cache->db, KND_MEDIUM_DICT_SIZE);
    if (err) goto final;

    err = ooDict_new(&cache->obj_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto final;
    
    /*err = ooDict_new(&cache->contain_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto final;

    err = ooDict_new(&cache->linear_seq_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto final;
    */
    
    err = kndRefSet_new(&cache->browser);
    if (err) goto final;
    cache->browser->name[0] = '/';
    cache->browser->name_size = 1;
    cache->browser->cache = cache;
    cache->browser->out = self->out;
    
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

/*
static int
kndRepo_get_repo(struct kndRepo *self, const char *uid,
                 struct kndRepo **repo)
{
    //char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size = KND_TEMP_BUF_SIZE;

    struct kndRepo *curr_repo;
    int err;

    if (DEBUG_REPO_LEVEL_3)
        knd_log("  ?? is \"%s\" a valid repo?\n", uid);

    curr_repo = self->repo_idx->get(self->repo_idx, uid);
    if (!curr_repo) {
        err = kndRepo_new(&curr_repo);
        if (err) return err;

        err = self->repo_idx->set(self->repo_idx, (const char*)uid, (void*)curr_repo);
        if (err) return err;

        curr_repo->writer = self->writer;
        curr_repo->reader = self->reader;
    }

    *repo = curr_repo;

    return knd_OK;
}
*/


static int
kndRepo_get_guid(struct kndRepo *self,
                 struct kndDataClass *dc,
                 const char *obj_name,
                 size_t      obj_name_size,
                 char *result)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    struct kndRepoCache *cache;
    struct kndRefSet *refset;
    //const char *guid;
    int err;
    
    if (DEBUG_REPO_LEVEL_TMP)
        knd_log("   .. get guid of %s::%s..\n",
                dc->name, obj_name);
    
    err = kndRepo_get_cache(self, dc, &cache);
    if (err) return err;

    /* read name IDX */
    if (!cache->name_idx) {
        buf_size = sprintf(buf, "%s/%s/AZ.idx",
                           self->path,
                           dc->name);

        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("  .. reading name IDX file: \"%s\" ..\n",
                    buf);

    
        self->out = self->user->reader->out;
        self->out->reset(self->out);
    
        err = self->out->read_file(self->out,
                                   (const char*)buf, buf_size);
        if (err) {
            if (DEBUG_REPO_LEVEL_3)
                knd_log("   -- no DB found for name IDX \"%s\" :(\n",
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
                                       obj_name, obj_name_size,
                                       obj_name, obj_name_size, result);
    if (err) {
        if (DEBUG_REPO_LEVEL_TMP)
            knd_log("   -- obj name not recognized :(\n");

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
kndRepo_run(struct kndRepo *self, struct kndSpecInstruction *instruct)
{
    //struct kndSpecArg *arg;
    int err;
    
    if (DEBUG_REPO_LEVEL_2)
        knd_log(".. REPO task to run: %s",
                instruct->proc_name);

    if (!strcmp(instruct->proc_name, "add")) {
        err = kndRepo_add_repo(self, instruct->args, instruct->num_args);
        if (err) return err;
        
    }
    
    return knd_OK;
}



static int
kndRepo_read_state(struct kndRepo *self, char *rec, size_t *total_size)
{
    char *b, *c;
    size_t buf_size;
    
    //bool in_field = true;
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
            
            *c = '\0';
            if (!strcmp(b, "last")) {
                in_val = true;
                in_last_id = true;
                b = c + 1;
                break;
            }

            b = c + 1;
            break;
        case '}':

            if (in_last_id) {
                *c = '\0';

                buf_size = c - b;
                if (buf_size != KND_ID_SIZE) return knd_FAIL;
                
                memcpy(self->last_id, b, buf_size);
                self->last_id[buf_size] = '\0';
                
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
    self->run = kndRepo_run;

    self->read_state = kndRepo_read_state;
    self->add_repo = kndRepo_add_repo;
    self->open = kndRepo_open;
    
    self->import = kndRepo_import;
    self->update = kndRepo_update;

    self->select = kndRepo_select;
    self->update_select = kndRepo_update_select;

    self->export = kndRepo_export;
    
    self->update_get_obj = kndRepo_update_get_obj;
    self->get_obj = kndRepo_get_obj;

    self->read_obj = kndRepo_read_obj_db;

    self->update_flatten = kndRepo_update_flatten;
    self->flatten = kndRepo_flatten;

    self->update_match = kndRepo_update_match;
    self->match = kndRepo_match;

    self->sync = kndRepo_sync;
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

    err = kndPolicy_new(&self->policy);
    if (err) return knd_NOMEM;

    memset(self->id, '0', KND_ID_SIZE);
    memset(self->last_id, '0', KND_ID_SIZE);

    self->intersect_matrix_size = sizeof(struct kndObject*) * (KND_ID_BASE * KND_ID_BASE * KND_ID_BASE);

    kndRepo_init(self);

    *repo = self;

    return knd_OK;
}
