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

#define DEBUG_TEXT_LEVEL_0 0
#define DEBUG_TEXT_LEVEL_1 0
#define DEBUG_TEXT_LEVEL_2 0
#define DEBUG_TEXT_LEVEL_3 0
#define DEBUG_TEXT_LEVEL_TMP 1

struct LocalContext {
    struct kndTask      *task;
    struct kndText      *text;
    struct kndPar       *par;
    struct kndSentence  *sent;
    struct kndSyNode    *syn;
    struct kndStatement *stm;
};

static int knd_sent_new(struct kndMemPool *mempool,
                        struct kndSentence **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                                sizeof(struct kndSentence), &page);
        if (err) return err;
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_SMALL,
                                     sizeof(struct kndSentence), &page);
        if (err) return err;
    }
    *result = page;
    return knd_OK;
}

static int knd_par_new(struct kndMemPool *mempool,
                       struct kndPar **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                                sizeof(struct kndPar), &page);
        if (err) return err;
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_TINY,
                                     sizeof(struct kndPar), &page);
        if (err) return err;
    }
    *result = page;
    return knd_OK;
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


void knd_text_str(struct kndText *self, size_t depth)
{
    struct kndState *state;
    struct kndStateVal *val;
    struct kndPar *par;
    struct kndSentence *sent;

    state = atomic_load_explicit(&self->states,
                                 memory_order_relaxed);
    if (!state) {
        if (self->seq_size) {
            knd_log("%*stext: \"%.*s\" (lang:%.*s)",
                    depth * KND_OFFSET_SIZE, "",
                    self->seq_size, self->seq, self->locale_size, self->locale);
            return;
        }
        if (self->num_pars) {
            knd_log("%*stext (lang:%.*s) [par",
                    depth * KND_OFFSET_SIZE, "",
                    self->locale_size, self->locale);
            for (par = self->pars; par; par = par->next) {
                knd_log("%*s#%zu:", (depth + 1) * KND_OFFSET_SIZE, "", par->numid);

                for (sent = par->sents; sent; sent = sent->next) {
                    knd_log("%*s#%zu: \"%.*s\"",
                            (depth + 2) * KND_OFFSET_SIZE, "",
                            sent->numid, sent->seq_size, sent->seq);
                }
            }
            knd_log("%*s]", depth * KND_OFFSET_SIZE, "");
        }
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
    struct kndPar *par;
    struct kndSentence *sent;
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

    if (seq_size) {
        err = out->write(out, seq, seq_size);                   RET_ERR();
    }
    if (self->locale_size) {
        err = out->writec(out, '{');                            RET_ERR();
        err = out->write(out, "_lang ", strlen("_lang "));      RET_ERR();
        err = out->write(out, self->locale, self->locale_size); RET_ERR();
        err = out->writec(out, '}');                            RET_ERR();
    }

    if (self->num_pars) {
        err = out->write(out, "[p", strlen("[p"));                  RET_ERR();
        for (par = self->pars; par; par = par->next) {
            err = out->writec(out, '{');                            RET_ERR();
            err = out->write(out, "[s", strlen("[s"));              RET_ERR();
            for (sent = par->sents; sent; sent = sent->next) {
                err = out->writec(out, '{');                        RET_ERR();
                err = out->write(out, sent->seq, sent->seq_size);   RET_ERR();
                err = out->writec(out, '}');                        RET_ERR();
            }
            err = out->writec(out, ']');                            RET_ERR();
            err = out->writec(out, '}');                            RET_ERR();
        }
        err = out->writec(out, ']');                            RET_ERR();
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

static gsl_err_t set_sent_seq(void *obj, const char *val, size_t val_size)    
{
    struct LocalContext *ctx = obj;
    struct kndSentence *self = ctx->sent;
    self->seq_size = val_size;
    self->seq = val;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_sent_item(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndPar *par = ctx->par;
    struct kndSentence *sent;
    struct kndMemPool *mempool = task->mempool;
    if (task->user_ctx)
        mempool = task->shard->user->mempool;
    gsl_err_t parser_err;
    int err;
    
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_sent_seq,
          .obj = obj
        }/*,
        { .type = GSL_GET_ARRAY_STATE,
          .name = "syn",
          .name_size = strlen("syn"),
          .parse = parse_synode_array,
          .obj = obj
          }*/
    };

    err = knd_sent_new(mempool, &sent);
    if (err) return make_gsl_err_external(err);
    ctx->sent = sent;
    
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (par->last_sent)
        par->last_sent->next = sent;
    else
        par->sents = sent;

    par->last_sent = sent;
    par->num_sents++;
    sent->numid = par->num_sents;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_sent_array(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .parse = parse_sent_item,
        .obj = obj
    };
    return gsl_parse_array(&item_spec, rec, total_size);
}

static gsl_err_t set_par_numid(void *obj, const char *val, size_t val_size)    
{
    struct LocalContext *ctx = obj;
    struct kndPar *self = ctx->par;
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
    self->numid = (size_t)numval;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_par_item(void *obj,
                                const char *rec,
                                size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndText *text = ctx->text;
    struct kndPar *par;
    struct kndMemPool *mempool = task->mempool;
    if (task->user_ctx)
        mempool = task->shard->user->mempool;
    gsl_err_t parser_err;
    int err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_par_numid,
          .obj = obj
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "s",
          .name_size = strlen("s"),
          .parse = parse_sent_array,
          .obj = obj
        }
    };

    err = knd_par_new(mempool, &par);
    if (err) return make_gsl_err_external(err);
    ctx->par = par;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (text->last_par)
        text->last_par->next = par;
    else
        text->pars = par;

    text->last_par = par;
    text->num_pars++;
    par->numid = text->num_pars;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_par_array(void *obj,
                                 const char *rec,
                                 size_t *total_size)
{
    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .parse = parse_par_item,
        .obj = obj
    };
    return gsl_parse_array(&item_spec, rec, total_size);
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
        { .name = "_lang",
          .name_size = strlen("_lang"),
          .run = set_text_lang,
          .obj = &ctx
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "p",
          .name_size = strlen("p"),
          .parse = parse_par_array,
          .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    return make_gsl_err(gsl_OK);
}

