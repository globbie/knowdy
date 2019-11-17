#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_text.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_shard.h"
#include "knd_user.h"
#include "knd_utils.h"
#include "knd_mempool.h"
#include "knd_output.h"

#define DEBUG_TEXT_SELECT_LEVEL_0 0
#define DEBUG_TEXT_SELECT_LEVEL_1 0
#define DEBUG_TEXT_SELECT_LEVEL_2 0
#define DEBUG_TEXT_SELECT_LEVEL_3 0
#define DEBUG_TEXT_SELECT_LEVEL_TMP 1

struct LocalContext {
    struct kndTask      *task;
    struct kndText      *text;
    struct kndPar       *par;
    struct kndSentence  *sent;
    struct kndSyNode    *syn;
    struct kndStatement *stm;
};

gsl_err_t knd_text_select(struct kndText *self,
                          const char *rec, size_t *total_size,
                          struct kndTask *task)
{
    gsl_err_t parser_err;
    int err;

    if (DEBUG_TEXT_SELECT_LEVEL_TMP) {
        knd_log("\n.. parsing text select rec: \"%.*s\"",
                32, rec);
    }

    struct LocalContext ctx = {
        .task = task,
        .repo = repo
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_class,
          .obj = &ctx
        },
        { .is_selector = true,
          .name = "_id",
          .name_size = strlen("_id"),
          .parse = parse_get_class_by_numid,
          .obj = &ctx
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}
