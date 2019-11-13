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
    if (!state) {
        knd_log("%*stext: \"%.*s\" (lang:%.*s)",
                depth * KND_OFFSET_SIZE, "",
                self->seq_size, self->seq, self->locale_size, self->locale);
        return;
    }
    val = state->val;
    knd_log("%*stext: \"%.*s\" (lang:%.*s)",
            depth * KND_OFFSET_SIZE, "",
            val->val_size, val->val, self->locale_size, self->locale);
}

static int export_GSL(struct kndText *self,
                      struct kndTask *task)
{
    struct kndOutput *out = task->out;
    //const char *locale = task->ctx->locale;
    // size_t locale_size = task->ctx->locale_size;
    struct kndState *state;
    const char *seq = self->seq;
    size_t seq_size = self->seq_size;
    //struct kndStateVal *val;
    // struct kndText *t;
    int err;

    state = atomic_load_explicit(&self->states,
                                 memory_order_relaxed);
    if (state) {
        seq = state->val->val;
        seq_size = state->val->val_size;
        return knd_OK;
    }

    err = out->write(out, seq, seq_size);                       RET_ERR();
    if (self->locale_size) {
        err = out->writec(out, '{');                            RET_ERR();
        err = out->write(out, "_lang ", strlen("_lang "));      RET_ERR();
        err = out->write(out, self->locale, self->locale_size); RET_ERR();
        err = out->writec(out, '}');                            RET_ERR();
    }

    return knd_OK;
}

static int export_JSON(struct kndText *self,
                       struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndState *state;
    int err;

    state = atomic_load_explicit(&self->states,
                                 memory_order_relaxed);
    if (!state) {
        err = out->write_escaped(out, self->seq, self->seq_size);
        if (err) return err;
        if (self->locale_size) {
            err = out->write(out, "\"_lang\":\"", strlen("\"_lang:\""));      RET_ERR();
            err = out->write(out, self->locale, self->locale_size); RET_ERR();
            err = out->writec(out, '"');                            RET_ERR();
        }
        return knd_OK;
    }

    err = out->write_escaped(out, state->val->val, state->val->val_size);
    if (err) return err;
    
    return knd_OK;
}

int knd_text_export(struct kndText *self,
                    knd_format format,
                    struct kndTask *task)
{
    int err;

    switch (format) {
    case KND_FORMAT_GSL:
        err = export_GSL(self, task);                            RET_ERR();
        break;
    case KND_FORMAT_JSON:
        err = export_JSON(self, task);                           RET_ERR();
        break;
        /*case KND_FORMAT_GSP:
        err = export_GSP(self, task, task->out);  RET_ERR();
        break;*/
    default:
        break;
    }

    return knd_OK;
}

static gsl_err_t set_text_lang(void *obj, const char *val, size_t val_size)    
{
    struct LocalContext *ctx = obj;
    struct kndText *self = ctx->text;
    self->locale_size = val_size;
    self->locale = val;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_text_seq(void *obj, const char *val, size_t val_size)    
{
    struct LocalContext *ctx = obj;
    struct kndText *self = ctx->text;

    //struct kndTask *task = ctx->task;
    // struct kndMemPool *mempool = task->mempool;
    //struct kndState *state;
    //struct kndStateVal *state_val;
    //struct kndStateRef *state_ref;
    // int err;

    //if (task->user_ctx) {
    //    mempool = task->shard->user->mempool;
    //}

    if (DEBUG_TEXT_LEVEL_2)
        knd_log("++ text val set: \"%.*s\"",
                val_size, val);

    /*err = knd_state_new(mempool, &state);
    if (err) {
        KND_TASK_LOG("kndState alloc failed");
        return make_gsl_err_external(err);
    }
    err = knd_state_val_new(mempool, &state_val);
    if (err) {
        knd_log("-- state val alloc failed");
        return make_gsl_err_external(err);
    }
    err = knd_state_ref_new(mempool, &state_ref);
    if (err) {
        knd_log("-- state ref alloc failed");
        return make_gsl_err_external(err);
    }
    state_ref->state = state;

    state_val->obj = (void*)ctx->text;
    state_val->val      = val;
    state_val->val_size = val_size;
    state->val          = state_val;

    state->commit = task->ctx->commit;
    */

    self->seq_size = val_size;
    self->seq = val;
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
          .run = set_text_seq,
          .obj = &ctx
        },
        {
          .name = "_lang",
          .name_size = strlen("_lang"),
          .run = set_text_lang,
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
