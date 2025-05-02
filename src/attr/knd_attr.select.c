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
    struct kndClass   *cls;
    struct kndTask    *task;
    struct kndRepo    *repo;
    struct kndAttr    *attr;
};

int knd_cls_attrs_select(struct kndQuery *query,
                         const char *rec, size_t *total_size, struct kndTask *task)
{
    gsl_err_t parser_err;

    struct LocalContext ctx = {
        .task = task,
        .query = query
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = NULL, //,
          .obj = &ctx
        }/*,
        { .validate = select_cls_attr,
          .obj = &ctx
          }*/
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err.code;
    
    return knd_OK;
}
