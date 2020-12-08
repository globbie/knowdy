#include "knd_class_inst.h"
#include "knd_task.h"
#include "knd_utils.h"
#include "knd_user.h"
#include "knd_repo.h"
#include "knd_shared_set.h"
#include "knd_shared_dict.h"
#include "knd_attr.h"

#define DEBUG_CLASS_INST_READ_LEVEL_1 0
#define DEBUG_CLASS_INST_READ_LEVEL_2 0
#define DEBUG_CLASS_INST_READ_LEVEL_3 0
#define DEBUG_CLASS_INST_READ_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
    struct kndAttrVar *attr_var;
    struct kndClass *class;
    struct kndClassRef *class_ref;
    struct kndClassInst *class_inst;
};

int knd_class_inst_unmarshall(const char *elem_id, size_t elem_id_size, const char *rec, size_t rec_size,
                              void **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndClassInst *inst = NULL;
    size_t total_size = rec_size;
    int err;

    assert(task->payload != NULL);

    if (DEBUG_CLASS_INST_READ_LEVEL_2)
        knd_log(">> GSP class inst \"%.*s\" => \"%.*s\"", elem_id_size, elem_id, rec_size, rec);

    err = knd_class_inst_new(mempool, &inst);
    KND_TASK_ERR("failed to alloc a class inst");

    err = knd_class_inst_read(inst, rec, &total_size, task);
    KND_TASK_ERR("failed to read GSP class inst rec");

    *result = inst;
    return knd_OK;
}

int knd_class_inst_acquire(struct kndClassInstEntry *entry, struct kndClassInst **result, struct kndTask *task)
{
    struct kndClassInst *inst = NULL, *prev_inst;

    assert(entry->blueprint != NULL);
    assert(entry->blueprint->class != NULL);

    struct kndClass *c = entry->blueprint->class;
    // int num_readers;
    int err;

    assert(c != NULL);

    // TODO read/write conflicts
    atomic_fetch_add_explicit(&entry->num_readers, 1, memory_order_relaxed);
 
    do {
        prev_inst = atomic_load_explicit(&entry->inst, memory_order_relaxed);
        if (prev_inst) {
            // TODO if inst - free 
            *result = prev_inst;
            return knd_OK;
        }
        if (!inst) {
            // NB: passing blueprint class entry via task
            task->payload = entry->blueprint;
            err = knd_shared_set_unmarshall_elem(c->inst_idx, entry->id, entry->id_size,
                                                 knd_class_inst_unmarshall, (void**)&inst, task);
            if (err) return err;
            inst->entry = entry;
            inst->name = entry->name;
            inst->name_size = entry->name_size;
        }
    } while (!atomic_compare_exchange_weak(&entry->inst, &prev_inst, inst));

    *result = inst;
    return knd_OK;
}

static gsl_err_t check_class_inst_name(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx      = obj;
    struct kndRepo *repo          = ctx->repo;

    if (DEBUG_CLASS_INST_READ_LEVEL_2)
        knd_log(".. repo \"%.*s\" to check a class name: \"%.*s\" (size:%zu)",
                repo->name_size, repo->name, name_size, name, name_size);
    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_attr_var(void *obj, const char *name, size_t name_size,
                               const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    int err;
    err = knd_read_attr_var(ctx->class_inst->class_var, name, name_size, rec, total_size, ctx->task);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_attr_var_list(void *obj, const char *name, size_t name_size, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    int err;
    err = knd_read_attr_var_list(ctx->class_inst->class_var, name, name_size, rec, total_size, ctx->task);
    if (err) return *total_size = 0, make_gsl_err_external(err);
    return make_gsl_err(gsl_OK);
}

int knd_class_inst_read(struct kndClassInst *self, const char *rec, size_t *total_size, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx->mempool;
    struct kndClassEntry *entry = task->payload;
    struct kndClassVar *class_var;
    int err;
    assert(entry != NULL);

    if (DEBUG_CLASS_INST_READ_LEVEL_2) {
        knd_log(".. reading class inst GSP (entry:%p): \"%.*s\"..", entry, 128, rec);
        entry->class->str(entry->class, 1);
    }
    
    err = knd_class_var_new(mempool, &class_var);
    KND_TASK_ERR("failed to alloc a class var");
    class_var->entry = entry;
    class_var->parent = entry->class;
    class_var->inst = self;
    self->class_var = class_var;

    struct LocalContext ctx = {
        .task = task,
        .class_inst = self,
        .repo = task->repo
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = check_class_inst_name,
          .obj = &ctx
        },
        { .validate = read_attr_var,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = read_attr_var_list,
          .obj = &ctx
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err.code;

    return knd_OK;
}
