#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_class_inst.h"
#include "knd_class.h"
#include "knd_mempool.h"
#include "knd_attr.h"
#include "knd_attr_inst.h"
#include "knd_repo.h"
#include "knd_shard.h"

#include "knd_text.h"
#include "knd_num.h"
#include "knd_set.h"

#include "knd_user.h"
#include "knd_state.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_INST_IMPORT_LEVEL_1 0
#define DEBUG_INST_IMPORT_LEVEL_2 0
#define DEBUG_INST_IMPORT_LEVEL_3 0
#define DEBUG_INST_IMPORT_LEVEL_4 0
#define DEBUG_INST_IMPORT_LEVEL_TMP 1

static gsl_err_t import_class_inst(struct kndClassInst *self,
                                   const char *rec, size_t *total_size,
                                   struct kndTask *task);

struct LocalContext {
    struct kndClassInst *class_inst;
    //struct kndClass *class;
    struct kndTask *task;
};

static gsl_err_t run_set_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndClassInst *self = ctx->class_inst;
    struct kndClassEntry *class_entry;
    struct kndClassInstEntry *entry;
    struct kndRepo *repo = ctx->task->repo;
    struct kndTask *task = ctx->task;
    struct kndSharedDict *class_name_idx = repo->class_name_idx;
    struct kndSharedDict *name_idx = self->blueprint->entry->inst_name_idx;
    struct kndClass *c;
    struct kndNameBuf *namebuf;
    int err;

    if (name_size == 0) return make_gsl_err(gsl_OK);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    /* autogenerate unique name */
    if (name_size == 1 && *name == '_') {
        knd_log("NB: auto generate name: \"%.*s\"", name_size, name);
        self->autogenerate_name = true;
        return make_gsl_err(gsl_OK);
    }

    /* inner obj? */
    if (self->type == KND_OBJ_INNER) {
        class_entry = knd_shared_dict_get(class_name_idx, name, name_size);
        if (!class_entry) {
            KND_TASK_LOG("inner obj: no such class: %.*s", name_size, name);
            return make_gsl_err(gsl_FAIL);
        }
        c = class_entry->class;

        err = knd_is_base(self->blueprint, c);
        if (err) {
            KND_TASK_LOG("no inheritance from %.*s to %.*s",
                         self->blueprint->name_size, self->blueprint->name,
                         c->name_size, c->name);
            return make_gsl_err_external(err);
        }
        self->blueprint = c;
        return make_gsl_err(gsl_OK);
    }

    if (name_idx) {
        entry = knd_shared_dict_get(name_idx, name, name_size);
        if (entry) {
            /*if (entry->inst && entry->inst->states->phase == KND_REMOVED) {
              knd_log("-- this class instance has been removed lately: %.*s",
              name_size, name);
              goto assign_name;
              }*/
            KND_TASK_LOG("class instance name already exists: %.*s", name_size, name);
            return make_gsl_err(gsl_EXISTS);
        }
    }

    self->name = name;
    self->name_size = name_size;
    self->entry->name = name;
    self->entry->name_size = name_size;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t run_set_alias(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndClassInst *self = ctx->class_inst;
    self->alias = name;
    self->alias_size = name_size;
    return make_gsl_err(gsl_OK);
}

static int validate_attr(struct kndClassInst *self,
                         const char *name,
                         size_t name_size,
                         struct kndAttr **result,
                         struct kndAttrInst **result_attr_inst,
                         struct kndTask *task)
{
    struct kndAttrRef *attr_ref;
    struct kndAttr *attr;
    struct kndAttrInst *attr_inst = NULL;
    int err;

    if (DEBUG_INST_IMPORT_LEVEL_2)
        knd_log(".. \"%.*s\" (base class: %.*s) to validate attr_inst: \"%.*s\"",
                self->name_size, self->name,
                self->blueprint->name_size, self->blueprint->name,
                name_size, name);

    /* check existing attr_insts */
    for (attr_inst = self->attr_insts; attr_inst; attr_inst = attr_inst->next) {
        if (!memcmp(attr_inst->attr->name, name, name_size)) {
            if (DEBUG_INST_IMPORT_LEVEL_2)
                knd_log("++ ATTR_INST \"%.*s\" is already set!", name_size, name);
            *result_attr_inst = attr_inst;
            return knd_OK;
        }
    }
    err = knd_class_get_attr(self->blueprint, name, name_size, &attr_ref);
    KND_TASK_ERR("\"%.*s\" attr is not approved", name_size, name);

    attr = attr_ref->attr;
    *result = attr;
    return knd_OK;
}

