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

#define DEBUG_ATTR_STM_SELECT_LEVEL_1 0
#define DEBUG_ATTR_STM_SELECT_LEVEL_2 0
#define DEBUG_ATTR_STM_SELECT_LEVEL_3 0
#define DEBUG_ATTR_STM_SELECT_LEVEL_4 0
#define DEBUG_ATTR_STM_SELECT_LEVEL_5 0
#define DEBUG_ATTR_STM_SELECT_LEVEL_TMP 1

int knd_attr_stm_plan(struct kndAttrStm *stm, struct kndTask *task)
{
    struct kndAttr *attr = stm->attr;
    struct kndAttrFacet *facet = attr->facets;
    struct kndQuantAttrStm *quant_attr_stm;
    struct kndClassRefAttrStm *cref;
    int err;

    if (DEBUG_ATTR_STM_SELECT_LEVEL_TMP) {
        knd_log(".. query planning of {attr %.*s} {attr-type %s}",
                attr->name_size, attr->name, knd_attr_names[attr->type]);
    }

    if (!facet) {
        knd_log("no index facets exist for attr %.*s", attr->name_size, attr->name);
        return knd_OK;
    }

    switch (attr->type) {
    case KND_ATTR_UINT:
        quant_attr_stm = stm->subtype;

        err = knd_quant_uint_query_plan(quant_attr_stm, facet, task);
        KND_TASK_ERR("failed to plan a quant uint query");

        if (quant_attr_stm->match) {
            stm->match = quant_attr_stm->match;
        }
        break;
    case KND_ATTR_CLASS_REF:
        cref = stm->subtype;

        if (DEBUG_ATTR_STM_SELECT_LEVEL_TMP) {
            knd_log(".. query {cls-ref %.s}", cref->cls_entry->name_size, cref->cls_entry->name);
        }
   
        break;
    default:
        break;
    }
    return knd_OK;
}
