#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>

typedef unsigned int uint;
#include <my_global.h>
#include <mysql.h>

#include "knd_config.h"
#include "knd_output.h"
#include "knd_utils.h"
#include "knd_msg.h"
#include "knd_task.h"
#include "knd_parser.h"

#include "knd_auth.h"

#define DEBUG_DELIV_LEVEL_0 0
#define DEBUG_DELIV_LEVEL_1 0
#define DEBUG_DELIV_LEVEL_2 0
#define DEBUG_DELIV_LEVEL_3 0
#define DEBUG_DELIV_LEVEL_TMP 1

const char *get_tokens_query = "SELECT * FROM access_token WHERE expires_at > UNIX_TIMESTAMP()";

static void
str(struct kndAuth *self)
{
    knd_log("<struct kndAuth at %p>", self);
}

static int
del(struct kndAuth *self)
{

    free(self);
    return knd_OK;
}

static int update_tokens(struct kndAuth *self)
{
    MYSQL *conn = mysql_init(NULL);
    MYSQL_RES *result = NULL;
    MYSQL_ROW row;
    unsigned int num_fields;
    unsigned int i;
    unsigned int row_count;
    int err;
    
    if (!conn) {
        fprintf(stderr, "%s\n", mysql_error(conn));
        return knd_FAIL;
    }

    if (mysql_real_connect(conn, self->db_host,
                           "content-server", "content-server",
                           "content-server_001", 0, NULL, 0) == NULL) {
        fprintf(stderr, "%s\n", mysql_error(conn));
        err = knd_FAIL;
        goto final;
    }

    err = mysql_query(conn, get_tokens_query);
    if (err) {
        fprintf(stderr, "%s\n", mysql_error(conn));
        goto final;
    }

    result = mysql_store_result(conn);
    if (!result) {
        fprintf(stderr, "%s\n", mysql_error(conn));
        goto final;
    }

    num_fields = mysql_num_fields(result);
    row_count = 0;
    while ((row = mysql_fetch_row(result))) {
        unsigned long *lengths;
        lengths = mysql_fetch_lengths(result);
        printf("%d: ", row_count);
        for (i = 0; i < num_fields; i++) {
            printf("[%.*s] ", (int)lengths[i],
                   row[i] ? row[i] : "NULL");
        }
        printf("\n");
        row_count++;
    }
    
    err = knd_OK;

 final:
    if (result)
        mysql_free_result(result);

    mysql_close(conn);
    return err;
}

static int run_set_tid(void *obj,
                       struct kndTaskArg *args, size_t num_args)
{
    struct kndAuth *self;
    struct kndTaskArg *arg;
    const char *tid = NULL;
    size_t tid_size = 0;

    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "tid", strlen("tid"))) {
            tid = arg->val;
            tid_size = arg->val_size;
        }
    }

    if (!tid_size) return knd_FAIL;
    self = (struct kndAuth*)obj;

    if (tid_size >= KND_TID_SIZE) return knd_LIMIT;
    memcpy(self->tid, tid, tid_size);
    self->tid[tid_size] = '\0';
    self->tid_size = tid_size;
   
    return knd_OK;
}

static int run_check_sid(void *obj,
                         struct kndTaskArg *args, size_t num_args)
{
    struct kndAuth *self;
    struct kndTaskArg *arg;
    const char *sid = NULL;
    size_t sid_size = 0;
    
    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "sid", strlen("sid"))) {
            sid = arg->val;
            sid_size = arg->val_size;
        }
    }
    if (!sid_size) return knd_FAIL;

    self = (struct kndAuth*)obj;
    memcpy(self->sid, sid, sid_size);
    self->sid[sid_size] = '\0';
    self->sid_size = sid_size;

    if (DEBUG_DELIV_LEVEL_TMP)
        knd_log("== check sid: \"%.*s\"", sid_size, sid);



    
    return knd_OK;
}


