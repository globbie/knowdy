#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>

#include <libxml/parser.h>

#include <knd_config.h>
#include <knd_output.h>
#include <knd_msg.h>

#include "knd_delivery.h"

#define DEBUG_DELIV_LEVEL_0 0
#define DEBUG_DELIV_LEVEL_1 0
#define DEBUG_DELIV_LEVEL_2 0
#define DEBUG_DELIV_LEVEL_3 0
#define DEBUG_DELIV_LEVEL_TMP 1

//static int kndDelivery_add_meta(struct kndDelivery *self,
//                                struct kndData *data);

static int
kndDelivery_str(struct kndDelivery *self)
{
    knd_log("<struct kndDelivery at %p>\n", self);

    return knd_OK;
}

static int
kndDelivery_del(struct kndDelivery *self)
{

    free(self);
    return knd_OK;
}

static int
kndDelivery_check_SID(struct kndDelivery *self, 
                      xmlNodePtr input_node __attribute__((unused)),
                      struct kndData *data,
                      const char *action)
{
    struct kndAuthRec *rec;
    //size_t buf_size;
    //int err;

    /* auth action doesn't require a valid SID */
    if (!strcmp(action, "retrieve")) {
        if (!strcmp(data->sid, "AUTH"))
            return knd_OK;
    }

    /* TODO: real SID */
    if (!strcmp(data->sid, "AUTH_SERVER_SID")) {

        /*buf_size = KND_ID_SIZE + 1;
        
        err = knd_get_xmlattr(input_node, "uid", 
                              data->uid, &buf_size);
        if (err) {
            knd_log("try acc_uid ..\n");
            buf_size = KND_ID_SIZE + 1;
            err = knd_get_xmlattr(input_node, "acc_uid", 
                                  data->uid, &buf_size);
            if (err) {
                knd_log("-- UID is required :(");
                return err;
            }
            }*/
        
        return knd_OK;
    }

    /* unauthenticated user */
    if (!strcmp(data->sid, "000")) {
        data->header_size = strlen("AUTH_OK");
        memcpy(data->header, "AUTH_OK", data->header_size);
        data->header[data->header_size] = '\0';

        memcpy(data->uid, "000", KND_ID_SIZE);
        data->uid_size =  KND_ID_SIZE;
        return knd_OK;
    }

    /* special user */
    if (!strcmp(data->sid, "m0n1t0r")) {
        data->header_size = strlen("AUTH_OK");
        memcpy(data->header, "AUTH_OK", data->header_size);
        data->header[data->header_size] = '\0';

        memcpy(data->uid, "069", KND_ID_SIZE);
        data->uid_size =  KND_ID_SIZE;
        return knd_OK;
    }
    
    rec = (struct kndAuthRec*)self->sid_idx->get(self->sid_idx, 
                                                 (const char*)data->sid);
    if (rec) {

        data->header_size = strlen("AUTH_OK");
        memcpy(data->header, "AUTH_OK", data->header_size);
        data->header[data->header_size] = '\0';

        
        memcpy(data->uid, rec->uid, KND_ID_SIZE);
        data->uid_size =  KND_ID_SIZE;

        return knd_OK;
    }

    data->header_size = strlen("AUTH_FAIL");
    memcpy(data->header, "AUTH_FAIL", data->header_size);
    data->header[data->header_size] = '\0';

    knd_log("   -- auth failure for SID \"%s\" :(\n", data->sid);

    return knd_AUTH_FAIL;
}

static int
kndDelivery_retrieve(struct kndDelivery *self, 
                     struct kndData *data)
{
    //char buf[KND_NAME_SIZE];
    //size_t buf_size;

    struct kndAuthRec *rec;
    struct kndResult *res;
    //time_t curr_time;
    //int err;

    if (!strcmp(data->sid, "000")) {
        rec = self->default_rec;

        res = (struct kndResult*)rec->cache->get\
            (rec->cache,
             (const char*)data->tid);
        if (!res) {
            data->header_size = strlen("WAIT");
            memcpy(data->header, "WAIT", data->header_size);
            data->header[data->header_size] = '\0';

            
            return knd_NEED_WAIT;
        }
        goto deliver;
    }
    
    if (!strcmp(data->sid, "AUTH")) {
        res = (struct kndResult*)self->auth_idx->get\
            (self->auth_idx,
             (const char*)data->tid);
        if (!res) {
            knd_log("-- no such TID: \"%s\"\n", data->tid);
            return knd_NEED_WAIT;
        }
    }
    else {
        rec = (struct kndAuthRec*)self->sid_idx->get(self->sid_idx, 
                                                     (const char*)data->sid);
        if (!rec) {
            knd_log("-- no such SID: %s\n", data->sid);
            return knd_AUTH_FAIL;
        }
        
        res = (struct kndResult*)rec->cache->get\
            (rec->cache,
             (const char*)data->tid);

        if (!res) return knd_NEED_WAIT;
    }
    
 deliver:


    knd_log("    ++ Delivery header: %s\n", res->header);
    knd_log("    ++ Delivery body: %s\n", res->body);

    
    strncpy(data->header, res->header, res->header_size);
    data->header_size = res->header_size;

    data->ref = res->body;
    data->ref_size = res->body_size;
    
    return knd_OK;
}


static int
kndDelivery_load_file(struct kndDelivery *self, 
                      struct kndData *data)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    struct kndAuthRec *rec;
    struct kndResult *res;
    char *output;
    int fd;
    int err;

    rec = (struct kndAuthRec*)self->uid_idx->get(self->uid_idx, 
                                                 (const char*)data->uid);
    if (!rec) {
        knd_log("-- no such UID: %s\n", data->uid);
        return knd_AUTH_FAIL;
    }
    
    res = (struct kndResult*)rec->cache->get\
	(rec->cache,
	 (const char*)data->tid);
    if (!res) return knd_NEED_WAIT;

    if (!res->filename_size) {
        knd_log("  --  no file size given :(\n");
        return knd_FAIL;
    }

    data->header_size = strlen("type=file");
    strncpy(data->header, "type=file", data->header_size);
    
    /* open file */
    buf_size = sprintf(buf, "%s/%s",
                       self->path,
                       res->filename);

    knd_log("  .. reading FILE \"%s\" [%lu] ...\n",
            buf, (unsigned long)res->filesize);

    output = malloc(sizeof(char) * res->filesize);
    if (!output) return knd_NOMEM;

    fd = open((const char*)buf, O_RDONLY);
    if (fd < 0) {
        knd_log("  --  no such file: \"%s\"\n", buf);
        err = knd_IO_FAIL;
        goto final;
    }

    err = read(fd, output, res->filesize);
    if (err == -1) {
        knd_log("  --  \"%s\" file read failure :(\n", buf);
        err = knd_IO_FAIL;
        goto final;
    }

    knd_log("  ++ FILE read OK!\n");

    data->reply = output;
    data->reply_size = res->filesize;

    err = knd_OK;

 final:
    return err;
}

