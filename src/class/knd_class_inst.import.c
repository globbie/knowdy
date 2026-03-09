#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_class_inst.h"
#include "knd_class.h"
#include "knd_mempool.h"
#include "knd_attr.h"
#include "knd_attr_stm.h"
#include "knd_repo.h"

#include "knd_text.h"
#include "knd_quant.h"
#include "knd_shared_dict.h"

#include "knd_user.h"
#include "knd_state.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_INST_IMPORT_LEVEL_1 0
#define DEBUG_INST_IMPORT_LEVEL_2 0
#define DEBUG_INST_IMPORT_LEVEL_3 0
#define DEBUG_INST_IMPORT_LEVEL_4 0
#define DEBUG_INST_IMPORT_LEVEL_TMP 1

static gsl_err_t import_class_inst(struct kndClassInst *self, const char *rec,
                                   size_t *total_size, struct kndRepo *repo, struct kndTask *task);

struct LocalContext {
    struct kndClassInst *class_inst;
    struct kndTask *task;
    struct kndRepo *repo;
};

static gsl_err_t run_set_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndRepo *repo = ctx->repo;
    struct kndClassInst *self = ctx->class_inst;
    struct kndClassEntry *class_entry;
    struct kndClassInstEntry *entry = NULL;
    struct kndTask *task = ctx->task;
    struct kndDict *class_name_idx = task->idxs.cls_name_idx;
    struct kndDict *name_idx;
    struct kndClass *c, *inner_c;
    int err;

    assert(self->entry->is_a != NULL);

    err = knd_class_acquire(self->entry->is_a, &c, repo, task);
    if (err) {
        KND_TASK_LOG("failed to acquire {cls %.*s}",
                     self->entry->is_a->name_size, self->entry->is_a->name);
        return make_gsl_err_external(err);
    }
    name_idx = c->inst_name_idx;
    
    if (name_size == 0) return make_gsl_err(gsl_OK);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    /* autogenerate unique name */
    if (name_size == 1 && *name == '_') {
        if (DEBUG_INST_IMPORT_LEVEL_2)
            knd_log("NB: auto generate name: \"%.*s\"", name_size, name);
        self->autogenerate_name = true;
        return make_gsl_err(gsl_OK);
    }

    /* inner obj? */
    if (self->type == KND_OBJ_INNER) {
        err = knd_dict_get(class_name_idx, name, name_size, (void**)&class_entry, task);
        // TODO
        /*if (!class_entry) {
            KND_TASK_LOG("inner obj: no such {cls %.*s}", name_size, name);
            return make_gsl_err(gsl_FAIL);
            }*/

        err = knd_class_acquire(class_entry, &inner_c, repo, task);
        if (err) {
            KND_TASK_LOG("failed to acquire {cls %.*s}", class_entry->name_size, class_entry->name);
            return make_gsl_err_external(err);
        }

        err = knd_class_is_base(inner_c, c);
        if (err) {
            KND_TASK_LOG("no inheritance from %.*s to %.*s",
                         inner_c->name_size, inner_c->name, c->name_size, c->name);
            return make_gsl_err_external(err);
        }
        self->entry->is_a = class_entry;
        return make_gsl_err(gsl_OK);
    }

    if (name_idx) {
        //entry = knd_dict_get(name_idx, name, name_size);
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

static gsl_err_t import_attr_stm(void *obj, const char *name, size_t name_size,
                                 const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttrStm *stm;
    int err;

    err = knd_attr_stm_new(&stm, NULL, task->mempool);
    if (err) {
        return *total_size = 0, make_gsl_err_external(err);
    }

    err = knd_import_attr_stm(stm, name, name_size,
                              rec, total_size, ctx->task);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t import_attr_stm_list(void *obj, const char *name, size_t name_size,
                                      const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttrStm *stm;
    int err;

    err = knd_attr_stm_new(&stm, NULL, task->mempool);
    if (err) {
        return *total_size = 0, make_gsl_err_external(err);
    }

    err = knd_import_attr_stm_list(stm, name, name_size,
                                   rec, total_size, ctx->task);
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

static gsl_err_t import_class_inst(struct kndClassInst *self, const char *rec, size_t *total_size,
                                   struct kndRepo *repo, struct kndTask *task)
{
    if (DEBUG_INST_IMPORT_LEVEL_2)
        knd_log(".. class inst to import REC: %.*s", 128, rec);

    struct LocalContext ctx = {
        .class_inst = self,
        .repo = repo,
        .task = task
    };
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = run_set_name,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "_gloss",
          .name_size = strlen("_gloss"),
          .parse = knd_parse_gloss_array,
          .obj = task
        },
        { .name = "_as",
          .name_size = strlen("_as"),
          .run = run_set_alias,
          .obj = &ctx
        },
        { .name = "_pos",
          .name_size = strlen("_pos"),
          .parse = gsl_parse_size_t,
          .obj = &self->linear_pos
        },
        { .name = "_len",
          .name_size = strlen("_len"),
          .parse = gsl_parse_size_t,
          .obj = &self->linear_len
        },
        { .validate = import_attr_stm,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = import_attr_stm_list,
          .obj = &ctx
        },
        { .is_default = true,
          .run = check_empty_inst,
          .obj = &ctx
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

static int generate_uniq_inst_name(struct kndClassInst *inst, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndOutput *out = task->out;
    struct kndNameBuf *namebuf;
    int err;

    err = knd_name_buf_new(mempool, &namebuf);
    KND_TASK_ERR("name buf alloc failed");

    out->reset(out);
    namebuf->name_size = knd_generate_random_id(namebuf->name, KND_RAND_CHUNK_SIZE, KND_NUM_RAND_CHUNKS, '-');

    inst->name = namebuf->name;
    inst->name_size = namebuf->name_size;
    inst->entry->name = namebuf->name;
    inst->entry->name_size = namebuf->name_size;
    return knd_OK;
}

static int register_by_name(struct kndClassInstEntry *entry, struct kndRepo *repo, struct kndTask *task)
{
    struct kndDict *name_idx;
    struct kndMemPool *mempool = task->mempool;
    struct kndClass *c;
    int err;

    err = knd_class_acquire(entry->is_a, &c, repo, task);
    KND_TASK_ERR("failed to acquire class %.*s", entry->is_a->name_size, entry->is_a->name);

    name_idx = c->inst_name_idx;
    if (!name_idx) {
        err = knd_dict_new(&name_idx, KND_MEDIUM_DICT_SIZE, mempool);
        KND_TASK_ERR("failed to create inst name idx");

        c->inst_name_idx = name_idx;
    }

    err = knd_dict_set(name_idx, entry->name, entry->name_size, (void*)entry, task);
    KND_TASK_ERR("name idx failed to register {cls-inst %.*s}", entry->name_size, entry->name);

    return knd_OK;
}

int knd_import_class_inst(struct kndClassEntry *entry, const char *rec, size_t *total_size,
                          struct kndRepo *repo, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndClass *c;
    struct kndClassInst *inst;
    struct kndClassInstEntry *inst_entry;
    struct kndState *state;
    struct kndStateRef *state_ref;
    struct kndTaskContext *ctx = task->ctx;
    struct kndClassDeclar *declar = NULL;
    int err;
    gsl_err_t parser_err;

    if (DEBUG_INST_IMPORT_LEVEL_2) {
        knd_log("{cls %.*s} to import {inst %.*s} {task-type %d}",
                entry->name_size, entry->name,  64, rec, task->type);
    }

    err = knd_class_acquire(entry, &c, repo, task);
    KND_TASK_ERR("failed to acquire {cls %.*s}", entry->name_size, entry->name);
   
    switch (task->type) {
    case KND_TASK_BULK_LOAD:
        break;
    case KND_TASK_RESTORE:
        break;
    case KND_TASK_INNER_COMMIT:
        // fall through
    case KND_TASK_INNER:
        task->type = KND_TASK_INNER_COMMIT;
        break;
    default:
        task->type = KND_TASK_COMMIT;
    }    
    err = knd_class_inst_entry_new(&inst_entry, mempool);
    KND_TASK_ERR("class inst  alloc failed");
    inst_entry->is_a = entry;

    err = knd_class_inst_new(&inst, mempool);
    KND_TASK_ERR("class inst alloc failed");
    inst->entry = inst_entry;
    inst_entry->inst = inst;

    parser_err = import_class_inst(inst, rec, total_size, repo, task);
    if (parser_err.code) return parser_err.code;

    /* reassign glosses if any */
    if (task->ctx->tr) {
        inst->tr = task->ctx->tr;
        task->ctx->tr = NULL;
    }

    /* generate unique inst id */
    inst->entry->numid = atomic_fetch_add_explicit(&c->inst_id_count, 1, memory_order_relaxed);
    inst->entry->numid++;
    knd_uid_create(inst->entry->numid, inst->entry->id, &inst->entry->id_size);

    if (!inst->name_size && inst->autogenerate_name) {
        err = generate_uniq_inst_name(inst, task);
        KND_TASK_ERR("failed to generate unique inst name");
    }

    switch (task->type) {
    case KND_TASK_BULK_LOAD:
        if (DEBUG_INST_IMPORT_LEVEL_3)
            knd_log("++ {class %.*s {inst %.*s}} numid:%zu init data import OK!",
                    entry->name_size, entry->name, inst->name_size, inst->name,
                    inst->entry->numid);
        /* register class inst by name */
        err = register_by_name(inst_entry, repo, task);
        KND_TASK_ERR("failed to register class inst by name");
        return knd_OK;

    case KND_TASK_INNER_COMMIT:
        FOREACH (declar, ctx->declars) {
            if (declar->entry == c->entry) break;
        }
        if (!declar) {
            err = knd_class_declar_new(&declar, mempool);
            KND_TASK_ERR("failed to alloc class declar");
            declar->entry = c->entry;
            declar->next = task->ctx->declars;
            task->ctx->declars = declar;
        }
        inst_entry->next = declar->insts;
        declar->insts = inst_entry;
        declar->num_insts++;
        return knd_OK;
    default:
        break;
    }

    err = knd_state_new(&state, mempool);
    KND_TASK_ERR("state alloc failed");
    state->phase = KND_CREATED;
    state->numid = 1;
    inst->states = state;
    inst->num_states = 1;

    err = knd_state_ref_new(&state_ref, mempool);
    KND_TASK_ERR("failed to alloc a state ref");
    state_ref->state = state;
    state_ref->type = KND_STATE_CLASS_INST;
    state_ref->obj = (void*)inst_entry;

    state_ref->next = ctx->class_inst_state_refs;
    ctx->class_inst_state_refs = state_ref;
    ctx->num_class_inst_state_refs++;

    if (DEBUG_INST_IMPORT_LEVEL_2)
        knd_log("++ {class %.*s {inst %.*s {numid %zu}}} initial import  OK {num-inst-states %zu}",
                entry->name_size, entry->name, inst->name_size, inst->name, inst->entry->numid,
                ctx->num_class_inst_state_refs);
    return knd_OK;
}
