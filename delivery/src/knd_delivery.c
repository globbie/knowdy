#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>

#include <knd_config.h>
#include <knd_output.h>
#include <knd_msg.h>
#include <knd_task.h>

#include "knd_delivery.h"

#define DEBUG_DELIV_LEVEL_0 0
#define DEBUG_DELIV_LEVEL_1 0
#define DEBUG_DELIV_LEVEL_2 0
#define DEBUG_DELIV_LEVEL_3 0
#define DEBUG_DELIV_LEVEL_TMP 1


static void
str(struct kndDelivery *self)
{
    knd_log("<struct kndDelivery at %p>", self);

}

static int
del(struct kndDelivery *self)
{

    free(self);
    return knd_OK;
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
        data->obj = knd_zmq_recv(service, &data->obj_size);

	knd_log("    ++ DELIVERY service has got spec:\n   %s  QUERY: %s  META/OBJ: %s\n\n",
                data->spec, data->query, data->obj);

	//err = self->process(self, data);
        
	/*if (err == knd_NO_MATCH || err == knd_AUTH_OK) {
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
        */
	/* TODO: notify controller */
	/*if (data->control_msg) {
	}*/

        

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
run_set_db_path(void *obj,
                struct kndTaskArg *args, size_t num_args)
{
    struct kndDelivery *self;
    struct kndTaskArg *arg;
    const char *path = NULL;
    size_t path_size = 0;
    int err;
    
    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "path", strlen("path"))) {
            path = arg->val;
            path_size = arg->val_size;
        }
    }

    if (!path_size) return knd_FAIL;

    self = (struct kndDelivery*)obj;

    if (DEBUG_DELIV_LEVEL_TMP)
        knd_log(".. set DB path to \"%.*s\"", path_size, path);
    
    return knd_OK;
}

static int
run_set_service_addr(void *obj,
                     struct kndTaskArg *args, size_t num_args)
{
    struct kndDelivery *self;
    struct kndTaskArg *arg;
    const char *addr = NULL;
    size_t addr_size = 0;
    int err;
    
    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "service", strlen("service"))) {
            addr = arg->val;
            addr_size = arg->val_size;
        }
    }

    if (!addr_size) return knd_FAIL;

    self = (struct kndDelivery*)obj;

    
    if (DEBUG_DELIV_LEVEL_TMP)
        knd_log(".. set service addr to \"%.*s\"", addr_size, addr);

    
    return knd_OK;
}

static int
parse_config_GSL(struct kndDelivery *self,
                 const char *rec,
                 size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = KND_NAME_SIZE;
    size_t chunk_size = 0;
    
    const char *gsl_format_tag = "{gsl";
    size_t gsl_format_tag_size = strlen(gsl_format_tag);

    const char *header_tag = "{knd::Delivery Service Configuration";
    size_t header_tag_size = strlen(header_tag);
    const char *c;
    
    struct kndTaskSpec specs[] = {
        { .name = "service",
          .name_size = strlen("service"),
          .run = run_set_service_addr,
          .obj = self
        },
        { .name = "path",
          .name_size = strlen("path"),
          .run = run_set_db_path,
          .obj = self
        }
    };
    int err;

    if (strncmp(rec, gsl_format_tag, gsl_format_tag_size)) {
        knd_log("-- not a GSL format");
        return err;
    }

    rec += gsl_format_tag_size;

    err = knd_get_schema_name(rec,
                              buf, &buf_size, &chunk_size);
    if (!err) {
        rec += chunk_size;
        if (DEBUG_DELIV_LEVEL_TMP)
            knd_log("== got schema: \"%s\"", buf);
    }
    
    if (strncmp(rec, header_tag, header_tag_size)) {
        knd_log("-- wrong GSL class header");
        return err;
    }
    c = rec + header_tag_size;
    
    err = knd_parse_task(c, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) {
        knd_log("-- config parse error: %d", err);
        return err;
    }
    
    return knd_OK;
}



static int
kndDelivery_init(struct kndDelivery *self)
{
    
    self->str = str;
    self->del = del;

    self->start = kndDelivery_start;
    
    return knd_OK;
}

extern int
kndDelivery_new(struct kndDelivery **deliv,
                const char          *config)
{
    struct kndDelivery *self;
    struct kndAuthRec *rec;
    size_t chunk_size = 0;
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

    err = self->out->read_file(self->out, config, strlen(config));
    if (err) return err;
    
    err = parse_config_GSL(self, self->out->file, &chunk_size);
    if (err) goto error;

    if (self->path) {
        err = knd_mkpath(self->path, 0777, false);
        if (err) return err;
    }
    
    *deliv = self;

    return knd_OK;

 error:

    del(self);

    return err;
}