static int
kndDelivery_lookup(struct kndDelivery *self, 
                   struct kndData *data)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    const char *key;
    const char *header;
    
    struct kndAuthRec *rec;
    struct kndResult *res;
    //time_t curr_time;
    //int i, err;

    /* non-authenticated user */
    if (!strcmp(data->uid, "000")) {
        rec = self->default_rec;
    }
    else if (!strcmp(data->uid, "069")) {
        rec = self->spec_rec;
    }
    else {
        rec = (struct kndAuthRec*)self->uid_idx->get(self->uid_idx, 
                                                     (const char*)data->uid);
        if (!rec) {
            knd_log("  -- no such UID: %s\n", data->uid);
            return knd_AUTH_FAIL;
        }
    }


    buf_size = sprintf(buf, "%s/%s",
                       data->classname, data->name);
    key = (const char*)buf;

    
    knd_log("\n    ?? CACHE lookup in Delivery storage: %s\n", key);
    
    res = (struct kndResult*)rec->cache->get\
	(rec->cache,
	 key);

    /* check output format */
    while (res) {
        if (DEBUG_DELIV_LEVEL_TMP)
            knd_log("   == available result format: %s\n",
                    knd_format_names[res->format]);
        if (res->format == data->format) break;

        res = res->next;
    }

    if (!res) {
        data->header_size = strlen("NOT_FOUND");
        memcpy(data->header, "NOT_FOUND", data->header_size);
        data->header[data->header_size] = '\0';

        knd_log("   -- no results for \"%s\" [format: %s]  :(\n", key,
                knd_format_names[data->format]);
        return knd_NO_MATCH;
    }

    knd_log("\n  ==  RESULTS in Delivery storage: %s FORMAT: %s HEADER: \"%s\"\n",
            res->body, knd_format_names[res->format], res->header);

    if (data->format == KND_FORMAT_HTML) {
        if (res->header_size && res->header_size < KND_TEMP_BUF_SIZE) {
            memcpy(data->header, res->header, res->header_size);
            data->header_size = res->header_size;
            data->header[data->header_size] = '\0';
        }
    }
    else {
        header = "{\"content-type\": \"application/json\"}";
        data->header_size = strlen(header);
        memcpy(data->header, header, data->header_size);
        data->header[data->header_size] = '\0';
    }

    data->ref = res->body;
    data->ref_size = res->body_size;

    return knd_OK;
}



static int
kndDelivery_get_updates(struct kndDelivery *self, 
                        xmlNodePtr input_node,
                        struct kndData *data)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size = KND_TEMP_BUF_SIZE;
    
    //char obj_id[KND_ID_SIZE + 1];
    //size_t obj_id_size = KND_ID_SIZE + 1;

    struct kndRepoRec *rec;
    int err;

    err = knd_get_xmlattr(input_node, "class", 
			  buf, &buf_size);
    if (err) {
        knd_log("  -- no class name given :(\n");
        goto final;
    }
    
    knd_log("   .. get updates of class \"%s\" ..\n", buf);

    rec = (struct kndRepoRec*)self->repo_idx->get(self->repo_idx,
                                                  (const char*)data->uid);
    
    while (rec) {
        if (!strcmp(rec->classname, buf))
            break;
        rec = rec->next;
    }

    if (!rec) {
        knd_log("  -- no data for class: %s\n", buf);

        data->header_size = strlen("NO DATA");
        memcpy(data->header, "NO DATA", data->header_size);
        data->header[data->header_size] = '\0';

        err = knd_FAIL;
        goto final;
    }

    data->ref = rec->obj;
    data->ref_size = rec->obj_size;
 
    err = knd_OK;

    data->header_size = strlen("OK");
    memcpy(data->header, "OK", data->header_size);
    data->header[data->header_size] = '\0';
    
 final:
    return err;
}

static int
kndDelivery_get_summaries(struct kndDelivery *self, 
                          xmlNodePtr input_node,
                          struct kndData *data)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size = KND_TEMP_BUF_SIZE;
    
    //char obj_id[KND_ID_SIZE + 1];
    //size_t obj_id_size = KND_ID_SIZE + 1;

    struct kndRepoRec *rec;
    int err;

    err = knd_get_xmlattr(input_node, "class", 
			  buf, &buf_size);
    if (err) {
        knd_log("  -- no class name given :(\n");
        goto final;
    }
    
    knd_log("   .. get updates of class \"%s\" ..\n", buf);

    rec = (struct kndRepoRec*)self->repo_idx->get(self->repo_idx,
                                                  (const char*)data->uid);
    
    while (rec) {
        if (!strcmp(rec->classname, buf))
            break;
        rec = rec->next;
    }

    if (!rec) {
        knd_log("  -- no data for class: %s\n", buf);

        data->header_size = strlen("NO DATA");
        memcpy(data->header, "NO DATA", data->header_size);
        data->header[data->header_size] = '\0';

        err = knd_FAIL;
        goto final;
    }

    data->ref = rec->summaries;
    data->ref_size = rec->summaries_size;
 
    err = knd_OK;

    data->header_size = strlen("OK");
    memcpy(data->header, "OK", data->header_size);
    data->header[data->header_size] = '\0';
    
 final:
    return err;
}



