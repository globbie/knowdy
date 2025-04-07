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

static int decode_glosses(struct kndText *trs, struct kndTask *task)
{
    struct kndText *t;
    int err;

    FOREACH (t, trs) {
        err = knd_charseq_decode(t->id, t->id_size, &t->seq, task);
        KND_TASK_ERR("failed to decode a charseq");
    }
    return knd_OK;
}

int knd_attr_decode(struct kndAttr *attr, struct kndTask *task)
{
    int err;

    if (DEBUG_ATTR_DECODE_LEVEL_2) {
        knd_log("decoding {cls %.*s {attr %.*s {id %.*s}}}",
                attr->owner->name_size, attr->owner->name,
                attr->name_size, attr->name, attr->id_size, attr->id);
    }

    if (attr->tr) {
        err = decode_glosses(attr->tr, task);
        KND_TASK_ERR("failed to decode glosses of {attr %.*s}", attr->name_size, attr->name);
    }
    
    return knd_OK;
}