static gsl_err_t import_attr_var(void *obj,
                                 const char *name, size_t name_size,
                                 const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    int err;
    err = knd_import_attr_var(ctx->class_inst->class_var, name, name_size, rec, total_size, ctx->task);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_attr_var_list(void *obj,
                                      const char *name, size_t name_size,
                                      const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    int err;
    err = knd_import_attr_var_list(ctx->class_inst->class_var, name, name_size, rec, total_size, ctx->task);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t check_empty_inst(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    KND_TASK_LOG("attempt to create an empty class instance");
    // TODO release resources
    return make_gsl_err(gsl_FORMAT);
}

static gsl_err_t import_class_inst(struct kndClassInst *self,
                                   const char *rec, size_t *total_size,
                                   struct kndTask *task)
{
    if (DEBUG_INST_IMPORT_LEVEL_2)
        knd_log(".. class inst import REC: %.*s", 128, rec);

    struct LocalContext ctx = {
        .class_inst = self,
        .task = task
    };
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = &ctx
        },
        { .name = "_as",
          .name_size = strlen("_as"),
          .run = run_set_alias,
          .obj = &ctx
        },
        { .validate = import_attr_var,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = import_attr_var_list,
          .obj = &ctx
        },
        { .is_default = true,
          .run = check_empty_inst,
          .obj = &ctx
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static gsl_err_t run_set_state_id(void *obj, const char *name, size_t name_size)
{
    struct kndClassInst *self = obj;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    if (DEBUG_INST_IMPORT_LEVEL_2)
        knd_log("++ class inst state: \"%.*s\" inst:%.*s",
                name_size, name, self->name_size, self->name);

    return make_gsl_err(gsl_OK);
}

static int generate_uniq_inst_name(struct kndClassInst *inst, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndOutput *out = task->out;
    struct kndNameBuf *namebuf;
    struct kndClass *c = inst->blueprint;
    int err;

    err = knd_name_buf_new(mempool, &namebuf);
    KND_TASK_ERR("name buf alloc failed");

    out->reset(out);
    OUT("/", 1);
    OUT(task->shard->name, task->shard->name_size);
    OUT(c->entry->repo->name, c->entry->repo->name_size);
    OUT(c->abbr, c->abbr_size);
    OUT("/", 1);
    OUT(inst->entry->id, inst->entry->id_size);

    memcpy(namebuf->name, out->buf, out->buf_size);
    namebuf->name_size = out->buf_size;
    inst->name = namebuf->name;
    inst->name_size = namebuf->name_size;
    inst->entry->name = namebuf->name;
    inst->entry->name_size = namebuf->name_size;
    return knd_OK;
}

int knd_import_class_inst(struct kndClass *self,
                          const char *rec, size_t *total_size,
                          struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndClass *c = self;
    struct kndClassInst *inst;
    struct kndClassInstEntry *entry;
    struct kndClassVar *class_var;
    struct kndRepo *repo = task->repo;
    struct kndState *state;
    struct kndStateRef *state_ref;
    struct kndTaskContext *ctx = task->ctx;
    int err;
    gsl_err_t parser_err;

    if (task->user_ctx) {
        repo = task->user_ctx->repo;
        // use non-ephemeral mempool
        mempool = task->shard->user->mempool;
    }
    if (DEBUG_INST_IMPORT_LEVEL_2) {
        knd_log(".. class \"%.*s\" to import inst \"%.*s\" (repo:%.*s) task #%d type:%d",
                self->name_size, self->name, 128, rec, repo->name_size, repo->name, task->id, task->type);
    }
    switch (task->type) {
    case KND_INNER_COMMIT_STATE:
        // fall through
    case KND_INNER_STATE:
        task->type = KND_INNER_COMMIT_STATE;
        break;
    default:
        task->type = KND_COMMIT_STATE;
    }
    
    err = knd_class_inst_entry_new(mempool, &entry);
    KND_TASK_ERR("class inst  alloc failed");
    err = knd_class_inst_new(mempool, &inst);
    KND_TASK_ERR("class inst alloc failed");
    inst->entry = entry;
    entry->inst = inst;
    inst->blueprint = c;

    err = knd_class_var_new(mempool, &class_var);
    KND_TASK_ERR("failed to alloc a class var");
    class_var->entry = self->entry;
    class_var->parent = self;
    class_var->inst = inst;
    inst->class_var = class_var;

    parser_err = import_class_inst(inst, rec, total_size, task);
    if (parser_err.code) return parser_err.code;

    /* generate unique inst id */
    inst->entry->numid = atomic_fetch_add_explicit(&c->entry->inst_id_count, 1, memory_order_relaxed);
    inst->entry->numid++;
    knd_uid_create(inst->entry->numid, inst->entry->id, &inst->entry->id_size);

    if (!inst->name_size && inst->autogenerate_name) {
        err = generate_uniq_inst_name(inst, task);
        KND_TASK_ERR("failed to generate unique inst name");
    }
    
    switch (task->type) {
    case KND_INNER_COMMIT_STATE:
        entry->next = ctx->stm_class_insts;
        ctx->stm_class_insts = entry;
        ctx->num_stm_class_insts++;
        return knd_OK;
    default:
        break;
    }

    err = knd_state_new(mempool, &state);
    KND_TASK_ERR("state alloc failed");
    state->phase = KND_CREATED;
    state->numid = 1;
    inst->states = state;
    inst->num_states = 1;

    err = knd_state_ref_new(mempool, &state_ref);
    KND_TASK_ERR("failed to alloc a state ref");
    state_ref->state = state;
    state_ref->type = KND_STATE_CLASS_INST;
    state_ref->obj = (void*)entry;

    state_ref->next = ctx->class_inst_state_refs;
    ctx->class_inst_state_refs = state_ref;
    ctx->num_class_inst_state_refs++;

    if (DEBUG_INST_IMPORT_LEVEL_TMP)
        knd_log("++ class inst \"%.*s::%.*s\" numid:%zu initial import  OK!",
                self->name_size, self->name, inst->name_size, inst->name, inst->entry->numid);
    return knd_OK;
}
