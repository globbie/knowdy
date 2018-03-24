#include <pthread.h>
#include <string.h>

#include <gsl-parser.h>
#include <glb-lib/output.h>

#include "learner-service.h"
#include <knd_dict.h>
#include <knd_err.h>
#include <knd_proc.h>
#include <knd_rel.h>
#include <knd_state.h>
#include <knd_utils.h>


static int
task_callback(struct kmqEndPoint *endpoint, struct kmqTask *task, void *cb_arg)
{
    struct kndLearnerService *self = cb_arg;
    const char *b;
    const char *data;
    size_t size;
    int err;

    knd_log("++ new task! curr storage size:%zu  capacity:%zu",
        self->task_storage->buf_size, self->task_storage->capacity);

    err = task->get_data(task, 0, &data, &size);
    if (err != knd_OK) { knd_log("-- task read failed"); return -1; }

    b = self->task_storage->buf + self->task_storage->buf_size;
    err = self->task_storage->write(self->task_storage, data, size);
    if (err) {
        knd_log("-- task storage limit reached!");
        return err;
    }

    self->task->reset(self->task);
    err = self->task->run(self->task, b, size, "None", sizeof("None"));
    if (err != knd_OK) {
        self->task->error = err;
        knd_log("-- task running failure: %d", err);
        goto final;
    }

final:

    /* save only the successful write transaction */
    switch (self->task->type) {
    case KND_UPDATE_STATE:
        if (!self->task->error)
        break;
    default:
        /* retract last write to task_storage */
        self->task_storage->rtrim(self->task_storage, size);
        break;
    }

    err = self->task->report(self->task);
    if (err != knd_OK) {
        knd_log("-- task report failed: %d", err);
        return -1;
    }

    {
        struct kmqTask *reply;
        err = kmqTask_new(&reply);
        if (err != 0) {
            knd_log("-- task report failed, allocation failed");
            return -1;
        }

        err = reply->copy_data(reply, self->task->out->buf, self->task->out->buf_size);
        if (err != 0) {
            knd_log("-- task report failed, reply data copy failed");
            goto free_reply;
        }

        err = endpoint->schedule_task(endpoint, reply);
        if (err != 0) {
            knd_log("-- task report failed, schedule reply failed");
            goto free_reply;
        }

    free_reply:
        reply->del(reply);
    }

    return 0;
}