static int parse_auth(void *obj,
                      const char *rec,
                      size_t *total_size)
{
    struct kndAuth *self = (struct kndAuth*)obj;
    struct kndTaskSpec specs[] = {
        { .name = "sid",
          .name_size = strlen("sid"),
          .run = run_check_sid,
          .obj = self
        }
    };
    int err;
    
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) {
        knd_log("-- auth parse error: %d", err);
        return err;
    }
    
    return knd_OK;
}

static int parse_user(void *obj,
                      const char *rec,
                      size_t *total_size)
{
    struct kndAuth *self = (struct kndAuth*)obj;
    struct kndTaskSpec specs[] = {
        { .name = "auth",
          .name_size = strlen("auth"),
          .parse = parse_auth,
          .obj = self
        }
    };
    int err;
    
    err = knd_parse_task(rec, total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) {
        knd_log("-- task parse error: %d", err);
        return err;
    }
    
    return knd_OK;
}


static int run_task(struct kndAuth *self)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = KND_NAME_SIZE;
    size_t chunk_size = 0;

    const char *header_tag = "{task";
    size_t header_tag_size = strlen(header_tag);
    const char *c;
    
    struct kndTaskSpec specs[] = {
        { .name = "tid",
          .name_size = strlen("tid"),
          .run = run_set_tid,
          .obj = self
        },
        { .name = "user",
          .name_size = strlen("user"),
          .parse = parse_user,
          .obj = self
        }
    };
    int err;

    const char *rec = self->task;
    size_t total_size;
    
    if (strncmp(rec, header_tag, header_tag_size)) {
        knd_log("-- wrong GSL header");
        return knd_FAIL;
    }

    c = rec + header_tag_size;
    
    err = knd_parse_task(c, &total_size, specs, sizeof(specs) / sizeof(struct kndTaskSpec));
    if (err) {
        knd_log("-- task parse error: %d", err);
        return err;
    }
    
    return knd_OK;
}

/**
 *  kndAuth network service startup
 */
static int kndAuth_start(struct kndAuth *self)
{
    void *context;
    void *service;
    int err;

    const char *header = "AUTH";
    size_t header_size = strlen(header);

    const char *reply = "{\"error\":\"auth error\"}";
    size_t reply_size = strlen(reply);

    context = zmq_init(1);

    service = zmq_socket(context, ZMQ_REP);
    assert(service);
    assert((zmq_bind(service, self->addr) == knd_OK));

    knd_log("++ %s is up and running: %s",
            self->name, self->addr);

    while (1) {
        knd_log("++ AUTH service is waiting for new tasks...");
        self->out->reset(self->out);
        self->reply_obj = NULL;
        self->reply_obj_size = 0;
        self->tid[0] = '\0';
        self->sid[0] = '\0';

        self->task = knd_zmq_recv(service, &self->task_size);
        self->obj = knd_zmq_recv(service, &self->obj_size);

	knd_log("++ AUTH service has got a task:   \"%s\"",
                self->task);

        err = run_task(self);
        if (self->reply_obj_size) {
            reply = self->reply_obj;
            reply_size = self->reply_obj_size;
        }
            
        knd_zmq_sendmore(service, header, header_size);
	knd_zmq_send(service, reply, reply_size);

        if (self->task) {
            free(self->task);
            self->task = NULL;
            self->task_size = 0;
        }

        /* TODO: free obj if it was not set to index */
        /*if (self->obj) {
            self->obj = NULL;
            self->obj_size = 0;
        }*/
        
    }

    /* we never get here */
    zmq_close(service);
    zmq_term(context);

    return knd_OK;
}



