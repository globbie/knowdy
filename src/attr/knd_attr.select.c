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
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_quant.h"
#include "knd_query.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_logic.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>

#define DEBUG_ATTR_SELECT_LEVEL_1 0
#define DEBUG_ATTR_SELECT_LEVEL_2 0
#define DEBUG_ATTR_SELECT_LEVEL_3 0
#define DEBUG_ATTR_SELECT_LEVEL_4 0
#define DEBUG_ATTR_SELECT_LEVEL_5 0
#define DEBUG_ATTR_SELECT_LEVEL_TMP 1

struct LocalContext {
    struct kndQuery   *query;
    struct kndClass   *class;
    struct kndTask    *task;
    struct kndRepo    *repo;

    struct kndAttrStm *clauses;
    struct kndAttrStm *attr_stm;
    struct kndAttr    *attr;
    knd_logic_t logic;
};

int knd_attr_parse_query_stm(struct kndAttrStm *stm,
                             const char *rec, size_t *total_size, struct kndTask *task)
{
    struct kndAttr *attr = stm->attr;
    struct kndQuantAttrStm *quant_attr_stm;
    //gsl_err_t parser_err;
    int err;

    if (DEBUG_ATTR_SELECT_LEVEL_TMP) {
        knd_log(".. query by {attr %.*s}", attr->name_size, attr->name);
    }

    switch (attr->type) {
        /*case KND_ATTR_INNER:
        parser_err = parse_inner_class_clause(attr, &ctx, rec, total_size);
        if (parser_err.code) return parser_err.code;
        break;
    case KND_ATTR_CLASS_REF:
        parser_err = parse_classref_clause(attr, &ctx, rec, total_size);
        if (parser_err.code) return parser_err.code;
        break;
        */
    case KND_ATTR_UINT:
        err = knd_quant_attr_stm_new(&quant_attr_stm, task->mempool);
        KND_TASK_ERR("failed to alloc a quant attr stm");
        stm->subtype = quant_attr_stm;

        err = knd_quant_uint_parse_stm(quant_attr_stm, rec, total_size, task);
        KND_TASK_ERR("failed to parse uint stm");
        break;
    default:
        knd_log("-- no clause filtering in attr %.*s",
                attr->name_size, attr->name);
        return knd_FAIL;
    }

    return knd_OK;
}
