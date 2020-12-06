#include "knd_class_inst.h"
#include "knd_task.h"
#include "knd_utils.h"
#include "knd_user.h"
#include "knd_repo.h"
#include "knd_shared_set.h"
#include "knd_shared_dict.h"
#include "knd_attr.h"

#define DEBUG_CLASS_INST_GSP_LEVEL_1 0
#define DEBUG_CLASS_INST_GSP_LEVEL_2 0
#define DEBUG_CLASS_INST_GSP_LEVEL_3 0
#define DEBUG_CLASS_INST_GSP_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndRepo *repo;
    struct kndAttrVar *attr_var;
    struct kndClass *class;
    struct kndClassRef *class_ref;
    struct kndClassInst *class_inst;
};

int knd_class_inst_marshall(void *obj, size_t *output_size, struct kndTask *task)
{
    struct kndClassInstEntry *entry = obj;
    struct kndOutput *out = task->out;
    size_t orig_size = out->buf_size;
    int err;
    assert(entry->inst != NULL);

    err = knd_class_inst_export_GSP(entry->inst, task);
    KND_TASK_ERR("failed to export class inst GSP");

    if (DEBUG_CLASS_INST_GSP_LEVEL_2)
        knd_log(">> GSP of class inst \"%.*s\" size:%zu",
                entry->name_size, entry->name, out->buf_size - orig_size);

    *output_size = out->buf_size - orig_size;
    return knd_OK;
}

int knd_class_inst_entry_unmarshall(const char *elem_id, size_t elem_id_size, const char *rec, size_t rec_size,
                                    void **result, struct kndTask *task)
{
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    struct kndClassInstEntry *entry = NULL;
    struct kndRepo *repo = task->repo;
    struct kndClassEntry *blueprint = task->blueprint;
    struct kndCharSeq *seq;
    struct kndSharedDictItem *item;
    const char *c, *name = rec;
    size_t name_size;
    int err;

    assert(blueprint != NULL);

    if (DEBUG_CLASS_INST_GSP_LEVEL_2)
        knd_log(">> GSP class inst entry \"%.*s\" => \"%.*s\"", elem_id_size, elem_id, rec_size, rec);

    err = knd_class_inst_entry_new(mempool, &entry);
    KND_TASK_ERR("failed to alloc a class entry");
    entry->repo = task->repo;
    memcpy(entry->id, elem_id, elem_id_size);
    entry->id_size = elem_id_size;
    entry->blueprint = blueprint;

    /* get name numid */
    c = name;
    while (*c) {
        if (*c == '{' || *c == '[') break;
        c++;
    }
    name_size = c - name;
    if (!name_size) {
        err = knd_FORMAT;
        KND_TASK_ERR("anonymous class inst entry in GSP");
    }
    entry->name = name;
    entry->name_size = name_size;
    /* check charseq decoding */
    if (name_size <= KND_ID_SIZE) {
        err = knd_shared_set_get(repo->str_idx, name, name_size, (void**)&seq);
        if (!err) {
            entry->name = seq->val;
            entry->name_size = seq->val_size;
            entry->seq = seq;
        }
    }

    err = knd_shared_dict_set(blueprint->class->inst_name_idx, entry->name, entry->name_size,
                              (void*)entry, task->mempool, NULL, &item, false);
    KND_TASK_ERR("failed to register class inst name");
    entry->dict_item = item;

    err = knd_shared_set_add(blueprint->class->inst_idx, entry->id, entry->id_size, (void*)entry);
    KND_TASK_ERR("failed to register class inst entry \"%.*s\"", entry->id_size, entry->id);

    if (DEBUG_CLASS_INST_GSP_LEVEL_3)
        knd_log("== class inst name decoded \"%.*s\" => \"%.*s\" (repo:%.*s)",
                entry->id_size, entry->id, entry->name_size, entry->name, repo->name_size, repo->name);

    *result = entry;
    return knd_OK;
}

int knd_class_inst_export_GSP(struct kndClassInst *self, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    size_t curr_depth;
    int err;

    OUT(self->name, self->name_size);
    if (task->ctx->use_alias && self->alias_size) {
        err = out->write(out, "{_as ", strlen("{_as "));             RET_ERR();
        err = out->write(out, self->alias,
                         self->alias_size);                   RET_ERR();
        err = out->writec(out, '}');                                 RET_ERR();
    }
    if (self->linear_pos) {
        err = out->write(out, "{_pos ", strlen("{_pos "));             RET_ERR();
        err = out->writef(out, "%zu", self->linear_pos);                   RET_ERR();
        err = out->writec(out, '}');                                 RET_ERR();
    }
    if (self->linear_len) {
        err = out->write(out, "{_len ", strlen("{_len "));             RET_ERR();
        err = out->writef(out, "%zu", self->linear_len);                   RET_ERR();
        err = out->writec(out, '}');                                 RET_ERR();
    }
    if (self->class_var->attrs) {
        curr_depth = task->ctx->depth;
        err = knd_attr_vars_export_GSP(self->class_var->attrs, out, task, 0, false);
        KND_TASK_ERR("failed to export attr vars GSP");
        task->ctx->depth = curr_depth;
    }
    return knd_OK;
}
