#include "delivery-service.h"

#include "knd_config.h"
#include "knd_output.h"
#include "knd_utils.h"
#include "knd_task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <knd_err.h>
#include <knd_utils.h>

#include <gsl-parser.h>

static int
task_callback(struct kmqEndPoint *endpoint __attribute__((unused)), struct kmqTask *task,
        void *cb_arg)
{
    struct kndDeliveryService *self = cb_arg;
    const char *data;
    size_t size;
    int err;

    (void) self;
    err = task->get_data(task, 0, &data, &size);
    if (err != knd_OK) { knd_log("-- task read failed"); return -1; }

    printf(">>>\n%.*s\n<<<\n", (int) size, data);

    return 0;
}

static gsl_err_t
run_check_schema(void *obj __attribute__((unused)), const char *val, size_t val_size)
{
    const char *schema_name = "Delivery Service Configuration";
    size_t schema_name_size = strlen(schema_name);

    if (val_size != schema_name_size)  return make_gsl_err(gsl_FAIL);
    if (memcmp(schema_name, val, val_size)) return make_gsl_err(gsl_FAIL);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t
run_set_addr(void *obj, const char *addr, size_t addr_size)
{
    struct kndDeliveryService *self = obj;
    struct addrinfo *address;
    int err;

    if (!addr_size) return make_gsl_err(gsl_FORMAT);

    err = addrinfo_new(&address, addr, addr_size);
    if (err != 0) return make_gsl_err(gsl_FAIL);

    err = self->entry_point->set_address(self->entry_point, address);
    if (err != 0) return make_gsl_err(gsl_FAIL);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t
run_set_db_path(void *obj, const char *path, size_t path_size)
{
    struct kndDeliveryService *self = obj;

    if (!path_size) return make_gsl_err(gsl_FORMAT);

    memcpy(self->path, path, path_size);
    self->path[path_size] = '\0';
    self->path_size = path_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t
parse_config(void *obj, const char *rec, size_t *total_size)
{
    struct kndDeliveryService *self = obj;
    struct gslTaskSpec specs[] = {
        {
            .is_implied = true,
            .run = run_check_schema,
            .obj = self
        },
        {
            .name = "service",
            .name_size = strlen("service"),
            .run = run_set_addr,
            .obj = self
        },
        {
            .name = "path",
            .name_size = strlen("path"),
            .run = run_set_db_path,
            .obj = self
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int
parse_schema(struct kndDeliveryService *self, const char *rec, size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        {
            .name = "schema",
            .name_size = strlen("schema"),
            .parse = parse_config,
            .obj = self
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code != gsl_OK) return gsl_err_to_knd_err_codes(parser_err);

    return knd_OK;
}

static int
start__(struct kndDeliveryService *self)
{
    knd_log("delivery has been started\n");
    self->knode->dispatch(self->knode);
    knd_log("delivery has been stopped\n");
    return knd_FAIL;
}

static int
delete__(struct kndDeliveryService *self)
{
    if (self->entry_point) self->entry_point->del(self->entry_point);
    if (self->knode) self->knode->del(self->knode);
    free(self);

    return knd_OK;
}

int
kndDeliveryService_new(struct kndDeliveryService **service, const struct kndDeliveryOptions *opts)
{
    struct kndDeliveryService *self;
    int err;

    self = calloc(1, sizeof(*self));
    if (!self) return knd_FAIL;
    self->opts = opts;

    err = kmqKnode_new(&self->knode);
    if (err != 0) goto error;

    err = kmqEndPoint_new(&self->entry_point);
    self->entry_point->options.type = KMQ_SUB;
    self->entry_point->options.role = KMQ_TARGET;
    self->entry_point->options.callback = task_callback;
    self->entry_point->options.cb_arg = self;
    self->knode->add_endpoint(self->knode, self->entry_point);

    err = ooDict_new(&self->auth_idx, KND_LARGE_DICT_SIZE);
    if (err != knd_OK) goto error;
    err = ooDict_new(&self->sid_idx, KND_LARGE_DICT_SIZE);
    if (err != knd_OK) goto error;
    err = ooDict_new(&self->uid_idx, KND_MEDIUM_DICT_SIZE);
    if (err != knd_OK) goto error;
    err = ooDict_new(&self->idx, KND_LARGE_DICT_SIZE);
    if (err != knd_OK) goto error;
    err = kndOutput_new(&self->out, KND_IDX_BUF_SIZE);
    if (err != knd_OK) goto error;

    // special user
    err = kndUser_new(&self->admin);
    if (err != knd_OK) goto error;
    self->admin->out = self->out;

    { // create default user record
        struct kndAuthRec *rec = calloc(1, sizeof(*rec));
        if (!rec) {
            err = knd_NOMEM;
            goto error;
        }

        memcpy(rec->uid, "000", KND_ID_SIZE);

        err = ooDict_new(&rec->cache, KND_LARGE_DICT_SIZE);
        if (err != knd_OK) goto error;

        self->default_rec = rec;
        self->spec_rec = rec;
    }

    { // parse config file
        size_t chunk_size;

        err = self->out->read_file(self->out, opts->config_file, strlen(opts->config_file));
        if (err != knd_OK) goto error;

        err = parse_schema(self, self->out->file, &chunk_size);
        if (err != knd_OK) goto error;
    }

    if (self->path_size) {
        err = knd_mkpath(self->path, self->path_size, 0777, false);
        if (err != knd_OK) goto error;
    }

    if (!self->max_tids)
        self->max_tids = KND_MAX_TIDS;

    self->tids = calloc(self->max_tids, sizeof(struct kndTID));
    if (!self->tids) goto error;

    self->start = start__;
    self->del = delete__;

    *service = self;

    return knd_OK;
error:
    delete__(self);
    return knd_FAIL;
}

