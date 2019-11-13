#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gsl-parser.h>

#include "knd_text.h"
#include "knd_task.h"
#include "knd_repo.h"
#include "knd_utils.h"
#include "knd_mempool.h"
#include "knd_output.h"

#define DEBUG_TEXT_LEVEL_0 0
#define DEBUG_TEXT_LEVEL_1 0
#define DEBUG_TEXT_LEVEL_2 0
#define DEBUG_TEXT_LEVEL_3 0
#define DEBUG_TEXT_LEVEL_TMP 1

struct LocalContext {
    struct kndTask *task;
    struct kndText *text;
};

void knd_text_str(struct kndText *self, size_t depth)
{
    struct kndState *state;
    struct kndStateVal *val;

    state = atomic_load_explicit(&self->states,
                                 memory_order_relaxed);
    val = state->val;

    knd_log("\n%*stext: \"%.*s\"",
            depth * KND_OFFSET_SIZE, "",
            val->val_size, val->val);
}

int knd_text_export(struct kndText *self,
                    knd_format format,
                    struct kndTask *task)
{
    int err;

    switch (format) {
        /*case KND_FORMAT_GSL:
        err = export_GSL(self, task);  RET_ERR();
        break;
    case KND_FORMAT_JSON:
        err = export_JSON(self, task);                           RET_ERR();
        break;
    case KND_FORMAT_GSP:
        err = export_GSP(self, task, task->out);  RET_ERR();
        break;*/
    default:
        break;
    }

    return knd_OK;
}

static gsl_err_t run_set_val(void *obj, const char *val, size_t val_size)    
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;

    
    if (DEBUG_TEXT_LEVEL_TMP)
        knd_log("++ text val set: \"%.*s\"",
                val_size, val);

    return make_gsl_err(gsl_OK);
   
}

gsl_err_t knd_text_import(struct kndText *self,
                          const char *rec,
                          size_t *total_size,
                          struct kndTask *task)
{
    struct LocalContext ctx = {
        .task = task,
        .text = self
    };
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_set_val,
          .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

int knd_text_new(struct kndMemPool *mempool,
                 struct kndText **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                                sizeof(struct kndText), &page);
        if (err) return err;
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_SMALL,
                                     sizeof(struct kndText), &page);
        if (err) return err;
    }
    *result = page;
    return knd_OK;
}
