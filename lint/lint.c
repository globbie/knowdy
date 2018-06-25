
#include <gsl-parser.h>
#include <glb-lib/output.h>

#include "lint.h"
#include <knd_dict.h>
#include <knd_set.h>
#include <knd_err.h>
#include <knd_proc.h>
#include <knd_rel.h>
#include <knd_state.h>
#include <knd_utils.h>

#include <string.h>
#include <errno.h>

static gsl_err_t
run_check_schema(void *obj __attribute__((unused)), const char *val, size_t val_size)
{
    const char *schema_name = "Knowdy Lint";
    size_t schema_name_size = strlen(schema_name);

    if (val_size != schema_name_size)  return make_gsl_err(gsl_FAIL);
    if (memcmp(schema_name, val, val_size)) return make_gsl_err(gsl_FAIL);
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
    struct kndLint *self = obj;

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
            .name = "memory",
            .name_size = strlen("memory"),
            .parse = parse_memory_settings,
            .obj = self->mempool,
        },
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

    knd_log("== Lint's settings:");

    return make_gsl_err(gsl_OK);
}

static knd_err_codes
parse_schema(struct kndLint *self, const char *rec, size_t *total_size)
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

knd_err_codes
kndLint_delete(struct kndLint *lint)
{
    // todo
    free(lint);
    return knd_OK;
}

knd_err_codes
kndLint_new(struct kndLint **lint, const struct kndLintOptions *opts)
{
    struct kndLint *self;
    struct glbOutput *out;
    struct kndClass *class;
    knd_err_codes err;

    self = calloc(1, sizeof(*self));
    if (!self) return knd_FAIL;

    err = glbOutput_new(&self->task_storage, KND_TASK_STORAGE_SIZE);
    if (err != knd_OK) goto error;
    err = glbOutput_new(&self->out, KND_IDX_BUF_SIZE);
    if (err != knd_OK) goto error;
    err = glbOutput_new(&self->log, KND_MED_BUF_SIZE);
    if (err != knd_OK) goto error;

    err = kndTask_new(&self->task);
    if (err != knd_OK) goto error;
    err = glbOutput_new(&self->task->out, KND_IDX_BUF_SIZE);
    if (err != knd_OK) goto error;
    err = kndUser_new(&self->admin);
    if (err != knd_OK) goto error;
    self->task->admin = self->admin;
    self->admin->out = self->out;

    err = kndMemPool_new(&self->mempool);
    if (err != knd_OK) goto error;

    { // read config
        size_t chunk_size;
        err = self->out->write_file_content(self->out, opts->config_file);
        if (err != knd_OK) goto error;
        err = parse_schema(self, self->out->buf, &chunk_size);
        if (err != knd_OK) goto error;
        self->out->reset(self->out);
    }

    err = self->mempool->alloc(self->mempool);
    err = kndStateControl_new(&self->task->state_ctrl);
    if (err != knd_OK) goto error;
    self->task->state_ctrl->max_updates = self->mempool->max_updates;
    self->task->state_ctrl->updates = self->mempool->update_idx;
    self->task->state_ctrl->task = self->task;

    memcpy(self->task->agent_name, self->name, self->name_size);
    self->task->agent_name_size = self->name_size;
    self->task->agent_name[self->name_size] = '\0';

    out = self->out;
    out->reset(out);
    err = out->write(out, self->path, self->path_size);
    if (err != knd_OK) goto error;

    err = out->write(out, "/frozen.gsp", strlen("/frozen.gsp"));
    if (err != knd_OK) goto error;
    memcpy(self->admin->frozen_output_file_name, out->buf, out->buf_size);
    self->admin->frozen_output_file_name_size = out->buf_size;
    self->admin->frozen_output_file_name[out->buf_size] = '\0';

    err = self->mempool->new_class(self->mempool, &class);
    if (err != knd_OK) goto error;
    class->out = self->out;
    class->task = self->task;
    class->log = self->task->log;
    class->name[0] = '/';
    class->name_size = 1;

    class->dbpath = self->schema_path;
    class->dbpath_size = self->schema_path_size;
    class->frozen_output_file_name = self->admin->frozen_output_file_name;
    class->frozen_output_file_name_size = self->admin->frozen_output_file_name_size;

    err = self->mempool->new_class_entry(self->mempool, &class->entry);
    if (err != knd_OK) goto error;
    memset(class->entry->name, '0', KND_ID_SIZE);
    class->entry->name_size = KND_ID_SIZE;
    memset(class->entry->id, '0', KND_ID_SIZE);
    class->entry->id_size = 1;
    class->entry->numid = 0;

    class->entry->conc = class;
    class->mempool = self->mempool;
    class->entry->mempool = self->mempool;

    err = kndProc_new(&class->proc);
    if (err != knd_OK) goto error;
    class->proc->mempool = self->mempool;
    class->proc->log = self->task->log;
    class->proc->out = self->task->out;
    class->proc->frozen_output_file_name = self->admin->frozen_output_file_name;
    class->proc->frozen_output_file_name_size = self->admin->frozen_output_file_name_size;

    err = kndRel_new(&class->rel);
    if (err != knd_OK) goto error;
    class->rel->log = self->task->log;
    class->rel->out = self->task->out;
    class->rel->mempool = self->mempool;
    class->rel->frozen_output_file_name = self->admin->frozen_output_file_name;
    class->rel->frozen_output_file_name_size = self->admin->frozen_output_file_name_size;

    err = self->mempool->new_set(self->mempool, &class->class_idx);
    if (err != knd_OK) goto error;
    class->class_idx->type = KND_SET_CLASS;

    err = ooDict_new(&class->class_name_idx, KND_MEDIUM_DICT_SIZE);
    if (err != knd_OK) goto error;
    err = ooDict_new(&class->proc->proc_name_idx, KND_MEDIUM_DICT_SIZE);
    if (err != knd_OK) goto error;
    class->proc->class_name_idx = class->class_name_idx;

    err = ooDict_new(&class->rel->rel_idx, KND_MEDIUM_DICT_SIZE);
    if (err != knd_OK) goto error;
    err = ooDict_new(&class->rel->rel_name_idx, KND_MEDIUM_DICT_SIZE);
    if (err != knd_OK) goto error;
    class->rel->class_idx = class->class_idx;
    class->rel->class_name_idx = class->class_name_idx;

    class->user = self->admin;
    self->admin->root_class = class;

    class->dbpath = self->schema_path;
    class->dbpath_size = self->schema_path_size;
    class->batch_mode = true;
    err = class->load(class, NULL, "index", sizeof("index") - 1);
    if (err != knd_OK) {
        knd_log("-- couldn't read any schema definitions :(");
        goto error;
    }
    knd_log(".. class DB coordination in progress ..");
    err = class->coordinate(class);
    if (err != knd_OK) goto error;
    class->batch_mode = false;

    *lint = self;
    return knd_OK;
error:
    kndLint_delete(self);
    return err;
}