static int
run_set_db_host(void *obj,
                struct kndTaskArg *args, size_t num_args)
{
    struct kndAuth *self;
    struct kndTaskArg *arg;
    const char *db_host = NULL;
    size_t db_host_size = 0;
    
    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "db_host", strlen("db_host"))) {
            db_host = arg->val;
            db_host_size = arg->val_size;
        }
    }
    if (!db_host_size) return knd_FAIL;

    self = (struct kndAuth*)obj;

    if (DEBUG_DELIV_LEVEL_TMP)
        knd_log(".. set DB HOST to \"%.*s\"", db_host_size, db_host);

    memcpy(self->db_host, db_host, db_host_size);
    self->db_host[db_host_size] = '\0';
    self->db_host_size = db_host_size;
   
    return knd_OK;
}

static int
run_set_service_addr(void *obj,
                     struct kndTaskArg *args, size_t num_args)
{
    struct kndAuth *self;
    struct kndTaskArg *arg;
    const char *addr = NULL;
    size_t addr_size = 0;
    
    for (size_t i = 0; i < num_args; i++) {
        arg = &args[i];
        if (!strncmp(arg->name, "service", strlen("service"))) {
            addr = arg->val;
            addr_size = arg->val_size;
        }
    }

    if (!addr_size) return knd_FAIL;

    self = (struct kndAuth*)obj;

    if (DEBUG_DELIV_LEVEL_1)
        knd_log(".. set service addr to \"%.*s\"", addr_size, addr);

    memcpy(self->addr, addr, addr_size);
    self->addr[addr_size] = '\0';
    self->addr_size = addr_size;

    
    return knd_OK;
}

static int
parse_config_GSL(struct kndAuth *self,
                 const char *rec,
                 size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = KND_NAME_SIZE;
    size_t chunk_size = 0;
    
    const char *gsl_format_tag = "{gsl";
    size_t gsl_format_tag_size = strlen(gsl_format_tag);

    const char *header_tag = "{knd::Auth Service Configuration";
    size_t header_tag_size = strlen(header_tag);
    const char *c;
    
    struct kndTaskSpec specs[] = {
        { .name = "service",
          .name_size = strlen("service"),
          .run = run_set_service_addr,
          .obj = self
        },
        { .name = "db_host",
          .name_size = strlen("db_host"),
          .run = run_set_db_host,
          .obj = self
        }
    };
    int err;

    if (!strncmp(rec, gsl_format_tag, gsl_format_tag_size)) {
        rec += gsl_format_tag_size;

        err = knd_get_schema_name(rec,
                                  buf, &buf_size, &chunk_size);
        if (!err) {
            rec += chunk_size;
        }
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
kndAuth_init(struct kndAuth *self)
{
    
    self->str = str;
    self->del = del;

    self->start = kndAuth_start;
    
    return knd_OK;
}

extern int
kndAuth_new(struct kndAuth **deliv,
                const char          *config)
{
    struct kndAuth *self;
    struct kndAuthRec *rec;
    size_t chunk_size = 0;
    int err;
    
    self = malloc(sizeof(struct kndAuth));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndAuth));


    
    err = ooDict_new(&self->auth_idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&self->sid_idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&self->uid_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&self->idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    err = kndOutput_new(&self->out, KND_IDX_BUF_SIZE);
    if (err) return err;
    
    /* special user */
    err = kndUser_new(&self->admin);
    if (err) return err;
    self->admin->out = self->out;

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
    
    err = ooDict_new(&rec->cache, KND_LARGE_DICT_SIZE);
    if (err) goto error;
    self->spec_rec = rec;
    
    kndAuth_init(self); 
    
    err = self->out->read_file(self->out, config, strlen(config));
    if (err) return err;
    
    err = parse_config_GSL(self, self->out->file, &chunk_size);
    if (err) goto error;
    
    if (!self->max_users)
        self->max_users = KND_MAX_USERS;

    self->users = malloc(sizeof(struct kndUserRec) * self->max_users);
    if (!self->users) return knd_NOMEM;
    memset(self->users, 0, sizeof(struct kndUserRec) * self->max_users);

    *deliv = self;

    return knd_OK;

 error:

    del(self);

    return err;
}