static int
kndDelivery_select(struct kndDelivery *self, 
                    struct kndData *data)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    const char *key;
    const char *header;
    struct kndAuthRec *rec;
    struct kndResult *res;
    //time_t curr_time;
    //int i, err;

    /* default reply */
    data->header_size = strlen("NOT_FOUND");
    memcpy(data->header, "NOT_FOUND", data->header_size);
    data->header[data->header_size] = '\0';

    if (!strcmp(data->uid, "000")) {
        rec = self->default_rec;
    }
    else if (!strcmp(data->uid, "069")) {
        rec = self->spec_rec;
    }
    else {
        rec = (struct kndAuthRec*)self->uid_idx->get(self->uid_idx, 
                                                     (const char*)data->uid);
        if (!rec) {
            knd_log("  -- no such UID: %s\n", data->uid);
            return knd_AUTH_FAIL;
        }
    }

    if (!data->query_size) {
        knd_log("    -- no query specified :(\n");
        return knd_NO_MATCH;
    }

    if (!data->classname_size) {
        knd_log("    -- no classname specified :(\n");
        return knd_NO_MATCH;
    }
    
    buf_size = sprintf(buf, "%s/%s",
                       data->classname, data->query);
    key = (const char*)buf;

    knd_log("    == SELECT KEY: %s\n", key);

    
    res = (struct kndResult*)rec->cache->get\
	(rec->cache, key);
    if (!res) {
        knd_log("   -- No results for name \"%s\" :(\n", key);
        return knd_NO_MATCH;
    }
    
    knd_log("  CACHE select: %s\n", res->body);


    header = "{\"content-type\": \"application/json\"}";
    
    data->header_size = strlen(header);
    memcpy(data->header, header, data->header_size);
    data->header[data->header_size] = '\0';

    if (data->format == KND_FORMAT_HTML) {

        knd_log("   NB: HTML required!\n");
        if (res->header_size && res->header_size < KND_TEMP_BUF_SIZE) {

            knd_log("    == special HTML header: %s\n", res->header);

            memcpy(data->header, res->header, res->header_size);
            data->header_size = res->header_size;
            data->header[data->header_size] = '\0';
        }
    }

    
    
    data->ref = res->body;
    data->ref_size = res->body_size;

    
    return knd_OK;
}



static int
kndDelivery_match(struct kndDelivery *self, 
                  xmlNodePtr input_node __attribute__((unused)),
                  struct kndData *data)
{
    //char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size;

    //const char *key;
    struct kndRepoRec *rec, *curr_rec;
    const char *match;
    //time_t curr_time;
    //int i, err;

    if (!data->classname_size) return knd_FAIL;
    if (!data->query_size) return knd_FAIL;

    data->header_size = strlen("NOT_FOUND");
    memcpy(data->header, "NOT_FOUND", data->header_size);
    data->header[data->header_size] = '\0';

    curr_rec = (struct kndRepoRec*)self->repo_idx->get(self->repo_idx,
                                                       (const char*)data->uid);
    if (!curr_rec) {
        return knd_OK;
    }
    
    rec = curr_rec;
    /* iterate over classes */
    while (rec) {
        if (!strcmp(rec->classname, data->classname))
            break;
        rec = rec->next;
    }

    if (!rec) return knd_OK;

    
    match = (char*)rec->matches->get(rec->matches,
                                     data->query);
    if (!match) {
        knd_log(" -- no match for \"%s\" :(\n", data->query);
        return knd_FAIL;
    }

    
    knd_log("  CACHE match: %s\n", match);

    data->ref = match;
    data->ref_size = strlen(match);


    data->header_size = strlen("OK");
    memcpy(data->header, "OK", data->header_size);
    data->header[data->header_size] = '\0';

    return knd_OK;
}


static int
kndDelivery_update_match(struct kndDelivery *self, 
                         xmlNodePtr input_node,
                         struct kndData *data)
{
    struct kndRepoRec *rec, *curr_rec;
    const char *match;
    int err;

    knd_log("\n  == UPDATE MATCH ..\n REC SIZE: %lu\n\n",
            (unsigned long)data->obj_size);

    data->classname_size = KND_NAME_SIZE;
    err = knd_get_xmlattr(input_node, "class",
                          data->classname, &data->classname_size);
    if (err) goto final;
    
    curr_rec = (struct kndRepoRec*)self->repo_idx->get(self->repo_idx,
                                                  (const char*)data->uid);
    rec = curr_rec;
    /* iterate over classes */
    while (rec) {
        if (!strcmp(rec->classname, data->classname))
            goto set_data;
        rec = rec->next;
    }

    if (!rec) {
        knd_log("  -- no prev IDX for class \"%s\" :(\n",
                data->classname);

        rec = malloc(sizeof(struct kndRepoRec));
        if (!rec) return knd_NOMEM;
        memset(rec, 0, sizeof(struct kndRepoRec));
        memcpy(rec->classname, data->classname, data->classname_size);

        err = ooDict_new(&rec->matches, KND_MEDIUM_DICT_SIZE);
        if (err) goto final;

        rec->next = curr_rec;
        
        err = self->repo_idx->set(self->repo_idx,
                                  (const char*)data->uid,
                                  (void*)rec);
        if (err) goto final;
    }

    set_data:
    
    knd_log("\n  == MATCH INPUT: %s Cache REC SIZE: %lu\n",
            data->query,
            (unsigned long)data->obj_size);

    match = (char*)rec->matches->get(rec->matches,
                                     data->query);
    if (!match) {
        err = rec->matches->set(rec->matches,
                                (const char*)data->query,
                                (void*)data->obj);
    }

    data->obj = NULL;
    
    /* construct reply message */
    data->reply = strdup(KND_DELIVERY_OK);
    if (!data->reply) return knd_NOMEM;
    data->reply_size = KND_DELIVERY_OK_SIZE;
    
    err = knd_OK;
    
 final:

    return err;
}


static int
kndDelivery_update_select(struct kndDelivery *self, 
                          xmlNodePtr input_node,
                          struct kndData *data)
{
    struct kndRepoRec *rec, *curr_rec;
    int err;

    /*knd_log("\n  == UPDATE SELECT ..\n REC SIZE: %lu\n\n",
      (unsigned long)data->obj_size); */

