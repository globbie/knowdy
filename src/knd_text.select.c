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

static gsl_err_t present_pars(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct LocalContext *ctx = obj;
    //struct kndTask *task = ctx->task;
    //int err;
    knd_log("ctx:%p", ctx);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t get_par_by_numid(void *obj, const char *val, size_t val_size)    
{
    struct LocalContext *ctx = obj;
    struct kndPar *par = NULL;
    char buf[KND_NAME_SIZE];
    long numval;
    int err;

    if (val_size >= KND_NAME_SIZE)
        return make_gsl_err(gsl_FAIL);

    memcpy(buf, val, val_size);
    buf[val_size] = '\0';
            
    err = knd_parse_num(buf, &numval);
    if (err) {
        return make_gsl_err_external(err);
    }

    ctx->par = par;
    knd_log("%p par:%zu", par, (size_t)numval);
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_text_par(void *obj,
                                const char *rec,
                                size_t *total_size)
{
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = get_par_by_numid,
          .obj = obj
        },
        { .is_default = true,
          .run = present_pars,
          .obj = obj
        }
    };

    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}

gsl_err_t knd_text_select(struct kndText *self,
                          const char *rec, size_t *total_size,
                          struct kndTask *task)
{
    if (DEBUG_TEXT_SELECT_LEVEL_TMP) {
        knd_log("\n.. parsing text select rec: \"%.*s\"",
                32, rec);
    }

    struct LocalContext ctx = {
        .task = task,
        .text = self
    };

    struct gslTaskSpec specs[] = {
        { .name = "p",
          .name_size = strlen("p"),
          .parse = parse_text_par,
          .obj = &ctx
        }
    };
    return gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
}
