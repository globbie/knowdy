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

#define DEBUG_AUTH_LEVEL_0 0
#define DEBUG_AUTH_LEVEL_1 0
#define DEBUG_AUTH_LEVEL_2 0
#define DEBUG_AUTH_LEVEL_3 0
#define DEBUG_AUTH_LEVEL_TMP 1

/* DB dependent values */
#define SQL_TOKEN_NUM_FIELDS      6
#define SQL_TOKEN_USER_FIELD_ID   2
#define SQL_TOKEN_STR_FIELD_ID    3
#define SQL_TOKEN_EXPIRY_FIELD_ID 4
#define SQL_TOKEN_SCOPE_FIELD_ID  5
#define AUTH_MAX_USERS 1024 * 1024

const char *get_tokens_query = "SELECT * FROM access_token WHERE expires_at > UNIX_TIMESTAMP()";

static void str(struct kndAuth *self)
{
    knd_log("<struct kndAuth at %p>", self);
}

static int
del(struct kndAuth *self)
{
    free(self);
    return knd_OK;
}


static int register_token(struct kndAuth *self,
                          struct kndUserRec *user_rec,
                          const char *tok,    size_t tok_size,
                          const char *expiry, size_t expiry_size,
                          const char *scope,  size_t scope_size)
{
    struct kndAuthTokenRec *tok_rec, *prev_tok_rec;
    long numval;
    int err;

    /* check limits */
    err = knd_parse_num((const char*)expiry, &numval);
    if (err) return err;

    if (tok_size >= KND_MAX_TOKEN_SIZE) return knd_LIMIT;
    if (scope_size >= KND_MAX_SCOPE_SIZE) return knd_LIMIT;

    /* take the oldest token from the pool */
    tok_rec = user_rec->tail;
    
    /* remove old token from the IDX */
    if (tok_rec->tok_size)
        self->token_idx->remove(self->token_idx, tok_rec->tok);

    /* assign new values */
    memcpy(tok_rec->tok, tok, tok_size);
    tok_rec->tok[tok_size] = '\0';
    tok_rec->tok_size = tok_size;

    tok_rec->expiry = (size_t)numval;

    memcpy(tok_rec->scope, scope, scope_size);
    tok_rec->scope_size = scope_size;

    /* register token */
    err = self->token_idx->set(self->token_idx, tok_rec->tok, (void*)tok_rec);
    if (err) return knd_FAIL;

    prev_tok_rec = tok_rec->prev;
    prev_tok_rec->next = NULL;
    tok_rec->prev = NULL;
    tok_rec->next = NULL;
    user_rec->tail = prev_tok_rec;

    /* put to front */
    user_rec->tokens->prev = tok_rec;
    tok_rec->next = user_rec->tokens;
    user_rec->tokens = tok_rec;

    
    return knd_OK;
}

static int update_token(struct kndAuth *self,
                        const char *userid, size_t userid_size,
                        const char *tok,    size_t tok_size,
                        const char *expiry, size_t expiry_size,
                        const char *scope,  size_t scope_size)
{
    struct kndUserRec *user_rec;
    struct kndAuthTokenRec *tok_rec;
    long numval;
    unsigned long userid_num;
    int err;

    if (DEBUG_AUTH_LEVEL_1)
        knd_log(".. update token \"%.*s\"..", tok_size, tok);

    /*** check user ***/
    
    /* empty values? */
    if (!userid_size) return knd_LIMIT;
    if (!tok_size) return knd_LIMIT;

    err = knd_parse_num((const char*)userid, &numval);
    if (err) return err;
    if (numval < 0 || numval >= AUTH_MAX_USERS) return knd_LIMIT;

    user_rec = self->users[numval];
    if (!user_rec) {
        if (DEBUG_AUTH_LEVEL_2)
            knd_log(".. create new user rec: %.*s", userid_size, userid);
        
        /* this user must be registered */
        user_rec = malloc(sizeof(struct kndUserRec));
        if (!user_rec) return knd_NOMEM;
        memset(user_rec, 0, sizeof(struct kndUserRec));
        user_rec->id = (size_t)numval;
        user_rec->name_size = sprintf(user_rec->name, "%lu", (unsigned long)numval);

        for (size_t i = 0; i < KND_MAX_TOKEN_CACHE; i++) {
            tok_rec = &user_rec->token_storage[i];
            tok_rec->user = user_rec;
            if (!user_rec->tail) {
                user_rec->tail = tok_rec;
            }
            tok_rec->next = user_rec->tokens;
            if (tok_rec->next) tok_rec->next->prev = tok_rec;
            user_rec->tokens = tok_rec;
        }

        /* TODO: retrieve full user acc */
        self->users[numval] = user_rec;
    }


    /* save token to the pool */
    err = register_token(self, user_rec,
                         tok, tok_size,
                         expiry, expiry_size,
                         scope, scope_size);
    if (err) return err;
    
    return knd_OK;
}


