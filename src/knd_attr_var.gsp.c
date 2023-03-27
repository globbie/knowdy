#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

/* numeric conversion by strtol */
#include <errno.h>
#include <limits.h>

#include "knd_config.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_state.h"
#include "knd_user.h"
#include "knd_repo.h"
#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_shared_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#define DEBUG_ATTR_VAR_GSP_LEVEL_1 0
#define DEBUG_ATTR_VAR_GSP_LEVEL_2 0
#define DEBUG_ATTR_VAR_GSP_LEVEL_3 0
#define DEBUG_ATTR_VAR_GSP_LEVEL_4 0
#define DEBUG_ATTR_VAR_GSP_LEVEL_5 0
#define DEBUG_ATTR_VAR_GSP_LEVEL_TMP 1

struct LocalContext {
    struct kndClassVar *class_var;
    struct kndAttrVar  *list_parent;
    struct kndSet      *attr_idx;
    struct kndAttr     *attr;
    struct kndAttrVar  *attr_var;
    struct kndRepo     *repo;
    struct kndTask     *task;
};

static int attr_var_list_export_GSP(struct kndAttrVar *parent_item, struct kndTask *task, struct kndOutput *out);

static int inner_var_export_GSP(struct kndAttrVar *var, struct kndTask *task)
{
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size;
    struct kndOutput *out = task->out;
    struct kndAttrVar *item;
    struct kndAttr *attr;
    // struct kndClass *c;
    struct kndCharSeq *seq;
    int err;

    if (DEBUG_ATTR_VAR_GSP_LEVEL_2) {
        knd_log(">> class \"%.*s\" var:%.*s (%p) attr type:%d",
                var->class_var->parent->name_size,
                var->class_var->parent->name,
                var->name_size, var->name, var, var->attr->type);
        // knd_log(".. GSP export inner item: %.*s",
        //        var->name_size, var->name);
    }

    if (var->implied_attr) {
        attr = var->implied_attr;

        switch (attr->type) {
        case KND_ATTR_REF:
            OUT(var->class_entry->id, var->class_entry->id_size);
            break;
        case KND_ATTR_STR:
            err = knd_charseq_fetch(task->repo, var->name, var->name_size, &seq, task);
            KND_TASK_ERR("failed to encode a charseq");
            knd_uid_create(seq->numid, idbuf, &idbuf_size);
            OUT(idbuf, idbuf_size);
            break;
        default:
            break;
        }
    }
    
    FOREACH (item, var->children) {
        attr = item->attr;
        if (attr->is_a_set) {
            err = attr_var_list_export_GSP(item, task, out);
            KND_TASK_ERR("failed to export inner attr var list");
            continue;
        }

        OUT("{", 1);
        OUT(attr->id, attr->id_size);
        OUT(" ", 1);

        switch (attr->type) {
        case KND_ATTR_REF:
            OUT(item->class_entry->id, item->class_entry->id_size);
            break;
        case KND_ATTR_TEXT:
            OUT("{_t ", strlen("{_t "));
            err = knd_text_export_GSP(item->text, task);
            KND_TASK_ERR("failed to export text GSP");
            OUT("}", 1);
            break;
        case KND_ATTR_INNER:
            err = inner_var_export_GSP(item, task);
            KND_TASK_ERR("failed to export inner var GSP");
            break;
        case KND_ATTR_BOOL:
            OUT("t", 1);
            break;
        default:
            assert(item->val != NULL);
            assert(item->val_size != 0);
            err = knd_charseq_fetch(task->repo, item->val, item->val_size, &seq, task);
            KND_TASK_ERR("failed to encode a charseq");
            knd_uid_create(seq->numid, idbuf, &idbuf_size);
            OUT(idbuf, idbuf_size);
            break;
        }
        OUT("}", 1);
    }
    return knd_OK;
}

#if 0
static int proc_item_export_GSP(struct kndAttrVar *item, struct kndTask *task)
{
    struct kndProc *proc = item->proc;
    struct kndOutput *out = task->out;
    assert(proc != NULL);
    OUT("{_p ", strlen("{_p "));
    OUT(proc->entry->id, proc->entry->id_size);
    OUT("}", 1);
    return knd_OK;
}
#endif

