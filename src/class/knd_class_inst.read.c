#include "knd_class_inst.h"
#include "knd_task.h"
#include "knd_utils.h"
#include "knd_user.h"
#include "knd_repo.h"
#include "knd_shared_set.h"
#include "knd_shared_dict.h"
#include "knd_attr.h"
#include "knd_attr_stm.h"

#define DEBUG_CLASS_INST_READ_LEVEL_1 0
#define DEBUG_CLASS_INST_READ_LEVEL_2 0
#define DEBUG_CLASS_INST_READ_LEVEL_3 0
#define DEBUG_CLASS_INST_READ_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
    struct kndAttrStm *attr_stm;
    struct kndClass *class;
    struct kndClassRef *class_ref;
    struct kndClassInst *class_inst;
};

int knd_class_inst_unmarshall(const char *elem_id, size_t elem_id_size, const char *rec, size_t rec_size,
                              void **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->mempool;
    struct kndClassInst *inst = NULL;
    size_t total_size = rec_size;
    int err;

    if (DEBUG_CLASS_INST_READ_LEVEL_2)
        knd_log(">> GSP class inst \"%.*s\" => \"%.*s\"", elem_id_size, elem_id, rec_size, rec);

    err = knd_class_inst_new(&inst, mempool);
    KND_TASK_ERR("failed to alloc a class inst");

    err = knd_class_inst_read(inst, rec, &total_size, task);
    KND_TASK_ERR("failed to read GSP class inst rec");

    *result = inst;
    return knd_OK;
}

int knd_class_inst_acquire(struct kndClassInstEntry *entry, struct kndClassInst **result,
                           struct kndTask *task)
{
    struct kndClassInst *inst = NULL;
    struct kndClass *c;
    //struct kndStorageLeaf *leaf;
    int err;

    assert(entry->is_a != NULL);

    err = knd_class_acquire(entry->is_a, &c, task);
    KND_TASK_ERR("failed to acquire {cls %.*s}", entry->is_a->name_size, entry->is_a->name);

    //    leaf = c->class_inst_idx_leaf;
    // TODO

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

static gsl_err_t read_attr_stm(void *obj, const char *name, size_t name_size,
                               const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttrStm *stm;
    int err;

    err = knd_attr_stm_new(&stm, NULL, task->mempool);
    if (err) {
        KND_TASK_LOG("failed to alloc an attr stm");
        return *total_size = 0, make_gsl_err_external(err);
    }

    err = knd_read_attr_stm(stm, name, name_size, rec, total_size, ctx->task);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    knd_class_inst_append_attr_stm(ctx->class_inst, stm);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t read_attr_stm_list(void *obj, const char *name, size_t name_size,
                                    const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndAttrStm *stm;
    int err;

    err = knd_attr_stm_new(&stm, NULL, task->mempool);
    if (err) {
        KND_TASK_LOG("failed to alloc an attr stm");
        return *total_size = 0, make_gsl_err_external(err);
    }

    err = knd_read_attr_stm_list(stm, name, name_size, rec, total_size, ctx->task);
    if (err) return *total_size = 0, make_gsl_err_external(err);

    knd_class_inst_append_attr_stm(ctx->class_inst, stm);

    return make_gsl_err(gsl_OK);
}

int knd_class_inst_read(struct kndClassInst *self, const char *rec, size_t *total_size,
                        struct kndTask *task)
{
    struct kndClassEntry *entry = NULL;
    struct kndClass *c;
    int err;

    assert(entry != NULL);

    if (DEBUG_CLASS_INST_READ_LEVEL_2) {
        knd_log(".. reading class inst GSP (entry:%p): \"%.*s\"..", entry, 128, rec);
    }

    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire class %.*s", entry->name_size, entry->name);

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
        { .validate = read_attr_stm,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .validate = read_attr_stm_list,
          .obj = &ctx
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err.code;

    return knd_OK;
}
