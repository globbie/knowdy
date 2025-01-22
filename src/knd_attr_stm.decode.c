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
#include "knd_mempool.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_attr_stm.h"
#include "knd_quant.h"
#include "knd_task.h"
#include "knd_dict.h"
#include "knd_shared_dict.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>

#define DEBUG_ATTR_STM_DECODE_LEVEL_1 0
#define DEBUG_ATTR_STM_DECODE_LEVEL_2 0
#define DEBUG_ATTR_STM_DECODE_LEVEL_3 0
#define DEBUG_ATTR_STM_DECODE_LEVEL_4 0
#define DEBUG_ATTR_STM_DECODE_LEVEL_5 0
#define DEBUG_ATTR_STM_DECODE_LEVEL_TMP 1

struct LocalContext {
    struct kndClassBasePred *class_var;
    struct kndClass    *class;
    struct kndClass    *inner_class;
    struct kndAttrStm  *list_parent;
    struct kndSet      *attr_idx;
    struct kndAttr     *attr;
    struct kndAttrStm  *attr_stm;
    struct kndRepo     *repo;
    struct kndTask     *task;
};

static int decode_inner_attr_stm(struct kndClass *base,
                                 struct kndAttrStm *parent, struct kndTask *task)
{
    struct kndAttr *attr = parent->attr;
    struct kndClassEntry *entry = attr->class_entry;
    struct kndClass *c = attr->owner;
    int err;

    if (DEBUG_ATTR_STM_DECODE_LEVEL_2) {
        knd_log(".. decoding {base %.*s} inner obj {%.*s {cls %.*s} {val %.*s}}",
                base->name_size, base->name,
                parent->name_size, parent->name,
                attr->classname_size, attr->classname,
                parent->val_id_size,  parent->val_id);
    }
    assert (entry != NULL);
    err = knd_class_acquire(entry, &c, task);
    KND_TASK_ERR("failed to acquire inner {class %.*s}",
                 entry->name_size, entry->name);

    if (c->implied_attr) {
        switch (c->implied_attr->type) {
        case KND_ATTR_CLASS_REF:
            err = knd_shared_set_get(task->idxs->class_idx,
                                     parent->val_id, parent->val_id_size,
                                     (void**)&parent->class_entry);
            KND_TASK_ERR("failed to get a {class-id %.*s}",
                         parent->val_id_size, parent->val_id);
            break;
        case KND_ATTR_STR:
            // decode str
            break;
        default:
            break;
        }
    }

    err = knd_decode_attr_stms(base, parent->children, task);
    KND_TASK_ERR("failed to decode attr stms of {class %.*s}", base->name_size, base->name);

    return knd_OK;
}

static int decode_ref_attr_stm(struct kndClass *unused_var(base),
                               struct kndAttrStm *stm, struct kndTask *task)
{
    struct kndClassEntry *entry;
    int err;

    err = knd_shared_set_get(task->idxs->class_idx, stm->val_id, stm->val_id_size,
                             (void**)&entry);
    KND_TASK_ERR("{class %.*s} not found in {repo %.*s}",
                 stm->val_id_size, stm->val_id,
                 task->repo->name_size, task->repo->name);

    if (DEBUG_ATTR_STM_DECODE_LEVEL_3) {
            knd_log(">> decoded {ref %.*s {cls %.*s}}",
                    stm->name_size, stm->name,
                    entry->name_size, entry->name);
    }
    stm->class_entry = entry;

    return knd_OK;
}

static int decode_uint(struct kndAttrStm *stm, struct kndTask *task)
{
    struct kndCharSeq *seq;
    struct kndQuantAttrStm *quant_attr_stm;
    struct kndQuantUInt *uint;
    int err;

    err = knd_charseq_decode(stm->val_id, stm->val_id_size, &seq, task);
    KND_TASK_ERR("failed to decode a charseq");

    err = knd_quant_parse_uint(seq->val, seq->val_size, &uint, task);
    KND_TASK_ERR("failed to parse uint value");

    if (DEBUG_ATTR_STM_DECODE_LEVEL_2) {
        knd_log(".. decode {seq %.*s} => {uint %zu}",
                seq->val_size, seq->val, uint->numval);
    }
    err = knd_quant_attr_stm_new(&quant_attr_stm, task->mempool);
    KND_TASK_ERR("failed to alloc a quant attr stm");

    quant_attr_stm->uint = uint;
    stm->subtype = quant_attr_stm;
    return knd_OK;
}

static int decode_str(struct kndAttrStm *stm, struct kndTask *task)
{
    struct kndCharSeq *seq;
    int err;

    err = knd_charseq_decode(stm->val_id, stm->val_id_size, &seq, task);
    KND_TASK_ERR("failed to decode a charseq");

    stm->val = seq->val;
    stm->val_size = seq->val_size;

    return knd_OK;
}