static int attr_var_list_export_GSP(struct kndAttrVar *var, struct kndTask *task, struct kndOutput *out)
{
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size;
    struct kndCharSeq *seq;
    struct kndAttrVar *item;

    assert(var->attr != NULL);
    knd_attr_type attr_type = var->attr->type;
    int err;

    if (DEBUG_ATTR_VAR_GSP_LEVEL_2)
        knd_log(".. export GSP list: %.*s", var->name_size, var->name);

    OUT("[", 1);
    OUT(var->attr->id, var->attr->id_size);
    FOREACH (item, var->list) {
        OUT("{", 1);
        switch (attr_type) {
        case KND_ATTR_REL:
            // fall through
        case KND_ATTR_REF:
            assert(item->class_entry != NULL);
            OUT(item->class_entry->id, item->class_entry->id_size);
            break;
        case KND_ATTR_TEXT:
            OUT("{_t ", strlen("{_t "));
            err = knd_text_export_GSP(item->text, task);
            KND_TASK_ERR("failed to export text GSP");
            OUT("}", 1);
            break;
        case KND_ATTR_INNER:
            err = inner_var_export_GSP(item, task);
            KND_TASK_ERR("failed to export inner attr var");
            break;
        case KND_ATTR_STR:
            err = knd_charseq_fetch(task->repo, item->name, item->name_size, &seq, task);
            KND_TASK_ERR("failed to encode a charseq");
            knd_uid_create(seq->numid, idbuf, &idbuf_size);
            OUT(idbuf, idbuf_size);
            break;
        default:
            OUT(item->name, item->name_size);
            // err = knd_attr_var_export_GSP(item, task, out, 0);
            // KND_TASK_ERR("failed to export attr var");
            break;
        }
        OUT("}", 1);
    }
    OUT("]", 1);
    return knd_OK;
}

int knd_attr_vars_export_GSP(struct kndAttrVar *items, struct kndOutput *out, struct kndTask *task,
                             size_t unused_var(depth), bool is_concise)
{
    struct kndAttrVar *item;
    struct kndAttr *attr;
    int err;

    FOREACH (item, items) {
        if (!item->attr) continue;
        attr = item->attr;
        if (is_concise && !attr->concise_level) continue;

        if (attr->is_a_set) {
            err = attr_var_list_export_GSP(item, task, out);
            KND_TASK_ERR("failed to export attr var list");
            continue;
        }
        OUT("{", 1);
        OUT(attr->id, attr->id_size);
        OUT(" ", 1);
        err = knd_attr_var_export_GSP(item, task, out, 0);
        KND_TASK_ERR("failed to export attr var");
        OUT("}", 1);
    }
    return knd_OK;
}

int knd_attr_var_export_GSP(struct kndAttrVar *var, struct kndTask *task, struct kndOutput *out,
                            size_t unused_var(depth))
{
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size;
    struct kndCharSeq *seq;
    int err;

    assert(var->attr != NULL);
    knd_attr_type attr_type = var->attr->type;

    switch (attr_type) {
    case KND_ATTR_REF:
        OUT(var->class_entry->id, var->class_entry->id_size);
        break;
    case KND_ATTR_PROC_REF:
        //err = proc_item_export_GSP(var, task);
        //KND_TASK_ERR("failed to export proc var GSP");
        break;
    case KND_ATTR_INNER:
        err = inner_var_export_GSP(var, task);
        KND_TASK_ERR("failed to export inner var GSP");
        break;
    case KND_ATTR_TEXT:
        OUT("{_t ", strlen("{_t "));
        err = knd_text_export_GSP(var->text, task);
        KND_TASK_ERR("GSP text export failed");
        OUT("}", 1);
        break;
    default:
        err = knd_charseq_fetch(task->repo, var->val, var->val_size, &seq, task);
        KND_TASK_ERR("failed to encode a charseq");
        knd_uid_create(seq->numid, idbuf, &idbuf_size);
        OUT(idbuf, idbuf_size);
        break;
    }
    return knd_OK;
}
