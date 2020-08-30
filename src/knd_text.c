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
    struct kndTask       *task;
    struct kndText       *text;
    struct kndPar        *par;
    struct kndSentence   *sent;
    struct kndClause     *clause;
    struct kndSyNode     *synode;
    struct kndSyNodeSpec *synode_spec;
    struct kndStatement  *stm;
};

static gsl_err_t parse_synode(void *obj, const char *rec, size_t *total_size);

int knd_text_idx_new(struct kndMemPool *mempool, struct kndTextIdx **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY, sizeof(struct kndTextIdx), &page);
        if (err) return err;
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_TINY, sizeof(struct kndTextIdx), &page);
        if (err) return err;
    }
    memset(page, 0, sizeof(struct kndTextIdx));
    *result = page;
    return knd_OK;
}

int knd_text_search_report_new(struct kndMemPool *mempool, struct kndTextSearchReport **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY, sizeof(struct kndTextSearchReport), &page);
        if (err) return err;
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_TINY, sizeof(struct kndTextSearchReport), &page);
        if (err) return err;
    }
    memset(page, 0, sizeof(struct kndTextSearchReport));
    *result = page;
    return knd_OK;
}

int knd_text_loc_new(struct kndMemPool *mempool, struct kndTextLoc **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY, sizeof(struct kndTextLoc), &page);
        if (err) return err;
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_TINY, sizeof(struct kndTextLoc), &page);
        if (err) return err;
    }
    memset(page, 0, sizeof(struct kndTextLoc));
    *result = page;
    return knd_OK;
}

int knd_class_declar_new(struct kndMemPool *mempool, struct kndClassDeclaration **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY, sizeof(struct kndClassDeclaration), &page);
        if (err) return err;
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_TINY, sizeof(struct kndClassDeclaration), &page);
        if (err) return err;
    }
    memset(page, 0, sizeof(struct kndClassDeclaration));
    *result = page;
    return knd_OK;
}

#if 0
static int knd_proc_declar_new(struct kndMemPool *mempool, struct kndProcDeclaration **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY, sizeof(struct kndProcDeclaration), &page);
        if (err) return err;
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_TINY, sizeof(struct kndProcDeclaration), &page);
        if (err) return err;
    }
    memset(page, 0, sizeof(struct kndProcDeclaration));
    *result = page;
    return knd_OK;
}
#endif

static int knd_synode_spec_new(struct kndMemPool *mempool, struct kndSyNodeSpec **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY, sizeof(struct kndSyNodeSpec), &page);
        if (err) return err;
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_TINY, sizeof(struct kndSyNodeSpec), &page);
        if (err) return err;
    }
    *result = page;
    return knd_OK;
}

static int knd_synode_new(struct kndMemPool *mempool,
                          struct kndSyNode **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY, sizeof(struct kndSyNode), &page);
        if (err) return err;
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_TINY, sizeof(struct kndSyNode), &page);
        if (err) return err;
    }
    *result = page;
    return knd_OK;
}

int knd_clause_new(struct kndMemPool *mempool, struct kndClause **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                                sizeof(struct kndClause), &page);
        if (err) return err;
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_TINY,
                                     sizeof(struct kndClause), &page);
        if (err) return err;
    }
    *result = page;
    return knd_OK;
}

int knd_sentence_new(struct kndMemPool *mempool, struct kndSentence **result)
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

int knd_statement_new(struct kndMemPool *mempool, struct kndStatement **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                                sizeof(struct kndStatement), &page);
        if (err) return err;
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_SMALL,
                                     sizeof(struct kndStatement), &page);
        if (err) return err;
    }
    *result = page;
    return knd_OK;
}

int knd_par_new(struct kndMemPool *mempool, struct kndPar **result)
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

