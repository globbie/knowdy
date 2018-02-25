#include "learner-service.h"

#include <knd_dict.h>
#include <knd_err.h>
#include <knd_proc.h>
#include <knd_rel.h>
#include <knd_state.h>
#include <knd_utils.h>

#include <gsl-parser.h>

#include <string.h>

static int
task_callback(struct kmqEndPoint *endpoint __attribute__((unused)), struct kmqTask *task,
        void *cb_arg)
{
    struct kndLearnerService *self = cb_arg;

    const char *data;
    size_t size;
    int err;

    err = task->get_data(task, 0, &data, &size);
    if (err != knd_OK) { knd_log("-- task read failed"); return -1; }

    printf(">>>\n%.*s\n<<<\n", (int) size, data);

    self->task->reset(self->task);
    err = self->task->run(self->task, data, size, "None", sizeof("None"));

    if (err != knd_OK) {
        self->task->error = err;
        knd_log("-- task running failure: %d", err);
        goto final;
    }

final:

    if (!self->task->tid_size) {
        self->task->tid[0] = '0';
        self->task->tid_size = 1;
    }

    err = self->task->report(self->task);
    if (err != knd_OK) {
        knd_log("-- task report failed: %d", err);
    }

    return 0;
}

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

    char address_str[256]; // fixme: hardcode
    size_t address_str_len = 0;

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
        {
            .name = "address",
            .name_size = strlen("address"),
            .buf = address_str,
            .buf_size = &address_str_len,
            .max_buf_size = sizeof(address_str)
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

    parser_err = gsl_parse_task(c, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- config parse error: %d", parser_err.code);
        return gsl_err_to_knd_err_codes(parser_err);
    }

    { // endpoint setup
        if (self->opts->address) {
            err = self->entry_point->set_address(self->entry_point, self->opts->address);
            if (err != 0) return knd_FAIL;
        } else {
            struct addrinfo *address;
            err = addrinfo_new(&address, address_str, address_str_len);
            if (err != 0) return knd_FAIL;

            err = self->entry_point->set_address(self->entry_point, address);
            if (err != 0) return knd_FAIL;

            // fixme: free address
            // todo: pass string address to knode
        }
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
    self->knode->dispatch(self->knode);
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
kndLearnerService_new(struct kndLearnerService **service, const struct kndLearnerOptions *opts)
{
    struct kndLearnerService *self;
    struct kndOutput *out;
    struct kndConcept *conc;
    int err;

    self = calloc(1, sizeof(*self));
    if (!self) return knd_FAIL;
    self->opts = opts;

    err = kmqKnode_new(&self->knode);
    if (err != 0) goto error;

    err = kmqEndPoint_new(&self->entry_point);
    self->entry_point->options.type = KMQ_PULL;
    self->entry_point->options.role = KMQ_TARGET;
    self->entry_point->options.callback = task_callback;
    self->entry_point->options.cb_arg = self;
    self->knode->add_endpoint(self->knode, self->entry_point);

    err = kndOutput_new(&self->out, KND_IDX_BUF_SIZE);
    if (err != knd_OK) goto error;
    err = kndOutput_new(&self->log, KND_MED_BUF_SIZE);
    if (err != knd_OK) goto error;

    err = kndTask_new(&self->task);
    if (err != knd_OK) goto error;
    err = kndOutput_new(&self->task->out, KND_IDX_BUF_SIZE);
    if (err) goto error;

    err = kndUser_new(&self->admin);
    if (err != knd_OK) goto error;
    self->task->admin = self->admin; // fixme: use public interface to set this fields
    self->admin->out = self->out;

    err = kndMemPool_new(&self->mempool);
    if (err != knd_OK) return err;

    {
        size_t chunk_size;
        err = self->out->read_file(self->out, opts->config_file, strlen(opts->config_file));
        if (err != knd_OK) goto error;
        err = parse_config_gsl(self, self->out->file, &chunk_size);
        if (err != knd_OK) goto error;
    }

    err = self->mempool->alloc(self->mempool);

    err = kndStateControl_new(&self->task->state_ctrl);
    if (err) return err;
    self->task->state_ctrl->max_updates = self->mempool->max_updates;
    self->task->state_ctrl->updates = self->mempool->update_idx;
    self->task->state_ctrl->task = self->task;

    memcpy(self->task->agent_name, self->name, self->name_size);
    self->task->agent_name_size = self->name_size;
    self->task->agent_name[self->name_size] = '\0';

    out = self->out;
    out->reset(out);
    err = out->write(out, self->path, self->path_size);
    if (err) return err;

    err = out->write(out, "/frozen.gsp", strlen("/frozen.gsp"));
    if (err) return err;
    memcpy(self->admin->frozen_output_file_name, out->buf, out->buf_size);
    self->admin->frozen_output_file_name_size = out->buf_size;
    self->admin->frozen_output_file_name[out->buf_size] = '\0';

    err = self->mempool->new_class(self->mempool, &conc);                         RET_ERR();
    conc->out = self->out;
    conc->task = self->task;
    conc->log = self->task->log;
    conc->name[0] = '/';
    conc->name_size = 1;

    conc->dbpath = self->schema_path;
    conc->dbpath_size = self->schema_path_size;
    conc->frozen_output_file_name = self->admin->frozen_output_file_name;
    conc->frozen_output_file_name_size = self->admin->frozen_output_file_name_size;

    err = self->mempool->new_conc_dir(self->mempool, &conc->dir);                 RET_ERR();
    memset(conc->dir->name, '0', KND_ID_SIZE);
    conc->dir->name_size = KND_ID_SIZE;
    conc->dir->conc = conc;
    conc->mempool = self->mempool;
    conc->dir->mempool = self->mempool;

    err = kndProc_new(&conc->proc);
    if (err) goto error;
    conc->proc->mempool = self->mempool;

    err = kndRel_new(&conc->rel);
    if (err) goto error;
    conc->rel->mempool = self->mempool;
    conc->rel->frozen_output_file_name = self->admin->frozen_output_file_name;
    conc->rel->frozen_output_file_name_size = self->admin->frozen_output_file_name_size;

    /* specific allocations of the root concs */
    err = ooDict_new(&conc->class_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&conc->class_name_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;

    err = ooDict_new(&conc->proc->proc_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;
    conc->proc->class_name_idx = conc->class_name_idx;

    err = ooDict_new(&conc->rel->rel_idx, KND_MEDIUM_DICT_SIZE);
    if (err) goto error;
    conc->rel->class_name_idx = conc->class_name_idx;

    /* user idx */
    if (self->mempool->max_users) {
        knd_log("MAX USERS: %zu", self->mempool->max_users);
        self->max_users = self->mempool->max_users;
        self->admin->user_idx = calloc(self->max_users,
                                       sizeof(struct kndObject*));
        if (!self->admin->user_idx) return knd_NOMEM;
        self->admin->max_users = self->max_users;
    }

    /* try opening the frozen DB */
    conc->user = self->admin;
    self->admin->root_class = conc;

    err = conc->open(conc);
    if (err) {
        if (err != knd_NO_MATCH) goto error;
        /* read class definitions */
        knd_log("-- no frozen DB found, reading schemas..");
        conc->dbpath = self->schema_path;
        conc->dbpath_size = self->schema_path_size;
        conc->batch_mode = true;
        err = conc->load(conc, "index", strlen("index"));
        if (err) {
            knd_log("-- couldn't read any schema definitions :(");
            goto error;
        }
        err = conc->coordinate(conc);
        if (err) goto error;
        conc->batch_mode = false;
    }

    conc->dbpath = self->path;
    conc->dbpath_size = self->path_size;

    /* obj manager */
    err = self->mempool->new_obj(self->mempool, &conc->curr_obj);                 RET_ERR();
    conc->curr_obj->mempool = self->mempool;

    /* read any existing updates to the frozen DB (failure recovery) */
    /*err = conc->restore(conc);
    if (err) return err;
    */
    /* test
    err = dc->build_diff(dc, "0001");
    if (err) return err;
    */

    self->admin->root_class = conc;

    self->start = start__;
    self->del = delete__;

    *service = self;

    return knd_OK;
error:
    delete__(self);
    return knd_FAIL;
}

