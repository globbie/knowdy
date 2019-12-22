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

static gsl_err_t parse_synode(void *obj,
                              const char *rec,
                              size_t *total_size);

static int knd_class_declar_new(struct kndMemPool *mempool,
                                struct kndClassDeclaration **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                                sizeof(struct kndClassDeclaration), &page);
        if (err) return err;
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_TINY,
                                     sizeof(struct kndClassDeclaration), &page);
        if (err) return err;
    }
    memset(page, 0, sizeof(struct kndClassDeclaration));

    *result = page;
    return knd_OK;
}

static int knd_proc_declar_new(struct kndMemPool *mempool,
                                struct kndProcDeclaration **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                                sizeof(struct kndProcDeclaration), &page);
        if (err) return err;
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_TINY,
                                     sizeof(struct kndProcDeclaration), &page);
        if (err) return err;
    }
    memset(page, 0, sizeof(struct kndProcDeclaration));
    *result = page;
    return knd_OK;
}

static int knd_synode_spec_new(struct kndMemPool *mempool,
                               struct kndSyNodeSpec **result)
{
    void *page;
    int err;
    switch (mempool->type) {
    case KND_ALLOC_LIST:
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                                sizeof(struct kndSyNodeSpec), &page);
        if (err) return err;
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_TINY,
                                     sizeof(struct kndSyNodeSpec), &page);
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
        err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                                sizeof(struct kndSyNode), &page);
        if (err) return err;
        break;
    default:
        err = knd_mempool_incr_alloc(mempool, KND_MEMPAGE_TINY,
                                     sizeof(struct kndSyNode), &page);
        if (err) return err;
    }
    *result = page;
    return knd_OK;
}

static int knd_clause_new(struct kndMemPool *mempool,
                          struct kndClause **result)
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

static int knd_statement_new(struct kndMemPool *mempool,
                             struct kndStatement **result)
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
    knd_log("synode class:%.*s", val_size, val);
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
    self->seq_size = val_size;
    self->seq = val;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t set_statement_id(void *obj, const char *val, size_t val_size)    
{
    struct LocalContext *ctx = obj;
    struct kndStatement *stm = ctx->stm;
    stm->name = val;
    stm->name_size = val_size;
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_synode_spec(void *obj,
                                   const char *rec,
                                   size_t *total_size)
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

static int append_class_declar(struct kndStatement *stm,
                               struct kndTask *task)
{
    struct kndClassDeclaration *decl;
    struct kndClassInstEntry *entry;
    struct kndTaskContext *ctx = task->ctx;
    struct kndMemPool *mempool = task->mempool;
    if (task->user_ctx)
        mempool = task->shard->user->mempool;
    int err;

    entry = ctx->stm_class_insts;

    // TODO pure classes
    if (!entry) return knd_FAIL;

    err = knd_class_declar_new(mempool, &decl);
    if (err) return err;

    decl->insts = entry;
    decl->num_insts = ctx->num_stm_class_insts;
    decl->class = entry->inst->blueprint;

    decl->next = stm->class_declars;
    stm->class_declars = decl;

    ctx->stm_class_insts = NULL;
    ctx->num_stm_class_insts = 0;

    return knd_OK;
}

static int append_proc_declar(struct kndStatement *stm,
                              struct kndTask *task)
{
    struct kndProcDeclaration *decl;
    struct kndProcInstEntry *entry;
    struct kndTaskContext *ctx = task->ctx;
    struct kndMemPool *mempool = task->mempool;
    if (task->user_ctx)
        mempool = task->shard->user->mempool;
    int err;

    entry = ctx->stm_proc_insts;
    if (!entry) return knd_FAIL;

    err = knd_proc_declar_new(mempool, &decl);
    if (err) return err;

    decl->insts = entry;
    decl->num_insts = ctx->num_stm_proc_insts;
    decl->proc = entry->inst->blueprint;

    decl->next = stm->proc_declars;
    stm->proc_declars = decl;

    ctx->stm_proc_insts = NULL;
    ctx->num_stm_proc_insts = 0;

    return knd_OK;
}

static gsl_err_t parse_class_select(void *obj,
                                    const char *rec,
                                    size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    knd_task_spec_type orig_task_type = task->type;
    gsl_err_t parser_err;
    int err;
    // TODO sys repo
    if (!task->user_ctx) {
        return make_gsl_err(gsl_FAIL);
    }

    /* switch to statement's local scope */
    task->type = KND_INNER_STATE;

    /* check private repo first */
    if (task->user_ctx->repo) {
        parser_err = knd_class_select(task->user_ctx->repo, rec, total_size, task);
        if (parser_err.code == gsl_OK) {
            task->type = orig_task_type;

            err = append_class_declar(ctx->stm, task);
            if (err) return make_gsl_err_external(err);        
            return parser_err;
        }

        // failed import?
        if (task->type == KND_INNER_COMMIT_STATE) {
            task->type = orig_task_type;
            return make_gsl_err(gsl_FAIL);
        }
    }

    /* shared read-only repo */
    parser_err = knd_class_select(task->user_ctx->base_repo, rec, total_size, task);
    task->type = orig_task_type;
    if (parser_err.code) return parser_err;

    err = append_class_declar(ctx->stm, task);
    if (err) return make_gsl_err_external(err);        

    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_proc_select(void *obj,
                                   const char *rec,
                                   size_t *total_size)
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    knd_task_spec_type orig_task_type = task->type;
    gsl_err_t parser_err;
    int err;
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

            err = append_proc_declar(ctx->stm, task);
            if (err) return make_gsl_err_external(err);        
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
    err = append_proc_declar(ctx->stm, task);
    if (err) return make_gsl_err_external(err);        
    return make_gsl_err(gsl_OK);
}

static gsl_err_t parse_statement(void *obj,
                                 const char *rec,
                                 size_t *total_size)
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
    ctx->stm = stm;

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .run = set_statement_id,
          .obj = obj
        },
        { .name = "class",
          .name_size = strlen("class"),
          .parse = parse_class_select,
          .obj = obj
        },
        { .name = "proc",
          .name_size = strlen("proc"),
          .parse = parse_proc_select,
          .obj = obj
        }        
    };
    parser_err = gsl_parse_task(rec, total_size, specs, sizeof specs / sizeof specs[0]);
    if (parser_err.code) return parser_err;

    sent->stm = stm;

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

    err = knd_sent_new(mempool, &sent);
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
    ctx->sent = NULL;

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