static gsl_err_t set_synode_spec_class(void *obj, const char *val, size_t val_size)    
{
    struct LocalContext *ctx = obj;
    struct kndSyNodeSpec *self = ctx->synode_spec;
    self->name = val;
    self->name_size = val_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_synode_class(void *obj, const char *val, size_t val_size)    
{
    struct kndSyNode *self = obj;
    self->name = val;
    self->name_size = val_size;
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
    struct kndMemPool *mempool = task->mempool;
    if (task->user_ctx)
        mempool = task->shard->user->mempool;

    struct kndSyNode *topic_synode = ctx->synode;
    struct kndSyNodeSpec *spec;
    gsl_err_t parser_err;
    int err;

    err = knd_synode_spec_new(mempool, &spec);
    if (err) return make_gsl_err_external(err);
    ctx->synode_spec = spec;

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

    ctx->synode = topic_synode;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_synode_code_pos(void *obj, const char *val, size_t val_size)    
{
    struct kndSyNode *self = obj;
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
    self->pos = (size_t)numval;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_synode_code_len(void *obj, const char *val, size_t val_size)    
{
    struct kndSyNode *self = obj;
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
    self->len = (size_t)numval;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_linear_pos(void *obj,
                                  const char *rec,
                                  size_t *total_size)
{
    gsl_err_t parser_err;
    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_synode_code_pos,
          .obj = obj
        },
        { .name = "l",
          .name_size = strlen("l"),
          .run = set_synode_code_len,
          .obj = obj
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_term_synode(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndMemPool *mempool = task->mempool;
    if (task->user_ctx)
        mempool = task->shard->user->mempool;
    struct kndSyNode *synode;
    gsl_err_t parser_err;
    int err;

    err = knd_synode_new(mempool, &synode);
    if (err) return make_gsl_err_external(err);
    ctx->synode = synode;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_synode_class,
          .obj = synode
        },
        { .name = "syn",
          .name_size = strlen("syn"),
          .parse = parse_synode,
          .obj = obj
        },
        { .name = "pos",
          .name_size = strlen("pos"),
          .parse = parse_linear_pos,
          .obj = synode
        }
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        return parser_err;
    }
    /*if (par->last_sent)
        par->last_sent->next = sent;
    else
        par->sents = sent;

    par->last_sent = sent;
    par->num_sents++;
    sent->numid = par->num_sents;
    */
    ctx->synode = NULL;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_synode_spec_array(void *obj,
                                         const char *rec,
                                         size_t *total_size)
{
    struct gslTaskSpec item_spec = {
        .is_list_item = true,
        .parse = parse_synode_spec,
        .obj = obj
    };
    return gsl_parse_array(&item_spec, rec, total_size);
}

static gsl_err_t parse_synode(void *obj,
                              const char *rec,
                              size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    // struct kndSentence *sent = ctx->sent;
    struct kndMemPool *mempool = task->mempool;
    if (task->user_ctx)
        mempool = task->shard->user->mempool;
    struct kndSyNode *synode;
    gsl_err_t parser_err;
    int err;

    err = knd_synode_new(mempool, &synode);
    if (err) return make_gsl_err_external(err);
    ctx->synode = synode;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_synode_class,
          .obj = synode
        },
        { .name = "syn",
          .name_size = strlen("syn"),
          .parse = parse_synode,
          .obj = obj
        },
        { .name = "pos",
          .name_size = strlen("pos"),
          .parse = parse_linear_pos,
          .obj = synode
        },
        { .name = "term",
          .name_size = strlen("term"),
          .parse = parse_term_synode,
          .obj = obj
        },
        { .type = GSL_GET_ARRAY_STATE,
          .name = "spec",
          .name_size = strlen("spec"),
          .parse = parse_synode_spec_array,
          .obj = obj
        }
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
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_subj(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClause *clause = ctx->clause;
    struct kndMemPool *mempool = task->mempool;
    if (task->user_ctx)
        mempool = task->shard->user->mempool;
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
          .obj = synode
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

static gsl_err_t parse_pred(void *obj,
                            const char *rec,
                            size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndClause *clause = ctx->clause;
    struct kndMemPool *mempool = task->mempool;
    if (task->user_ctx)
        mempool = task->shard->user->mempool;
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
          .obj = synode
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

static gsl_err_t parse_clause(void *obj,
                              const char *rec,
                              size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndSentence *sent = ctx->sent;
    // struct kndClause *parent_clause = ctx->clause;
    struct kndClause *clause;
    struct kndMemPool *mempool = task->mempool;
    if (task->user_ctx)
        mempool = task->shard->user->mempool;
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
        { .name = "subj",
          .name_size = strlen("subj"),
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

    // TODO sys repo
    if (!task->user_ctx) {
        knd_log("no user ctx given");
        return make_gsl_err(gsl_FAIL);
    }

    /* switch to statement's local scope */
    task->type = KND_INNER_STATE;

    parser_err = knd_class_select(task->user_ctx->repo, rec, total_size, task);
    task->type = orig_task_type;
    return parser_err;
}

static gsl_err_t parse_proc_select(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    knd_task_spec_type orig_task_type = task->type;
    gsl_err_t parser_err;
    // TODO sys repo
    if (!task->user_ctx) {
        return make_gsl_err(gsl_FAIL);
    }

    /* switch to statement's local scope */
    task->type = KND_INNER_STATE;

    /* check private repo first */
    if (task->user_ctx->repo) {
        parser_err = knd_proc_select(task->user_ctx->repo, rec, total_size, task);
        if (parser_err.code == gsl_OK) {
            task->type = orig_task_type;
            return parser_err;
        }

        // failed import?
        if (task->type == KND_INNER_COMMIT_STATE) {
            task->type = orig_task_type;
            return make_gsl_err(gsl_FAIL);
        }
    }

    /* shared read-only repo */
    parser_err = knd_proc_select(task->user_ctx->base_repo, rec, total_size, task);
    task->type = orig_task_type;
    if (parser_err.code) {
        knd_log("-- proc select failed: %d", parser_err.code);
        return parser_err;
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_statement(void *obj, const char *rec, size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndSentence *sent = ctx->sent;
    struct kndMemPool *mempool = task->mempool;
    if (task->user_ctx)
        mempool = task->shard->user->mempool;
    struct kndStatement *stm;
    gsl_err_t parser_err;
    int err;

    err = knd_statement_new(mempool, &stm);
    if (err) return make_gsl_err_external(err);
    sent->stm = stm;

    parser_err = knd_statement_import(stm, rec, total_size, task);
    if (parser_err.code) {
        knd_log("-- text stm parsing failed: %d", parser_err.code);
        return *total_size = 0, parser_err;
    }
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_sentence(void *obj, const char *rec, size_t *total_size)
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
    if (DEBUG_TEXT_LEVEL_2)
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
    if (parser_err.code) return parser_err;

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
    if (DEBUG_TEXT_LEVEL_2)
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
    if (DEBUG_TEXT_LEVEL_2)
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

void knd_sentence_str(struct kndSentence *self, size_t depth)
{
    if (self->stm) {
        knd_log("%*s#%zu:", depth * KND_OFFSET_SIZE, "", self->numid);
    }
}

void knd_text_str(struct kndText *self, size_t depth)
{
    struct kndState *state;
    struct kndStateVal *val;
    struct kndPar *par;
    struct kndSentence *sent;

    state = atomic_load_explicit(&self->states, memory_order_relaxed);
    if (!state) {
        if (self->seq_size) {
            knd_log("%*stext: \"%.*s\" (lang:%.*s)", depth * KND_OFFSET_SIZE, "",
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
                    knd_sentence_str(sent, depth + 2);
                }
            }
            knd_log("%*s]", depth * KND_OFFSET_SIZE, "");
        }
        return;
    }
    val = state->val;
    knd_log("%*stext: \"%.*s\" (lang:%.*s)", depth * KND_OFFSET_SIZE, "",
            val->val_size, val->val, self->locale_size, self->locale);
}