    data->classname_size = KND_NAME_SIZE;
    err = knd_get_xmlattr(input_node, "class",
                          data->classname, &data->classname_size);
    if (err) goto final;
    
    curr_rec = (struct kndRepoRec*)self->repo_idx->get(self->repo_idx,
                                                  (const char*)data->uid);
    rec = curr_rec;
    /* iterate over classes */
    while (rec) {
        if (!strcmp(rec->classname, data->classname))
            goto set_data;
        rec = rec->next;
    }

    if (!rec) {
        knd_log("  -- no rec for class \"%s\" :(\n", data->classname);
        rec = malloc(sizeof(struct kndRepoRec));
        if (!rec) return knd_NOMEM;
        memset(rec, 0, sizeof(struct kndRepoRec));
        memcpy(rec->classname, data->classname, data->classname_size);

        err = ooDict_new(&rec->matches, KND_MEDIUM_DICT_SIZE);
        if (err) goto final;
        rec->next = curr_rec;
        
        err = self->repo_idx->set(self->repo_idx,
                                  (const char*)data->uid,
                                  (void*)rec);
        if (err) goto final;
    }

    set_data:
    
    /* remove old cache data */
    if (rec->obj) {
        free(rec->obj);
    }
    
    /*knd_log("\n  == Cache REC SIZE: %lu\n",
            (unsigned long)data->obj_size);
    */
    
    rec->obj = data->obj;
    rec->obj_size = data->obj_size;
    data->obj = NULL;
    
    /* construct reply message */
    data->reply = strdup(KND_DELIVERY_OK);
    if (!data->reply) return knd_NOMEM;
    data->reply_size = KND_DELIVERY_OK_SIZE;
    
    err = knd_OK;
    
 final:

    return err;
}


static int
kndDelivery_update_summaries(struct kndDelivery *self, 
                          xmlNodePtr input_node,
                          struct kndData *data)
{
    struct kndRepoRec *rec, *curr_rec;
    int err;

    /*knd_log("\n  == UPDATE SUMMARIES ..\n REC: %s\n\n", data->obj);*/

    data->classname_size = KND_NAME_SIZE;
    err = knd_get_xmlattr(input_node, "class",
                          data->classname, &data->classname_size);
    if (err) goto final;
    
    curr_rec = (struct kndRepoRec*)self->repo_idx->get(self->repo_idx,
                                                  (const char*)data->uid);
    rec = curr_rec;
    /* iterate over classes */
    while (rec) {
        if (!strcmp(rec->classname, data->classname))
            goto set_data;
        rec = rec->next;
    }

    if (!rec) {
        knd_log("  -- no rec for class \"%s\" :(\n", data->classname);
        rec = malloc(sizeof(struct kndRepoRec));
        if (!rec) return knd_NOMEM;
        memset(rec, 0, sizeof(struct kndRepoRec));
        memcpy(rec->classname, data->classname, data->classname_size);

        rec->next = curr_rec;
        
        err = self->repo_idx->set(self->repo_idx,
                                  (const char*)data->uid,
                                  (void*)rec);
        if (err) goto final;
    }

    set_data:
    
    /* remove old cache data */
    if (rec->summaries) {
        free(rec->summaries);
    }
    
    rec->summaries = data->obj;
    rec->summaries_size = data->obj_size;
    data->obj = NULL;
    
    /* construct reply message */
    data->reply = strdup(KND_DELIVERY_OK);
    if (!data->reply) return knd_NOMEM;
    data->reply_size = KND_DELIVERY_OK_SIZE;
    
    err = knd_OK;
    
 final:

    return err;
}


static int
kndDelivery_set_SID(struct kndDelivery *self, 
                    xmlNodePtr input_node,
                    struct kndData *data)
{
    struct kndAuthRec *rec;
    //size_t curr_size;
    int err;

    data->sid_size = KND_TEMP_BUF_SIZE;
    err = knd_get_xmlattr(input_node, "acc_sid", 
                          data->sid, &data->sid_size);
    if (err) {
        strcpy(data->sid, "RANDOM_SID");
        err = knd_OK;
        goto final;
    }
    
    knd_log("ACC SID: %s\n", data->sid);
    
    data->uid_size = KND_ID_SIZE + 1;
    err = knd_get_xmlattr(input_node, "acc_uid", 
                          data->uid, &data->uid_size);
    if (err) {
        goto final;
    }

    knd_log("\n    Delivery Register SID: \"%s\" UID: \"%s\"\n", 
            data->sid, data->uid);

    /* create new auth record */
    rec = malloc(sizeof(struct kndAuthRec));
    if (!rec) return knd_NOMEM;
    
    memset(rec, 0, sizeof(struct kndAuthRec));
    memcpy(rec->uid, data->uid, KND_ID_SIZE);

    err = ooDict_new(&rec->cache, KND_MEDIUM_DICT_SIZE);
    if (err) goto final;

    err = self->sid_idx->set(self->sid_idx, 
			       (const char*)data->sid, 
			       (void*)rec);
    if (err) return err;

    err = self->uid_idx->set(self->uid_idx, 
			       (const char*)data->uid, 
			       (void*)rec);
    if (err) return err;

 final:
    return err;
}

