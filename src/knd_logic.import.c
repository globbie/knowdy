#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_text.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_class.h"
#include "knd_proc.h"
#include "knd_shard.h"
#include "knd_shared_set.h"
#include "knd_user.h"
#include "knd_utils.h"
#include "knd_ignore.h"
#include "knd_mempool.h"
#include "knd_output.h"

#define DEBUG_LOGIC_IMPORT_LEVEL_0 0
#define DEBUG_LOGIC_IMPORT_LEVEL_1 0
#define DEBUG_LOGIC_IMPORT_LEVEL_2 0
#define DEBUG_LOGIC_IMPORT_LEVEL_3 0
#define DEBUG_LOGIC_IMPORT_LEVEL_TMP 1

struct LocalContext {
    struct kndTask       *task;
    struct kndRepo       *repo;
    struct kndSituation  *sit;
    struct kndLogicClause  *clause;
};

int knd_logic_clause_parse(struct kndLogicClause *self, const char *rec,
                           size_t *total_size, struct kndTask *task)
{
    if (DEBUG_LOGIC_IMPORT_LEVEL_2)
        knd_log(".. import logic clause: \"%.*s\"", 128, rec);

    struct LocalContext ctx = {
        .task = task,
        .clause = self
    };
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = knd_ignore_value,
          .obj = &ctx
        },
        { .type = GSL_SET_STATE,
          .validate = knd_ignore_named_area,
          .obj = &ctx
        },
        { .validate = knd_ignore_named_area,
          .obj = &ctx
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code != gsl_OK) return gsl_err_to_knd_err_codes(parser_err);
    return knd_OK;
}