static gsl_err_t
run_check_schema(void *obj __attribute__((unused)), const char *val, size_t val_size)
{
    const char *schema_name = "Knowdy Learner Service";
    size_t schema_name_size = strlen(schema_name);

    if (val_size != schema_name_size)  return make_gsl_err(gsl_FAIL);
    if (memcmp(schema_name, val, val_size)) return make_gsl_err(gsl_FAIL);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t
run_set_address(void *obj, const char *val, size_t val_size)
{
    struct kndLearnerService *self = obj;
    struct addrinfo *address;
    int err;

    err = addrinfo_new(&address, val, val_size);
    if (err != 0) return make_gsl_err(gsl_FAIL);

    err = self->entry_point->set_address(self->entry_point, address);
    if (err != 0) return make_gsl_err(gsl_FAIL);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t
parse_memory_settings(void *obj, const char *rec, size_t *total_size)
{
    struct kndMemPool *mempool = obj;
    return mempool->parse(mempool, rec, total_size);
}

static gsl_err_t
parse_config(void *obj, const char *rec, size_t *total_size)
{
    struct kndLearnerService *self = obj;

    struct gslTaskSpec specs[] = {
        {
            .is_implied = true,
            .run = run_check_schema,
            .obj = self
        },
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
            .name = "owners",
            .name_size = strlen("owners"),
            .parse = gsl_parse_size_t,
            .obj = &self->num_owners
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
            .run = run_set_address,
            .obj = self
        },
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

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        knd_log("-- config parse error: %d", parser_err.code);
        return parser_err;
    }

    if (!self->path_size) {
        knd_log("-- DB path not set :(");
        return make_gsl_err(gsl_FAIL);
    }
    err = knd_mkpath(self->path, self->path_size, 0755, false);
    if (err != knd_OK) return make_gsl_err_external(err);

    if (!self->schema_path_size) {
        knd_log("-- schema path not set :(");
        return make_gsl_err(gsl_FAIL);
    }

    if (!self->admin->sid_size) {
        knd_log("-- administrative SID is not set :(");
        return make_gsl_err(gsl_FAIL);
    }

    if (self->num_owners > KND_MAX_OWNERS) {
	knd_log("-- too many owners requested, limiting to %zu",
		KND_MAX_OWNERS);
	self->num_owners = KND_MAX_OWNERS;
    }
    if (!self->num_owners)
	self->num_owners = 1;

    knd_log("== Learner's settings:");
    knd_log("   total owners: %zu", self->num_owners);

    memcpy(self->admin->id, "000", strlen("000"));

    /* users path */
    self->path[self->path_size] = '\0';
    self->admin->dbpath = self->path;
    self->admin->dbpath_size = self->path_size;

    memcpy(self->admin->path, self->path, self->path_size);
    memcpy(self->admin->path + self->path_size, "/users", strlen("/users"));
    self->admin->path_size = self->path_size + strlen("/users");
    self->admin->path[self->admin->path_size] = '\0';

    return make_gsl_err(gsl_OK);
}

static int
parse_schema(struct kndLearnerService *self, const char *rec, size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        {
            .name = "schema",
            .name_size = sizeof("schema") - 1,
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
start__(struct kndLearnerService *self)
{
    knd_log("learner has been started..\n");
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

void *start_owner(void *arg)
{
    struct kndLearnerOwner *owner = arg;
    knd_log(".. owner %zu started!", owner->id);
    return NULL;
}

int
kndLearnerService_new(struct kndLearnerService **service, const struct kndLearnerOptions *opts)
{
    struct kndLearnerService *self;
    struct glbOutput *out;
    struct kndConcept *conc;
    //struct kndLearnerOwner *owner;
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

    err = glbOutput_new(&self->task_storage, KND_TASK_STORAGE_SIZE);
    if (err != knd_OK) goto error;

    err = glbOutput_new(&self->out, KND_IDX_BUF_SIZE);
    if (err != knd_OK) goto error;
    err = glbOutput_new(&self->log, KND_MED_BUF_SIZE);
    if (err != knd_OK) goto error;

    // task specification
    err = kndTask_new(&self->task);
    if (err != knd_OK) goto error;
    err = glbOutput_new(&self->task->out, KND_IDX_BUF_SIZE);
    if (err) goto error;

    // special user
    err = kndUser_new(&self->admin);
    if (err != knd_OK) goto error;
    self->task->admin = self->admin; // fixme: use public interface to set this field
    self->admin->out = self->out;

    err = kndMemPool_new(&self->mempool);
    if (err != knd_OK) return err;

    { // read config
        size_t chunk_size;
        err = self->out->write_file_content(self->out,
					    opts->config_file);
        if (err != knd_OK) goto error;

        err = parse_schema(self, self->out->buf, &chunk_size);
        if (err != knd_OK) goto error;
	self->out->reset(self->out);
    }

    self->owners = calloc(self->num_owners, sizeof(struct kndLearnerOwner));
    if (err) goto error;

    /* start owners */
    /*for (size_t i = 0; i < self->num_owners; i++) {
	owner = &self->owners[i];
	owner->id = i;
	owner->service = self;
        err = pthread_create(&owner->thread, 
                             NULL,
                             start_owner,
                             (void*)owner);
			     }*/

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
    memset(conc->dir->id, '0', KND_ID_SIZE);
    conc->dir->id_size = 1;
    conc->dir->numid = 0;

    conc->dir->conc = conc;
    conc->mempool = self->mempool;
    conc->dir->mempool = self->mempool;

    err = kndProc_new(&conc->proc);
    if (err) goto error;
    conc->proc->mempool = self->mempool;

    err = kndRel_new(&conc->rel);
    if (err) goto error;
    conc->rel->log = self->task->log;
    conc->rel->out = self->task->out;
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
    /*if (self->mempool->max_users) {
        knd_log("MAX USERS: %zu", self->mempool->max_users);
        self->max_users = self->mempool->max_users;
        self->admin->user_idx = calloc(self->max_users,
                                       sizeof(struct kndObject*));
        if (!self->admin->user_idx) return knd_NOMEM;
        self->admin->max_users = self->max_users;
	}*/

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
        err = conc->load(conc, NULL, "index", strlen("index"));
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

    self->admin->root_class = conc;

    self->start = start__;
    self->del = delete__;

    *service = self;

    return knd_OK;
error:
    delete__(self);
    return knd_FAIL;
}