static int
kndDelivery_save_file(struct kndDelivery *self, 
                      xmlNodePtr input_node,
                      struct kndData *data,
                      struct kndResult *res)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    struct stat st;
    long filesize = 0;
    int fd;
    int err;

    err = knd_get_xmlattr_num(input_node,
                              "filesize", &filesize);
    if (!filesize) {
        knd_log("  -- no file size specified :(\n");
        return err;
    }
    
    if (filesize < 0) {
        knd_log("  -- negative file size given :(\n");
        return knd_FAIL;
    }
    
    data->filesize = (unsigned long)filesize;
    res->filesize = (unsigned long)filesize;

    /*if (data->obj_size != data->filesize) {
        knd_log("  -- wrong file size given? :(\n");
        return knd_FAIL;
        }*/

    data->mimetype_size = KND_NAME_SIZE;
    err = knd_get_xmlattr(input_node, "mime",
                          data->mimetype, &data->mimetype_size);
    if (err) {
        knd_log("  -- MIME type not specified :(\n");
        return knd_FAIL;
    }

    res->mimetype = strdup(data->mimetype);
    if (!res->mimetype) return knd_NOMEM;
    res->mimetype_size = data->mimetype_size;
    
    res->filename = strdup(data->filepath);
    if (!res->filename) return knd_NOMEM;
    res->filename_size = data->filepath_size;

    
    knd_log("PATH: %s size: %lu\n",
            self->path,
            (unsigned long)self->path_size);

    
    if (self->path_size && self->path[self->path_size - 1] == '/')
        buf_size = sprintf(buf, "%s%s", self->path, data->filepath);
    else
        buf_size = sprintf(buf, "%s/%s", self->path, data->filepath);
    

    knd_log("\n    .. DELIVERY service: saving file in \"%s\"..\n\n", buf);

    
    /* file already exists? */
    if (!stat(buf, &st)) {
        knd_log("   .. File \"%s\" already exists!\n", buf);
        return knd_OK;
    }

    return knd_OK;
    
    err = knd_mkpath(buf, 0755, true);
    if (err) return err;

    /* write file */
    fd = open((const char*)buf,  
              O_WRONLY | O_TRUNC | O_CREAT, 0644);
    if (fd < 0) return knd_IO_FAIL;
    err = write(fd, data->obj, data->obj_size);
    if (err == -1)
        err = knd_FAIL;
    else
        err = knd_OK;

    /*res->filesize = data->obj_size;*/
    
    close(fd);

    return err;
}


static int
kndDelivery_auth(struct kndDelivery *self, 
                 xmlNodePtr input_node,
                 struct kndData *data)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size = KND_TEMP_BUF_SIZE;

    //const char *query;
    const char *key;
    struct kndResult *res;
    int err;
    
    key = (const char*)data->tid;

    res = (struct kndResult*)self->auth_idx->get\
        (self->auth_idx, key);
    if (res) {
        err = knd_OK;
        knd_log("   .. \"%s\" is already in cache!\n", key);
        goto final;
    }

    err = kndDelivery_set_SID(self, input_node, data);
    if (err) goto final;
    
    /* create new record */
    res = malloc(sizeof(struct kndResult));
    if (!res) return knd_NOMEM;
    
    memset(res, 0, sizeof(struct kndResult));

    buf_size = sprintf(buf, "{\"sid\": \"%s\"}", data->sid);

    res->body = strdup(buf);
    if (!res->body) return knd_NOMEM;
    res->body_size = buf_size;

    err = self->auth_idx->set(self->auth_idx, 
                              (const char*)data->tid,
                              (void*)res);
    if (err) return err;

    
    knd_log("    ++ saved auth SID \"%s\" in cache!\n", data->sid);
    
    /* TODO: inform controller about new results */

final:

    res->header = strdup("type=JSON");
    res->header_size = strlen("type=JSON");

    /* construct reply message */
    data->reply = strdup(KND_DELIVERY_OK);
    if (!data->reply) return knd_NOMEM;

    data->reply_size = KND_DELIVERY_OK_SIZE;

    return err;
}



static int
kndDelivery_report(struct kndDelivery *self, 
                   xmlNodePtr input_node,
                   struct kndData *data)
{
    //char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size = KND_TEMP_BUF_SIZE;

    struct kndAuthRec *rec;
    //struct kndTrans *trn;
    struct kndResult *res;

    long numval = 0;
    int err = knd_FAIL;

    buf_size = KND_ID_SIZE + 1;
    err = knd_get_xmlattr(input_node, "uid",
                          data->uid, &buf_size);
    if (err) {
        knd_log("  -- UID is required :(");
        return err;
    }

    if (!strcmp(data->uid, "000")) {
        rec = self->default_rec;
    }
    else {
        rec = (struct kndAuthRec*)self->uid_idx->get(self->uid_idx, 
                                                     (const char*)data->uid);
        if (!rec) {
            knd_log("  -- UID %s is not valid :(", data->uid);
            return err;
        }
    }
    err = knd_get_xmlattr_num(input_node, "state",
                          &numval);
    if (err) {
        knd_log("  -- state field is required :(");
        return err;
    }
    
    knd_log("   == got REPORT for TID %s, state: %lu\n", data->tid, (unsigned long)numval);
    
    /* create new record */
    res = malloc(sizeof(struct kndResult));
    if (!res) return knd_NOMEM;
    
    memset(res, 0, sizeof(struct kndResult));

    res->proc_state = numval;
    
    /* register by TID */
    err = rec->cache->set(rec->cache, 
                          (const char*)data->tid,
                          (void*)res);
    if (err) return err;
    
    knd_log("  ++ saved report for TID \"%s\" in cache (UID: %s)!\n",
            data->tid, data->uid);

    /* construct reply message */
    data->reply = strdup(KND_DELIVERY_NO_RESULTS);
    if (!data->reply) return knd_NOMEM;
    data->reply_size = KND_DELIVERY_NO_RESULTS_SIZE;

    
    return err;
}


static int
kndDelivery_save(struct kndDelivery *self, 
                 xmlNodePtr input_node,
                 struct kndData *data)
{
    char keybuf[KND_TEMP_BUF_SIZE];
    size_t keybuf_size = 0;

    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size = KND_TEMP_BUF_SIZE;

    struct kndAuthRec *rec;
    struct kndTrans *trn;
    //const char *query;
    const char *key;
    knd_format format = KND_FORMAT_JSON;
    
    struct kndResult *res = NULL;
    struct kndResult *prev_res = NULL;
    int err;

    buf_size = KND_ID_SIZE + 1;
    err = knd_get_xmlattr(input_node, "uid",
                          data->uid, &buf_size);
    if (err) {
        knd_log("  -- UID is required :(");
        return err;
    }

    if (!strcmp(data->uid, "000")) {
        rec = self->default_rec;
    }
    else {
        rec = (struct kndAuthRec*)self->uid_idx->get(self->uid_idx, 
                                                     (const char*)data->uid);
        if (!rec) {
            knd_log("  -- UID %s is not valid :(", data->uid);
            return err;
        }
    }