static int decode_attr_stm(struct kndClass *base,
                           struct kndAttrStm *stm, struct kndTask *task)
{
    struct kndAttr *attr = stm->attr;
    int err;

    if (DEBUG_ATTR_STM_DECODE_LEVEL_2) {
            const char *attr_type_name = knd_attr_names[attr->type];
            size_t attr_type_name_size = strlen(attr_type_name);
            knd_log(".. decoding {base %.*s} {attr %.*s {type %.*s} {cls %.*s} {parent %.*s}}",
                    base->name_size, base->name,
                    stm->name_size, stm->name, attr_type_name_size, attr_type_name,
                    attr->classname_size, attr->classname,
                    attr->owner->name_size, attr->owner->name);
    }

    switch (attr->type) {
    case KND_ATTR_INNER:
        err = decode_inner_attr_stm(base, stm, task);
        KND_TASK_ERR("failed to decode {inner %.*s {id %.*s}}",
                     stm->name_size, stm->name, stm->id_size, stm->id);
        break;
    case KND_ATTR_UINT:
        err = decode_uint(stm, task);
        KND_TASK_ERR("failed to decode {%.*s {uint %.*s}}",
                     stm->name_size, stm->name, stm->val_id_size, stm->val_id);
        break;
    case KND_ATTR_UREAL:
        //err = decode_ureal(stm, task);
        //KND_TASK_ERR("failed to decode {%.*s {val-id %.*s}}",
        //             stm->name_size, stm->name, stm->val_id_size, stm->val_id);
        break;
    case KND_ATTR_STR:
        err = decode_str(stm, task);
        KND_TASK_ERR("failed to decode {%.*s {val-id %.*s}}",
                     stm->name_size, stm->name, stm->val_id_size, stm->val_id);
        break;
    case KND_ATTR_CLASS_REF:
        err = decode_ref_attr_stm(base, stm, task);
        KND_TASK_ERR("failed to decode {rel %.*s {id %.*s}}",
                     stm->name_size, stm->name, stm->id_size, stm->id);
        break;
    default:
        break;
    }
    return knd_OK;
}

static int decode_attr_stm_list(struct kndClass *base,
                                struct kndAttrStm *parent, struct kndTask *task)
{
    struct kndAttr *attr = parent->attr;
    struct kndAttrStm *stm;
    int err;

    assert(parent->list != NULL);

    if (DEBUG_ATTR_STM_DECODE_LEVEL_2) {
            const char *attr_type_name = knd_attr_names[attr->type];
            size_t attr_type_name_size = strlen(attr_type_name);
            knd_log(".. decoding a list of {attr %.*s {type %.*s {set}} {cls %.*s}}",
                    parent->name_size, parent->name, attr_type_name_size, attr_type_name,
                    attr->classname_size, attr->classname);
    }

    FOREACH (stm, parent->list) {
        stm->attr = attr;
        memcpy(stm->val_id, stm->id, stm->id_size);
        stm->val_id_size = stm->id_size;

        err = decode_attr_stm(base, stm, task);
        KND_TASK_ERR("failed to decode {attr-stm %.*s}", stm->id_size, stm->id);
    }
    return knd_OK;
}

int knd_decode_attr_stms(struct kndClass *base, struct kndAttrStm *attr_stms, struct kndTask *task)
{
    struct kndAttrStm *stm;
    struct kndAttrRef *ref;
    int err;

    if (DEBUG_ATTR_STM_DECODE_LEVEL_2) {
        knd_log(".. decoding attr stms of {base %.*s}", base->name_size, base->name);
    }

    FOREACH (stm, attr_stms) {
        err = knd_shared_set_get(task->idxs->attr_idx, stm->id, stm->id_size, (void**)&ref);
        KND_TASK_ERR("failed to get attr ref {attr %.*s}", stm->id_size, stm->id);
        if (!ref->attr) {
            err = knd_CONFLICT;
            KND_TASK_ERR("failed to decode {ref %.*s} {base %.*s {attr-stm %.*s {id %.*s}}}",
                    ref->name_size, ref->name,
                    base->name_size, base->name,
                    stm->name_size, stm->name, stm->id_size, stm->id);
        }
        stm->name = ref->name;
        stm->name_size = ref->name_size;
        stm->attr = ref->attr;

        if (stm->attr->is_a_set) {
            err = decode_attr_stm_list(base, stm, task);
            KND_TASK_ERR("failed to decode a list of {attr-stm %.*s}", stm->id_size, stm->id);
            continue;
        }

        err = decode_attr_stm(base, stm, task);
        KND_TASK_ERR("failed to decode a single {attr-stm %.*s}", stm->id_size, stm->id);
    }
    return knd_OK;
}