static int update_tokens(struct kndAuth *self)
{
    char tok_buf[KND_TEMP_BUF_SIZE];
    size_t tok_buf_size;
    MYSQL_RES *result = NULL;
    MYSQL_ROW row;
    unsigned int num_fields;
    unsigned int i;
    unsigned int row_count;
    unsigned int error_count;
    unsigned int doublet_count;

    const char *err_msg;
    const char *internal_err_msg = "{\"err\":\"internal error\",\"http_code\":500}";
    int err, e;

    MYSQL *conn = mysql_init(NULL);
    if (!conn) {
        fprintf(stderr, "%s\n", mysql_error(conn));
        return knd_FAIL;
    }

    if (mysql_real_connect(conn, self->db_host,
                           "content-server", "content-server",
                           "content-server_001", 0, NULL, 0) == NULL) {
        err_msg = mysql_error(conn);
        //buf_size = strlen(err_msg);
        fprintf(stderr, "%s [%lu]\n", err_msg);
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
    if (num_fields != SQL_TOKEN_NUM_FIELDS) {
        err = knd_FAIL;
        goto final;
    }
    
    row_count = 0;
    error_count = 0;
    doublet_count = 0;

    while ((row = mysql_fetch_row(result))) {
        unsigned long *lengths;
        lengths = mysql_fetch_lengths(result);

        tok_buf_size = lengths[SQL_TOKEN_STR_FIELD_ID];
        if (!tok_buf_size || tok_buf_size >= KND_MAX_TOKEN_SIZE) {
            error_count++;
            continue;
        }
        
        memcpy(tok_buf, row[SQL_TOKEN_STR_FIELD_ID], tok_buf_size);
        tok_buf[tok_buf_size] = '\0';
        
        /* lookup IDX */
        tok_rec = self->token_idx->get(self->token_idx, (const char*)tok_buf);
        if (tok_rec) {
            doublet_count++;
            continue;
        }

        err = update_token(self,
                           row[SQL_TOKEN_USER_FIELD_ID],   lengths[SQL_TOKEN_USER_FIELD_ID],
                           tok_buf,                        tok_buf_size,
                           row[SQL_TOKEN_EXPIRY_FIELD_ID], lengths[SQL_TOKEN_EXPIRY_FIELD_ID],
                           row[SQL_TOKEN_SCOPE_FIELD_ID],  lengths[SQL_TOKEN_SCOPE_FIELD_ID]);
        if (err) {
            /* TODO: report */
            knd_log("-- token update failed for in REC: %.*s ERR:%d", lengths[0], row[0], err);
            error_count++;
            continue;
        }
        row_count++;
    }

    knd_log("== total tokens updated: %lu   failed: %lu  doublets: %lu",
            (unsigned long)row_count, (unsigned long)error_count, (unsigned long)doublet_count);
    err = knd_OK;

 final:
    if (result)
        mysql_free_result(result);

    mysql_close(conn);

    if (err) {
        e = self->log->write(self->log, internal_err_msg,
                             strlen(internal_err_msg));
        if (e) return e;
    }

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
    struct kndAuthTokenRec *tok_rec;
    struct kndUserRec *user_rec;
    const char *sid = NULL;
    size_t sid_size = 0;
    int err;
    
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

    if (DEBUG_AUTH_LEVEL_TMP)
        knd_log("== check sid: \"%.*s\"", sid_size, sid);

    tok_rec = self->token_idx->get(self->token_idx, sid);
    if (!tok_rec) {
        /* time to update our token cache */
        err = update_tokens(self);
        if (err) return err;

        /* one more try */
        tok_rec = self->token_idx->get(self->token_idx, sid);
        if (!tok_rec) return knd_NO_MATCH;
    }

    user_rec = tok_rec->user;
    
    err = self->out->write(self->out,
                           "{\"http_code\":200", strlen("{\"http_code\":200"));
    if (err) return err;
    err = self->out->write(self->out,
                           ",\"user\":\"", strlen(",\"user\":\""));
    if (err) return err;
    err = self->out->write(self->out,
                          user_rec->name, user_rec->name_size);
    if (err) return err;
    err = self->out->write(self->out, "\"}", 1);
    if (err) return err;
    err = self->out->write(self->out, "}", 1);
    if (err) return err;
    
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

    const char *header = "{reply{auth _obj}}";
    size_t header_size = strlen(header);

    /* default reply */
    const char *reply = "{\"error\":\"auth error\",\"http_code\":401}";
    size_t reply_size = strlen(reply);

    context = zmq_init(1);

    service = zmq_socket(context, ZMQ_REP);
    assert(service);
    assert((zmq_bind(service, self->addr) == knd_OK));

    knd_log("++ %s is up and running: %s", self->name, self->addr);

    while (1) {
        knd_log("++ AUTH service is waiting for new tasks...");

        self->out->reset(self->out);
        self->log->reset(self->log);
        self->tid[0] = '\0';
        self->sid[0] = '\0';
        self->sid_size = 0;

        self->task = knd_zmq_recv(service, &self->task_size);
        self->obj = knd_zmq_recv(service, &self->obj_size);

	knd_log("++ AUTH service has got a task: \"%s\"", self->task);

        err = run_task(self);
        if (err) {
            if (self->log->buf_size) {
                reply = self->log->buf;
                reply_size = self->log->buf_size;
            }
        }
        else {
            if (self->out->buf_size) {
                reply = self->out->buf;
                reply_size = self->out->buf_size;
            }
        }

        knd_log("== REPLY: %s\n\n", reply);
        
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

    if (DEBUG_AUTH_LEVEL_TMP)
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

    if (DEBUG_AUTH_LEVEL_1)
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
          .buf = self->db_host,
          .buf_size = &self->db_host_size,
          .max_buf_size = KND_NAME_SIZE,
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
    self->update = update_tokens;
    self->start = kndAuth_start;
    
    return knd_OK;
}

extern int
kndAuth_new(struct kndAuth **result,
                const char          *config)
{
    struct kndAuth *self;
    size_t chunk_size = 0;
    int err;
    
    self = malloc(sizeof(struct kndAuth));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndAuth));

    err = ooDict_new(&self->token_idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    err = kndOutput_new(&self->out, KND_IDX_BUF_SIZE);
    if (err) return err;

    err = kndOutput_new(&self->log, KND_LARGE_BUF_SIZE);
    if (err) return err;

    /* special user */
    err = kndUser_new(&self->admin);
    if (err) return err;
    self->admin->out = self->out;
    
    kndAuth_init(self); 
    
    err = self->out->read_file(self->out, config, strlen(config));
    if (err) return err;
    
    err = parse_config_GSL(self, self->out->file, &chunk_size);
    if (err) goto error;
    
    if (!self->max_users) self->max_users = AUTH_MAX_USERS;
    self->users = malloc(sizeof(struct kndUserRec*) * self->max_users);
    if (!self->users) return knd_NOMEM;
    memset(self->users, 0, sizeof(struct kndUserRec*) * self->max_users);

    *result = self;

    return knd_OK;

 error:

    del(self);

    return err;
}