    buf_size = KND_NAME_SIZE;
    err = knd_get_xmlattr(input_node, "format",
                          buf, &buf_size);
    if (!err) {
        if (!strcmp(buf, "HTML")) {
            format = KND_FORMAT_HTML;
        }
    }

    
    /* check previous results */
    if (data->query_size) {
        keybuf_size = data->classname_size + data->query_size + 1;
        if (keybuf_size >= KND_TEMP_BUF_SIZE) {
            err = knd_LIMIT;
            goto final;
        }


        /* TODO: classname storage  */
        
        keybuf_size = sprintf(keybuf, "%s/%s",
                              data->classname, data->query);
        key = (const char*)keybuf;
    }
    else {
        key = (const char*)data->tid;
    }

    
    prev_res = (struct kndResult*)rec->cache->get\
        (rec->cache,  key);

    res = prev_res;
    while (res) {
        if (res->format == format)
            break;
        res = res->next;
    }

    /* result already in cache */
    if (res) {

        knd_log("   == KEY \"%s\"  existing RESULT: %s\n", key, res->body);

        knd_log("   == NEW RESULT: %s\n", key, data->body);

        if (res->body) free(res->body);
        res->body = strdup(data->body);
        
        if (!res->body) return knd_NOMEM;
        res->body_size = data->body_size;
        
        err = knd_OK;
        goto confirm;
    }


    knd_log("   -- no \"%s\" query in cache, creating new RESULT REC..\n", key);
    
    /* create new record */
    res = malloc(sizeof(struct kndResult));
    if (!res) return knd_NOMEM;
    
    memset(res, 0, sizeof(struct kndResult));
    res->format = format;
    
    res->body = strdup(data->body);
    if (!res->body) return knd_NOMEM;
    res->body_size = data->body_size;

    res->next = prev_res;
    
    /*  file attachment present? */
    err = knd_copy_xmlattr(input_node,
                           "filepath",
                           &data->filepath,
                           &data->filepath_size);

    if (err) {
        /* default simple header */
        buf_size = sprintf(buf, "{content-type: \"text/json\"}");
    }
    else {
        /* file attach */
        /*knd_log("\n\n  == FILE to save: %s\n\n", data->filepath);*/
        
        err = kndDelivery_save_file(self, input_node, data, res);
        if (err) goto final;
        
        /* build header */
        buf_size = sprintf(buf, "filename=%s&size=%lu",
                           data->filepath,
                           (unsigned long)data->filesize);
    }

    /* set meta headers */
    if (format == KND_FORMAT_HTML) {
        res->header = strdup(data->obj);
        if (!res->header) return knd_NOMEM;
        res->header_size = data->obj_size;
    }
    else {
        res->header = strdup(buf);
        if (!res->header) return knd_NOMEM;

        res->header_size = buf_size;
    }
    
    /* register by TID */
    err = rec->cache->set(rec->cache, 
                          (const char*)data->tid,
                          (void*)res);
    if (err) return err;

    knd_log("  ++ saved TID \"%s\" in cache (UID: %s)!\n",
            data->tid, data->uid);
    
    /* register by text key */
    if (keybuf_size) {

        if (prev_res)
            res->next = prev_res;
        
        err = rec->cache->set(rec->cache, 
                              key,
                              (void*)res);
        if (err) return err;
        
        knd_log("    ++ saved KEY \"%s\" in cache [format: %s]  HEADER: %s\n\n",
                key, knd_format_names[format], res->header);
    }

    err = knd_OK;
    
    /* TODO: inform controller about new results */

 confirm:    

    /* confirm TID in monitor */
    trn = self->monitor->idx->get(self->monitor->idx, (const char*)data->tid);
    if (trn) {
        /* TODO: check positive answer! */
        trn->proc_state = KND_SUCCESS;
    }

    
final:

    /* construct reply message */
    data->reply = strdup(KND_DELIVERY_OK);
    if (!data->reply) return knd_NOMEM;
    data->reply_size = KND_DELIVERY_OK_SIZE;

    return err;
}

static int
kndDelivery_process(struct kndDelivery *self, 
		    struct kndData *data)
{
    xmlDocPtr doc;
    xmlNodePtr root;
    //xmlNodePtr cur_node;

    char action[KND_NAME_SIZE];
    size_t action_size;

    char buf[KND_NAME_SIZE];
    size_t buf_size = KND_NAME_SIZE;

    //long filesize = 0;
    int err = knd_OK;
    bool filter_set = false;
    int ret;
    
    if (!data->spec) return knd_FAIL;

    doc = xmlReadMemory(data->spec, 
			data->spec_size, 
			"none.xml", NULL, 0);
    if (!doc) {
        knd_log("   -- Failed to parse document :(\n");
	return knd_FAIL;
    }
    
    root = xmlDocGetRootElement(doc);
    if (!root) {
	err = knd_FAIL;
	goto final;
    }

    if (xmlStrcmp(root->name, (const xmlChar *) "spec")) {
        knd_log("  -- SPEC XML-document error: the root node " 
		" must be \"spec\" :(");
	err = knd_FAIL;
	goto final;
    }

    /* SID is mandatory */
    data->sid_size = KND_TEMP_BUF_SIZE;
    err = knd_get_xmlattr(root, "sid", 
                          data->sid, &data->sid_size);
    if (err) {
        knd_log("  -- no SID provided :(\n");
        goto final;
    }
    
    action_size = KND_NAME_SIZE;
    err = knd_get_xmlattr(root, "action", 
			  action, &action_size);
    if (err) {
        knd_log("  -- no action specified :(\n");
        goto final;
    }

    /* check SID
       TODO: add real certificates  */
    err = kndDelivery_check_SID(self, root, data, action);
    if (err) {
        goto final;
    }
    
    /*knd_log("  ++ SID \"%s\" check OK: UID: %s\n",
      data->sid, data->uid); */

    err = knd_AUTH_OK;

    /*if (!strcmp(action, "get_uid")) {
        err = knd_AUTH_OK;
        goto final;
        }*/
    

    /* optional fields (pass to funcs?) */

