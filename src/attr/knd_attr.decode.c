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
#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_shared_set.h"
#include "knd_utils.h"
#include "knd_output.h"

#define DEBUG_ATTR_DECODE_LEVEL_1 0
#define DEBUG_ATTR_DECODE_LEVEL_2 0
#define DEBUG_ATTR_DECODE_LEVEL_3 0
#define DEBUG_ATTR_DECODE_LEVEL_4 0
#define DEBUG_ATTR_DECODE_LEVEL_5 0
#define DEBUG_ATTR_DECODE_LEVEL_TMP 1

static int decode_glosses(struct kndText *trs, struct kndSet *str_idx, struct kndTask *task)
{
    struct kndText *t;
    int err;

    FOREACH (t, trs) {
        err = knd_charseq_decode(str_idx, t->id, t->id_size, &t->seq, task);
        KND_TASK_ERR("failed to decode a charseq");
    }
    return knd_OK;
}

int knd_attr_decode(struct kndAttr *attr, struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndSet *str_idx = snapshot->cache.str_idx;
    struct kndCharSeq *seq;
    //struct kndClassEntry *owner;
    int err;

    if (DEBUG_ATTR_DECODE_LEVEL_2) {
        knd_log("decoding {attr {id %.*s}}}", attr->id_size, attr->id);
    }

    err = knd_charseq_decode(str_idx, attr->name_id, attr->name_id_size, &seq, task);
    KND_TASK_ERR("failed to decode cls entry name {id %.*s}", attr->name_id_size, attr->name_id);
    attr->name = seq->val;
    attr->name_size = seq->val_size;
    attr->seq = seq;

    // TODO owner

    err = decode_glosses(attr->glosses, str_idx, task);
    KND_TASK_ERR("failed to decode glosses of {attr %.*s}", attr->name_size, attr->name);
    return knd_OK;
}

int knd_attr_ref_decode(struct kndAttrRef *ref, struct kndRepoSnapshot *snapshot, struct kndTask *task)
{
    struct kndAttr *attr;
    int err;

    if (DEBUG_ATTR_DECODE_LEVEL_2) {
        knd_log(".. decoding {attr-ref {id %.*s}}}", ref->id_size, ref->id);
    }

    err = knd_attr_get_by_id(snapshot, ref->id, ref->id_size, &attr, task);
    KND_TASK_ERR("failed to get attr by {id %.*s}", ref->id_size, ref->id);
    ref->attr = attr;

    if (DEBUG_ATTR_DECODE_LEVEL_TMP) {
        const char *attr_type_name = knd_attr_names[attr->type];
        size_t attr_type_name_size = strlen(attr_type_name);
        knd_log("++ decoded {attr %.*s {type %.*s}}}",
                attr->name_size, attr->name,
                attr_type_name_size, attr_type_name);
    }

    return knd_OK;
}
