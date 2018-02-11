#include "learner-service.h"

#include <knd_dict.h>
#include <knd_err.h>
#include <knd_parser.h>
#include <knd_utils.h>

#include <gsl-parser.h>

#include <string.h>


static gsl_err_t
parse_memory_settings(void *obj, const char *rec, size_t *total_size)
{
    struct kndMemPool *self = obj;
    struct gslTaskSpec specs[] = {
        {
            .name = "max_users",
            .name_size = strlen("max_users"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_users
        },
        {
            .name = "max_classes",
            .name_size = strlen("max_classes"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_classes
        },
        {
            .name = "max_states",
            .name_size = strlen("max_states"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_states
        },
        {
            .name = "max_objs",
            .name_size = strlen("max_objs"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_objs
        },
        {
            .name = "max_elems",
            .name_size = strlen("max_elems"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_elems
        },
        {
            .name = "max_rels",
            .name_size = strlen("max_rels"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_rels
        },
        {
            .name = "max_rel_args",
            .name_size = strlen("max_rels_args"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_rel_args
        },
        {
            .name = "max_rel_refs",
            .name_size = strlen("max_rel_refs"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_rel_refs
        },
        {
            .name = "max_rel_instances",
            .name_size = strlen("max_rel_instances"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_rel_insts
        },
        {
            .name = "max_rel_arg_instances",
            .name_size = strlen("max_rel_arg_instances"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_rel_arg_insts
        },
        {
            .name = "max_rel_arg_inst_refs",
            .name_size = strlen("max_rel_arg_inst_refs"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_rel_arg_inst_refs
        },
        {
            .name = "max_procs",
            .name_size = strlen("max_procs"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_procs
        },
        {
            .name = "max_proc_instances",
            .name_size = strlen("max_proc_instances"),
            .parse = gsl_parse_size_t,
            .obj = &self->max_proc_insts
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int
parse_config_gsl(struct kndLearnerService *self, const char *rec, size_t *total_size)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = KND_NAME_SIZE;
    size_t chunk_size = 0;

    const char *gsl_format_tag = "{gsl";
    size_t gsl_format_tag_size = strlen(gsl_format_tag);

    const char *header_tag = "{knd::Knowdy Learner Service Configuration";
    size_t header_tag_size = strlen(header_tag);
    const char *c;

    struct gslTaskSpec specs[] = {
        {
            .name = "path",
            .name_size = strlen("path"),
            .buf = self->path,
            .buf_size = &self->path_size,
            .max_buf_size = KND_NAME_SIZE
        },
        {
            .name = "schemas",
            .name_size = strlen("schemas"),
            .buf = self->schema_path,
            .buf_size = &self->schema_path_size,
            .max_buf_size = KND_NAME_SIZE
        },
        {
            .name = "sid",
            .name_size = strlen("sid"),
            .buf = self->admin->sid,
            .buf_size = &self->admin->sid_size,
            .max_buf_size = KND_NAME_SIZE
        },
        {
            .name = "memory",
            .name_size = strlen("memory"),
            .parse = parse_memory_settings,
            .obj = self->mempool,
        },
        {
            .name = "delivery",
            .name_size = strlen("delivery"),
            .buf = self->delivery_addr,
            .buf_size = &self->delivery_addr_size,
            .max_buf_size = KND_NAME_SIZE
        },
        /*
        {
            .name = "inbox",
            .name_size = strlen("inbox"),
            .parse = parse_inbox_addr,
            .obj = self
        },
        */
        /*
        {
            .name = "publish",
            .name_size = strlen("publish"),
            .parse = parse_publisher_service_addr,
            .obj = self
        },
        */
        {
            .name = "agent",
            .name_size = strlen("agent"),
            .buf = self->name,
            .buf_size = &self->name_size,
            .max_buf_size = KND_NAME_SIZE
        }
    };

    int err = knd_FAIL;
    gsl_err_t parser_err;

    if (!strncmp(rec, gsl_format_tag, gsl_format_tag_size)) {
        rec += gsl_format_tag_size;

        err = knd_get_schema_name(rec, buf, &buf_size, &chunk_size);
        if (!err) {
            rec += chunk_size;
            //if (DEBUG_LEARNER_LEVEL_TMP)
            //    knd_log("== got schema: \"%.*s\"", buf_size, buf);
        }
    }

    if (strncmp(rec, header_tag, header_tag_size)) {
        knd_log("-- wrong GSL class header");
        return knd_FAIL;
    }

    c = rec + header_tag_size;

    parser_err = gsl_parse_task(c, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- config parse error: %d", parser_err.code);
        return gsl_err_to_knd_err_codes(parser_err);
    }

    if (!self->path_size) {
        knd_log("-- DB path not set :(");
        return knd_FAIL;
    }
    err = knd_mkpath(self->path, self->path_size, 0755, false);
    if (err != knd_OK) return err;

    if (!self->schema_path_size) {
        knd_log("-- schema path not set :(");
        return knd_FAIL;
    }

    if (!self->admin->sid_size) {
        knd_log("-- administrative SID is not set :(");
        return knd_FAIL;
    }
    memcpy(self->admin->id, "000", strlen("000"));

    /* users path */
    self->path[self->path_size] = '\0';
    self->admin->dbpath = self->path;
    self->admin->dbpath_size = self->path_size;

    memcpy(self->admin->path, self->path, self->path_size);
    memcpy(self->admin->path + self->path_size, "/users", strlen("/users"));
    self->admin->path_size = self->path_size + strlen("/users");
    self->admin->path[self->admin->path_size] = '\0';

    return knd_OK;
}

static int
start__(struct kndLearnerService *self __attribute__((unused)))
{
    knd_log("learner has been started\n");
    // todo
    knd_log("learner has been stopped\n");
    return knd_FAIL;
}

static int
delete__(struct kndLearnerService *self)
{
    if (self->entry_point) self->entry_point->del(self->entry_point);
    if (self->knode) self->knode->del(self->knode);
    free(self);

    return knd_OK;
}

int
kndLearnerService_new(struct kndLearnerService **service, const char *config_file)
{
    struct kndLearnerService *self;
    int err;

    self = calloc(1, sizeof(*self));
    if (!self) return knd_FAIL;

    err = kndOutput_new(&self->out, KND_IDX_BUF_SIZE);
    if (err != knd_OK) goto error;
    err = kndOutput_new(&self->log, KND_MED_BUF_SIZE);
    if (err != knd_OK) goto error;

    err = kndTask_new(&self->task);
    if (err != knd_OK) goto error;

    err = kndUser_new(&self->admin);
    if (err != knd_OK) goto error;

    self->task->admin = self->admin; // fixme: use public interface to set this fields
    self->admin->out = self->out;

    err = kndMemPool_new(&self->mempool);
    if (err != knd_OK) return err;

    {
        size_t chunk_size;
        err = self->out->read_file(self->out, config_file, strlen(config_file));
        if (err != knd_OK) goto error;
        err = parse_config_gsl(self, self->out->file, &chunk_size);
        if (err != knd_OK) goto error;
    }

    /**************************************************************************/
    /*                           SERVICE FACILITIES                           */
    /**************************************************************************/

    err = kmqKnode_new(&self->knode);
    if (err != 0) goto error;

    self->start = start__;
    self->del = delete__;

    *service = self;

    return knd_OK;
error:
    delete__(self);
    return knd_FAIL;
}