    data->tid_size = KND_TID_SIZE + 1;
    ret = knd_get_xmlattr(root, "tid", 
                          data->tid, &data->tid_size);
    if (ret) {
        memset(data->tid, '0', KND_TID_SIZE);
        data->tid[KND_TID_SIZE] = '\0';
        data->tid_size = KND_TID_SIZE;
    }
    
    data->classname_size = KND_NAME_SIZE;
    ret = knd_get_xmlattr(root, "class",
                          data->classname, &data->classname_size);
    if (ret) data->classname_size = 0;
    
    data->name_size = KND_NAME_SIZE;
    ret = knd_get_xmlattr(root, "name",
                          data->name, &data->name_size);
    if (ret) data->name_size = 0;

    ret = knd_get_xmlattr(root, "f",
                          buf, &buf_size);
    if (!ret) filter_set = true;

    ret = knd_get_xmlattr(root, "format",
                          buf, &buf_size);
    if (!ret) {
        if (!strcmp(buf, "HTML"))
            data->format = KND_FORMAT_HTML;
        else if (!strcmp(buf, "JS"))
            data->format = KND_FORMAT_JS;
    }
    
    /* TODO: run security check on TID */
    /* TODO: check
       - service abuse
       - priviliges
       - priority
    */


    /* TODO
       err = kndDelivery_learn_action_type(action, action_size, &action_type);
    if (err) goto final;
    */
    

    /* TODO: update request DB */

    
    if (!strcmp(action, "select")) {
        if (filter_set) {
            err = self->monitor->select(self->monitor, root, data);
            goto final;
        }
        
        if (!strcmp(data->sid, "m0n1t0r")) {
            err = self->monitor->select(self->monitor, root, data);
            goto final;
        }

	err = kndDelivery_select(self, data);
        ret = self->monitor->process(self->monitor, root, data, action);
        goto final;
    }

    if (!strcmp(action, "match")) {
	err = kndDelivery_match(self, root, data);
        goto final;
    }

    if (!strcmp(action, "update_match")) {
	err = kndDelivery_update_match(self, root, data);
        goto final;
    }

    if (!strcmp(action, "update_select")) {
	err = kndDelivery_update_select(self, root, data);
        goto final;
    }

    if (!strcmp(action, "update_summaries")) {
	err = kndDelivery_update_summaries(self, root, data);
        goto final;
    }
   
    if (!strcmp(action, "auth")) {
	err = kndDelivery_auth(self, root, data);
    }

    if (!strcmp(action, "save")) {
	err = kndDelivery_save(self, root, data);
    }

    if (!strcmp(action, "report")) {
	err = kndDelivery_report(self, root, data);
    }

    if (!strcmp(action, "retrieve")) {
	err = kndDelivery_retrieve(self, data);
    }

    if (!strcmp(action, "load")) {
	err = kndDelivery_load_file(self, data);
    }

    /*if (!strcmp(action, "set_updates")) {
	err = kndDelivery_set_updates(self, root, data);
        }*/

    /*if (!strcmp(action, "get_history")) {
	err = kndDelivery_get_history(self, root, data);
        }*/

    if (!strcmp(action, "get_updates")) {
	err = kndDelivery_get_updates(self, root, data);
    }
    
    if (!strcmp(action, "get_summaries")) {
	err = kndDelivery_get_summaries(self, root, data);
    }

    if (!strcmp(action, "get")) {
	err = kndDelivery_lookup(self, data);
        ret = self->monitor->process(self->monitor, root, data, action);
    }

    if (!strcmp(action, "interp")) {
        data->query_size = KND_NAME_SIZE;

        err = knd_get_xmlattr(root, "t",
                              data->query, &data->query_size);

        data->header_size = strlen("NOT_FOUND");
        memcpy(data->header, "NOT_FOUND", data->header_size);
        data->header[data->header_size] = '\0';
        
        if (data->body_size && strcmp(data->body, "None")) {
            knd_log("  ++ save interp to cache: %s\n\n", data->body);
        }
        else {
            ret = self->monitor->process(self->monitor, root, data, action);
        }
        
        err = knd_NO_MATCH;
        /*	err = kndDelivery_interp(self, data);*/
    }

    if (!strcmp(action, "flatten")) {
	err = kndDelivery_lookup(self, data);
    }

    if (!strcmp(action, "sync")) {
	err = self->monitor->sync(self->monitor);
    }

 final:

    xmlFreeDoc(doc);

    return err;
}



/**
 *  kndDelivery network service startup
 */
