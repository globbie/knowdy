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
#include "knd_mempool.h"
#include "knd_output.h"

#define DEBUG_TEXT_IMPORT_LEVEL_0 0
#define DEBUG_TEXT_IMPORT_LEVEL_1 0
#define DEBUG_TEXT_IMPORT_LEVEL_2 0
#define DEBUG_TEXT_IMPORT_LEVEL_3 0
#define DEBUG_TEXT_IMPORT_LEVEL_TMP 1

struct LocalContext {
    struct kndTask       *task;
    struct kndRepo       *repo;
    struct kndText       *text;
    struct kndPar        *par;
    struct kndSentence   *sent;
    struct kndClause     *clause;
    struct kndSyNode     *synode;
    struct kndSyNodeSpec *synode_spec;
    struct kndStatement  *stm;
};

static gsl_err_t parse_synode(void *obj, const char *rec, size_t *total_size);

static gsl_err_t set_gloss_locale(void *obj, const char *name, size_t name_size)
{
    struct kndText *self = obj;
    if (name_size >= KND_SHORT_NAME_SIZE) return make_gsl_err(gsl_LIMIT);
    self->locale = name;
    self->locale_size = name_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_gloss_value(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = ctx->repo;
    struct kndText *text = ctx->text;
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    struct kndCharSeq *seq;
    int err;
    assert(val_size != 0);

    switch (task->type) {
    case KND_UNFREEZE_STATE:
        err = knd_shared_set_get(repo->str_idx, val, val_size, (void**)&seq);
        if (err) {
            KND_TASK_LOG("failed to decode a gloss");
            return make_gsl_err_external(err);
        }
        text->seq = seq;

        if (DEBUG_TEXT_IMPORT_LEVEL_2)
            knd_log(">> gloss decoded \"%.*s\" => \"%.*s\"", val_size, val, text->seq->val_size, text->seq->val);
        return make_gsl_err(gsl_OK);
    default:
        break;
    }
    seq = knd_shared_dict_get(repo->str_dict, val, val_size);
    if (!seq) {
        err = knd_charseq_new(mempool, &seq);
        if (err) {
            KND_TASK_LOG("failed to alloc a charseq");
            return make_gsl_err_external(err);
        }
        seq->val = val;
        seq->val_size = val_size;
        seq->numid = atomic_fetch_add_explicit(&repo->num_strs, 1, memory_order_relaxed);

        err = knd_shared_dict_set(repo->str_dict, val, val_size, (void*)seq, mempool, NULL, &seq->item, false);
        if (err) {
            KND_TASK_LOG("failed to register a gloss by name");
            return make_gsl_err_external(err);
        }
        knd_uid_create(seq->numid, idbuf, &idbuf_size);
        err = knd_shared_set_add(repo->str_idx, idbuf, idbuf_size, (void*)seq);
        if (err) {
            KND_TASK_LOG("failed to register a gloss by numid");
            return make_gsl_err_external(err);
        }
        // knd_log(">> new str %.*s", val_size, val);
    }
    text->seq = seq;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_gloss_abbr(void *obj, const char *val, size_t val_size)
{
    struct LocalContext *ctx = obj;
    char idbuf[KND_ID_SIZE];
    size_t idbuf_size;
    struct kndTask *task = ctx->task;
    struct kndRepo *repo = ctx->repo;
    struct kndText *text = ctx->text;
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    struct kndCharSeq *seq;
    int err;
    assert(val_size != 0);

    switch (task->type) {
    case KND_UNFREEZE_STATE:
        err = knd_shared_set_get(repo->str_idx, val, val_size, (void**)&seq);
        if (err) {
            KND_TASK_LOG("failed to decode a gloss abbr");
            return make_gsl_err_external(err);
        }
        text->seq = seq;

        if (DEBUG_TEXT_IMPORT_LEVEL_2)
            knd_log(">> gloss abbr decoded \"%.*s\" => \"%.*s\"", val_size, val, text->seq->val_size, text->seq->val);
        return make_gsl_err(gsl_OK);
    default:
        break;
    }
    seq = knd_shared_dict_get(repo->str_dict, val, val_size);
    if (!seq) {
        err = knd_charseq_new(mempool, &seq);
        if (err) {
            KND_TASK_LOG("failed to alloc a gloss abbr charseq");
            return make_gsl_err_external(err);
        }
        seq->val = val;
        seq->val_size = val_size;
        seq->numid = atomic_fetch_add_explicit(&repo->num_strs, 1, memory_order_relaxed);

        err = knd_shared_dict_set(repo->str_dict, val, val_size, (void*)seq, mempool, NULL, &seq->item, false);
        if (err) {
            KND_TASK_LOG("failed to register a gloss by name");
            return make_gsl_err_external(err);
        }
        knd_uid_create(seq->numid, idbuf, &idbuf_size);
        err = knd_shared_set_add(repo->str_idx, idbuf, idbuf_size, (void*)seq);
        if (err) {
            KND_TASK_LOG("failed to register a gloss abbr by numid");
            return make_gsl_err_external(err);
        }
    }
    text->abbr = seq;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_gloss_item(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;
    struct kndText *t;
    int err;
    err = knd_text_new(task->mempool, &t);
    if (err) {
        KND_TASK_LOG("failed to alloc a text");
        return *total_size = 0, make_gsl_err_external(err);
    }
    struct LocalContext ctx = {
        .task = task,
        .repo = task->repo,
        .text = t
    };

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_gloss_locale,
          .obj = t
        },
        { .name = "t",
          .name_size = strlen("t"),
          .run = set_gloss_value,
          .obj = &ctx
        },
        { .name = "abbr",
          .name_size = strlen("abbr"),
          .run = set_gloss_abbr,
          .obj = &ctx
        }
    };
    gsl_err_t parser_err;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (t->locale_size == 0 || t->seq == NULL)
        return make_gsl_err(gsl_FORMAT);  // error: both attrs required

    if (DEBUG_TEXT_IMPORT_LEVEL_2)
        knd_log(".. read gloss translation: \"%.*s\",  text: \"%.*s\"",
                t->locale_size, t->locale, t->seq->val_size, t->seq->val);

    // append
    t->next = task->ctx->tr;
    task->ctx->tr = t;
    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_parse_gloss_array(void *obj, const char *rec, size_t *total_size)
{
    struct kndTask *task = obj;

    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .parse = parse_gloss_item,
        .obj = task
    };
    return gsl_parse_array(&item_spec, rec, total_size);
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
    // struct kndMemPool *mempool = task->user_ctx->mempool;
    //struct kndState *state;
    //struct kndStateVal *state_val;
    //struct kndStateRef *state_ref;
    // int err;

    if (DEBUG_TEXT_IMPORT_LEVEL_2)
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

    self->seq->val_size = val_size;
    self->seq->val = val;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_synode_spec_class(void *obj, const char *val, size_t val_size)    
{
    struct LocalContext *ctx = obj;
    struct kndSyNodeSpec *self = ctx->synode_spec;
    self->name = val;
    self->name_size = val_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_synode_class(void *obj, const char *name, size_t name_size)    
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndSyNode *synode = ctx->synode;
    int err;

    synode->name = name;
    synode->name_size = name_size;

    err = knd_get_class(ctx->task->repo, name, name_size, &synode->class, ctx->task);
    if (err) {
        KND_TASK_LOG("no such class: %.*s", name_size, name);
        return make_gsl_err(gsl_NO_MATCH);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_clause_class(void *obj, const char *val, size_t val_size)    
{
    struct LocalContext *ctx = obj;
    struct kndClause *self = ctx->clause;
    self->name = val;
    self->name_size = val_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_sent_seq(void *obj, const char *val, size_t val_size)    
{
    struct kndSentence *self = obj;

    if (!val_size) return make_gsl_err(gsl_FORMAT);
    self->seq_size = val_size;
    self->seq = val;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_statement_schema(void *obj, const char *val, size_t val_size)    
{
    struct LocalContext *ctx = obj;
    struct kndStatement *stm = ctx->stm;
    stm->schema_name = val;
    stm->schema_name_size = val_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_synode_spec(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    struct kndSyNode *base_synode = ctx->synode;
    struct kndSyNodeSpec *spec;
    gsl_err_t parser_err;
    int err;

    err = knd_synode_spec_new(mempool, &spec);
    if (err) return make_gsl_err_external(err);
    ctx->synode_spec = spec;
    ctx->synode = NULL;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_synode_spec_class,
          .obj = obj
        },
        { .name = "syn",
          .name_size = strlen("syn"),
          .parse = parse_synode,
          .obj = obj
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    base_synode->spec = spec;
    ctx->synode = base_synode;
    ctx->synode_spec = NULL;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_term_synode(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    struct kndSyNode *base_synode = ctx->synode;
    struct kndSyNode *synode;
    gsl_err_t parser_err;
    int err;

    err = knd_synode_new(mempool, &synode);
    if (err) return make_gsl_err_external(err);
    synode->is_terminal = true;
    base_synode->topic = synode;

    ctx->synode = synode;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_synode_class,
          .obj = ctx
        },
        { .name = "pos",
          .name_size = strlen("pos"),
          .parse = gsl_parse_size_t,
          .obj = &synode->linear_pos
        },
        { .name = "len",
          .name_size = strlen("len"),
          .parse = gsl_parse_size_t,
          .obj = &synode->linear_len
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        return parser_err;
    }

    ctx->synode = base_synode;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_synode(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    struct kndSyNode *base_synode = ctx->synode;
    struct kndSyNodeSpec *spec = ctx->synode_spec;
    struct kndSyNode *synode;
    gsl_err_t parser_err;
    int err;

    err = knd_synode_new(mempool, &synode);
    if (err) return make_gsl_err_external(err);
    ctx->synode = synode;
    ctx->synode_spec = NULL;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_synode_class,
          .obj = obj
        },
        { .name = "syn",
          .name_size = strlen("syn"),
          .parse = parse_synode,
          .obj = obj
        },
        { .name = "pos",
          .name_size = strlen("pos"),
          .parse = gsl_parse_size_t,
          .obj = &synode->linear_pos
        },
        { .name = "len",
          .name_size = strlen("len"),
          .parse = gsl_parse_size_t,
          .obj = &synode->linear_len
        },
        { .name = "term",
          .name_size = strlen("term"),
          .parse = parse_term_synode,
          .obj = obj
        },
        { .name = "spec",
          .name_size = strlen("spec"),
          .parse = parse_synode_spec,
          .obj = obj
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (base_synode)
        base_synode->topic = synode;
    if (spec)
        spec->synode = synode;
    
    ctx->synode = base_synode;
    ctx->synode_spec = spec;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_subj(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClause *clause = ctx->clause;
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    struct kndSyNode *synode;
    gsl_err_t parser_err;
    int err;

    err = knd_synode_new(mempool, &synode);
    if (err) return make_gsl_err_external(err);
    ctx->synode = synode;
    ctx->synode_spec = NULL;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_synode_class,
          .obj = obj
        },
        { .name = "syn",
          .name_size = strlen("syn"),
          .parse = parse_synode,
          .obj = obj
        },
        { .name = "term",
          .name_size = strlen("term"),
          .parse = parse_term_synode,
          .obj = obj
        },
        { .name = "spec",
          .name_size = strlen("spec"),
          .parse = parse_synode_spec,
          .obj = obj
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        return parser_err;
    }
    clause->subj = synode;
    ctx->clause = clause;
    ctx->synode = NULL;
    ctx->synode_spec = NULL;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_pred(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClause *clause = ctx->clause;
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    struct kndSyNode *synode;
    gsl_err_t parser_err;
    int err;

    err = knd_synode_new(mempool, &synode);
    if (err) return make_gsl_err_external(err);
    ctx->synode = synode;
    ctx->synode_spec = NULL;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_synode_class,
          .obj = obj
        },
        { .name = "syn",
          .name_size = strlen("syn"),
          .parse = parse_synode,
          .obj = obj
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    clause->pred = synode;
    ctx->clause = clause;
    ctx->synode = NULL;
    ctx->synode_spec = NULL;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_clause(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndSentence *sent = ctx->sent;
    // struct kndClause *parent_clause = ctx->clause;
    struct kndClause *clause;
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    gsl_err_t parser_err;
    int err;

    err = knd_clause_new(mempool, &clause);
    if (err) return make_gsl_err_external(err);
    if (!sent->clause)
        sent->clause = clause;
    ctx->clause = clause;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_clause_class,
          .obj = obj
        }, /* subclauses */
        { .name = "clause",
          .name_size = strlen("clause"),
          .parse = parse_clause,
          .obj = obj
        },
        { .name = "syn",
          .name_size = strlen("syn"),
          .parse = parse_subj,
          .obj = obj
        },
        { .name = "pred",
          .name_size = strlen("pred"),
          .parse = parse_pred,
          .obj = obj
        }, /* exclamations etc */
        /* { .name = "phono",
          .name_size = strlen("phono"),
          .parse = parse_phone,
          .obj = obj
          }*/
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    /*if (par->last_sent)
        par->last_sent->next = sent;
    else
        par->sents = sent;

    par->last_sent = sent;
    par->num_sents++;
    sent->numid = par->num_sents;
    */
    ctx->clause = NULL;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_class_select(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    knd_task_spec_type orig_task_type = task->type;
    gsl_err_t parser_err;

    /* switch to statement's local scope */
    task->type = KND_INNER_STATE;
    parser_err = knd_class_select(task->repo, rec, total_size, task);
    task->type = orig_task_type;

    return parser_err;
}

static gsl_err_t parse_proc_select(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    knd_task_spec_type orig_task_type = task->type;
    gsl_err_t parser_err;

    knd_log("proc inner state  repo:%.*s", task->repo->name_size, task->repo->name);
    /* switch to statement's local scope */
    task->type = KND_INNER_STATE;
    parser_err = knd_proc_select(task->repo, rec, total_size, task);
    task->type = orig_task_type;
    return parser_err;
}

static gsl_err_t parse_statement(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndSentence *sent = ctx->sent;
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    struct kndStatement *stm;
    gsl_err_t parser_err;
    int err;

    err = knd_statement_new(mempool, &stm);
    if (err) return make_gsl_err_external(err);
    sent->stm = stm;

    parser_err = knd_statement_import(stm, rec, total_size, task);
    if (parser_err.code) {
        KND_TASK_LOG("text stm import failed");
        return *total_size = 0, parser_err;
    }

    err = knd_statement_resolve(stm, task);
    if (err) return make_gsl_err_external(err);
    
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_sentence(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndPar *par = ctx->par;
    struct kndSentence *sent;
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
    gsl_err_t parser_err;
    int err;

    err = knd_sentence_new(mempool, &sent);
    if (err) return make_gsl_err_external(err);
    ctx->sent = sent;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_sent_seq,
          .obj = sent
        },
        { .name = "clause",
          .name_size = strlen("clause"),
          .parse = parse_clause,
          .obj = obj
        },
        { .name = "stm",
          .name_size = strlen("stm"),
          .parse = parse_statement,
          .obj = obj
        }
    };
    
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    if (par->last_sent)
        par->last_sent->next = sent;
    else
        par->sents = sent;

    par->last_sent = sent;
    par->num_sents++;
    sent->numid = par->num_sents;
    ctx->sent = sent;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_sent_array(void *obj, const char *rec, size_t *total_size)
{
    if (DEBUG_TEXT_IMPORT_LEVEL_2)
        knd_log(".. parse sentence array");

    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .parse = parse_sentence,
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

    if (val_size >= KND_NAME_SIZE) return make_gsl_err(gsl_FAIL);
    memcpy(buf, val, val_size);
    buf[val_size] = '\0';
            
    err = knd_parse_num(buf, &numval);
    if (err) return make_gsl_err_external(err);
    self->numid = (size_t)numval;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_valid_par(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct LocalContext *ctx = obj;
    struct kndPar *par = ctx->par;
    if (!par->num_sents) {
        knd_log("NB: empty text par #zu", par->numid);
        return make_gsl_err(gsl_FORMAT);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t confirm_statement(void *obj, const char *unused_var(name), size_t unused_var(name_size))
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;

    if (!task->ctx->class_declars && !task->ctx->proc_declars) {
        knd_log("-- empty stm");
        KND_TASK_LOG("empty statement");
        return make_gsl_err(gsl_FORMAT);
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_par(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndText *text = ctx->text;
    struct kndPar *par;
    struct kndMemPool *mempool = task->user_ctx ? task->user_ctx->mempool : task->mempool;
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
        },
        { .is_default = true,
          .run = confirm_valid_par,
          .obj = obj
        }
    };

    err = knd_par_new(mempool, &par);
    if (err) return make_gsl_err_external(err);
    ctx->par = par;
    ctx->sent = NULL;

    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        switch (parser_err.code) {
        case gsl_NO_MATCH:
            KND_TASK_LOG("text par got an unrecognized tag \"%.*s\"", parser_err.val_size, parser_err.val);
            break;
        default:
            break;
        }
        return parser_err;
    }
    if (!par->num_sents) {
        KND_TASK_LOG("empty paragraphs not accepted");
        return make_gsl_err_external(err);
    }
    
    if (text->last_par)
        text->last_par->next = par;
    else
        text->pars = par;

    text->last_par = par;
    text->num_pars++;
    par->numid = text->num_pars;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_par_array(void *obj, const char *rec, size_t *total_size)
{
    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .parse = parse_par,
        .obj = obj
    };
    return gsl_parse_array(&item_spec, rec, total_size);
}

gsl_err_t knd_statement_import(struct kndStatement *stm, const char *rec, size_t *total_size, struct kndTask *task)
{
    if (DEBUG_TEXT_IMPORT_LEVEL_2)
        knd_log(".. import statement: \"%.*s\"", 64, rec);

    struct LocalContext ctx = {
        .task = task,
        .stm = stm
    };
    gsl_err_t parser_err;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = set_statement_schema,
          .obj = &ctx
        },
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_select,
          .obj = &ctx
        },
        { .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_select,
          .obj = &ctx
        },
        { .is_default = true,
          .run = confirm_statement,
          .obj = &ctx
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        return parser_err;
    }
    stm->class_declars = task->ctx->class_declars;
    stm->proc_declars = task->ctx->proc_declars;
    return make_gsl_err(gsl_OK);
}

gsl_err_t knd_text_import(struct kndText *self, const char *rec, size_t *total_size, struct kndTask *task)
{
    if (DEBUG_TEXT_IMPORT_LEVEL_2)
        knd_log(".. import text: \"%.*s\"", 64, rec);
   
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