static int 
kndDelivery_start(struct kndDelivery *self)
{
    void *context;
    void *service;
    //void *control;

    struct kndData *data;

    const char *reply = NULL;
    size_t reply_size = 0;

    //char buf[KND_TEMP_BUF_SIZE];

    int err;

    const char *err_msg = "{\"error\": \"incorrect call\"}";
    size_t err_msg_size = strlen(err_msg);

    const char *wait_msg = "{\"wait\": \"1\"}";
    size_t wait_msg_size = strlen(wait_msg);

    const char *auth_msg = "{\"error\": \"authentication failure\"}";
    size_t auth_msg_size = strlen(auth_msg);

    context = zmq_init(1);

    /*control = zmq_socket(context, ZMQ_PUSH);
    if (!control) return knd_FAIL;
    err = zmq_connect(control, "tcp://127.0.0.1:5561"); */

    service = zmq_socket(context, ZMQ_REP);
    assert(service);

    /* tcp://127.0.0.1:6902 */
    assert((zmq_bind(service, "ipc:///var/lib/knowdy/deliv") == knd_OK)); // fixme: remove hardcode

    err = kndData_new(&data);
    if (err) return knd_FAIL;

    knd_log("\n\n    ++ %s is up and running: %s\n\n",
            self->name, "ipc:///var/lib/knowdy/deliv"); // fixme: remove hardcode

    while (1) {
        /* reset data */
	data->reset(data);
        
	reply = err_msg;
	reply_size = err_msg_size;

        knd_log("\n    ++ DELIVERY service is waiting for new tasks...\n");

        data->spec = knd_zmq_recv(service, &data->spec_size);
        data->query = knd_zmq_recv(service, &data->query_size);
        data->body = knd_zmq_recv(service, &data->body_size);
        data->obj = knd_zmq_recv(service, &data->obj_size);

	knd_log("    ++ DELIVERY service has got spec:\n   %s  QUERY: %s  META/OBJ: %s\n\n",
                data->spec, data->query, data->obj);

	err = self->process(self, data);
        
	if (err == knd_NO_MATCH || err == knd_AUTH_OK) {
	    reply = data->uid;
	    reply_size = KND_ID_SIZE;
            goto final;
	}

        if (err == knd_NEED_WAIT) {
            data->header_size = strlen("WAIT");
            memcpy(data->header, "WAIT", data->header_size);
            data->header[data->header_size] = '\0';

            reply = wait_msg;
	    reply_size = wait_msg_size;
            goto final;
	}

        if (err == knd_AUTH_FAIL) {
	    reply = auth_msg;
	    reply_size = auth_msg_size;
	    goto final;
	}

	/* TODO: notify controller */
	/*if (data->control_msg) {
	}*/

        if (!data->header_size) {
            data->header_size = strlen("type=JSON");
            memcpy(data->header, "type=JSON", data->header_size);
            data->header[data->header_size] = '\0';
        }

        /* newly allocated reply */
	if (data->reply_size) {
	    reply = (const char*)data->reply;
	    reply_size = data->reply_size;
	}

        /* passing results by ref */
	if (data->ref_size) {
	    reply = (const char*)data->ref;
	    reply_size = data->ref_size;
        }

    final:

        knd_log("     .. ERR code: %d  sending  HEADER: \"%s\"  REPLY: \"%s\"\n\n",
                err, data->header, data->reply);

        
        knd_zmq_sendmore(service, (const char*)data->header, data->header_size);
	knd_zmq_send(service, reply, reply_size);

        fflush(stdout);
    }

    /* we never get here */
    zmq_close(service);
    zmq_term(context);

    return knd_OK;
}



static int
kndDelivery_read_config(struct kndDelivery *self,
                        const char *config)
{
    char buf[KND_TEMP_BUF_SIZE];
    xmlDocPtr doc;
    xmlNodePtr root, cur_node, child;
    int err;

    buf[0] = '\0';

    doc = xmlParseFile((const char*)config);
    if (!doc) {
	fprintf(stderr, "\n    -- No prior config file found."
                        " Fresh start!\n");
	err = -1;
	goto final;
    }

    root = xmlDocGetRootElement(doc);
    if (!root) {
	fprintf(stderr,"empty document\n");
	err = -2;
	goto final;
    }

    if (xmlStrcmp(root->name, (const xmlChar *) "deliv")) {
	fprintf(stderr,"Document of the wrong type: the root node " 
		" must be \"deliv\"");
	err = -3;
	goto final;
    }

    err = knd_copy_xmlattr(root, "name", 
			   &self->name, &self->name_size);
    if (err) goto final;

    err = knd_copy_xmlattr(root, "path", 
			   &self->path, &self->path_size);
    if (err) goto final;


    for (cur_node = root->children; cur_node; cur_node = cur_node->next) {
        if (cur_node->type != XML_ELEMENT_NODE) continue;

        if ((!xmlStrcmp(cur_node->name, (const xmlChar *)"monitor"))) {

            err = knd_copy_xmlattr(cur_node, "path", 
                                   &self->monitor->path, &self->monitor->path_size);
            if (err) goto final;

            sprintf(buf, "%s/event.db", self->monitor->path);
            self->monitor->db_filename = strdup(buf);
            if (!self->monitor->db_filename) {
                err = knd_NOMEM;
                goto final;
            }

    

            for (child = cur_node->children; child; child = child->next) {
                if (child->type != XML_ELEMENT_NODE) continue;
                
                if ((!xmlStrcmp(child->name, (const xmlChar *)"geoip"))) {
                    err = self->monitor->read_geo(self->monitor, child);
                    if (err) return err;
                }
                
            }


        }
        
    }
    
final:
    xmlFreeDoc(doc);

    return err;
}

static int
kndDelivery_init(struct kndDelivery *self)
{
    
    self->str = kndDelivery_str;
    self->del = kndDelivery_del;

    self->start = kndDelivery_start;
    self->process = kndDelivery_process;


    
    return knd_OK;
}

extern int
kndDelivery_new(struct kndDelivery **deliv,
                const char          *config)
{
    struct kndDelivery *self;
    struct kndAuthRec *rec;
    //char *path;
    int err;
    
    self = malloc(sizeof(struct kndDelivery));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndDelivery));

    /* TODO: tid allocation ring */

    err = ooDict_new(&self->auth_idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&self->sid_idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&self->uid_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&self->repo_idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    /* create DEFAULT user record */
    rec = malloc(sizeof(struct kndAuthRec));
    if (!rec) {
        err = knd_NOMEM;
        goto error;
    }
    
    memset(rec, 0, sizeof(struct kndAuthRec));
    memcpy(rec->uid, "000", KND_ID_SIZE);

    err = ooDict_new(&rec->cache, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    self->default_rec = rec;

    /*  special user record */
    rec = malloc(sizeof(struct kndAuthRec));
    if (!rec) {
        err = knd_NOMEM;
        goto error;
    }
    
    memset(rec, 0, sizeof(struct kndAuthRec));
    memcpy(rec->uid, "069", KND_ID_SIZE);

    err = ooDict_new(&rec->cache, KND_LARGE_DICT_SIZE);
    if (err) goto error;
    self->spec_rec = rec;

    
    /* output buffer */
    err = kndOutput_new(&self->out, KND_LARGE_BUF_SIZE);
    if (err) return err;

    kndDelivery_init(self); 

    err = kndMonitor_new(&self->monitor);
    if (err) {
        fprintf(stderr, "Couldn\'t load kndMonitor... ");
        return -1;
    }
    self->monitor->out = self->out;
    
    err = kndDelivery_read_config(self, config);
    if (err) goto error;


    if (self->path) {
        err = knd_mkpath(self->path, 0777, false);
        if (err) return err;
    }
    
    *deliv = self;

    return knd_OK;

 error:

    kndDelivery_del(self);

    return err;
}
